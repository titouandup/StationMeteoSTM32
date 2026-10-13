#include "modes.h"
#include "stm32l4xx_hal.h"
#include "console.h"
#include "i2c.h"
#include "mesures.h"

#define NUM_LEDS 1

#define CLK_PORT GPIOB
#define CLK_PIN  GPIO_PIN_10   // D6 : horloge (CI)
#define DAT_PORT GPIOA
#define DAT_PIN  GPIO_PIN_8    // D7 : données (DI)

void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pin = CLK_PIN; HAL_GPIO_Init(CLK_PORT, &gpio);
    gpio.Pin = DAT_PIN; HAL_GPIO_Init(DAT_PORT, &gpio);
}

/* ---------- LED ---------- */
static void send_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DAT_PORT, DAT_PIN, (b & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(CLK_PORT, CLK_PIN, GPIO_PIN_SET);
        b <<= 1;
    }
}

static void send_frame(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t prefix = 0xC0 | ((~b & 0xC0) >> 2) | ((~g & 0xC0) >> 4) | ((~r & 0xC0) >> 6);
    send_byte(prefix); send_byte(b); send_byte(g); send_byte(r);
}

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
#define HOLD_MS    5000UL

Button button_green = { GREEN_PORT, GREEN_PIN, 0, 0, 0 };
Button button_red   = { RED_PORT,   RED_PIN,   0, 0, 0 };

int button_pressed(const Button *b)
{
    return HAL_GPIO_ReadPin(b->port, b->pin) == PRESSED;
}

// Vrai une seule fois quand le bouton est maintenu HOLD_MS (re-armé au relâchement)
int button_held(Button *b)
{
    uint32_t now = HAL_GetTick();
    if (!button_pressed(b)) { b->active = 0; return 0; }
    if (!b->active) { b->active = 1; b->since = now; b->fired = 0; }
    if (!b->fired && now - b->since >= HOLD_MS) { b->fired = 1; return 1; }
    return 0;
}

void buttons_init(void)                       // actifs à l'état bas
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GREEN_PIN; HAL_GPIO_Init(GREEN_PORT, &gpio);
    gpio.Pin = RED_PIN;   HAL_GPIO_Init(RED_PORT, &gpio);
}

#define CONFIG_TIMEOUT  (30UL * 60UL * 1000UL)


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
static uint8_t err_state;                     // erreur affichée sur la LED (0 = aucune, 1 = RTC, 2 = SD)


void set_mode(Mode m)
{
    mode = m;
    mode_since = HAL_GetTick();
    err_state = 0;                            // la LED reprend la couleur du mode
    if (m == MODE_MAINT) log_stop();          // ferme le fichier : la carte peut être retirée sans risque
    leds_set_all(mode_color[m][0], mode_color[m][1], mode_color[m][2]);
    console("Mode : %s\r\n", mode_name[m]);
}

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

// Erreurs : RTC = LED qui alterne rouge / bleu à 1 Hz (500 ms chaque couleur), SD = rouge fixe
void error_led(void)
{
    static uint8_t phase;
    static uint32_t last;
    uint32_t now = HAL_GetTick();
    uint8_t want = rtc_fail ? 1 : (sd_error ? 2 : 0);

    if (want == 1) {
        if (err_state != 1 || now - last >= 500) {
            last = now; phase ^= 1;
            if (phase) leds_set_all(255, 0, 0);
            else       leds_set_all(0, 0, 255);
        }
    } else if (want != err_state) {
        if (want == 2) leds_set_all(255, 0, 0);
        else           leds_set_all(mode_color[mode][0], mode_color[mode][1], mode_color[mode][2]);
    }
    err_state = want;
}
