#ifndef GROVE_GPS_AIR530Z_H
#define GROVE_GPS_AIR530Z_H

#include <stdbool.h>
#include "cesi_types.h"

void GroveGPS_Init(void);
bool GroveGPS_ReadLine(char *buffer, unsigned int max_len, unsigned int timeout_ms);
bool GroveGPS_ReadGGA(GPS_Data *gps, unsigned int timeout_ms);

#endif
