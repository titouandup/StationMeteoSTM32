#include "grove_gps_air530z.h"
#include "board_pins.h"
#include <string.h>
#include <stdlib.h>

UART_HandleTypeDef GPS_UART_HANDLE;

static double nmea_to_decimal(const char *value, const char hemisphere)
{
    if (value == 0 || value[0] == '\0') return 0.0;

    double raw = atof(value);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - (degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 'W')
        decimal = -decimal;

    return decimal;
}

void GroveGPS_Init(void)
{
    /*
        * Port Grove A0 : Signal1=PA0=GPS_TX, Signal2=PA1=GPS_RX.
        * Le module Air530Z émet sur Signal1 (PA0).
        * Pin swap UART4 activé : PA0=UART4_RX, PA1=UART4_TX.
        * L'init GPIO est centralisée dans HAL_UART_MspInit (setup.c).
     */
    GPS_UART_HANDLE.Instance = UART4;
    GPS_UART_HANDLE.Init.BaudRate = 9600;
    GPS_UART_HANDLE.Init.WordLength = UART_WORDLENGTH_8B;
    GPS_UART_HANDLE.Init.StopBits = UART_STOPBITS_1;
    GPS_UART_HANDLE.Init.Parity = UART_PARITY_NONE;
    GPS_UART_HANDLE.Init.Mode = UART_MODE_TX_RX;
    GPS_UART_HANDLE.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    GPS_UART_HANDLE.Init.OverSampling = UART_OVERSAMPLING_16;
    GPS_UART_HANDLE.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    GPS_UART_HANDLE.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
    GPS_UART_HANDLE.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_ENABLE;

    HAL_UART_Init(&GPS_UART_HANDLE);
}

bool GroveGPS_ReadLine(char *buffer, unsigned int max_len, unsigned int timeout_ms)
{
    if (buffer == 0 || max_len == 0) return false;

    unsigned int count = 0;
    uint8_t c = 0;
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms && count < (max_len - 1))
    {
        if (HAL_UART_Receive(&GPS_UART_HANDLE, &c, 1, 10) == HAL_OK)
        {
            if (c == '\n')
            {
                buffer[count] = '\0';
                return true;
            }

            if (c != '\r')
            {
                buffer[count++] = (char)c;
            }
        }
    }

    buffer[count] = '\0';
    return count > 0;
}

bool GroveGPS_ReadGGA(GPS_Data *gps, unsigned int timeout_ms)
{
    if (gps == 0) return false;

    char line[96];
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (!GroveGPS_ReadLine(line, sizeof(line), 500))
            continue;

        if (strncmp(line, "$GPGGA", 6) != 0 && strncmp(line, "$GNGGA", 6) != 0)
            continue;

        strncpy(gps->raw_sentence, line, sizeof(gps->raw_sentence) - 1);
        gps->raw_sentence[sizeof(gps->raw_sentence) - 1] = '\0';

        /*
         * Format GGA :
         * $GPGGA,time,lat,N,lon,E,fix,sats,hdop,alt,M,...
         */
        char copy[96];
        strncpy(copy, line, sizeof(copy));
        copy[sizeof(copy) - 1] = '\0';

        char *fields[16] = {0};
        int i = 0;
        char *token = strtok(copy, ",");

        while (token != 0 && i < 16)
        {
            fields[i++] = token;
            token = strtok(0, ",");
        }

        if (i < 8) return false;

        gps->latitude = nmea_to_decimal(fields[2], fields[3] ? fields[3][0] : 'N');
        gps->longitude = nmea_to_decimal(fields[4], fields[5] ? fields[5][0] : 'E');
        gps->fix_quality = fields[6] ? (uint8_t)atoi(fields[6]) : 0;
        gps->satellites = fields[7] ? (uint8_t)atoi(fields[7]) : 0;
        gps->valid = gps->fix_quality > 0;

        return true;
    }

    return false;
}
