#ifndef GROVE_BME680_H
#define GROVE_BME680_H

#include <stdbool.h>
#include "cesi_types.h"

bool GroveBME680_Init(void);
bool GroveBME680_ReadChipId(uint8_t *chip_id);
bool GroveBME680_ReadEnvironment(Env_Data *data);

#endif
