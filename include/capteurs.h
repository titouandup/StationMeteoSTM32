#ifndef CAPTEURS_H
#define CAPTEURS_H

#include <stdint.h>
#include "settings.h"


void     adc_init(void);
uint16_t lumin_read(void);   // 0 à 1023

extern int    gps_valid, gps_alt_valid;           // 1 = fix valide
extern double gps_lat, gps_lon, gps_alt;    // degrés décimaux

void gps_rx_byte(char c);          // à appeler pour chaque octet reçu
void gps_poll(void);               // décode la dernière trame RMC
int  gps_alive(uint32_t max_ms);   // 1 si une trame RMC est arrivée depuis moins de max_ms
void gps_report(void);             // affichage périodique sur la console

#endif
