#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "setup.h"

extern UART_HandleTypeDef huart2;   // console (USB ST-Link)
extern UART_HandleTypeDef huart4;   // GPS SIM28 (port A1 : PA1 = RX)

#define NUM_LEDS 1

#define CLK_PORT GPIOB
#define CLK_PIN  GPIO_PIN_10   // D6 : horloge (CI)
#define DAT_PORT GPIOA
#define DAT_PIN  GPIO_PIN_8    // D7 : données (DI)

#define GREEN_PORT GPIOA
#define GREEN_PIN  GPIO_PIN_10 // D2 : bouton vert
#define RED_PORT   GPIOB
#define RED_PIN    GPIO_PIN_3  // D3 : bouton rouge
#define PRESSED    GPIO_PIN_RESET

#define HOLD_MS         5000UL
#define CONFIG_TIMEOUT  (30UL * 60UL * 1000UL)
#define GPS_PRINT_MS    2000UL   // période d'affichage (x2 en économique)
#define GPS_SILENCE_MS  3000UL   // sans trame RMC depuis ce délai = problème de câblage

typedef enum { MODE_CONFIG, MODE_STANDARD, MODE_ECO, MODE_MAINT } Mode;

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

/* ---------- Console (USART2, sans printf) ---------- */
static void console(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
    if (n > 0) HAL_UART_Transmit(&huart2, (uint8_t *)buf, n, 100);
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

static void leds_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 4; i++) send_byte(0);
    for (int i = 0; i < NUM_LEDS; i++) send_frame(r, g, b);
    for (int i = 0; i < 4; i++) send_byte(0);
}

/* ---------- Boutons ---------- */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint32_t since;
    uint8_t active, fired;
} Button;

static Button green = { GREEN_PORT, GREEN_PIN, 0, 0, 0 };
static Button red   = { RED_PORT,   RED_PIN,   0, 0, 0 };

static int is_pressed(const Button *b)
{
    return HAL_GPIO_ReadPin(b->port, b->pin) == PRESSED;
}

// Vrai une seule fois quand le bouton est maintenu HOLD_MS (re-armé au relâchement)
static int held(Button *b)
{
    uint32_t now = HAL_GetTick();
    if (!is_pressed(b)) { b->active = 0; return 0; }
    if (!b->active) { b->active = 1; b->since = now; b->fired = 0; }
    if (!b->fired && now - b->since >= HOLD_MS) { b->fired = 1; return 1; }
    return 0;
}

/* ---------- Modes ---------- */
static Mode mode, prev_mode;
static uint32_t mode_since;

static void set_mode(Mode m)
{
    mode = m;
    mode_since = HAL_GetTick();
    leds_set_all(mode_color[m][0], mode_color[m][1], mode_color[m][2]);
    console("Mode : %s\r\n", mode_name[m]);
}

static void update_mode(void)
{
    switch (mode) {
    case MODE_STANDARD:
        if (held(&green))     set_mode(MODE_ECO);
        else if (held(&red))  { prev_mode = MODE_STANDARD; set_mode(MODE_MAINT); }
        break;
    case MODE_ECO:
        if (held(&green))     set_mode(MODE_STANDARD);
        else if (held(&red))  { prev_mode = MODE_ECO; set_mode(MODE_MAINT); }
        break;
    case MODE_MAINT:
        if (held(&red))       set_mode(prev_mode);
        break;
    case MODE_CONFIG:
        if (HAL_GetTick() - mode_since >= CONFIG_TIMEOUT) set_mode(MODE_STANDARD);
        break;
    }
}

/* ---------- GPS (UART4, réception par interruption) ---------- */
static uint8_t rx_byte;
static char line[100], rmc[100];
static uint8_t idx;
static volatile uint8_t rmc_ready;

void UART4_IRQHandler(void) { HAL_UART_IRQHandler(&huart4); }

