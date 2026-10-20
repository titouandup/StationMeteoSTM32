#ifndef MESURES_H
#define MESURES_H

#include <stdint.h>

extern uint8_t sd_error;   // erreur d'accès ou d'écriture
extern uint8_t sd_full;    // carte pleine

void sd_boot(void);                  // monte la carte et ouvre le fichier du jour (AAMMJJ_0.LOG) (message sur la console)
void log_stop(void);                 // ferme le fichier et démonte la carte (retrait sans risque)

void sd_watch(void);                 // détecte le retrait / retour de la carte SD

void sensor_watch(void);             // détecte le débranchement / retour du BME280

void gps_watch(void);                // détecte le débranchement / retour du GPS

void sensors_report(void);   // affichage périodique sur la console
void measure_cycle(void);     // mesure + écriture SD toutes les LOG_INTERVAL minutes

#endif
