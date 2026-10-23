#include "console.h"
#include <stdio.h>
#include <stdarg.h>
#include "stm32l4xx_hal.h"
#include "capteurs.h"

extern UART_HandleTypeDef huart2;   // console (USB ST-Link)

static char cmd[64];
static uint8_t cmd_len;
static volatile uint8_t cmd_ready;

/* ---------- Console (USART2, sans printf) ---------- */
// Affiche un texte (comme printf) sur la console série
void console(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
    if (n > 0) HAL_UART_Transmit(&huart2, (uint8_t *)buf, n, 100);
}

// Appelée à chaque octet reçu sur la console : assemble une ligne de commande
void console_rx_byte(char c)
{
    if (cmd_ready) return;
    if (c == '\r' || c == '\n') {
        if (cmd_len > 0) { cmd[cmd_len] = 0; cmd_ready = 1; }
    } else if (cmd_len < sizeof(cmd) - 1) {
        cmd[cmd_len++] = c;
    }
}

char *console_cmd(void)                       // ligne complète reçue, ou NULL
{
    return cmd_ready ? cmd : NULL;
}

// Libère la ligne de commande une fois traitée, pour pouvoir en recevoir une autre
void console_cmd_done(void)
{
    cmd_len = 0;
    cmd_ready = 0;
}

extern UART_HandleTypeDef huart4;   // GPS Air530Z (port A1 : PA1 = RX)

static uint8_t rx2_byte;            // console

void UART4_IRQHandler(void)  { HAL_UART_IRQHandler(&huart4); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }

// Active les interruptions des deux liaisons série et lance l'écoute de la console
void serial_start(void)
{
    HAL_NVIC_SetPriority(UART4_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);            // la réception du GPS n'est armée que pendant une acquisition (gps_start)

    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
}

// Appelée à chaque octet reçu
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    if (h == &huart2) {
        console_rx_byte((char)rx2_byte);
        HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
    } else if (h == &huart4) {
        gps_rx_done();
    }
}

// Après une erreur (overrun...), la réception s'arrête : on la relance
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    if (h == &huart4) gps_rx_error();
    if (h == &huart2) HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
}
