#ifndef MODES_H
#define MODES_H

#include <stdint.h>
#include "stm32l4xx_hal.h"

void led_init(void);
void leds_set_all(uint8_t r, uint8_t g, uint8_t b);

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint32_t since;
    uint8_t active, fired;
} Button;

extern Button button_green, button_red;

void buttons_init(void);
int  button_pressed(const Button *b);
int  button_held(Button *b);     // vrai une seule fois après 5 s d'appui

typedef enum { MODE_CONFIG, MODE_STANDARD, MODE_ECO, MODE_MAINT } Mode;

extern Mode mode;
extern uint32_t mode_since;     // instant du dernier changement de mode (ou activité en configuration)

void set_mode(Mode m);
void update_mode(void);         // changements de mode par les boutons
// Erreurs, par ordre de priorité d'affichage sur la LED (la plus prioritaire en premier)
typedef enum {
    ERR_RTC = 1,         // E01 : rouge / bleu 1 Hz
    ERR_SD_IO,           // E02 : rouge / blanc, blanc 2 fois plus long
    ERR_SD_FULL,         // E03 : rouge / blanc, durées égales
    ERR_GPS,             // E04 : rouge / jaune, durées égales
    ERR_SENSOR,          // E05 : rouge / vert, durées égales
    ERR_INCOHERENT,      // E06 : rouge / vert, vert 2 fois plus long
    ERR_COUNT
} ErrCode;

void error_set(ErrCode e, int on);   // lève ou retire une erreur (message console au moment où elle apparaît)
void error_led(void);                // LED d'erreur (prioritaire sur la couleur du mode)

#endif
