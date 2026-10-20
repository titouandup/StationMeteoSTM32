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
void gps_start(void);              // écoute le GPS (début d'une acquisition)
void gps_stop(void);               // arrête la réception : le GPS n'est plus lu
int  gps_fresh(void);              // 1 si une trame RMC est arrivée depuis gps_start()
void gps_rx_done(void);            // à appeler quand un octet du GPS est reçu (callback UART)
void gps_rx_error(void);           // à appeler après une erreur de réception du GPS

#endif
