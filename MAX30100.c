#include "MAX30100.h"

#include <xc.h>
#include <util/delay.h>

#include "mcc_generated_files/i2c_host/twi0.h"

bool MAX30100_WriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t data[2];

    data[0] = reg;
    data[1] = value;

    if (!TWI0_Write(MAX30100_ADDR, data, 2))
    {
        return false;
    }

    while (TWI0_IsBusy());

    return true;
}

bool MAX30100_ReadRegister(uint8_t reg, uint8_t *value)
{
    if (value == 0)
    {
        return false;
    }

    if (!TWI0_WriteRead(MAX30100_ADDR, &reg, 1, value, 1))
    {
        return false;
    }

    while (TWI0_IsBusy());

    return true;
}

bool MAX30100_ReadFIFO(MAX30100_Sample_t *sample)
{
    uint8_t reg = MAX30100_REG_FIFO_DATA;
    uint8_t raw[4];

    if (sample == 0)
    {
        return false;
    }

    if (!TWI0_WriteRead(MAX30100_ADDR, &reg, 1, raw, 4))
    {
        return false;
    }

    while (TWI0_IsBusy());

    sample->ir  = ((uint16_t)raw[0] << 8) | raw[1];
    sample->red = ((uint16_t)raw[2] << 8) | raw[3];

    return true;
}

void MAX30100_ResetFIFO(void)
{
    MAX30100_WriteRegister(MAX30100_REG_FIFO_WR_PTR, 0x00);
    MAX30100_WriteRegister(MAX30100_REG_OVF_COUNTER, 0x00);
    MAX30100_WriteRegister(MAX30100_REG_FIFO_RD_PTR, 0x00);
}

void MAX30100_Init(void)
{
    // Sensor resetten
    MAX30100_WriteRegister(MAX30100_REG_MODE_CONFIG, MAX30100_MODE_RESET);
    _delay_ms(100);

    // FIFO leeren
    MAX30100_ResetFIFO();

    // SpO2-Konfiguration:
    // 100 Samples/s, 1600 us LED-Pulsweite
    MAX30100_WriteRegister(MAX30100_REG_SPO2_CONFIG, MAX30100_SPO2_CONFIG_100HZ_1600US);

    // LED-Strom einstellen
    // 0xFF = beide LEDs maximal
    MAX30100_WriteRegister(MAX30100_REG_LED_CONFIG, MAX30100_LED_CURRENT_MAX);

    // SpO2 Mode starten
    MAX30100_WriteRegister(MAX30100_REG_MODE_CONFIG, MAX30100_MODE_SPO2);
}
