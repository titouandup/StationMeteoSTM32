#include "../include/sd_spi.h"
#include "stm32l4xx_hal.h"
#include "board_pins.h"          // SD_CS_PORT, SD_CS_PIN
#include "diskio.h"             // fonctions disk_* utilisées par FatFs

extern SPI_HandleTypeDef hspi1;  // défini dans setup.c

#define CMD0   0
#define CMD8   8
#define CMD9   9
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

/* ---------- SPI bas niveau ---------- */
static void spi_speed(uint32_t prescaler)
{
    hspi1.Init.BaudRatePrescaler = prescaler;
    HAL_SPI_Init(&hspi1);
}

static uint8_t xfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1, 10);
    return rx;
}

static void deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    xfer(0xFF);                                   // libère la ligne MISO
}

static int select_card(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
    xfer(0xFF);
    uint32_t t = HAL_GetTick();
    while (xfer(0xFF) != 0xFF)                    // la carte tient MISO bas tant qu'elle est occupée
        if (HAL_GetTick() - t > 500) { deselect(); return 0; }
    return 1;
}

// Envoie une commande et renvoie la réponse R1 (0xFF = pas de réponse). CS reste bas.
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

static int wait_token(void)                       // attend le jeton 0xFE qui précède les données
{
    uint32_t t = HAL_GetTick();
    uint8_t b;
    do { b = xfer(0xFF); } while (b == 0xFF && HAL_GetTick() - t < 200);
    return (b == 0xFE) ? 0 : -1;
}

/* ---------- Capacité (registre CSD) ---------- */
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

// Après un reset du processeur seul, la carte peut être restée dans un état intermédiaire : on réessaie
int sd_init(void)
{
    for (int try = 0; try < 3; try++) {
        last_error = sd_init_once();
        if (last_error == ERR_NONE) break;
        HAL_Delay(20);
    }
    return last_error;
}

int sd_last_error(void) { return last_error; }

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

const char *sd_type_name(void)
{
    switch (card_type) {
    case T_SD1:  return "SD v1";
    case T_SD2:  return "SD v2";
    case T_SDHC: return "SDHC/SDXC";
    default:     return "aucune";
    }
}

uint32_t sd_sector_count(void) { return sectors; }

int sd_read_block(uint32_t lba, uint8_t *buf)
{
    uint32_t addr = (card_type == T_SDHC) ? lba : lba * SD_BLOCK;   // SDHC : adresse en blocs
    if (send_cmd(CMD17, addr) != 0 || wait_token() != 0) { deselect(); return -1; }
    for (int i = 0; i < SD_BLOCK; i++) buf[i] = xfer(0xFF);
    xfer(0xFF); xfer(0xFF);                       // CRC ignoré
    deselect();
    return 0;
}
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
static DSTATUS stat = STA_NOINIT;

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    stat = (sd_init() == 0) ? 0 : STA_NOINIT;
    return stat;
}

DSTATUS disk_status(BYTE pdrv)
{
    return pdrv == 0 ? stat : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || stat) return RES_NOTRDY;
    for (; count; count--, sector++, buff += SD_BLOCK)
        if (sd_read_block(sector, buff) != 0) return RES_ERROR;
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || stat) return RES_NOTRDY;
    for (; count; count--, sector++, buff += SD_BLOCK)
        if (sd_write_block(sector, buff) != 0) return RES_ERROR;
    return RES_OK;
}

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
