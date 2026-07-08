#ifndef SPO2_DETECTOR_H
#define SPO2_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

#define SPO2_CALCULATE_EVERY_N_BEATS  3U
#define SPO2_INVALID_VALUE            0U

typedef struct
{
    uint64_t irAcSqSum;
    uint64_t redAcSqSum;

    uint64_t irDcSum;
    uint64_t redDcSum;

    uint32_t samplesRecorded;
    uint8_t beatsDetectedNum;

    uint8_t spo2;
    uint16_t rValue1000;
} Spo2Detector_t;

void spo2_init(Spo2Detector_t *ctx);
void spo2_reset(Spo2Detector_t *ctx);

void spo2_update(
    Spo2Detector_t *ctx,
    int32_t irAC,
    int32_t redAC,
    uint16_t irRaw,
    uint16_t redRaw,
    bool beatDetected
);

uint8_t spo2_get(const Spo2Detector_t *ctx);
uint16_t spo2_get_r1000(const Spo2Detector_t *ctx);
bool spo2_is_valid(const Spo2Detector_t *ctx);

#endif