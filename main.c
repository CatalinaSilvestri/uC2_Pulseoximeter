#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <util/delay.h>
#include <stdio.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/twi0.h"
#include "lcd.h"
#include "LcdUtils.h"
#include "beat_detector_c.h"
#include "spo2_detector.h"
#include "keypad_start.h"

/*
 * MAX30100 registers
 */
#define MAX30100_ADDR              0x57

#define REG_FIFO_WR_PTR            0x02
#define REG_OVF_COUNTER            0x03
#define REG_FIFO_RD_PTR            0x04
#define REG_FIFO_DATA              0x05
#define REG_MODE_CONFIG            0x06
#define REG_SPO2_CONFIG            0x07
#define REG_LED_CONFIG             0x09

#define MODE_SPO2                  0x03
#define MODE_RESET                 0x40

/*
 * Main timing.
 * The Arduino MAX30100 library assumes 100 Hz = 10 ms/sample.
 */
#define LOOP_DELAY_MS              10UL
#define LCD_UPDATE_MS              1000UL

/*
 * Raw signal limits.
 */
#define RAW_MIN_FINGER_LIMIT       10000U
#define RAW_SATURATION_LIMIT       62000U

/*
 * Beat detector timing.
 */
#define BEAT_INIT_HOLDOFF_MS       2000UL
#define BEAT_MASKING_HOLDOFF_MS    400UL
#define BEAT_INVALID_DELAY_MS      2500UL

/*
 * Valid beat interval range.
 * 500 ms  = 120 BPM
 * 1500 ms = 40 BPM
 */
#define MIN_VALID_BEAT_MS          500UL
#define MAX_VALID_BEAT_MS          1500UL

/*
 * Beat detector threshold settings.
 */
#define BEAT_MIN_THRESHOLD         40
#define BEAT_MAX_THRESHOLD         800
#define BEAT_STEP_RESILIENCY       100

/*
 * Beat period smoothing:
 * beatPeriod = 60% new value + 40% old value
 */
#define BEAT_PERIOD_ALPHA_NUM      6UL
#define BEAT_PERIOD_ALPHA_DEN      10UL

/*
 * Q15 fixed-point format.
 * 1.0 = 32768
 */
#define Q15_SHIFT                  15

/*
 * Fixed-point coefficients.
 */
#define DC_ALPHA_Q15               31130L
#define LPF_B0_Q15                 8036L
#define LPF_A1_Q15                 16696L

#define SENSOR_WARMUP_MS           10000UL

#define Q15_MUL(a, b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> Q15_SHIFT))

typedef enum {
    BEAT_INIT,
    BEAT_WAITING,
    BEAT_FOLLOWING_SLOPE,
    BEAT_MAYBE_DETECTED,
    BEAT_MASKING
} BeatState;

typedef struct {
    int32_t dcw;
    bool initialized;
} DCRemoverQ15;

typedef struct {
    int32_t v0;
    int32_t v1;
} LowPassQ15;

/*
 * Beat detector state.
 */
static BeatState beatState = BEAT_INIT;

static int16_t threshold = BEAT_MIN_THRESHOLD;
static int16_t lastMaxValue = 0;
static uint32_t lastBeatMs = 0;
static uint32_t beatPeriod = 0;
static uint16_t bpm = 0;

/*
 * Filter state.
 */
static DCRemoverQ15 ir_dc;
static DCRemoverQ15 red_dc;
static LowPassQ15 lpf;
static LowPassQ15 lpf2;

/*
 * SPO2 detection.
 */
static Spo2Detector_t spo2Detector;
static uint8_t spo2 = 0;

/*
 * Last status for LCD.
 */
static bool fingerPresent = false;
static bool saturated = false;
static bool beatRecentlyDetected = false;
static uint32_t lastBeatDisplayMs = 0;

/*
 * State-change tracking.
 */
static bool lastFingerPresent = false;
static bool lastSaturated = false;

static bool measurementReady = false;
static bool measurementEnabled = false;
static uint32_t fingerPlacedMs = 0;

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
 */
static void logInt16ToDataStream(int16_t value)
{
    USART2_Write(0x03);
    USART2_Write((uint8_t)((value >> 8) & 0xFF));
    USART2_Write((uint8_t)(value & 0xFF));
    USART2_Write(0xFC);
}

/*
 * MAX30100 low-level helpers.
 */
