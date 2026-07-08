#include "spo2_detector.h"

static uint32_t isqrt_u64(uint64_t x)
{
    uint64_t op = x;
    uint64_t res = 0;
    uint64_t one = 1ULL << 62;

    while (one > op)
    {
        one >>= 2;
    }

    while (one != 0)
    {
        if (op >= res + one)
        {
            op -= res + one;
            res = res + 2 * one;
        }

        res >>= 1;
        one >>= 2;
    }

    return (uint32_t)res;
}

static uint32_t abs_i32_to_u32(int32_t x)
{
    if (x < 0)
    {
        return (uint32_t)(-x);
    }

    return (uint32_t)x;
}

void spo2_init(Spo2Detector_t *ctx)
{
    spo2_reset(ctx);
}

void spo2_reset(Spo2Detector_t *ctx)
{
    if (ctx == 0)
    {
        return;
    }

    ctx->irAcSqSum = 0ULL;
    ctx->redAcSqSum = 0ULL;
    ctx->irDcSum = 0ULL;
    ctx->redDcSum = 0ULL;
    ctx->samplesRecorded = 0UL;
    ctx->beatsDetectedNum = 0U;
    ctx->spo2 = SPO2_INVALID_VALUE;
    ctx->rValue1000 = 0U;
}

void spo2_update(
    Spo2Detector_t *ctx,
    int32_t irAC,
    int32_t redAC,
    uint16_t irRaw,
    uint16_t redRaw,
    bool beatDetected
)
{
    uint32_t irAbs;
    uint32_t redAbs;

    uint32_t irRms;
    uint32_t redRms;

    uint64_t numerator;
    uint64_t denominator;
    uint32_t r1000;

    int16_t estimated;

    if (ctx == 0)
    {
        return;
    }

    irAbs = abs_i32_to_u32(irAC);
    redAbs = abs_i32_to_u32(redAC);

    ctx->irAcSqSum += (uint64_t)irAbs * (uint64_t)irAbs;
    ctx->redAcSqSum += (uint64_t)redAbs * (uint64_t)redAbs;

    ctx->irDcSum += irRaw;
    ctx->redDcSum += redRaw;

    ctx->samplesRecorded++;

    if (!beatDetected)
    {
        return;
    }

    ctx->beatsDetectedNum++;

    if (ctx->beatsDetectedNum < SPO2_CALCULATE_EVERY_N_BEATS)
    {
        return;
    }

    if (ctx->samplesRecorded == 0UL ||
        ctx->irAcSqSum == 0ULL ||
        ctx->redAcSqSum == 0ULL ||
        ctx->irDcSum == 0ULL ||
        ctx->redDcSum == 0ULL)
    {
        spo2_reset(ctx);
        return;
    }

    /*
     * Ratio of ratios:
     *
     * R = (RED_AC / RED_DC) / (IR_AC / IR_DC)
     *
     * Integer-scaled:
     *
     * R1000 = 1000 * RED_AC_RMS * IR_DC
     *              / (IR_AC_RMS * RED_DC)
     *
     * samplesRecorded cancels out, so we do not need to divide the sums by N.
     */
    redRms = isqrt_u64(ctx->redAcSqSum);
    irRms = isqrt_u64(ctx->irAcSqSum);

    if (redRms == 0UL || irRms == 0UL)
    {
        spo2_reset(ctx);
        return;
    }

    numerator = (uint64_t)redRms * ctx->irDcSum * 1000ULL;
    denominator = (uint64_t)irRms * ctx->redDcSum;

    if (denominator == 0ULL)
    {
        spo2_reset(ctx);
        return;
    }

    r1000 = (uint32_t)(numerator / denominator);

    ctx->rValue1000 = (uint16_t)r1000;

    /*
     * Simple integer calibration:
     *
     * SpO2 ~= 110 - 25R
     *
     * R is scaled by 1000, so:
     *
     * SpO2 = 110 - (25 * R1000) / 1000
     */
    estimated = 110 - (int16_t)((25UL * r1000) / 1000UL);

    if (estimated > 100)
    {
        estimated = 100;
    }
    else if (estimated < 70)
    {
        estimated = 70;
    }

    ctx->spo2 = (uint8_t)estimated;

    /*
     * Reset accumulation window but keep the calculated SpO2 value.
     */
    ctx->irAcSqSum = 0ULL;
    ctx->redAcSqSum = 0ULL;
    ctx->irDcSum = 0ULL;
    ctx->redDcSum = 0ULL;
    ctx->samplesRecorded = 0UL;
    ctx->beatsDetectedNum = 0U;
}

uint8_t spo2_get(const Spo2Detector_t *ctx)
{
    if (ctx == 0)
    {
        return SPO2_INVALID_VALUE;
    }

    return ctx->spo2;
}

uint16_t spo2_get_r1000(const Spo2Detector_t *ctx)
{
    if (ctx == 0)
    {
        return 0U;
    }

    return ctx->rValue1000;
}

bool spo2_is_valid(const Spo2Detector_t *ctx)
{
    return (ctx != 0 && ctx->spo2 != SPO2_INVALID_VALUE);
}