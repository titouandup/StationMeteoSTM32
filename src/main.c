#include "stm32l4xx_hal.h"
#include "capteurs.h"
#include "config.h"
#include "console.h"
#include "i2c.h"
#include "mesures.h"
#include "modes.h"
#include "setup.h"

// Démarrage : initialise tout, puis boucle sans fin (toutes les 10 ms : boutons, heure, LED, GPS, console, mesure, carte SD)
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
