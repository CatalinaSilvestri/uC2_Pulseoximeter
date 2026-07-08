/*
 * Fixed-point implementation of plain C conversions
 * for MAX30100 filters and beat detector.
 *
 * No floating point is used.
 */

#include "beat_detector_c.h"

/*
 * Q15 coefficients.
 *
 * Original Arduino low-pass:
 *
 * v[0] = v[1];
 * v[1] = 0.2452372752527856026 * x
 *      + 0.50952544949442879485 * v[0];
 * return v[0] + v[1];
 *
 * Q15:
 * 0.2452372752527856026 * 32768 = 8036
 * 0.50952544949442879485 * 32768 = 16696
 */
#define FILTER_B0_Q15      8036L
#define FILTER_A1_Q15      16696L

/*
 * Default DC remover alpha:
 *
 * 0.95 * 32768 = 31130
 */
#define DC_REMOVER_ALPHA_Q15 31130L

static inline int32_t min_i32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

/*
 * Safe Q15 multiply.
 *
 * Result:
 *     (a_q15 * b) / 32768
 *
 * int64_t is used only inside the multiply to avoid overflow.
 * This is slower on AVR, but safe and still fine at 100 Hz.
 */
static inline int32_t q15_mul_i32(int32_t a_q15, int32_t b)
{
    return (int32_t)(((int64_t)a_q15 * (int64_t)b) >> Q15_SHIFT);
}


/* ----- BeatDetector ----- */

static void BeatDetector_decreaseThreshold(BeatDetector* bd)
{
    if (!bd)
    {
        return;
    }

    if (bd->lastMaxValue > 0 && bd->beatPeriod > 0)
    {
        /*
         * Original float:
         *
         * threshold -= lastMaxValue * 0.7
         *              / (beatPeriod / BEATDETECTOR_SAMPLES_PERIOD)
         *
         * Integer/fixed-point:
         *
         * falloff = lastMaxValue * 0.7
         * samples_per_beat = beatPeriod / sample_period_ms
         * threshold -= falloff / samples_per_beat
         */
        uint32_t samples_per_beat;
        int32_t falloff;

        samples_per_beat = bd->beatPeriod / BEATDETECTOR_SAMPLES_PERIOD;

        if (samples_per_beat == 0)
        {
            samples_per_beat = 1;
        }

        falloff = q15_mul_i32(
            BEATDETECTOR_THRESHOLD_FALLOFF_Q15,
            bd->lastMaxValue
        );

        bd->threshold -= falloff / (int32_t)samples_per_beat;
    }
    else
    {
        /*
         * Original:
         *
         * threshold *= 0.99
         */
        bd->threshold = q15_mul_i32(
            BEATDETECTOR_THRESHOLD_DECAY_Q15,
            bd->threshold
        );
    }

    if (bd->threshold < BEATDETECTOR_MIN_THRESHOLD)
    {
        bd->threshold = BEATDETECTOR_MIN_THRESHOLD;
    }
}

void BeatDetector_init(BeatDetector* bd)
{
    if (!bd)
    {
        return;
    }

    bd->state = BEATDETECTOR_STATE_INIT;
    bd->threshold = BEATDETECTOR_MIN_THRESHOLD;
    bd->beatPeriod = 0;
    bd->lastMaxValue = 0;
    bd->tsLastBeat = 0;
}

int BeatDetector_addSample(BeatDetector* bd, uint32_t now_ms, int32_t sample)
{
    int beatDetected = 0;

    if (!bd)
    {
        return 0;
    }

    switch (bd->state)
    {
        case BEATDETECTOR_STATE_INIT:
            if (now_ms > (uint32_t)BEATDETECTOR_INIT_HOLDOFF)
            {
                bd->state = BEATDETECTOR_STATE_WAITING;
            }
            break;

        case BEATDETECTOR_STATE_WAITING:
            if (sample > bd->threshold)
            {
                bd->threshold = min_i32(sample, BEATDETECTOR_MAX_THRESHOLD);
                bd->state = BEATDETECTOR_STATE_FOLLOWING_SLOPE;
            }

            if ((uint32_t)(now_ms - bd->tsLastBeat) >
                (uint32_t)BEATDETECTOR_INVALID_READOUT_DELAY)
            {
                bd->beatPeriod = 0;
                bd->lastMaxValue = 0;
            }

            BeatDetector_decreaseThreshold(bd);
            break;

        case BEATDETECTOR_STATE_FOLLOWING_SLOPE:
            if (sample < bd->threshold)
            {
                bd->state = BEATDETECTOR_STATE_MAYBE_DETECTED;
            }
            else
            {
                bd->threshold = min_i32(sample, BEATDETECTOR_MAX_THRESHOLD);
            }
            break;

        case BEATDETECTOR_STATE_MAYBE_DETECTED:
            if ((sample + BEATDETECTOR_STEP_RESILIENCY) < bd->threshold)
            {
                uint32_t delta;

                beatDetected = 1;

                /*
                 * Original code used:
                 *
                 * lastMaxValue = sample;
                 *
                 * But note: the threshold is actually the tracked peak.
                 * To stay closer to your original C++ translation, keep sample here.
                 */
                bd->lastMaxValue = sample;
                bd->state = BEATDETECTOR_STATE_MASKING;

                delta = now_ms - bd->tsLastBeat;

                if (delta)
                {
                    /*
                     * Original float:
                     *
                     * beatPeriod = 0.6 * delta + 0.4 * beatPeriod;
                     *
                     * Fixed-point:
                     */
                    uint32_t new_part;
                    uint32_t old_part;

                    new_part = (uint32_t)q15_mul_i32(
                        BEATDETECTOR_BPFILTER_ALPHA_Q15,
                        (int32_t)delta
                    );

                    old_part = (uint32_t)q15_mul_i32(
                        32768L - BEATDETECTOR_BPFILTER_ALPHA_Q15,
                        (int32_t)bd->beatPeriod
                    );

                    bd->beatPeriod = new_part + old_part;
                }

                bd->tsLastBeat = now_ms;
            }
            else
            {
                bd->state = BEATDETECTOR_STATE_FOLLOWING_SLOPE;
            }
            break;

        case BEATDETECTOR_STATE_MASKING:
            if ((uint32_t)(now_ms - bd->tsLastBeat) >
                (uint32_t)BEATDETECTOR_MASKING_HOLDOFF)
            {
                bd->state = BEATDETECTOR_STATE_WAITING;
            }

            BeatDetector_decreaseThreshold(bd);
            break;

        default:
            BeatDetector_init(bd);
            break;
    }

    return beatDetected;
}

uint16_t BeatDetector_getRate(const BeatDetector* bd)
{
    if (!bd)
    {
        return 0;
    }

    if (bd->beatPeriod != 0)
    {
        /*
         * Original:
         *
         * bpm = 1 / beatPeriod * 1000 * 60
         *
         * Same as:
         *
         * bpm = 60000 / beatPeriod
         */
        return (uint16_t)(60000UL / bd->beatPeriod);
    }

    return 0;
}

int32_t BeatDetector_getCurrentThreshold(const BeatDetector* bd)
{
    return bd ? bd->threshold : 0;
}