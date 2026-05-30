#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <util/delay.h>
#include <stdio.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/twi0.h"
#include "lcd.h"
#include "LcdUtils.h"

#define MAX30100_ADDR 0x57

#define LOOP_DELAY_MS 10UL
#define LCD_UPDATE_MS 500UL

#define BEAT_INIT_HOLDOFF_MS      2000UL
#define BEAT_MASKING_HOLDOFF_MS   400UL
#define BEAT_INVALID_DELAY_MS     2000UL

#define BEAT_MIN_THRESHOLD        20
#define BEAT_MAX_THRESHOLD        800
#define BEAT_STEP_RESILIENCY      30

#define BEAT_PERIOD_ALPHA_NUM     6
#define BEAT_PERIOD_ALPHA_DEN     10

typedef enum {
    BEAT_INIT,
    BEAT_WAITING,
    BEAT_FOLLOWING_SLOPE,
    BEAT_MAYBE_DETECTED,
    BEAT_MASKING
} BeatState;

static BeatState beatState = BEAT_INIT;

static int16_t threshold = BEAT_MIN_THRESHOLD;
static int16_t lastMaxValue = 0;
static uint32_t lastBeatMs = 0;
static uint32_t beatPeriod = 0;
static uint16_t bpm = 0;

static int32_t dcw = 0;
static int16_t lpf_prev = 0;

#define LOOP_DELAY_MS 8UL
#define LCD_UPDATE_MS 1000UL

#define BEAT_INIT_HOLDOFF_MS      2000UL
#define BEAT_INVALID_DELAY_MS     2500UL

#define MIN_VALID_BEAT_MS         500UL
#define MAX_VALID_BEAT_MS         1500UL

#define BEAT_MIN_THRESHOLD        40
#define BEAT_MAX_THRESHOLD        800
#define BEAT_STEP_RESILIENCY      100

void logToDataStream(uint8_t* raw)
{
    USART2_Write(0x03);
    for (uint8_t i = 0; i < 4; i++)
    {
        USART2_Write(raw[i]);
    }
    USART2_Write(0xfc);
}

static void BeatDetector_Reset(void)
{
    beatState = BEAT_INIT;
    threshold = BEAT_MIN_THRESHOLD;
    lastMaxValue = 0;
    lastBeatMs = 0;
    beatPeriod = 0;
    bpm = 0;
    dcw = 0;
    lpf_prev = 0;
}

static int16_t DCRemove(uint16_t raw)
{
    int32_t old = dcw;

    // Approximation of alpha = 0.95 from the Arduino source
    dcw = (int32_t)raw + ((dcw * 95L) / 100L);

    return (int16_t)(dcw - old);
}

static int16_t LowPass(int16_t x)
{
    lpf_prev = lpf_prev + ((x - lpf_prev) / 4);
    return lpf_prev;
}

static void decreaseThreshold(void)
{
    if (lastMaxValue > 0 && beatPeriod > 0)
    {
        uint32_t div = beatPeriod / 10UL;
        if (div == 0)
        {
            div = 1;
        }

        threshold -= (int16_t)(((int32_t)lastMaxValue * 7L) / (int32_t)div);
    }
    else
    {
        threshold = (threshold * 99) / 100;
    }

    if (threshold < BEAT_MIN_THRESHOLD)
    {
        threshold = BEAT_MIN_THRESHOLD;
    }
}

