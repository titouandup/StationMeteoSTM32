#ifndef CESI_TYPES_H
#define CESI_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t dayOfWeek;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} RTC_DateTime;

typedef struct
{
    bool valid;
    char raw_sentence[96];

    double latitude;
    double longitude;

    uint8_t fix_quality;
    uint8_t satellites;
} GPS_Data;

typedef struct
{
    float temperature_c;
    float humidity_percent;
    float pressure_hpa;
    float gas_resistance_ohm;
} Env_Data;

#endif
