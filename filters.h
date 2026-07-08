#ifndef FILTERS_H
#define FILTERS_H

#include <stdint.h>
#include <stdbool.h>
#include "beat_detector_c.h"

/*
 * DC remover filter state.
 */
typedef struct {
    int32_t dcw;
    bool initialized;
} DCRemoverQ15;

/*
 * Low-pass filter state.
 */
typedef struct {
    int32_t v0;
    int32_t v1;
} LowPassQ15;

#define Q15_MUL(a, b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> Q15_SHIFT))

#define DC_ALPHA_Q15               31130L
#define LPF_B0_Q15                 8036L
#define LPF_A1_Q15                 16696L

/*
 * Filter functions.
 */
void DCRemover_Init(DCRemoverQ15 *f);
int32_t DCRemover_Step(DCRemoverQ15 *f, uint16_t raw);

void LowPass_Init(LowPassQ15 *f);
int32_t LowPass_Step(LowPassQ15 *f, int32_t x);








#endif