static bool max30100_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t d[2];

    d[0] = reg;
    d[1] = value;

    if (!TWI0_Write(MAX30100_ADDR, d, 2))
    {
        return false;
    }

    while (TWI0_IsBusy());

    return true;
}

static bool max30100_read_reg(uint8_t reg, uint8_t *value)
{
    if (!TWI0_WriteRead(MAX30100_ADDR, &reg, 1, value, 1))
    {
        return false;
    }

    while (TWI0_IsBusy());

    return true;
}

static void max30100_reset_fifo(void)
{
    max30100_write_reg(REG_FIFO_WR_PTR, 0x00);
    max30100_write_reg(REG_OVF_COUNTER, 0x00);
    max30100_write_reg(REG_FIFO_RD_PTR, 0x00);
}

static uint8_t max30100_available_samples(void)
{
    uint8_t wr = 0;
    uint8_t rd = 0;

    max30100_read_reg(REG_FIFO_WR_PTR, &wr);
    max30100_read_reg(REG_FIFO_RD_PTR, &rd);

    wr &= 0x0F;
    rd &= 0x0F;

    if (wr >= rd)
    {
        return wr - rd;
    }

    return 16 + wr - rd;
}

static bool max30100_read_fifo(uint16_t *ir, uint16_t *red)
{
    uint8_t reg = REG_FIFO_DATA;
    uint8_t raw[4];

    if (!TWI0_WriteRead(MAX30100_ADDR, &reg, 1, raw, 4))
    {
        return false;
    }

    while (TWI0_IsBusy());

    *ir  = ((uint16_t)raw[0] << 8) | raw[1];
    *red = ((uint16_t)raw[2] << 8) | raw[3];

    return true;
}

/*
 * Utility.
 */
static int16_t clamp_i32_to_i16(int32_t x)
{
    if (x > 32767L)
    {
        return 32767;
    }

    if (x < -32768L)
    {
        return -32768;
    }

    return (int16_t)x;
}

/*
 * Filters.
 */
static void DCRemover_Init(DCRemoverQ15 *f)
{
    f->dcw = 0;
    f->initialized = false;
}

static int32_t DCRemover_Step(DCRemoverQ15 *f, uint16_t raw)
{
    int32_t old;

    if (!f->initialized)
    {
        f->dcw = (int32_t)raw * 20L;
        f->initialized = true;
        return 0;
    }

    old = f->dcw;

    f->dcw = (int32_t)raw + Q15_MUL(DC_ALPHA_Q15, f->dcw);

    return f->dcw - old;
}

static void LowPass_Init(LowPassQ15 *f)
{
    f->v0 = 0;
    f->v1 = 0;
}

static int32_t LowPass_Step(LowPassQ15 *f, int32_t x)
{
    f->v0 = f->v1;

    f->v1 = Q15_MUL(LPF_B0_Q15, x)
          + Q15_MUL(LPF_A1_Q15, f->v0);

    return f->v0 + f->v1;
}

static void Filters_Reset(void)
{
    DCRemover_Init(&ir_dc);
    DCRemover_Init(&red_dc);
    LowPass_Init(&lpf);
    LowPass_Init(&lpf2);
}

/*
 * Beat detector.
 */
static void BeatDetector_Reset(void)
{
    beatState = BEAT_INIT;
    threshold = BEAT_MIN_THRESHOLD;
    lastMaxValue = 0;
    lastBeatMs = 0;
    beatPeriod = 0;
    bpm = 0;

    Filters_Reset();

    spo2_reset(&spo2Detector);
    spo2 = 0;

    beatRecentlyDetected = false;
    lastBeatDisplayMs = 0;
}

