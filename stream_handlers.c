#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <util/delay.h>
#include <stdio.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/twi0.h"

/*
 * Optional raw logging.
 */
void logToDataStream(uint8_t* raw)
{
    USART2_Write(0x03);

    for (uint8_t i = 0; i < 4; i++)
    {
        USART2_Write(raw[i]);
    }

    USART2_Write(0xFC);
}

/*
 * Optional signed 16-bit logging for Data Visualizer.
 * Configure DV as signed 16-bit, big-endian, frame 0x03 ... 0xFC.
 */
void logInt16ToDataStream(int16_t value)
{
    USART2_Write(0x03);
    USART2_Write((uint8_t)((value >> 8) & 0xFF));
    USART2_Write((uint8_t)(value & 0xFF));
    USART2_Write(0xFC);
}

void logFinalSignalToDataStream(int16_t value)
{
    USART2_Write(0x03);

    /*
     * Send signed 16-bit value, big-endian:
     * high byte first, low byte second.
     */
    USART2_Write((uint8_t)((value >> 8) & 0xFF));
    USART2_Write((uint8_t)(value & 0xFF));

    USART2_Write(0xFC);
}
static void uartSendString(const char *s)
{
    while (*s)
    {
        while (!USART2_IsTxReady())
        {
            ;
        }

        USART2_Write((uint8_t)*s++);
    }
}
void lcdMirrorSend(const char *line1, const char *line2)
{
    uartSendString("<LCD>\r\n");
    uartSendString(line1);
    uartSendString("\r\n");
    uartSendString(line2);
    uartSendString("\r\n");
    uartSendString("</LCD>\r\n");
}