#include "sd_logger.h"
#include "board_pins.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

#define SD_CMD0     0
#define SD_CMD1     1
#define SD_CMD8     8
#define SD_CMD55    55
#define SD_CMD58    58
#define SD_ACMD41   41

#define SD_R1_IDLE_STATE      0x01U
#define SD_R1_ILLEGAL_COMMAND 0x04U

#define SD_INIT_TIMEOUT_MS 1000U
#define SD_POWERUP_DELAY_MS 50U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    const char *label;
} SDChipSelect;

extern SPI_HandleTypeDef SD_SPI_HANDLE;

static SDLoggerCardType g_card_type = SDLOGGER_CARD_UNKNOWN;
static SDLoggerInitStatus g_init_status = SDLOGGER_INIT_ERR_CMD0;
static uint8_t g_active_cs_index = 0;

static const SDChipSelect g_cs_candidates[] = {
    {SD_CS_PORT, SD_CS_PIN, "D4/PB5"}
};

#define SD_CS_CANDIDATE_COUNT ((uint8_t)(sizeof(g_cs_candidates) / sizeof(g_cs_candidates[0])))

static uint8_t SD_SPI_Transfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    (void)HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &tx, &rx, 1, 100);
    return rx;
}

static void SD_Deselect(void)
{
    HAL_GPIO_WritePin(g_cs_candidates[g_active_cs_index].port,
                      g_cs_candidates[g_active_cs_index].pin,
                      GPIO_PIN_SET);
    (void)SD_SPI_Transfer(0xFF);
}

static void SD_Select(void)
{
    HAL_GPIO_WritePin(g_cs_candidates[g_active_cs_index].port,
                      g_cs_candidates[g_active_cs_index].pin,
                      GPIO_PIN_RESET);
}

static void SD_SetActiveChipSelect(uint8_t index)
{
    uint8_t i;

    g_active_cs_index = index;
    for (i = 0; i < SD_CS_CANDIDATE_COUNT; ++i)
    {
        HAL_GPIO_WritePin(g_cs_candidates[i].port, g_cs_candidates[i].pin, GPIO_PIN_SET);
    }
}

static bool SD_WaitReady(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (SD_SPI_Transfer(0xFF) == 0xFF)
        {
            return true;
        }
    }

    return false;
}

static uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t response = 0xFF;
    uint8_t frame[6];
    uint8_t i;

    SD_Deselect();
    SD_Select();

    if (cmd != SD_CMD0)
    {
        if (!SD_WaitReady(50U))
        {
            SD_Deselect();
            return 0xFF;
        }
    }

    frame[0] = (uint8_t)(0x40U | cmd);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)(arg);
    frame[5] = crc;

    for (i = 0; i < 6; ++i)
    {
        (void)SD_SPI_Transfer(frame[i]);
    }

    for (i = 0; i < 10; ++i)
    {
        response = SD_SPI_Transfer(0xFF);
        if ((response & 0x80U) == 0U)
        {
            return response;
        }
    }

    return response;
}

