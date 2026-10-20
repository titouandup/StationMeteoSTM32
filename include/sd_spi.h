#ifndef SD_SPI_H
#define SD_SPI_H

#include <stdint.h>

#define SD_BLOCK 512

int         sd_init(void);                               // 0 = OK, sinon code d'erreur
int         sd_probe(void);                              // 1 = une carte répond (test rapide avant sd_init)
int         sd_present(void);                            // 1 = la carte répond encore
int         sd_last_error(void);                         // code de la dernière initialisation
const char *sd_error_text(int code);
const char *sd_type_name(void);
uint32_t    sd_sector_count(void);                       // nombre de secteurs de 512 octets
int         sd_read_block(uint32_t lba, uint8_t *buf);   // 0 = OK
int         sd_write_block(uint32_t lba, const uint8_t *buf);   // 0 = OK

#endif