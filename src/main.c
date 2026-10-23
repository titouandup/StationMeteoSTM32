#include "stm32l4xx_hal.h"
#include "capteurs.h"
#include "config.h"
#include "console.h"
#include "i2c.h"
#include "mesures.h"
#include "modes.h"
#include "setup.h"

// Programme principal.
// - Initialise le matériel, puis relit les réglages enregistrés en flash.
// - Prépare l'interface (LED, boutons, console), les capteurs (I2C, BME280, ADC, RTC) et la carte SD.
// - Choisit le mode de départ : configuration si le bouton rouge est maintenu, sinon standard.
// - Boucle sans fin : toutes les 10 ms, chaque fonction fait un petit travail puis rend la main (boutons,
//   heure, LED, GPS, commandes console, mesure, surveillance de la carte SD).
int main(void)
{
    Global_Init();
    params_load();   // inclut HAL_Init(), l'USART2 (console), l'UART4 (GPS, 9600 bauds), l'I2C1 et le SPI1

    led_init();
    buttons_init();
    serial_start();

    HAL_Delay(10);
    console("\r\n3W demarrage\r\n");

    i2c_init();
    i2c_scan();
    bme_ok = (bme_init() == 0);
    adc_init();
    rtc_poll();
    console("RTC : %s\r\n", rtc_fail ? "non trouvee (0x68)" : "OK");
    sd_boot();

    set_mode(button_pressed(&button_red) ? MODE_CONFIG : MODE_STANDARD);

    while (1) {
        update_mode();
        rtc_poll();
        error_led();
        gps_poll();
        console_poll();
        measure_cycle();
        sd_watch();
        HAL_Delay(10);
    }
}