bool SDLogger_Init(void)
{
    uint8_t cs_index;
    uint8_t i;
    uint8_t r1;
    uint8_t r7[4];
    uint8_t ocr[4];
    uint32_t start;
    uint8_t cmd0_try;
    uint8_t idle_samples[8];
    uint8_t cmd0_samples[8];
    uint8_t sample_count;

    g_card_type = SDLOGGER_CARD_UNKNOWN;
    g_init_status = SDLOGGER_INIT_ERR_CMD0;

    for (cs_index = 0; cs_index < SD_CS_CANDIDATE_COUNT; ++cs_index)
    {
        SD_SetActiveChipSelect(cs_index);
        SD_Deselect();

        printf("[SD diag] try CS %s\r\n", g_cs_candidates[cs_index].label);

        HAL_Delay(SD_POWERUP_DELAY_MS);

        /* Send at least 74 clocks with CS high as mandated by SD SPI mode. */
        for (i = 0; i < 10; ++i)
        {
            (void)SD_SPI_Transfer(0xFF);
        }

        for (i = 0; i < 8; ++i)
        {
            idle_samples[i] = SD_SPI_Transfer(0xFF);
        }
        printf("[SD diag] MISO idle bytes: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               idle_samples[0], idle_samples[1], idle_samples[2], idle_samples[3],
               idle_samples[4], idle_samples[5], idle_samples[6], idle_samples[7]);

        r1 = 0xFFU;
        sample_count = 0U;
        for (cmd0_try = 0; cmd0_try < 32U; ++cmd0_try)
        {
            r1 = SD_SendCommand(SD_CMD0, 0x00000000UL, 0x95U);
            SD_Deselect();

            if (sample_count < 8U)
            {
                cmd0_samples[sample_count] = r1;
                ++sample_count;
            }

            if (r1 == SD_R1_IDLE_STATE)
            {
                break;
            }

            HAL_Delay(2U);
        }

        if (sample_count > 0U)
        {
            printf("[SD diag] CMD0 R1 first bytes:");
            for (i = 0; i < sample_count; ++i)
            {
                printf(" %02X", cmd0_samples[i]);
            }
            printf("\r\n");
        }

        if (r1 != SD_R1_IDLE_STATE)
        {
            g_init_status = SDLOGGER_INIT_ERR_CMD0;
            printf("[SD diag] CMD0 failed on %s\r\n", g_cs_candidates[cs_index].label);
            continue;
        }

        r1 = SD_SendCommand(SD_CMD8, 0x000001AAUL, 0x87U);
        if (r1 == SD_R1_IDLE_STATE)
        {
            for (i = 0; i < 4; ++i)
            {
                r7[i] = SD_SPI_Transfer(0xFF);
            }
            SD_Deselect();

            if ((r7[2] != 0x01U) || (r7[3] != 0xAAU))
            {
                g_init_status = SDLOGGER_INIT_ERR_CMD8_PATTERN;
                continue;
            }

            start = HAL_GetTick();
            do
            {
                r1 = SD_SendCommand(SD_CMD55, 0x00000000UL, 0x01U);
                SD_Deselect();
                if (r1 > 0x01U)
                {
                    g_init_status = SDLOGGER_INIT_ERR_ACMD41_TIMEOUT;
                    break;
                }

                r1 = SD_SendCommand(SD_ACMD41, 0x40000000UL, 0x01U);
                SD_Deselect();
                if (r1 == 0x00U)
                {
                    break;
                }
            }
            while ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS);

            if (r1 != 0x00U)
            {
                g_init_status = SDLOGGER_INIT_ERR_ACMD41_TIMEOUT;
                continue;
            }

            r1 = SD_SendCommand(SD_CMD58, 0x00000000UL, 0x01U);
            if (r1 != 0x00U)
            {
                SD_Deselect();
                g_init_status = SDLOGGER_INIT_ERR_CMD58;
                continue;
            }

            for (i = 0; i < 4; ++i)
            {
                ocr[i] = SD_SPI_Transfer(0xFF);
            }
            SD_Deselect();

            g_card_type = ((ocr[0] & 0x40U) != 0U) ? SDLOGGER_CARD_SDHC : SDLOGGER_CARD_SDSC;
            g_init_status = SDLOGGER_INIT_OK;
            return true;
        }

        if ((r1 & SD_R1_ILLEGAL_COMMAND) == 0U)
        {
            SD_Deselect();
            g_init_status = SDLOGGER_INIT_ERR_CMD8_PATTERN;
            continue;
        }

        SD_Deselect();

        start = HAL_GetTick();
        do
        {
            r1 = SD_SendCommand(SD_CMD1, 0x00000000UL, 0x01U);
            SD_Deselect();
            if (r1 == 0x00U)
            {
                g_card_type = SDLOGGER_CARD_SDSC;
                g_init_status = SDLOGGER_INIT_OK;
                return true;
            }
        }
        while ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS);

        g_init_status = SDLOGGER_INIT_ERR_CMD1_TIMEOUT;
    }

    return false;
}

SDLoggerCardType SDLogger_GetCardType(void)
{
    return g_card_type;
}

SDLoggerInitStatus SDLogger_GetLastInitStatus(void)
{
    return g_init_status;
}

