#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "settings.h"

typedef struct {
    int32_t log_interval;     // minutes
    int32_t file_max_size;    // octets
    int32_t timeout;          // secondes
    int32_t lumin, lumin_low, lumin_high;
    int32_t temp_air, min_temp_air, max_temp_air;
    int32_t hygr, hygr_mint, hygr_maxt;
    int32_t pressure, pressure_min, pressure_max;
} Params;

#define DEFAULT_PARAMS { LOG_INTERVAL_DEFAULT, FILE_MAX_SIZE_DEFAULT, TIMEOUT_DEFAULT, \
                         LUMIN_DEFAULT, LUMIN_LOW_DEFAULT, LUMIN_HIGH_DEFAULT, \
                         TEMP_AIR_DEFAULT, MIN_TEMP_AIR_DEFAULT, MAX_TEMP_AIR_DEFAULT, \
                         HYGR_DEFAULT, HYGR_MINT_DEFAULT, HYGR_MAXT_DEFAULT, \
                         PRESSURE_DEFAULT, PRESSURE_MIN_DEFAULT, PRESSURE_MAX_DEFAULT }

extern Params params;

void params_load(void);    // relit les paramètres en flash (valeurs par défaut si absents ou corrompus)
void params_save(void);    // enregistre les paramètres en flash

void console_poll(void);   // exécute la commande reçue sur la console (mode configuration seulement)

#endif
