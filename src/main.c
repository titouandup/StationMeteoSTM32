#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "setup.h"

extern UART_HandleTypeDef huart2;   // console (USB ST-Link)
extern UART_HandleTypeDef huart4;   // GPS SIM28 (port A1 : PA1 = RX)
extern I2C_HandleTypeDef  hi2c1;    // défini dans setup.c

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
#define SENSOR_PRINT_MS 2000UL   // période d'affichage des capteurs (x2 en économique)
#define LUMIN_LOW       255
#define LUMIN_HIGH      768

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

/* ---------- Réception UART : GPS (UART4) + console (USART2) ---------- */
static uint8_t rx_byte;                       // GPS
static char line[100], rmc[100];
static uint8_t idx;
static volatile uint8_t rmc_ready;

static uint8_t rx2_byte;                      // console
static char cmd[64];
static uint8_t cmd_len;
static volatile uint8_t cmd_ready;

void UART4_IRQHandler(void)  { HAL_UART_IRQHandler(&huart4); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }

// Appelée à chaque octet reçu
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    if (h == &huart2) {                       // console : on assemble une ligne de commande
        char c = (char)rx2_byte;
        if (!cmd_ready) {
            if (c == '\r' || c == '\n') {
                if (cmd_len > 0) { cmd[cmd_len] = 0; cmd_ready = 1; }
            } else if (cmd_len < sizeof(cmd) - 1) {
                cmd[cmd_len++] = c;
            }
        }
        HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
        return;
    }

    if (h != &huart4) return;                 // GPS : on ne garde que les trames RMC
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
    if (h == &huart2) HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
}

/* ---------- GPS ---------- */
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

/* ---------- I2C1 (PB8 = SCL, PB9 = SDA) : broches et horloge déjà gérées par setup.c ---------- */
static void i2c_init(void)
{
    uint32_t f = HAL_RCC_GetPCLK1Freq();
    hi2c1.Init.Timing = (f == 80000000UL) ? 0x10909CEC : 0x00000E14;   // 100 kHz pour 80 MHz ou 4 MHz
    HAL_I2C_Init(&hi2c1);                                              // ré-applique seulement le timing
    console("I2C : PCLK1 = %lu Hz (timing prevu pour 80 MHz ou 4 MHz)\r\n", (unsigned long)f);
}

static void i2c_scan(void)                    // liste les modules présents sur le bus
{
    console("Scan I2C :");
    for (uint8_t a = 1; a < 127; a++)
        if (HAL_I2C_IsDeviceReady(&hi2c1, a << 1, 1, 10) == HAL_OK)
            console(" 0x%02X", a);
    console("\r\n");
}

/* ---------- RTC (DS1307 / DS3231, adresse 0x68) ---------- */
#define RTC_ADDR (0x68 << 1)                  // HAL attend l'adresse décalée d'un bit

typedef struct { uint8_t h, m, s, dow, day, month; uint16_t year; } DateTime;   // dow : 1 = MON ... 7 = SUN

