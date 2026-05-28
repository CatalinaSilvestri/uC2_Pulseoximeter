#ifndef MAX30100_H
#define MAX30100_H

#include <stdint.h>
#include <stdbool.h>

// I2C-Adresse
#define MAX30100_ADDR 0x57

// Register-Adressen
#define MAX30100_REG_INT_STATUS      0x00
#define MAX30100_REG_INT_ENABLE      0x01
#define MAX30100_REG_FIFO_WR_PTR     0x02
#define MAX30100_REG_OVF_COUNTER     0x03
#define MAX30100_REG_FIFO_RD_PTR     0x04
#define MAX30100_REG_FIFO_DATA       0x05
#define MAX30100_REG_MODE_CONFIG     0x06
#define MAX30100_REG_SPO2_CONFIG     0x07
#define MAX30100_REG_LED_CONFIG      0x09
#define MAX30100_REG_TEMP_INTEGER    0x16
#define MAX30100_REG_TEMP_FRACTION   0x17
#define MAX30100_REG_REV_ID          0xFE
#define MAX30100_REG_PART_ID         0xFF

// Mode Config
#define MAX30100_MODE_HR_ONLY        0x02
#define MAX30100_MODE_SPO2           0x03
#define MAX30100_MODE_RESET          0x40

// Standard-Konfiguration für ersten Test
#define MAX30100_SPO2_CONFIG_100HZ_1600US   0x47
#define MAX30100_LED_CURRENT_MAX            0x77

// Datenstruktur für ein FIFO-Sample
typedef struct
{
    uint16_t ir;
    uint16_t red;
} MAX30100_Sample_t;

// Grundfunktionen
bool MAX30100_WriteRegister(uint8_t reg, uint8_t value);
bool MAX30100_ReadRegister(uint8_t reg, uint8_t *value);
bool MAX30100_ReadFIFO(MAX30100_Sample_t *sample);

// Hilfsfunktionen
void MAX30100_ResetFIFO(void);

// Initialisierung
void MAX30100_Init(void);

#endif