// Appelée à chaque octet reçu : on ne garde que les trames RMC
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    if (h != &huart4) return;
    char c = (char)rx_byte;
    if (c == '$') idx = 0;
    if (idx < sizeof(line) - 1) line[idx++] = c;
    if (c == '\n') {
        line[idx] = 0;
        if (idx > 6 && memcmp(line + 3, "RMC", 3) == 0 && !rmc_ready) {
            memcpy(rmc, line, idx + 1);
            rmc_ready = 1;
        }
        idx = 0;
    }
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1);
}

// Après une erreur (overrun...), la réception s'arrête : on la relance
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    if (h == &huart4) HAL_UART_Receive_IT(&huart4, &rx_byte, 1);
}

static int split(char *s, char *f[], int max)   // découpe sur ',' en gardant les champs vides
{
    int n = 0;
    f[n++] = s;
    for (; *s && n < max; s++)
        if (*s == ',') { *s = 0; f[n++] = s + 1; }
    return n;
}

static double to_deg(const char *s, char hemi)  // ddmm.mmmm -> degrés décimaux
{
    double v = strtod(s, NULL);
    int d = (int)(v / 100);
    double deg = d + (v - d * 100) / 60.0;
    return (hemi == 'S' || hemi == 'W') ? -deg : deg;
}

static int gps_valid;
static double gps_lat, gps_lon;
static char gps_time[7] = "------";    // hhmmss
static uint32_t last_rmc_tick;

static void gps_poll(void)
{
    if (!rmc_ready) return;
    char *f[12];
    int n = split(rmc, f, 12);
    gps_valid = (n >= 7 && f[2][0] == 'A');
    if (n >= 2 && strlen(f[1]) >= 6) memcpy(gps_time, f[1], 6);
    if (gps_valid) {
        gps_lat = to_deg(f[3], f[4][0]);
        gps_lon = to_deg(f[5], f[6][0]);
    }
    last_rmc_tick = HAL_GetTick();
    rmc_ready = 0;
}

static void gps_report(void)
{
    static uint32_t last_print;
    uint32_t period = (mode == MODE_ECO) ? 2 * GPS_PRINT_MS : GPS_PRINT_MS;
    uint32_t now = HAL_GetTick();

    if (mode == MODE_CONFIG || now - last_print < period) return;   // pas d'acquisition en configuration
    last_print = now;

    if (last_rmc_tick == 0 || now - last_rmc_tick > GPS_SILENCE_MS)
        console("GPS : aucune donnee (verifier cablage port A1)\r\n");
    else if (!gps_valid)
        console("GPS : pas de fix (UTC %.2s:%.2s:%.2s)\r\n", gps_time, gps_time + 2, gps_time + 4);
    else
        console("GPS : %.2s:%.2s:%.2s UTC  lat = %.5f  lon = %.5f\r\n",
                gps_time, gps_time + 2, gps_time + 4, gps_lat, gps_lon);
}

int main(void)
{
    Global_Init();   // inclut HAL_Init(), l'USART2 (console) et l'UART4 (GPS, 9600 bauds)

    GPIO_InitTypeDef gpio = {0};

    gpio.Mode = GPIO_MODE_OUTPUT_PP;       // sorties LED
    gpio.Pin = CLK_PIN; HAL_GPIO_Init(CLK_PORT, &gpio);
    gpio.Pin = DAT_PIN; HAL_GPIO_Init(DAT_PORT, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;           // boutons (actifs à l'état bas)
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GREEN_PIN; HAL_GPIO_Init(GREEN_PORT, &gpio);
    gpio.Pin = RED_PIN;   HAL_GPIO_Init(RED_PORT, &gpio);

    HAL_NVIC_SetPriority(UART4_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1);

    HAL_Delay(10);
    console("\r\n3W demarrage\r\n");
    set_mode(is_pressed(&red) ? MODE_CONFIG : MODE_STANDARD);

    while (1) {
        update_mode();
        gps_poll();
        gps_report();
        HAL_Delay(10);
    }
}