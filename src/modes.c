#include "modes.h"
#include "settings.h"
#include "stm32l4xx_hal.h"
#include "console.h"
#include "i2c.h"
#include "mesures.h"

#define NUM_LEDS 1

#define CLK_PORT GPIOB
#define CLK_PIN  GPIO_PIN_10   // D6 : horloge (CI)
#define DAT_PORT GPIOA
#define DAT_PIN  GPIO_PIN_8    // D7 : données (DI)

// Configure en sortie les deux broches (horloge et données) de la LED RGB.
void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pin = CLK_PIN; HAL_GPIO_Init(CLK_PORT, &gpio);
    gpio.Pin = DAT_PIN; HAL_GPIO_Init(DAT_PORT, &gpio);
}

/* ---------- LED ---------- */
// Envoie un octet à la LED, bit de poids fort en premier : la ligne de données est posée pendant que
// l'horloge est basse, puis validée par un front montant.
static void send_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DAT_PORT, DAT_PIN, (b & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_SET);
        b <<= 1;
    }
}

// Envoie une couleur à la LED P9813 : un octet de début (deux bits à 1, puis les 2 bits de poids fort de
// chaque couleur, inversés, pour la détection d'erreur) suivi du bleu, du vert et du rouge.
static void send_frame(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t prefix = 0xC0 | ((~b & 0xC0) >> 2) | ((~g & 0xC0) >> 4) | ((~r & 0xC0) >> 6);
    send_byte(prefix); send_byte(b); send_byte(g); send_byte(r);
}

// Allume la LED de la couleur donnée (0 à 255 par composante). Transmission : 32 bits à 0 (début), une
// trame par LED, 32 bits à 0 (fin).
void leds_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 4; i++) send_byte(0);
    for (int i = 0; i < NUM_LEDS; i++) send_frame(r, g, b);
    for (int i = 0; i < 4; i++) send_byte(0);
}

#define GREEN_PORT GPIOA
#define GREEN_PIN  GPIO_PIN_10 // D2 : bouton vert
#define RED_PORT   GPIOB
#define RED_PIN    GPIO_PIN_3  // D3 : bouton rouge
#define PRESSED    GPIO_PIN_RESET

Button button_green = { GREEN_PORT, GREEN_PIN, 0, 0, 0 };
Button button_red   = { RED_PORT,   RED_PIN,   0, 0, 0 };

// Vrai tant que le bouton est enfoncé (les boutons relient la broche à la masse : état bas).
int button_pressed(const Button *b)
{
    return HAL_GPIO_ReadPin(b->port, b->pin) == PRESSED;
}

// Vrai une seule fois quand le bouton est maintenu HOLD_MS (5 s). Au début de l'appui on note l'instant ;
// quand la durée est atteinte on le signale une fois ; au relâchement on réarme.
int button_held(Button *b)
{
    uint32_t now = HAL_GetTick();
    if (!button_pressed(b)) { b->active = 0; return 0; }
    if (!b->active) { b->active = 1; b->since = now; b->fired = 0; }
    if (!b->fired && now - b->since >= HOLD_MS) { b->fired = 1; return 1; }
    return 0;
}

// Configure les deux boutons en entrée avec une résistance de rappel vers le haut : ils sont actifs à
// l'état bas.
void buttons_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GREEN_PIN; HAL_GPIO_Init(GREEN_PORT, &gpio);
    gpio.Pin = RED_PIN;   HAL_GPIO_Init(RED_PORT, &gpio);
}

#define CONFIG_TIMEOUT  (CONFIG_TIMEOUT_MIN * 60UL * 1000UL)


static const uint8_t mode_color[][3] = {   // R, G, B
    [MODE_CONFIG]   = {255, 255, 0},       // jaune
    [MODE_STANDARD] = {0, 255, 0},         // vert
    [MODE_ECO]      = {0, 0, 255},         // bleu
    [MODE_MAINT]    = {255, 80, 0},        // orange
};

static const char *mode_name[] = {
    [MODE_CONFIG]   = "CONFIGURATION",
    [MODE_STANDARD] = "STANDARD",
    [MODE_ECO]      = "ECONOMIQUE",
    [MODE_MAINT]    = "MAINTENANCE",
};

Mode mode;
static Mode prev_mode;
uint32_t mode_since;


