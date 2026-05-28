#define F_CPU 4000000UL

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <util/delay.h>
#include <stdio.h>
#include <avr/interrupt.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/timer/tca0.h"

#include "lcd.h"
#include "LcdUtils.h"
#include "MAX30100.h"

int32_t ir_avg = 0;
int16_t ir_ac = 0;
int16_t ir_smooth = 0;

int16_t prev = 0;

uint16_t sample_counter = 0;
uint16_t last_beat_sample = 0;
uint16_t distance = 0;

uint16_t bpm = 0;
uint16_t bpm_new = 0;

uint8_t finger_detected = 0;
uint8_t beat_detected = 0;

uint16_t lcd_counter = 0;

#define THRESHOLD       100
#define MIN_DISTANCE    55
#define MAX_DISTANCE    130
int main(void)
{
    SYSTEM_Initialize();
    LCD_Initialize();

    MAX30100_Sample_t sample;
    char line[17];

    _delay_ms(500);

    MAX30100_Init();
while (1)
{
    beat_detected = 0;

    if (MAX30100_ReadFIFO(&sample))
    {
        sample_counter++;
        lcd_counter++;

        if (sample.ir < 10000 || sample.red < 10000)
        {
            finger_detected = 0;

            ir_avg = 0;
            ir_ac = 0;
            ir_smooth = 0;
            prev = 0;

            sample_counter = 0;
            last_beat_sample = 0;
            distance = 0;

            bpm = 0;
            bpm_new = 0;
        }
        else
        {
            finger_detected = 1;

            if (ir_avg == 0)
            {
                ir_avg = sample.ir;
            }

            // DC-Anteil entfernen
            ir_avg = ir_avg + (((int32_t)sample.ir - ir_avg) / 32);

            // AC-Signal bilden
            ir_ac = (int16_t)((int32_t)sample.ir - ir_avg);

            // Signal glätten
            ir_smooth = ir_smooth + ((ir_ac - ir_smooth) / 4);

            /*
             * Einfache Beat-Erkennung:
             * Wir erkennen den Beat, wenn das Signal von unterhalb der Schwelle
             * nach oberhalb der Schwelle steigt.
             */
            if ((prev < THRESHOLD) && (ir_smooth >= THRESHOLD))
            {
                if (last_beat_sample == 0)
                {
                    last_beat_sample = sample_counter;
                    beat_detected = 1;
                }
                else
                {
                    distance = sample_counter - last_beat_sample;

                    if (distance > MIN_DISTANCE && distance < MAX_DISTANCE)
                    {
                        bpm_new = (uint16_t)(6000UL / distance);

                        if (bpm == 0)
                        {
                            bpm = bpm_new;
                        }
                        else
                        {
                            // BPM langsam glätten
                            bpm = bpm + ((int16_t)bpm_new - (int16_t)bpm) / 4;
                        }

                        last_beat_sample = sample_counter;
                        beat_detected = 1;
                    }
                    else if (distance >= MAX_DISTANCE)
                    {
                        /*
                         * Wenn sehr lange kein Beat erkannt wurde:
                         * neuen Startpunkt setzen, aber BPM nicht berechnen.
                         */
                        last_beat_sample = sample_counter;
                        beat_detected = 1;
                    }
                }
            }

            prev = ir_smooth;
        }

        /*
         * LCD nur alle ca. 500 ms aktualisieren.
         * Bei 100 Hz entspricht 50 Samples ca. 0,5 s.
         */
        if (lcd_counter >= 50)
        {
            lcd_counter = 0;

            LCDGoto(0, 0);

            if (finger_detected)
            {
                sprintf(line, "BPM:%3u D:%3u ", bpm, distance);
            }
            else
            {
                sprintf(line, "Finger fehlt ");
            }

            LCDPutStr(line);

            LCDGoto(0, 1);

            if (beat_detected)
            {
                sprintf(line, "BEAT AC:%5d ", ir_smooth);
            }
            else
            {
                sprintf(line, "AC:%7d     ", ir_smooth);
            }

            LCDPutStr(line);
        }
    }

    _delay_ms(10);
}
}