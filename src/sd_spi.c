#include "../include/sd_spi.h"
#include "stm32l4xx_hal.h"
#include "board_pins.h"          // SD_CS_PORT, SD_CS_PIN
#include "diskio.h"             // fonctions disk_* utilisées par FatFs

extern SPI_HandleTypeDef hspi1;  // défini dans setup.c

#define CMD0   0
#define CMD8   8
#define CMD9   9
#define CMD13  13
#define CMD16  16
#define CMD17  17
#define CMD24  24
#define CMD55  55
#define CMD58  58
#define ACMD41 (0x80 | 41)       // bit 7 = commande applicative (précédée de CMD55)

enum { ERR_NONE, ERR_NO_CARD, ERR_CMD8, ERR_INIT_TIMEOUT, ERR_OCR, ERR_BLOCKLEN, ERR_CSD };
enum { T_NONE, T_SD1, T_SD2, T_SDHC };

static uint8_t  card_type;
static uint32_t sectors;
static int      last_error;
static DSTATUS  stat = STA_NOINIT;   // état FatFs : 0 = carte initialisée

/* ---------- SPI bas niveau ---------- */
// Change la vitesse du SPI (diviseur de l'horloge) : lente pendant l'initialisation de la carte, plus
// rapide ensuite.
static void spi_speed(uint32_t prescaler)
{
    hspi1.Init.BaudRatePrescaler = prescaler;
    HAL_SPI_Init(&hspi1);
}

// Échange un octet sur le SPI : envoie tx et retourne l'octet reçu. Pour seulement lire, on envoie 0xFF.
static uint8_t xfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1, 10);
    return rx;
}

// Désélectionne la carte (CS haut) et envoie un octet pour qu'elle relâche la ligne MISO.
static void deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    xfer(0xFF);                                   // libère la ligne MISO
}

// Sélectionne la carte (CS bas) et attend qu'elle soit prête : elle tient MISO bas tant qu'elle est
// occupée. Retourne 0 si elle reste occupée plus de 500 ms.
static int select_card(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
    xfer(0xFF);
    uint32_t t = HAL_GetTick();
    while (xfer(0xFF) != 0xFF)                    // la carte tient MISO bas tant qu'elle est occupée
        if (HAL_GetTick() - t > 500) { deselect(); return 0; }
    return 1;
}

// Envoie une commande à la carte et retourne sa réponse R1 (0xFF = pas de réponse). Une commande = 0x40 +
// son numéro, un argument sur 4 octets, un CRC ; la carte répond dans les 10 octets suivants. Les
// commandes applicatives (ACMD) sont précédées de CMD55. CS reste bas pour la suite de l'échange.
static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
    if (cmd & 0x80) {                             // commande applicative : CMD55 d'abord
        cmd &= 0x7F;
        uint8_t r55 = send_cmd(CMD55, 0);
        if (r55 > 1) return r55;
    }
    deselect();
    if (!select_card()) return 0xFF;

    uint8_t crc = 0x01;
    if (cmd == CMD0) crc = 0x95;                  // seuls CMD0 et CMD8 exigent un CRC valide
    if (cmd == CMD8) crc = 0x87;

    xfer(0x40 | cmd);
    xfer(arg >> 24); xfer(arg >> 16); xfer(arg >> 8); xfer(arg);
    xfer(crc);

    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && (r & 0x80); i++) r = xfer(0xFF);
    return r;
}

// Attend le jeton 0xFE qui précède les données d'un bloc (200 ms au maximum). Retourne 0 s'il est reçu.
static int wait_token(void)
{
    uint32_t t = HAL_GetTick();
    uint8_t b;
    do { b = xfer(0xFF); } while (b == 0xFF && HAL_GetTick() - t < 200);
    return (b == 0xFE) ? 0 : -1;
}

/* ---------- Capacité (registre CSD) ---------- */
// Lit la taille de la carte dans son registre CSD (CMD9) et remplit sectors. Le format du registre dépend
// du type : CSD v2 pour les SDHC/SDXC, v1 pour les anciennes cartes (2 Go et moins).
static int read_capacity(void)
{
    uint8_t csd[16];
    if (send_cmd(CMD9, 0) != 0 || wait_token() != 0) { deselect(); return -1; }
    for (int i = 0; i < 16; i++) csd[i] = xfer(0xFF);
    xfer(0xFF); xfer(0xFF);                       // CRC ignoré
    deselect();

    if ((csd[0] >> 6) == 1) {                     // CSD v2 (SDHC / SDXC)
        uint32_t c = ((uint32_t)(csd[7] & 0x3F) << 16) | (csd[8] << 8) | csd[9];
        sectors = (c + 1) * 1024;
    } else {                                      // CSD v1 (SD <= 2 Go)
        uint32_t read_bl_len = csd[5] & 0x0F;
        uint32_t c    = ((uint32_t)(csd[6] & 0x03) << 10) | (csd[7] << 2) | (csd[8] >> 6);
        uint32_t mult = ((csd[9] & 0x03) << 1) | (csd[10] >> 7);
        uint64_t bytes = ((uint64_t)(c + 1) << (mult + 2)) << read_bl_len;
        sectors = (uint32_t)(bytes >> 9);
    }
    return 0;
}

