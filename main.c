



#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <util/delay.h>
#include <stdio.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/twi0.h"
#include "lcd.h"
#include "LcdUtils.h"
#include "MAX30100.h"
#define MAX30100_ADDR 0x57

void logToDataStream(uint8_t* raw)
{
    USART2_Write(0x03);
    for(uint8_t i=0; i<4; i++)
    {
        USART2_Write(raw[i]);
    }
    USART2_Write(0xfc);
}
int main(void)
{
    SYSTEM_Initialize();
    LCD_Initialize();

    uint8_t d[2];
    uint8_t reg;
    uint8_t raw[4];
    uint16_t ir;
    uint16_t ir_avg = 0;
    uint16_t red;
    int16_t ir_peak = 0;
    int16_t ir_smooth = 0;

    char line[17];

    _delay_ms(500);

    // LED current max
    d[0] = 0x09;
    d[1] = 0xFF;
    TWI0_Write(MAX30100_ADDR, d, 2);
    while (TWI0_IsBusy());

    // SpO2 Mode starten
    d[0] = 0x06;
    d[1] = 0x03;
    TWI0_Write(MAX30100_ADDR, d, 2);
    while (TWI0_IsBusy());
    
   

    while (1)
    {
        // FIFO Data Register auswählen und 4 Bytes lesen
        reg = 0x05;

        TWI0_WriteRead(MAX30100_ADDR, &reg, 1, raw, 4);
        while (TWI0_IsBusy());

        ir  = ((uint16_t)raw[0] << 8) | raw[1];

        red = ((uint16_t)raw[2] << 8) | raw[3];

        if(ir < 10000 || red < 10000)

        {
            
            ir = 0;

            red = 0;

        }

        ir_avg = ir_avg + (ir - ir_avg) / 16;

        ir_peak = (int16_t)ir - (int16_t)ir_avg;

        // Peak-Signal zusätzlich glätten

        ir_smooth = ir_smooth + (ir_peak - ir_smooth) / 4;

        LCDGoto(0, 0);

        sprintf(line, "IR:%6d       ", ir_smooth);

        LCDPutStr(line);

        LCDGoto(0, 1);

        sprintf(line, "R:%5u         ", red);

        LCDPutStr(line);
        if (USART2_IsTxReady())
        {
            logToDataStream(raw);
        }
    }
}
