/**
   LCD driver source file

  Company:
    Microchip Technology Inc.

  File Name:
    lcd.c

  Summary:
    This is the  user  source file which has specific functions for LCD.

  Description:
    This modules includes all service functions for LCD operations.
*/

/*
Copyright (c) 2013 - 2014 released Microchip Technology Inc.  All rights reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
*/

/**
  Section: Included Files
*/
#include <xc.h>
#include "lcd.h"
#include <util/delay.h>

void LCD_Initialize()
{
    // clear latches and set LCD_PORT pins to output
    RST_PIN(LCD_PORT, (LCD_HIGH_NIBBLE | LCD_EN | LCD_RS));
    LCD_PORT.DIRSET = (LCD_HIGH_NIBBLE | LCD_EN | LCD_RS);
    
    // required by display controller to allow power to stabilize
    _delay_ms(LCD_Startup);

    // Configure Background lighting switch and turn on
    RST_PIN(BGL_PORT, BGL_SW);
    BGL_PORT.DIRSET = BGL_SW;
    SET_PIN(BGL_PORT, BGL_SW);
    
    // required by display initialization
    LCDWriteNibble(0x30);
    _delay_ms(LCD_delay);
    LCDWriteNibble(0x30);
    _delay_ms(LCD_delay);
    LCDWriteNibble(0x30);
    _delay_us(150);
    LCDWriteNibble(0x20);

    // set interface size, # of lines and font
    LCDPutCmd(FUNCTION_SET);

    // turn on display and sets up cursor
    LCDPutCmd(DISPLAY_SETUP);
    
    DisplayClr();
    LCDLine1();

    // set cursor movement direction
    LCDPutCmd(ENTRY_MODE_FORWARD);
	// wait before continuing
    _delay_ms(50);
}


void LCDWriteNibble(uint8_t ch)
{
    // always send the upper nibble
    ch = (ch >> 4);

    // mask off the nibble to be transmitted
    ch = (ch & 0x0F);

    // clear the lower half of LCD_PORT
    RST_PIN(LCD_PORT, LCD_HIGH_NIBBLE);
    // move the nibble onto LCD_PORT
    SET_PIN(LCD_PORT, (ch << LCD_DATA_OFFSET));

    _delay_us(LCD_delay);
    // set up enable before writing nibble
    SET_PIN(LCD_PORT, LCD_EN);
    _delay_us(LCD_delay);
    // turn off enable after write of nibble
    RST_PIN(LCD_PORT, LCD_EN);
    _delay_us(100);
}

void LCDPutChar(uint8_t ch)
{
    // set data/instr bit to 1 = data
    SET_PIN(LCD_PORT, LCD_RS);
    //_delay_ms(LCD_delay);
    //Send higher nibble first
    LCDWriteNibble(ch);

    //get the lower nibble
    ch = (ch << 4);

    // Now send the low nibble
    LCDWriteNibble(ch);
}

    
void LCDPutCmd(uint8_t ch)
{
    // set data/instr bit to 0 = command
    RST_PIN(LCD_PORT, LCD_RS);
    //_delay_ms(LCD_delay);

    //Send the higher nibble
    LCDWriteNibble(ch);

    //get the lower nibble
    ch = (ch << 4);

    //Now send the lower nibble
    LCDWriteNibble(ch);
    
    _delay_us(1000);
	
	if ((ch>>4) < 0x3)
		_delay_us(5000);
}

 
void LCDPutStr(const char *str)
{
    uint8_t i=0;
    
    // While string has not been fully traveresed
    while (str[i])
    {
        // Go display current char
        LCDPutChar(str[i++]);
    }
    
}

void LCDGoto(uint8_t pos,uint8_t ln)
{
    // if incorrect line or column
    if ((ln > (NB_LINES-1)) || (pos > (NB_COL-1)))
    {
        // Just do nothing
        return;
    }

    // LCD_Goto command
    LCDPutCmd((ln == 1) ? (0xC0 | pos) : (0x80 | pos));

    // Wait for the LCD to finish
    //_delay_ms(LCD_delay);
}
/**
 End of File
*/