/* ---------- API ---------- */
// Une tentative d'initialisation de la carte.
// - Horloge lente (< 400 kHz) et 80 impulsions pour réveiller la carte.
// - CMD0 la remet à zéro : pas de réponse = pas de carte.
// - CMD8 distingue les cartes v2 des v1, ACMD41 lance l'initialisation interne (1 s au plus).
// - CMD58 lit l'OCR, dont le bit 30 indique une carte haute capacité (SDHC).
// - Vitesse relevée, puis lecture de la taille.
// Retourne un code d'erreur (ERR_NONE si OK).
static int sd_init_once(void)
{
    uint8_t ocr[4];
    card_type = T_NONE;
    sectors = 0;

    spi_speed(SPI_BAUDRATEPRESCALER_16);          // 250 kHz (PCLK2 = 4 MHz) : l'initialisation exige < 400 kHz
    deselect();
    HAL_Delay(10);
    for (int i = 0; i < 10; i++) xfer(0xFF);      // 80 impulsions d'horloge, CS haut

    // Après un reset du processeur seul, la carte a pu être coupée en plein transfert d'un bloc :
    // 520 octets d'horloge, CS bas, lui laissent le temps de finir avant CMD0
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
    for (int i = 0; i < SD_BLOCK + 8; i++) xfer(0xFF);
    deselect();

    if (send_cmd(CMD0, 0) != 1) { deselect(); return ERR_NO_CARD; }

    uint32_t t0 = HAL_GetTick();
    if (send_cmd(CMD8, 0x1AA) == 1) {             // carte SD v2 (SDSC ou SDHC)
        for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
        if (ocr[2] != 0x01 || ocr[3] != 0xAA) { deselect(); return ERR_CMD8; }
        while (send_cmd(ACMD41, 1UL << 30) != 0)
            if (HAL_GetTick() - t0 > 1000) { deselect(); return ERR_INIT_TIMEOUT; }
        if (send_cmd(CMD58, 0) != 0) { deselect(); return ERR_OCR; }
        for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
        card_type = (ocr[0] & 0x40) ? T_SDHC : T_SD2;
    } else {                                      // carte SD v1
        while (send_cmd(ACMD41, 0) != 0)
            if (HAL_GetTick() - t0 > 1000) { deselect(); return ERR_INIT_TIMEOUT; }
        card_type = T_SD1;
    }

    if (card_type != T_SDHC && send_cmd(CMD16, SD_BLOCK) != 0) { deselect(); return ERR_BLOCKLEN; }
    deselect();

    spi_speed(SPI_BAUDRATEPRESCALER_2);           // 2 MHz une fois la carte initialisée
    if (read_capacity() != 0) return ERR_CSD;
    return ERR_NONE;
}

// Initialise la carte, avec 3 essais : après un reset du processeur seul, la carte peut être restée dans
// un état intermédiaire. Retourne ERR_NONE (0) si OK.
int sd_init(void)
{
    for (int try = 0; try < 3; try++) {
        last_error = sd_init_once();
        if (last_error == ERR_NONE) break;
        HAL_Delay(20);
    }
    return last_error;
}

// Code d'erreur de la dernière initialisation
int sd_last_error(void) { return last_error; }

// Texte clair pour un code d'erreur d'initialisation
const char *sd_error_text(int code)
{
    switch (code) {
    case ERR_NONE:         return "OK";
    case ERR_NO_CARD:      return "pas de reponse a CMD0 (carte absente, mauvais CS ou cablage SPI)";
    case ERR_CMD8:         return "reponse CMD8 invalide";
    case ERR_INIT_TIMEOUT: return "carte non initialisee apres 1 s (carte non supportee ?)";
    case ERR_OCR:          return "lecture OCR impossible";
    case ERR_BLOCKLEN:     return "taille de bloc refusee";
    case ERR_CSD:          return "lecture de la capacite impossible";
    default:               return "erreur inconnue";
    }
}

// Type de la carte détectée (SD v1, v2 ou SDHC/SDXC)
const char *sd_type_name(void)
{
    switch (card_type) {
    case T_SD1:  return "SD v1";
    case T_SD2:  return "SD v2";
    case T_SDHC: return "SDHC/SDXC";
    default:     return "aucune";
    }
}

// Nombre de secteurs de 512 octets de la carte
uint32_t sd_sector_count(void) { return sectors; }