static const char *day_name[] = { "", "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static int rtc_read(DateTime *t)              // 0 = OK, -1 = erreur d'accès
{
    uint8_t r[7];
    if (HAL_I2C_Mem_Read(&hi2c1, RTC_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, r, 7, 50) != HAL_OK)
        return -1;
    t->s     = bcd2bin(r[0] & 0x7F);          // bit 7 = horloge arrêtée (DS1307), masqué
    t->m     = bcd2bin(r[1] & 0x7F);
    t->h     = bcd2bin(r[2] & 0x3F);
    t->dow   = bcd2bin(r[3] & 0x07);
    t->day   = bcd2bin(r[4] & 0x3F);
    t->month = bcd2bin(r[5] & 0x1F);
    t->year  = 2000 + bcd2bin(r[6]);
    return 0;
}

static int rtc_write(const DateTime *t)       // écrire les secondes avec le bit 7 à 0 démarre l'horloge
{
    uint8_t r[7] = { bin2bcd(t->s), bin2bcd(t->m), bin2bcd(t->h), t->dow,
                     bin2bcd(t->day), bin2bcd(t->month), bin2bcd(t->year - 2000) };
    return HAL_I2C_Mem_Write(&hi2c1, RTC_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, r, 7, 50) == HAL_OK ? 0 : -1;
}

// Lit l'heure avant modification ; si le module est neuf (valeurs absurdes), repart d'une date valide
static int rtc_load(DateTime *t)
{
    if (rtc_read(t) != 0) return -1;
    if (t->h > 23 || t->m > 59 || t->s > 59 || t->day < 1 || t->day > 31 ||
        t->month < 1 || t->month > 12 || t->dow < 1 || t->dow > 7) {
        t->h = t->m = t->s = 0; t->dow = 1; t->day = 1; t->month = 1; t->year = 2000;
    }
    return 0;
}

static DateTime now_dt;
static uint8_t rtc_fail = 1;                  // 1 tant que la RTC n'a pas répondu

// Lit la RTC une fois par seconde et mémorise le résultat
static void rtc_poll(void)
{
    static uint32_t last;
    static uint8_t first = 1;
    uint32_t now = HAL_GetTick();
    if (!first && now - last < 1000) return;
    first = 0;
    last = now;
    rtc_fail = (rtc_read(&now_dt) != 0);
}

// Horodatage texte pour les messages et, plus tard, les lignes du fichier SD
static void timestamp(char *buf, size_t n)
{
    if (rtc_fail)
        snprintf(buf, n, "NA");
    else
        snprintf(buf, n, "%02d/%02d/%04d %02d:%02d:%02d",
                 now_dt.day, now_dt.month, now_dt.year, now_dt.h, now_dt.m, now_dt.s);
}

// Erreur RTC : LED qui alterne rouge / bleu à 1 Hz (500 ms chaque couleur)
static void rtc_error_led(void)
{
    static uint8_t shown, phase;
    static uint32_t last;
    uint32_t now = HAL_GetTick();

    if (rtc_fail) {
        if (!shown || now - last >= 500) {
            last = now; shown = 1; phase ^= 1;
            if (phase) leds_set_all(255, 0, 0);
            else       leds_set_all(0, 0, 255);
        }
    } else if (shown) {                       // la RTC répond de nouveau : on remet la couleur du mode
        shown = 0;
        leds_set_all(mode_color[mode][0], mode_color[mode][1], mode_color[mode][2]);
    }
}

/* ---------- BME280 ---------- */
#define U16(a, i) ((uint16_t)((a)[i] | ((a)[(i) + 1] << 8)))
#define S16(a, i) ((int16_t)U16(a, i))

static uint8_t  bme_addr, bme_ok;
static uint16_t dig_T1, dig_P1;
static int16_t  dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t  dig_H1, dig_H3;
static int16_t  dig_H2, dig_H4, dig_H5;
static int8_t   dig_H6;
static int32_t  t_fine;

static int bme_init(void)
{
    uint8_t id = 0, c[24], h1, h[7];

    for (uint8_t a = 0x76; a <= 0x77 && !bme_addr; a++)
        if (HAL_I2C_Mem_Read(&hi2c1, a << 1, 0xD0, I2C_MEMADD_SIZE_8BIT, &id, 1, 100) == HAL_OK)
            bme_addr = a << 1;
    if (!bme_addr) { console("BME280 : non trouve (0x76/0x77)\r\n"); return -1; }

    console("BME280 : trouve en 0x%02X, ID = 0x%02X\r\n", bme_addr >> 1, id);
    if (id != 0x60) { console("Puce differente d'un BME280 (0x58 = BMP280 sans humidite, 0x61 = BME680)\r\n"); return -2; }

    // Coefficients de calibration (propres à chaque puce)
    HAL_I2C_Mem_Read(&hi2c1, bme_addr, 0x88, I2C_MEMADD_SIZE_8BIT, c, 24, 100);
    dig_T1 = U16(c, 0);  dig_T2 = S16(c, 2);  dig_T3 = S16(c, 4);
    dig_P1 = U16(c, 6);  dig_P2 = S16(c, 8);  dig_P3 = S16(c, 10);
    dig_P4 = S16(c, 12); dig_P5 = S16(c, 14); dig_P6 = S16(c, 16);
    dig_P7 = S16(c, 18); dig_P8 = S16(c, 20); dig_P9 = S16(c, 22);

    HAL_I2C_Mem_Read(&hi2c1, bme_addr, 0xA1, I2C_MEMADD_SIZE_8BIT, &h1, 1, 100);
    HAL_I2C_Mem_Read(&hi2c1, bme_addr, 0xE1, I2C_MEMADD_SIZE_8BIT, h, 7, 100);
    dig_H1 = h1;
    dig_H2 = S16(h, 0);
    dig_H3 = h[2];
    dig_H4 = (int16_t)(((int16_t)(int8_t)h[3] << 4) | (h[4] & 0x0F));
    dig_H5 = (int16_t)(((int16_t)(int8_t)h[5] << 4) | (h[4] >> 4));
    dig_H6 = (int8_t)h[6];

    uint8_t v = 0x01;   // humidité x1 (à écrire AVANT ctrl_meas)
    HAL_I2C_Mem_Write(&hi2c1, bme_addr, 0xF2, I2C_MEMADD_SIZE_8BIT, &v, 1, 100);
    v = 0x27;           // temp x1, pression x1, mode normal
    HAL_I2C_Mem_Write(&hi2c1, bme_addr, 0xF4, I2C_MEMADD_SIZE_8BIT, &v, 1, 100);
    return 0;
}

// Formules de compensation du datasheet Bosch (entiers)
static int32_t comp_T(int32_t adc)
{
    int32_t v1 = (((adc >> 3) - ((int32_t)dig_T1 << 1)) * dig_T2) >> 11;
    int32_t v2 = (((((adc >> 4) - dig_T1) * ((adc >> 4) - dig_T1)) >> 12) * dig_T3) >> 14;
    t_fine = v1 + v2;
    return (t_fine * 5 + 128) >> 8;                 // centièmes de °C
}

static uint32_t comp_P(int32_t adc)
{
    int64_t v1 = (int64_t)t_fine - 128000;
    int64_t v2 = v1 * v1 * dig_P6;
    v2 += (v1 * dig_P5) << 17;
    v2 += (int64_t)dig_P4 << 35;
    v1 = ((v1 * v1 * dig_P3) >> 8) + ((v1 * dig_P2) << 12);
    v1 = (((int64_t)1 << 47) + v1) * dig_P1 >> 33;
    if (v1 == 0) return 0;
    int64_t p = 1048576 - adc;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    v2 = ((int64_t)dig_P8 * p) >> 19;
    p = ((p + v1 + v2) >> 8) + ((int64_t)dig_P7 << 4);
    return (uint32_t)p;                             // Pa x 256
}

static uint32_t comp_H(int32_t adc)
{
    int32_t v = t_fine - 76800;
    v = (((((adc << 14) - ((int32_t)dig_H4 << 20) - (dig_H5 * v)) + 16384) >> 15) *
         (((((((v * dig_H6) >> 10) * (((v * dig_H3) >> 11) + 32768)) >> 10) + 2097152) * dig_H2 + 8192) >> 14));
    v -= (((((v >> 15) * (v >> 15)) >> 7) * dig_H1) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;
    return (uint32_t)(v >> 12);                     // %HR x 1024
}

static int bme_read(float *t, float *p, float *h)
{
    uint8_t d[8];
    if (HAL_I2C_Mem_Read(&hi2c1, bme_addr, 0xF7, I2C_MEMADD_SIZE_8BIT, d, 8, 100) != HAL_OK)
        return -1;
    int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    int32_t adc_H = ((int32_t)d[6] << 8) | d[7];
    *t = comp_T(adc_T) / 100.0f;                    // température d'abord : elle calcule t_fine
    *p = comp_P(adc_P) / 256.0f / 100.0f;           // en hPa
    *h = comp_H(adc_H) / 1024.0f;
    return 0;
}

/* ---------- Luminosité (ADC1, PA0, 10 bits : 0 à 1023) ---------- */
static ADC_HandleTypeDef hadc1;

static void adc_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin  = GPIO_PIN_0;
    g.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_10B;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    HAL_ADC_Init(&hadc1);
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel      = ADC_CHANNEL_5;                // PA0 = ADC1_IN5
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
    ch.SingleDiff   = ADC_SINGLE_ENDED;
    ch.OffsetNumber = ADC_OFFSET_NONE;
    HAL_ADC_ConfigChannel(&hadc1, &ch);
}

static uint16_t lumin_read(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;
}

/* ---------- Paramètres (en RAM pour l'instant, sauvegarde en flash plus tard) ---------- */
typedef struct {
    uint32_t log_interval;    // minutes
    uint32_t file_max_size;   // octets
    uint32_t timeout;         // secondes
} Params;

#define DEFAULT_PARAMS { 10, 2048, 30 }
static Params params = DEFAULT_PARAMS;

// Valide et applique "NOM=valeur" pour un paramètre numérique
static void set_param(uint32_t *p, const char *val, uint32_t min, uint32_t max)
{
    char *end;
    uint32_t v = strtoul(val, &end, 10);
    if (*val == 0 || *end != 0 || v < min || v > max)
        console("Valeur invalide (attendu : %lu a %lu)\r\n", (unsigned long)min, (unsigned long)max);
    else {
        *p = v;
        console("OK\r\n");
    }
}

/* ---------- Commandes de date et d'heure ---------- */
static void cmd_clock(const char *v)          // CLOCK=HH:MM:SS
{
    int h, m, s;
    DateTime t;
    if (sscanf(v, "%d:%d:%d", &h, &m, &s) != 3 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
        console("Format : CLOCK=HH:MM:SS\r\n");
        return;
    }
    if (rtc_load(&t) != 0) { console("RTC : erreur d'acces\r\n"); return; }
    t.h = h; t.m = m; t.s = s;
    if (rtc_write(&t) == 0) console("OK\r\n");
    else console("RTC : erreur d'ecriture\r\n");
}

static void cmd_date(const char *v)           // DATE=MOIS,JOUR,ANNEE
{
    int mo, d, y;
    DateTime t;
    if (sscanf(v, "%d,%d,%d", &mo, &d, &y) != 3 || mo < 1 || mo > 12 || d < 1 || d > 31 || y < 2000 || y > 2099) {
        console("Format : DATE=MM,JJ,AAAA (annee 2000 a 2099)\r\n");
        return;
    }
    if (rtc_load(&t) != 0) { console("RTC : erreur d'acces\r\n"); return; }
    t.month = mo; t.day = d; t.year = y;
    if (rtc_write(&t) == 0) console("OK\r\n");
    else console("RTC : erreur d'ecriture\r\n");
}

static void cmd_day(const char *v)            // DAY=MON..SUN
{
    DateTime t;
    int d = 0;
    for (int i = 1; i <= 7; i++)
        if (!strcmp(v, day_name[i])) d = i;
    if (!d) { console("Format : DAY=MON|TUE|WED|THU|FRI|SAT|SUN\r\n"); return; }
    if (rtc_load(&t) != 0) { console("RTC : erreur d'acces\r\n"); return; }
    t.dow = d;
    if (rtc_write(&t) == 0) console("OK\r\n");
    else console("RTC : erreur d'ecriture\r\n");
}

static void config_command(char *s)
{
    char *val = strchr(s, '=');
    if (val) *val++ = 0;                     // sépare le nom et la valeur

    if (!strcmp(s, "VERSION"))
        console("3W v0.1 - lot 0001\r\n");
    else if (!strcmp(s, "RESET")) {
        params = (Params)DEFAULT_PARAMS;
        console("Parametres reinitialises\r\n");
    }
    else if (val && !strcmp(s, "LOG_INTERVAL"))
        set_param(&params.log_interval, val, 1, 1440);
    else if (val && !strcmp(s, "FILE_MAX_SIZE"))
        set_param(&params.file_max_size, val, 512, 65536);
    else if (val && !strcmp(s, "TIMEOUT"))
        set_param(&params.timeout, val, 1, 600);
    else if (val && !strcmp(s, "CLOCK"))
        cmd_clock(val);
    else if (val && !strcmp(s, "DATE"))
        cmd_date(val);
    else if (val && !strcmp(s, "DAY"))
        cmd_day(val);
    else if (!strcmp(s, "TIME")) {            // pratique pour vérifier : affiche la date et l'heure
        char ts[24];
        timestamp(ts, sizeof(ts));
        console("%s (%s)\r\n", ts, rtc_fail ? "?" : day_name[now_dt.dow > 7 ? 0 : now_dt.dow]);
    }
    else
        console("Commande inconnue : %s\r\n", s);
}

static void console_poll(void)
{
    if (!cmd_ready) return;
    if (mode == MODE_CONFIG) {
        mode_since = HAL_GetTick();          // activité : le délai de 30 min repart de zéro
        config_command(cmd);
    } else {
        console("Refuse : commandes acceptees uniquement en mode configuration\r\n");
    }
    cmd_len = 0;
    cmd_ready = 0;
}

/* ---------- Affichage des capteurs (x2 en économique) ---------- */
static void sensors_report(void)
{
    static uint32_t last;
    uint32_t period = (mode == MODE_ECO) ? 2 * SENSOR_PRINT_MS : SENSOR_PRINT_MS;
    uint32_t now = HAL_GetTick();
    if (mode == MODE_CONFIG || now - last < period) return;   // pas d'acquisition en configuration
    last = now;

    char ts[24];
    timestamp(ts, sizeof(ts));

    float t, p, h;
    if (bme_ok && bme_read(&t, &p, &h) == 0)
        console("[%s] BME280 : T = %.1f C  P = %.1f hPa  H = %.1f %%\r\n", ts, t, p, h);
    else
        console("[%s] BME280 : NA\r\n", ts);

    uint16_t lum = lumin_read();
    const char *lvl = lum < LUMIN_LOW ? "faible" : (lum > LUMIN_HIGH ? "forte" : "moyenne");
    console("[%s] Luminosite : %u (%s)\r\n", ts, lum, lvl);
}

/* ---------- Cycle de mesure ---------- */
static void measure_cycle(void)
{
    static uint32_t last, n;
    uint32_t period = params.log_interval * 60000UL * (mode == MODE_ECO ? 2 : 1);
    uint32_t now = HAL_GetTick();

    if (mode == MODE_CONFIG || now - last < period) return;   // pas d'acquisition en configuration
    last = now;

    char ts[24];
    timestamp(ts, sizeof(ts));
    console("Mesure #%lu  date = %s\r\n", (unsigned long)++n, ts);
}

int main(void)
{
    Global_Init();   // inclut HAL_Init(), l'USART2 (console), l'UART4 (GPS, 9600 bauds) et l'I2C1

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

    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);

    HAL_Delay(10);
    console("\r\n3W demarrage\r\n");

    i2c_init();
    i2c_scan();
    bme_ok = (bme_init() == 0);
    adc_init();
    rtc_poll();
    console("RTC : %s\r\n", rtc_fail ? "non trouvee (0x68)" : "OK");

    set_mode(is_pressed(&red) ? MODE_CONFIG : MODE_STANDARD);

    while (1) {
        update_mode();
        rtc_poll();
        rtc_error_led();
        gps_poll();
        gps_report();
        sensors_report();
        console_poll();
        measure_cycle();
        HAL_Delay(10);
    }
}