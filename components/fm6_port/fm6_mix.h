#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#include "fm6_port.h"

/* Internal audio-owner helpers. At most 12 int32 voice blocks are added, so
 * int64 cannot overflow. Do NOT saturate per addition: later voices may cancel.
 * Narrow exactly once, after every voice has contributed. */
static inline void fm6_mix_add(int64_t sum[FM6_BENCH_BLOCK_FRAMES],
                               const int32_t voice[FM6_BENCH_BLOCK_FRAMES])
{
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        sum[i] += (int64_t)voice[i];
    }
}

static inline int32_t fm6_mix_narrow(int64_t value, bool *saturated)
{
    *saturated = value > INT32_MAX || value < INT32_MIN;
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    if (value < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
}
