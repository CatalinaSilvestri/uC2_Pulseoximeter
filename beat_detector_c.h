#ifndef BEAT_DETECTOR_C_H
#define BEAT_DETECTOR_C_H

#include <stdint.h>

#define BEATDETECTOR_INIT_HOLDOFF                2000u
#define BEATDETECTOR_MASKING_HOLDOFF             200u

/*
 * Original Arduino value:
 * 0.6
 * Q15: 0.6 * 32768 = 19660.8
 */
#define BEATDETECTOR_BPFILTER_ALPHA_Q15          19661L

#define BEATDETECTOR_MIN_THRESHOLD               20L
#define BEATDETECTOR_MAX_THRESHOLD               800L
#define BEATDETECTOR_STEP_RESILIENCY             30L

/*
 * Original:
 * BEATDETECTOR_THRESHOLD_FALLOFF_TARGET = 0.3
 *
 * But decreaseThreshold uses:
 * 1.0 - 0.3 = 0.7
 *
 * Q15: 0.7 * 32768 = 22937.6
 */
#define BEATDETECTOR_THRESHOLD_FALLOFF_Q15       22938L

/*
 * Original:
 * threshold *= 0.99
 *
 * Q15: 0.99 * 32768 = 32440.32
 */
#define BEATDETECTOR_THRESHOLD_DECAY_Q15         32440L

#define BEATDETECTOR_INVALID_READOUT_DELAY       2000u
#define BEATDETECTOR_SAMPLES_PERIOD              10u

/*
 * Q15 fixed-point format.
 */
#define Q15_SHIFT                                15

typedef enum BeatDetectorState {
    BEATDETECTOR_STATE_INIT,
    BEATDETECTOR_STATE_WAITING,
    BEATDETECTOR_STATE_FOLLOWING_SLOPE,
    BEATDETECTOR_STATE_MAYBE_DETECTED,
    BEATDETECTOR_STATE_MASKING
} BeatDetectorState;

typedef enum {
    BEAT_INIT,
    BEAT_WAITING,
    BEAT_FOLLOWING_SLOPE,
    BEAT_MAYBE_DETECTED,
    BEAT_MASKING
} BeatState;

typedef struct {
    int32_t v[2];
} FilterBuLp1;

typedef struct {
    int32_t alpha_q15;
    int32_t dcw;
    uint8_t initialized;
} DCRemover;

typedef struct {
    BeatDetectorState state;
    int32_t threshold;
    uint32_t beatPeriod;
    int32_t lastMaxValue;
    uint32_t tsLastBeat;
} BeatDetector;


/*
 * BeatDetector
 */
void BeatDetector_init(BeatDetector* bd);
int BeatDetector_addSample(BeatDetector* bd, uint32_t now_ms, int32_t sample);
uint16_t BeatDetector_getRate(const BeatDetector* bd);
int32_t BeatDetector_getCurrentThreshold(const BeatDetector* bd);

#endif