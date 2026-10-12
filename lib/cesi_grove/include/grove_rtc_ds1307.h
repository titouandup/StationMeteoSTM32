#ifndef GROVE_RTC_DS1307_H
#define GROVE_RTC_DS1307_H

#include <stdbool.h>
#include "cesi_types.h"

bool GroveRTC_Init(void);
bool GroveRTC_GetDateTime(RTC_DateTime *dt);
bool GroveRTC_SetDateTime(const RTC_DateTime *dt);

#endif
