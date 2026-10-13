#ifndef MESURES_H
#define MESURES_H

#include <stdint.h>

extern uint8_t sd_error;

void sd_boot(void);                  // monte la carte et ouvre DATA.LOG (message sur la console)
void log_stop(void);                 // ferme le fichier et démonte la carte (retrait sans risque)

void sensors_report(void);   // affichage périodique sur la console
void measure_cycle(void);     // mesure + écriture SD toutes les LOG_INTERVAL minutes

#endif
