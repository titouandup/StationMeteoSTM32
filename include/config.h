#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

typedef struct {
    uint32_t log_interval;    // minutes
    uint32_t file_max_size;   // octets
    uint32_t timeout;         // secondes
} Params;

#define DEFAULT_PARAMS { 10, 2048, 30 }

extern Params params;

void set_param(uint32_t *p, const char *val, uint32_t min, uint32_t max);   // valide et applique

void console_poll(void);   // exécute la commande reçue sur la console (mode configuration seulement)

#endif
