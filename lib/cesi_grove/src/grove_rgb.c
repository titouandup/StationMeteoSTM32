#include "grove_rgb.h"
#include "board_pins.h"

static void GroveRGB_ClockPulse(void)
{
    HAL_GPIO_WritePin(RGB_CLK_PORT, RGB_CLK_PIN, GPIO_PIN_RESET);
    for (volatile int i = 0; i < 50; i++);

    HAL_GPIO_WritePin(RGB_CLK_PORT, RGB_CLK_PIN, GPIO_PIN_SET);
    for (volatile int i = 0; i < 50; i++);
}

static void GroveRGB_SendByte(uint8_t value)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        HAL_GPIO_WritePin(
            RGB_DATA_PORT,
            RGB_DATA_PIN,
            (value & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET
        );

        GroveRGB_ClockPulse();
        value <<= 1;
    }
}

static void GroveRGB_SendColor(uint8_t r, uint8_t g, uint8_t b)
{
    /*
     * Grove Chainable RGB LED / P9813
     * Format : prefix + B + G + R
     */
    uint8_t prefix = 0xC0;

    if ((b & 0x80) == 0) prefix |= 0x20;
    if ((b & 0x40) == 0) prefix |= 0x10;
    if ((g & 0x80) == 0) prefix |= 0x08;
    if ((g & 0x40) == 0) prefix |= 0x04;
    if ((r & 0x80) == 0) prefix |= 0x02;
    if ((r & 0x40) == 0) prefix |= 0x01;

    GroveRGB_SendByte(prefix);
    GroveRGB_SendByte(b);
    GroveRGB_SendByte(g);
    GroveRGB_SendByte(r);
}

void GroveRGB_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = RGB_DATA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RGB_DATA_PORT, &gpio);

    gpio.Pin = RGB_CLK_PIN;
    HAL_GPIO_Init(RGB_CLK_PORT, &gpio);

    GroveRGB_Off();
}

void GroveRGB_SetColor(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 4; i++) GroveRGB_SendByte(0x00);
    GroveRGB_SendColor(r, g, b);
    for (int i = 0; i < 4; i++) GroveRGB_SendByte(0x00);
}

void GroveRGB_Off(void)
{
    GroveRGB_SetColor(0, 0, 0);
}