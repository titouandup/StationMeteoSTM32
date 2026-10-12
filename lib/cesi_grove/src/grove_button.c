#include "grove_button.h"
#include "board_pins.h"

void GroveButton_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;

    gpio.Pin = BUTTON1_PIN;
    HAL_GPIO_Init(BUTTON1_PORT, &gpio);

    gpio.Pin = BUTTON2_PIN;
    HAL_GPIO_Init(BUTTON2_PORT, &gpio);
}

bool GroveButton1_IsPressed(void)
{
    return HAL_GPIO_ReadPin(BUTTON1_PORT, BUTTON1_PIN) == GPIO_PIN_RESET;
}

bool GroveButton2_IsPressed(void)
{
    return HAL_GPIO_ReadPin(BUTTON2_PORT, BUTTON2_PIN) == GPIO_PIN_RESET;
}