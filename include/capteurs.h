#ifndef CAPTEURS_H
#define CAPTEURS_H

#include <stdint.h>

#define LUMIN_LOW   255
#define LUMIN_HIGH  768

void     adc_init(void);
uint16_t lumin_read(void);   // 0 à 1023

extern int    gps_valid;           // 1 = fix valide
extern double gps_lat, gps_lon;    // degrés décimaux

void gps_rx_byte(char c);          // à appeler pour chaque octet reçu
void gps_poll(void);               // décode la dernière trame RMC
void gps_report(void);             // affichage périodique sur la console

#endif