// Lit le bloc lba (512 octets) dans buf avec CMD17. Sur une SDHC l'adresse est un numéro de bloc, sur les
// anciennes cartes un numéro d'octet. Retourne 0 si OK.
int sd_read_block(uint32_t lba, uint8_t *buf)
{
    uint32_t addr = (card_type == T_SDHC) ? lba : lba * SD_BLOCK;   // SDHC : adresse en blocs
    if (send_cmd(CMD17, addr) != 0 || wait_token() != 0) { deselect(); return -1; }
    for (int i = 0; i < SD_BLOCK; i++) buf[i] = xfer(0xFF);
    xfer(0xFF); xfer(0xFF);                       // CRC ignoré
    deselect();
    return 0;
}
// Écrit buf (512 octets) dans le bloc lba avec CMD24 : jeton de début 0xFE, données, CRC ignoré. La carte
// répond 0x05 si elle accepte, puis reste occupée (MISO bas) le temps d'écrire (500 ms au maximum).
// Retourne 0 si OK.
int sd_write_block(uint32_t lba, const uint8_t *buf)
{
    uint32_t addr = (card_type == T_SDHC) ? lba : lba * SD_BLOCK;
    if (send_cmd(CMD24, addr) != 0) { deselect(); return -1; }
    xfer(0xFF);
    xfer(0xFE);                                   // jeton de début de données
    for (int i = 0; i < SD_BLOCK; i++) xfer(buf[i]);
    xfer(0xFF); xfer(0xFF);                       // CRC ignoré
    if ((xfer(0xFF) & 0x1F) != 0x05) { deselect(); return -1; }   // 0x05 = données acceptées

    uint32_t t = HAL_GetTick();
    while (xfer(0xFF) != 0xFF)                    // la carte tient MISO bas pendant l'écriture
        if (HAL_GetTick() - t > 500) { deselect(); return -1; }
    deselect();
    return 0;
}

/* ---------- Lien avec FatFs (diskio) ---------- */
// Envoie une commande sans attendre que la carte soit libre (select_card pourrait bloquer 500 ms sans
// carte). Sert à détecter vite un retrait ou un retour. Retourne la réponse R1 (0xFF = rien).
static uint8_t quick_cmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    deselect();
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
    xfer(0xFF);
    xfer(0x40 | cmd);
    xfer(arg >> 24); xfer(arg >> 16); xfer(arg >> 8); xfer(arg);
    xfer(crc);
    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && (r & 0x80); i++) r = xfer(0xFF);
    if (r != 0xFF) xfer(0xFF);                    // 2e octet de la réponse R2 de CMD13
    deselect();
    return r;
}

// Vrai si la carte répond encore. On lit le secteur 0 : seul un vrai secteur renvoie le jeton 0xFE (une
// simple réponse 0x00 à CMD13 ne suffit pas : sans carte, MISO peut rester à 0). Si la lecture échoue, la
// carte est marquée à réinitialiser (STA_NOINIT).
int sd_present(void)
{
    int ok = 0;
    deselect();
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
    xfer(0xFF);
    xfer(0x40 | CMD17); xfer(0); xfer(0); xfer(0); xfer(0); xfer(0x01);
    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && (r & 0x80); i++) r = xfer(0xFF);
    if (r == 0x00) {
        uint8_t b;
        uint32_t t = HAL_GetTick();
        do { b = xfer(0xFF); } while (b == 0xFF && HAL_GetTick() - t < 100);
        if (b == 0xFE) {
            for (int i = 0; i < SD_BLOCK + 2; i++) xfer(0xFF);   // données + CRC, ignorés
            ok = 1;
        }
    }
    deselect();
    if (!ok) stat = STA_NOINIT;                   // retirée : elle devra être réinitialisée au retour
    return ok;
}

// Une carte est-elle (re)venue ? Envoie CMD0 et regarde si elle répond 0x01. Rapide, sans la longue
// initialisation.
int sd_probe(void)
{
    return quick_cmd(CMD0, 0, 0x95) == 0x01;
}

// Fonctions disk_* : c'est l'interface que FatFs appelle pour lire et écrire. Elles s'appuient sur
// sd_init, sd_read_block et sd_write_block. Une erreur de lecture ou d'écriture marque la carte à
// réinitialiser.
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    stat = (sd_init() == 0) ? 0 : STA_NOINIT;
    return stat;
}

// FatFs : la carte est-elle prête ?
DSTATUS disk_status(BYTE pdrv)
{
    return pdrv == 0 ? stat : STA_NOINIT;
}

// FatFs : lit des secteurs
DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || stat) return RES_NOTRDY;
    for (; count; count--, sector++, buff += SD_BLOCK)
        if (sd_read_block(sector, buff) != 0) { stat = STA_NOINIT; return RES_ERROR; }   // prochain montage : réinitialisation
    return RES_OK;
}

// FatFs : écrit des secteurs
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || stat) return RES_NOTRDY;
    for (; count; count--, sector++, buff += SD_BLOCK)
        if (sd_write_block(sector, buff) != 0) { stat = STA_NOINIT; return RES_ERROR; }
    return RES_OK;
}

// FatFs : renseignements sur la carte (nombre et taille des secteurs)
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || stat) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:         return RES_OK;        // sd_write_block attend déjà la fin de l'écriture
    case GET_SECTOR_COUNT:  *(DWORD *)buff = sd_sector_count(); return RES_OK;
    case GET_SECTOR_SIZE:   *(WORD *)buff = SD_BLOCK; return RES_OK;
    case GET_BLOCK_SIZE:    *(DWORD *)buff = 1; return RES_OK;
    default:                return RES_PARERR;
    }
}
