#ifndef I2C_H
#define I2C_H

#include <stddef.h>
#include <stdint.h>

void i2c_init(void);   // règle le timing 100 kHz selon PCLK1
void i2c_scan(void);   // liste les modules présents sur le bus

#define TS_LEN 32   // taille d'un horodatage "JJ/MM/AAAA hh:mm:ss" + marge

typedef struct { uint8_t h, m, s, dow, day, month; uint16_t year; } DateTime;   // dow : 1 = MON ... 7 = SUN

extern DateTime now_dt;        // dernière lecture de la RTC
extern uint8_t  rtc_fail;      // 1 tant que la RTC ne répond pas
extern const char *day_name[]; // indexé par dow (1 = MON)

int  rtc_load(DateTime *t);    // lecture avant modification (0 = OK)
int  rtc_write(const DateTime *t);
void rtc_poll(void);           // lit la RTC une fois par seconde
void timestamp(char *buf, size_t n);   // "JJ/MM/AAAA hh:mm:ss" ou "NA"

extern uint8_t bme_ok;   // 1 si le capteur a été trouvé au démarrage

int bme_init(void);                          // 0 = OK
int bme_read(float *t, float *p, float *h);  // °C, hPa, %HR ; 0 = OK

#endif