// Change de mode : mémorise l'instant (pour le délai de la configuration), allume la LED à la couleur du
// mode et l'annonce sur la console. En maintenance, ferme le fichier SD pour qu'on puisse retirer la carte
// sans risque.
void set_mode(Mode m)
{
    mode = m;
    mode_since = HAL_GetTick();
    if (m == MODE_MAINT) log_stop();          // ferme le fichier : la carte peut être retirée sans risque
    leds_set_all(mode_color[m][0], mode_color[m][1], mode_color[m][2]);
    console("Mode : %s\r\n", mode_name[m]);
}

// Gère les changements de mode avec les boutons (appui long de 5 s).
// - Standard ou économique : vert = bascule entre les deux, rouge = maintenance (le mode d'origine est
//   retenu).
// - Maintenance : rouge = retour au mode d'origine.
// - Configuration : retour au mode standard après 30 min sans commande.
void update_mode(void)
{
    switch (mode) {
    case MODE_STANDARD:
        if (button_held(&button_green))     set_mode(MODE_ECO);
        else if (button_held(&button_red))  { prev_mode = MODE_STANDARD; set_mode(MODE_MAINT); }
        break;
    case MODE_ECO:
        if (button_held(&button_green))     set_mode(MODE_STANDARD);
        else if (button_held(&button_red))  { prev_mode = MODE_ECO; set_mode(MODE_MAINT); }
        break;
    case MODE_MAINT:
        if (button_held(&button_red))       set_mode(prev_mode);
        break;
    case MODE_CONFIG:
        if (HAL_GetTick() - mode_since >= CONFIG_TIMEOUT) set_mode(MODE_STANDARD);
        break;
    }
}

/* ---------- Erreurs ---------- */
typedef struct {
    const char *text;
    uint8_t a[3], b[3];     // couleurs R, G, B (a en premier, b ensuite)
    uint16_t a_ms;          // durée de a dans la période ; b dure le reste
} ErrInfo;

static const ErrInfo err_info[ERR_COUNT] = {
    [ERR_RTC]        = { "acces a l'horloge RTC",              {255, 0, 0}, {0, 0, 255},     ERR_PERIOD_MS / 2 },
    [ERR_SD_IO]      = { "acces ou ecriture sur la carte SD",  {255, 0, 0}, {255, 255, 255}, ERR_PERIOD_MS / 3 },
    [ERR_SD_FULL]    = { "carte SD pleine",                    {255, 0, 0}, {255, 255, 255}, ERR_PERIOD_MS / 2 },
    [ERR_GPS]        = { "acces aux donnees du GPS",           {255, 0, 0}, {255, 255, 0},   ERR_PERIOD_MS / 2 },
    [ERR_SENSOR]     = { "acces aux donnees d'un capteur",     {255, 0, 0}, {0, 255, 0},     ERR_PERIOD_MS / 2 },
    [ERR_INCOHERENT] = { "donnees incoherentes, verifier le materiel", {255, 0, 0}, {0, 255, 0}, ERR_PERIOD_MS / 3 },
};

static uint8_t err_mask;                      // bit e = erreur e présente

// Lève ou retire une erreur dans err_mask. Le message n'est affiché sur la console qu'à l'apparition de
// l'erreur, pas à chaque mesure.
void error_set(ErrCode e, int on)
{
    uint8_t bit = 1u << e;
    if (on && !(err_mask & bit)) console("ERREUR E%02d : %s\r\n", e, err_info[e].text);
    if (on) err_mask |= bit;
    else    err_mask &= ~bit;
}

// Affiche l'erreur la plus prioritaire (E01 en premier) sur la LED, en alternant deux couleurs sur une
// période de 1 s ; sans erreur, la LED reprend la couleur du mode. La LED n'est réécrite que si l'erreur
// ou la phase a changé. L'erreur d'horloge (E01) est suivie ici car rtc_fail change dans rtc_poll.
void error_led(void)
{
    error_set(ERR_RTC, rtc_fail);
    static uint8_t shown, phase;              // erreur affichée (0 = couleur du mode) et phase du clignotement
    uint8_t want = 0;
    for (uint8_t e = 1; e < ERR_COUNT && !want; e++)
        if (err_mask & (1u << e)) want = e;

    if (!want) {
        if (shown) leds_set_all(mode_color[mode][0], mode_color[mode][1], mode_color[mode][2]);
        shown = 0;
        return;
    }

    const ErrInfo *i = &err_info[want];
    uint8_t ph = (HAL_GetTick() % ERR_PERIOD_MS) >= i->a_ms;
    if (want != shown || ph != phase) {
        const uint8_t *c = ph ? i->b : i->a;
        leds_set_all(c[0], c[1], c[2]);
    }
    shown = want;
    phase = ph;
}