const char *SDLogger_GetActiveCsLabel(void)
{
    return g_cs_candidates[g_active_cs_index].label;
}

/* ---------- bit-bang diagnostic ----------------------------------------- */

static void SD_ReconfigPinsGPIO(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Disable SPI1 peripheral while we take over the pins */
    SD_SPI_HANDLE.Instance->CR1 &= ~SPI_CR1_SPE;

    /* PA5 = SCK, PA7 = MOSI: push-pull outputs */
    gpio.Pin   = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); /* SCK idle low  */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);   /* MOSI idle high */

    /* PA6 = MISO: input with pull-up */
    gpio.Pin  = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void SD_ReconfigPinsSPI(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin       = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin  = GPIO_PIN_6;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    SD_SPI_HANDLE.Instance->CR1 |= SPI_CR1_SPE;
}

static uint8_t SD_BitBangByte(uint8_t tx)
{
    uint8_t rx = 0;
    uint8_t bit;

    for (bit = 0; bit < 8U; ++bit)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7,
                          (tx & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        tx <<= 1;
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);   /* rising edge  */
        rx <<= 1;
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_SET)
        {
            rx |= 1U;
        }
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); /* falling edge */
    }

    return rx;
}

bool SDLogger_BitBangCMD0Test(void)
{
    uint8_t i;
    uint8_t r1 = 0xFFU;
    GPIO_PinState cs_rb;

    SD_ReconfigPinsGPIO();

    /* --- GPIO self-test: verify CS output and MISO input are accessible --- */
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    cs_rb = HAL_GPIO_ReadPin(SD_CS_PORT, SD_CS_PIN);
    printf("[SD bitbang] CS readback HIGH: %d (expected 1)\r\n", cs_rb);

    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
    cs_rb = HAL_GPIO_ReadPin(SD_CS_PORT, SD_CS_PIN);
    printf("[SD bitbang] CS readback LOW:  %d (expected 0)\r\n", cs_rb);

    /* Raw IDR dump: GPIOA bits 5(SCK) 6(MISO) 7(MOSI), GPIOB bit 5(CS) */
    {
        uint32_t idr_a = GPIOA->IDR;
        uint32_t idr_b = GPIOB->IDR;
        printf("[SD bitbang] GPIOA IDR: SCK(5)=%lu MISO(6)=%lu MOSI(7)=%lu\r\n",
               (idr_a >> 5U) & 1U, (idr_a >> 6U) & 1U, (idr_a >> 7U) & 1U);
        printf("[SD bitbang] GPIOB IDR: CS(5)=%lu\r\n", (idr_b >> 5U) & 1U);
    }

    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    HAL_Delay(10U);

    /* --- 80 init clocks with CS high --- */
    for (i = 0; i < 10U; ++i)
    {
        (void)SD_BitBangByte(0xFFU);
    }

    /* MISO state just before CMD0 */
    printf("[SD bitbang] MISO before CMD0: %d\r\n",
           HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6));

    /* CS low, send CMD0 */
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
    (void)SD_BitBangByte(0x40U);
    (void)SD_BitBangByte(0x00U);
    (void)SD_BitBangByte(0x00U);
    (void)SD_BitBangByte(0x00U);
    (void)SD_BitBangByte(0x00U);
    (void)SD_BitBangByte(0x95U);

    printf("[SD bitbang] CMD0 responses:");
    for (i = 0; i < 10U; ++i)
    {
        r1 = SD_BitBangByte(0xFFU);
        printf(" %02X", r1);
        if ((r1 & 0x80U) == 0U)
        {
            break;
        }
    }
    printf("\r\n");

    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    (void)SD_BitBangByte(0xFFU);

    SD_ReconfigPinsSPI();

    if (r1 == 0x01U)
    {
        printf("[SD bitbang] CMD0 OK -> SPI hardware config issue\r\n");
        return true;
    }

    printf("[SD bitbang] CMD0 still failed -> physical wiring/shield issue\r\n");
    return false;
}

bool SDLogger_WriteLine(const char *filename, const char *line)
{
    (void)filename;
    (void)line;

    /* File write support needs FatFs + diskio SPI. */
    return false;
}
