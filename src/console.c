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
// Affiche un texte sur la console série, comme printf. Le message est mis en forme dans un tampon de 128
// caractères (un texte plus long est coupé), puis envoyé sur l'USART2 (USB ST-Link).
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

// Appelée à chaque octet reçu sur la console : assemble une ligne de commande. Entrée (\r ou \n) termine
// la ligne ; les autres caractères sont ajoutés (63 au plus). Tant qu'une commande attend d'être traitée,
// les nouveaux caractères sont ignorés.
void console_rx_byte(char c)
{
    if (cmd_ready) return;
    if (c == '\r' || c == '\n') {
        if (cmd_len > 0) { cmd[cmd_len] = 0; cmd_ready = 1; }
    } else if (cmd_len < sizeof(cmd) - 1) {
        cmd[cmd_len++] = c;
    }
}

// Retourne la ligne de commande complète reçue, ou NULL s'il n'y en a pas.
char *console_cmd(void)
{
    return cmd_ready ? cmd : NULL;
}

// Libère la ligne de commande une fois traitée, pour pouvoir en recevoir une autre.
void console_cmd_done(void)
{
    cmd_len = 0;
    cmd_ready = 0;
}

extern UART_HandleTypeDef huart4;   // GPS Air530Z (port A1 : PA1 = RX)

static uint8_t rx2_byte;            // console

// Interruptions des liaisons série : le processeur les appelle dès qu'un octet arrive, et HAL les redirige
// vers HAL_UART_RxCpltCallback et HAL_UART_ErrorCallback.
void UART4_IRQHandler(void)  { HAL_UART_IRQHandler(&huart4); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }

// Active les interruptions des deux liaisons série. La console écoute en permanence (un octet à la fois) ;
// le GPS n'est armé que pendant une acquisition (gps_start).
void serial_start(void)
{
    HAL_NVIC_SetPriority(UART4_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);            // la réception du GPS n'est armée que pendant une acquisition (gps_start)

    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
}

// Appelée par HAL à chaque octet reçu. Console : l'octet est traité, puis l'écoute est relancée. GPS :
// voir gps_rx_done.
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    if (h == &huart2) {
        console_rx_byte((char)rx2_byte);
        HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
    } else if (h == &huart4) {
        gps_rx_done();
    }
}

// Après une erreur de réception (octet perdu...), la réception s'arrête : on la relance pour la console et
// pour le GPS.
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    if (h == &huart4) gps_rx_error();
    if (h == &huart2) HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
}