static void decreaseThreshold(void)
{
    if (lastMaxValue > 0 && beatPeriod > 0)
    {
        uint32_t div = beatPeriod / LOOP_DELAY_MS;

        if (div == 0)
        {
            div = 1;
        }

        threshold -= (int16_t)(((int32_t)lastMaxValue * 7L) / ((int32_t)div * 10L));
    }
    else
    {
        threshold = (int16_t)(((int32_t)threshold * 99L) / 100L);
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

                    if (beatPeriod > 0)
                    {
                        uint16_t newBpm = (uint16_t)(60000UL / beatPeriod);

                        if (bpm == 0)
                        {
                            bpm = newBpm;
                        }
                        else
                        {
                            bpm = (uint16_t)((bpm * 7U + newBpm) / 8U);
                        }
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

        default:
            BeatDetector_Reset();
            break;
    }

    return beatDetected;
}

static void logFinalSignalToDataStream(int16_t value)
{
    USART2_Write(0x03);
    USART2_Write((uint8_t)((value >> 8) & 0xFF));
    USART2_Write((uint8_t)(value & 0xFF));
    USART2_Write(0xFC);
}

/*
 * Measurement start.
 */
static void Measurement_Start(void)
{
    /*
     * Full sensor restart.
     */
    max30100_write_reg(REG_MODE_CONFIG, MODE_RESET);
    _delay_ms(20);

    max30100_write_reg(REG_LED_CONFIG, 0x66);
    max30100_write_reg(REG_SPO2_CONFIG, 0x47);
    max30100_write_reg(REG_MODE_CONFIG, MODE_SPO2);
    max30100_reset_fifo();

    BeatDetector_Reset();
    spo2_reset(&spo2Detector);

    fingerPresent = false;
    saturated = false;
    beatRecentlyDetected = false;

    lastFingerPresent = false;
    lastSaturated = false;

    measurementReady = false;
    measurementEnabled = true;

    fingerPlacedMs = 0;
    lastBeatDisplayMs = 0;

    LCDGoto(0, 0);
    LCDPutStr("Place finger    ");
    LCDGoto(0, 1);
    LCDPutStr("BPM: --         ");
}

/*
 * Pulse traffic light.
 */
#define LED_GREEN_PIN   PIN0_bm
#define LED_YELLOW_PIN  PIN1_bm
#define LED_RED_PIN     PIN1_bm

typedef enum {
    PULSE_STATE_GREEN = 0,
    PULSE_STATE_YELLOW,
    PULSE_STATE_RED
} PulseState_t;

void PulseAmpel_Init(void)
{
    PORTA.DIRSET = LED_GREEN_PIN | LED_YELLOW_PIN;
    PORTC.DIRSET = LED_RED_PIN;

    PORTA.OUTCLR = LED_GREEN_PIN | LED_YELLOW_PIN;
    PORTC.OUTCLR = LED_RED_PIN;
}

static PulseState_t PulseAmpel_GetState(uint16_t bpm)
{
    if (bpm >= 60 && bpm <= 90)
    {
        return PULSE_STATE_GREEN;
    }

    if ((bpm >= 50 && bpm < 60) || (bpm > 90 && bpm <= 120))
    {
        return PULSE_STATE_YELLOW;
    }

    return PULSE_STATE_RED;
}

void PulseAmpel_Update(uint16_t bpm)
{
    PulseState_t state = PulseAmpel_GetState(bpm);

    PORTA.OUTCLR = LED_GREEN_PIN | LED_YELLOW_PIN;
    PORTC.OUTCLR = LED_RED_PIN;

    switch (state)
    {
        case PULSE_STATE_GREEN:
            PORTA.OUTSET = LED_GREEN_PIN;
            break;

        case PULSE_STATE_YELLOW:
            PORTA.OUTSET = LED_YELLOW_PIN;
            break;

        case PULSE_STATE_RED:
        default:
            PORTC.OUTSET = LED_RED_PIN;
            break;
    }
}

/*
 * Process one MAX30100 sample.
 */
static void process_sample(uint16_t ir, uint16_t red, uint32_t timeMs)
{
    int32_t irAC;
    int32_t redAC;
    int32_t filteredPulse32;
    int16_t filteredPulse16;
    bool beatDetected;

    if (ir < RAW_MIN_FINGER_LIMIT || red < RAW_MIN_FINGER_LIMIT)
    {
        fingerPresent = false;
        saturated = false;
        measurementReady = false;

        if (lastFingerPresent || lastSaturated)
        {
            BeatDetector_Reset();
            max30100_reset_fifo();
        }

        lastFingerPresent = false;
        lastSaturated = false;
        return;
    }

    if (ir > RAW_SATURATION_LIMIT || red > RAW_SATURATION_LIMIT)
    {
        fingerPresent = true;
        saturated = true;
        measurementReady = false;

        if (!lastSaturated)
        {
            BeatDetector_Reset();
            max30100_reset_fifo();
        }

        lastFingerPresent = true;
        lastSaturated = true;
        return;
    }

    fingerPresent = true;
    saturated = false;

    if (!lastFingerPresent || lastSaturated)
    {
        BeatDetector_Reset();
        max30100_reset_fifo();

        fingerPlacedMs = timeMs;
        measurementReady = false;

        lastFingerPresent = true;
        lastSaturated = false;

        return;
    }

    if ((timeMs - fingerPlacedMs) >= SENSOR_WARMUP_MS)
    {
        measurementReady = true;
    }

    lastFingerPresent = true;
    lastSaturated = false;

    irAC = DCRemover_Step(&ir_dc, ir);
    redAC = DCRemover_Step(&red_dc, red);

    filteredPulse32 = LowPass_Step(&lpf, -irAC);
    filteredPulse32 = LowPass_Step(&lpf2, filteredPulse32);

    filteredPulse16 = clamp_i32_to_i16(filteredPulse32);

    if (measurementReady && USART2_IsTxReady())
    {
        logFinalSignalToDataStream(filteredPulse16);
    }

    beatDetected = BeatDetector_Update(filteredPulse16, timeMs);

    if (measurementReady && bpm > 0)
    {
        spo2_update(&spo2Detector, irAC, redAC, ir, red, beatDetected);
        spo2 = spo2_get(&spo2Detector);
    }

    if (beatDetected)
    {
        beatRecentlyDetected = true;
        lastBeatDisplayMs = timeMs;
    }

    if ((timeMs - lastBeatDisplayMs) > 250UL)
    {
        beatRecentlyDetected = false;
    }
}

int main(void)
{
    uint8_t samples;
    uint16_t ir;
    uint16_t red;

    uint32_t timeMs = 0;
    uint32_t lastLcdMs = 0;

    char line[17];

    SYSTEM_Initialize();
    LCD_Initialize();
    PulseAmpel_Init();

    keypad_start_init();

    _delay_ms(500);

    BeatDetector_Reset();
    spo2_init(&spo2Detector);

    /*
     * MAX30100 initial setup.
     */
    max30100_write_reg(REG_LED_CONFIG, 0x66);
    max30100_write_reg(REG_MODE_CONFIG, MODE_SPO2);
    max30100_write_reg(REG_SPO2_CONFIG, 0x47);
    max30100_reset_fifo();

    LCDGoto(0, 0);
    LCDPutStr("RIGHT = Start   ");
    LCDGoto(0, 1);
    LCDPutStr("Waiting...      ");

    while (1)
    {
        keypad_start_task();

        if (keypad_start_requested())
        {
            timeMs = 0;
            lastLcdMs = 0;

            Measurement_Start();
        }

        if (!measurementEnabled)
        {
            _delay_ms(10);
            continue;
        }

        samples = max30100_available_samples();

        PulseAmpel_Update(bpm);

        while (samples > 0)
        {
            if (max30100_read_fifo(&ir, &red))
            {
                process_sample(ir, red, timeMs);
                timeMs += LOOP_DELAY_MS;
            }

            samples--;
        }

        if ((timeMs - lastLcdMs) >= LCD_UPDATE_MS)
        {
            lastLcdMs = timeMs;

            LCDGoto(0, 0);

            if (!fingerPresent)
            {
                LCDPutStr("No finger       ");
            }
            else if (saturated)
            {
                LCDPutStr("Too bright      ");
            }
            else if (!measurementReady)
            {
                LCDPutStr("Stabilizing...  ");
            }
            else if (beatRecentlyDetected)
            {
                LCDPutStr("Finger: beat *  ");
            }
            else
            {
                LCDPutStr("Finger detected ");
            }

            LCDGoto(0, 1);

            if (saturated)
            {
                LCDPutStr("Lower LED curr  ");
            }
            else if (!fingerPresent)
            {
                LCDPutStr("BPM: --         ");
            }
            else if (!measurementReady)
            {
                uint16_t remaining;

                remaining = (uint16_t)((SENSOR_WARMUP_MS - (timeMs - fingerPlacedMs)) / 1000UL);

                sprintf(line, "Wait:%2us        ", remaining);
                LCDPutStr(line);
            }
            else if (bpm == 0)
            {
                LCDPutStr("BPM: --         ");
            }
            else
            {
                if (spo2_is_valid(&spo2Detector))
                {
                    sprintf(line, "B:%3u O2:%3u%%  ", bpm, spo2);
                }
                else
                {
                    sprintf(line, "B:%3u O2:--%%   ", bpm);
                }

                LCDPutStr(line);
            }
        }

        _delay_ms(1);
    }
}