static bool BeatDetector_Update(int16_t sample, uint32_t nowMs)
{
    bool beatDetected = false;

    switch (beatState)
    {
        case BEAT_INIT:
            if (nowMs > BEAT_INIT_HOLDOFF_MS)
            {
                beatState = BEAT_WAITING;
            }
            break;

        case BEAT_WAITING:
            if (sample > threshold)
            {
                threshold = sample;

                if (threshold > BEAT_MAX_THRESHOLD)
                {
                    threshold = BEAT_MAX_THRESHOLD;
                }

                beatState = BEAT_FOLLOWING_SLOPE;
            }

            if ((nowMs - lastBeatMs) > BEAT_INVALID_DELAY_MS)
            {
                beatPeriod = 0;
                lastMaxValue = 0;
                bpm = 0;
            }

            decreaseThreshold();
            break;

        case BEAT_FOLLOWING_SLOPE:
            if (sample < threshold)
            {
                beatState = BEAT_MAYBE_DETECTED;
            }
            else
            {
                threshold = sample;

                if (threshold > BEAT_MAX_THRESHOLD)
                {
                    threshold = BEAT_MAX_THRESHOLD;
                }
            }
            break;

        case BEAT_MAYBE_DETECTED:
            if ((sample + BEAT_STEP_RESILIENCY) < threshold)
            {
                uint32_t delta = nowMs - lastBeatMs;

                beatDetected = true;
                lastMaxValue = threshold;
                beatState = BEAT_MASKING;

                if (delta >= MIN_VALID_BEAT_MS && delta <= MAX_VALID_BEAT_MS)
                {
                    if (beatPeriod == 0)
                    {
                        beatPeriod = delta;
                    }
                    else
                    {
                        beatPeriod =
                            ((BEAT_PERIOD_ALPHA_NUM * delta) +
                             ((BEAT_PERIOD_ALPHA_DEN - BEAT_PERIOD_ALPHA_NUM) * beatPeriod))
                            / BEAT_PERIOD_ALPHA_DEN;
                    }

                    uint16_t newBpm = (uint16_t)(60000UL / beatPeriod);

                    if (bpm == 0)
                    {
                        bpm = newBpm;
                    }
                    else
                    {
                        bpm = (bpm * 7 + newBpm) / 8;
                    }
                                }

                lastBeatMs = nowMs;
            }
            else
            {
                beatState = BEAT_FOLLOWING_SLOPE;
            }
            break;

        case BEAT_MASKING:
            if ((nowMs - lastBeatMs) > BEAT_MASKING_HOLDOFF_MS)
            {
                beatState = BEAT_WAITING;
            }

            decreaseThreshold();
            break;
    }

    return beatDetected;
}

int main(void)
{
    SYSTEM_Initialize();
    LCD_Initialize();

    uint8_t d[2];
    uint8_t reg;
    uint8_t raw[4];

    uint16_t ir;
    uint16_t red;

    int16_t irAC;
    int16_t filteredPulse;

    uint32_t timeMs = 0;
    uint32_t lastLcdMs = 0;

    char line[17];

    _delay_ms(500);

    d[0] = 0x09;
    d[1] = 0xFF;
    TWI0_Write(MAX30100_ADDR, d, 2);
    while (TWI0_IsBusy());

    d[0] = 0x06;
    d[1] = 0x03;
    TWI0_Write(MAX30100_ADDR, d, 2);
    while (TWI0_IsBusy());

    LCDGoto(0, 0);
    LCDPutStr("Place finger    ");
    LCDGoto(0, 1);
    LCDPutStr("BPM: --         ");

    while (1)
    {
        reg = 0x05;

        TWI0_WriteRead(MAX30100_ADDR, &reg, 1, raw, 4);
        while (TWI0_IsBusy());

        ir  = ((uint16_t)raw[0] << 8) | raw[1];
        red = ((uint16_t)raw[2] << 8) | raw[3];

        if (ir < 10000 || red < 10000)
        {
            BeatDetector_Reset();

            if ((timeMs - lastLcdMs) >= LCD_UPDATE_MS)
            {
                lastLcdMs = timeMs;

                LCDGoto(0, 0);
                LCDPutStr("No finger       ");
                LCDGoto(0, 1);
                LCDPutStr("BPM: --         ");
            }
        }
        else
        {
            irAC = DCRemove(ir);

            // Arduino source mirrors the IR AC signal before beat detection
            filteredPulse = LowPass(-irAC);

            BeatDetector_Update(filteredPulse, timeMs);

            if ((timeMs - lastLcdMs) >= LCD_UPDATE_MS)
            {
                lastLcdMs = timeMs;

                LCDGoto(0, 0);
                LCDPutStr("Finger detected ");

                LCDGoto(0, 1);
                if (bpm == 0)
                {
                    LCDPutStr("BPM: --         ");
                }
                else
                {
                    sprintf(line, "BPM:%3u         ", bpm);
                    LCDPutStr(line);
                }
            }
        }

        if (USART2_IsTxReady())
        {
            logToDataStream(raw);
        }

        _delay_ms(LOOP_DELAY_MS);
        timeMs += LOOP_DELAY_MS;
    }
}