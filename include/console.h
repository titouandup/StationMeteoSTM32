#ifndef CONSOLE_H
#define CONSOLE_H

void  console(const char *fmt, ...);   // écrit sur USART2 (sans printf)
void  console_rx_byte(char c);            // à appeler pour chaque octet reçu
char *console_cmd(void);                  // ligne de commande complète, ou NULL
void  console_cmd_done(void);             // libère la ligne pour la suivante

void serial_start(void);   // active les interruptions de réception : GPS (UART4) et console (USART2)

#endif
