#include "filters.h"

void DCRemover_Init(DCRemoverQ15 *f)
{
    f->dcw = 0;
    f->initialized = false;
}

int32_t DCRemover_Step(DCRemoverQ15 *f, uint16_t raw)
{
    int32_t old;

    /*
     * Avoid startup false pulse.
     *
     * Original formula:
     * dcw = raw + 0.95 * dcw
     *
     * For constant input:
     * dcw = raw / (1 - 0.95) = raw * 20
     */
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

void LowPass_Init(LowPassQ15 *f)
{
    f->v0 = 0;
    f->v1 = 0;
}

int32_t LowPass_Step(LowPassQ15 *f, int32_t x)
{
    /*
     * Fixed-point translation of original Arduino library filter.
     */
    f->v0 = f->v1;

    f->v1 = Q15_MUL(LPF_B0_Q15, x)
          + Q15_MUL(LPF_A1_Q15, f->v0);

    return f->v0 + f->v1;
}