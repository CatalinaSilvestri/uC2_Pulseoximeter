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
#include "filters.h"
#include "stream_handlers.h"
#include "max30100_helpers.h"



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
#define FIFO_NEAR_FULL_LIMIT       15U

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
 *
 * DC remover alpha = 0.95
 * 0.95 * 32768 = 31130
 *
 * Original Arduino MAX30100 low-pass:
 *
 * v[0] = v[1];
 * v[1] = 0.2452372752527856026 * x
 *      + 0.50952544949442879485 * v[0];
 * return v[0] + v[1];
 */



#define SENSOR_WARMUP_MS           10000UL
#define FIFO_NEAR_FULL_LIMIT       15U


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

// SPO2 detection

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
 * These prevent resetting the filters/beat detector on every bad sample.
 */
static bool lastFingerPresent = false;
static bool lastSaturated = false;

static bool measurementReady = false;
static uint32_t fingerPlacedMs = 0;


/*
 * MAX30100 low-level helpers
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
 * Utility
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
 * Filters
 */

static void Filters_Reset(void)
{
    DCRemover_Init(&ir_dc);
    DCRemover_Init(&red_dc);
    LowPass_Init(&lpf);    
    //LowPass_Init(&lpf2);
}

/*
 * Beat detector
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

        /*
         * Original idea:
         * threshold -= lastMaxValue * 0.7 / samples_per_beat
         */
        threshold -= (int16_t)(((int32_t)lastMaxValue * 7L) / ((int32_t)div * 10L));
    }
    else
    {
        /*
         * threshold *= 0.99
         */
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



static uint8_t max30100_get_overflow_count(void)
{
    uint8_t ovf = 0;

    if (!max30100_read_reg(REG_OVF_COUNTER, &ovf))
    {
        return 0;
    }

    return ovf & 0x0F;
}



#define LED_GREEN_PIN   PIN0_bm   // PA0

#define LED_YELLOW_PIN  PIN1_bm   // PA1

#define LED_RED_PIN     PIN1_bm   // PC1

typedef enum {

    PULSE_STATE_GREEN = 0,

    PULSE_STATE_YELLOW,

    PULSE_STATE_RED

} PulseState_t;

void PulseAmpel_Init(void)

{

    // PA0 und PA1 als Ausgang

    PORTA.DIRSET = LED_GREEN_PIN | LED_YELLOW_PIN;

    // PC1 als Ausgang

    PORTC.DIRSET = LED_RED_PIN;

    // alle LEDs aus

    PORTA.OUTCLR = LED_GREEN_PIN | LED_YELLOW_PIN;

    PORTC.OUTCLR = LED_RED_PIN;

}

static PulseState_t PulseAmpel_GetState(uint16_t bpm)

{

    if (bpm >= 60 && bpm <= 90) {

        return PULSE_STATE_GREEN;

    }

    if ((bpm >= 50 && bpm < 60) || (bpm > 90 && bpm <= 120)) {

        return PULSE_STATE_YELLOW;

    }

    return PULSE_STATE_RED;

}

void PulseAmpel_Update(uint16_t bpm)

{

    PulseState_t state = PulseAmpel_GetState(bpm);

    // erst alle LEDs aus

    PORTA.OUTCLR = LED_GREEN_PIN | LED_YELLOW_PIN;

    PORTC.OUTCLR = LED_RED_PIN;

    switch (state)

    {

        case PULSE_STATE_GREEN:

            PORTA.OUTSET = LED_GREEN_PIN;      // gr?n an

            break;

        case PULSE_STATE_YELLOW:

            PORTA.OUTSET = LED_YELLOW_PIN;     // gelb an

            break;

        case PULSE_STATE_RED:

        default:

            PORTC.OUTSET = LED_RED_PIN;        // rot an

            break;

    }

}

/*
 * Process one MAX30100 sample
 */
static void process_sample(uint16_t ir, uint16_t red, uint32_t timeMs)
{
    int32_t irAC;
    int32_t redAC;
    int32_t filteredPulse32;
    int16_t filteredPulse16;
    bool beatDetected;

    /*
     * No finger.
     * Clear averages only once when we transition from finger-present to no-finger.
     */
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

    /*
     * Sensor saturated / too bright.
     * Also reset only once when entering saturated state.
     */
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

    /*
     * Valid finger signal.
     */
    fingerPresent = true;
    saturated = false;

    /*
     * Finger just became valid.
     * Start clean, flush old FIFO data, and start 10-second warmup.
     */
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

    /*
     * During warmup, keep processing samples so the DC remover, low-pass filters,
     * threshold, and beat period can settle, but do not show BPM yet.
     */
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

    /*
     * Optional: only log final signal after warmup.
     * This avoids plotting startup settling as if it were real.
     */
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
    uint8_t ovf;
    uint16_t ir;
    uint16_t red;

    uint32_t timeMs = 0;
    uint32_t lastLcdMs = 0;

    char line[17];

    SYSTEM_Initialize();
    LCD_Initialize();
    PulseAmpel_Init();

    _delay_ms(500);

    BeatDetector_Reset();
    spo2_init(&spo2Detector);

    /*
     * MAX30100 setup
     */

    /*
     * LED current.
     *
     * Register 0x09:
     * high nibble = RED current
     * low nibble  = IR current
     *
     * Try:
     * 0x66 = 20.8 mA / 20.8 mA
     * 0x88 = 27.1 mA / 27.1 mA
     * 0xAA = 33.8 mA / 33.8 mA
     */
    max30100_write_reg(REG_LED_CONFIG, 0x66); // changed manually

    /*
     * SpO2 + heart-rate mode.
     */
    max30100_write_reg(REG_MODE_CONFIG, MODE_SPO2);

    /*
     * SpO2 config:
     *
     * 0x47:
     * bit 6    = high-resolution enabled
     * bits 4:2 = 100 Hz
     * bits 1:0 = 1600 us / 16-bit
     */
    max30100_write_reg(REG_SPO2_CONFIG, 0x47);

    /*
     * Important: reset FIFO after configuration.
     */
    max30100_reset_fifo();

    LCDGoto(0, 0);
    LCDPutStr("Place finger    ");
    LCDGoto(0, 1);
    LCDPutStr("BPM: --         ");

    while (1)
{
    uint8_t ovf;

    ovf = max30100_get_overflow_count();

    if (ovf > 0)
    {
        BeatDetector_Reset();
        max30100_reset_fifo();

        measurementReady = false;
        lastFingerPresent = false;
        lastSaturated = false;

        continue;
    }

    samples = max30100_available_samples();

    if (samples >= FIFO_NEAR_FULL_LIMIT)
    {
        BeatDetector_Reset();
        max30100_reset_fifo();

        measurementReady = false;
        lastFingerPresent = false;
        lastSaturated = false;

        continue;
    }
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

    /*
     * Do not sleep 10 ms here. The sensor itself produces samples at 100 Hz.
     * This delay is only to avoid hammering I2C constantly.
     */
    _delay_ms(1);
}
}