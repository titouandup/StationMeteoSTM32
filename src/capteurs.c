#include "capteurs.h"
#include <stdlib.h>
#include <string.h>
#include "stm32l4xx_hal.h"
#include "console.h"
#include "modes.h"

// Luminosité (ADC1, PA0, 10 bits : 0 à 1023)
static ADC_HandleTypeDef hadc1;

void adc_init(void)
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

uint16_t lumin_read(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;
}

#define GPS_PRINT_MS    2000UL   // période d'affichage (x2 en économique)
#define GPS_SILENCE_MS  3000UL   // sans trame RMC depuis ce délai = problème de câblage

static char line[100], rmc[100];
static uint8_t idx;
static volatile uint8_t rmc_ready;

// Appelée à chaque octet reçu du GPS : on ne garde que les trames RMC
void gps_rx_byte(char c)
{
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

int gps_valid;
double gps_lat, gps_lon;
static char gps_time[7] = "------";    // hhmmss
static uint32_t last_rmc_tick;

void gps_poll(void)
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

void gps_report(void)
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
