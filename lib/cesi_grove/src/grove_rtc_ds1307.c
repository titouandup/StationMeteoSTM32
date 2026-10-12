#include "grove_rtc_ds1307.h"
#include "board_pins.h"

extern I2C_HandleTypeDef GROVE_I2C_HANDLE;

#define DS1307_ADDR (0x68 << 1)

static uint8_t bcd_to_dec(uint8_t bcd)
{
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

static uint8_t dec_to_bcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

bool GroveRTC_Init(void)
{
    return HAL_I2C_IsDeviceReady(&GROVE_I2C_HANDLE, DS1307_ADDR, 3, 100) == HAL_OK;
}

bool GroveRTC_GetDateTime(RTC_DateTime *dt)
{
    if (dt == 0) return false;

    uint8_t reg = 0x00;
    uint8_t data[7] = {0};

    if (HAL_I2C_Master_Transmit(&GROVE_I2C_HANDLE, DS1307_ADDR, &reg, 1, 100) != HAL_OK)
        return false;

    if (HAL_I2C_Master_Receive(&GROVE_I2C_HANDLE, DS1307_ADDR, data, 7, 100) != HAL_OK)
        return false;

    dt->seconds   = bcd_to_dec(data[0] & 0x7F);
    dt->minutes   = bcd_to_dec(data[1]);
    dt->hours     = bcd_to_dec(data[2] & 0x3F);
    dt->dayOfWeek = bcd_to_dec(data[3]);
    dt->day       = bcd_to_dec(data[4]);
    dt->month     = bcd_to_dec(data[5]);
    dt->year      = 2000 + bcd_to_dec(data[6]);

    return true;
}

bool GroveRTC_SetDateTime(const RTC_DateTime *dt)
{
    if (dt == 0) return false;

    uint8_t data[8];

    data[0] = 0x00;
    data[1] = dec_to_bcd(dt->seconds) & 0x7F; // CH=0 : oscillator enabled
    data[2] = dec_to_bcd(dt->minutes);
    data[3] = dec_to_bcd(dt->hours);
    data[4] = dec_to_bcd(dt->dayOfWeek);
    data[5] = dec_to_bcd(dt->day);
    data[6] = dec_to_bcd(dt->month);
    data[7] = dec_to_bcd((uint8_t)(dt->year - 2000));

    return HAL_I2C_Master_Transmit(&GROVE_I2C_HANDLE, DS1307_ADDR, data, 8, 100) == HAL_OK;
}
