#include "bench_stats.h"

#include <stdlib.h>
#include <string.h>

static int cmp_u32(const void *a, const void *b)
{
    const uint32_t aa = *(const uint32_t *)a;
    const uint32_t bb = *(const uint32_t *)b;
    return (aa > bb) - (aa < bb);
}

static uint32_t percentile(const uint32_t *sorted, size_t count, unsigned pct)
{
    if (!count) {
        return 0;
    }

    size_t index = ((count - 1u) * pct + 99u) / 100u;
    if (index >= count) {
        index = count - 1u;
    }
    return sorted[index];
}

void bench_stats_compute(uint32_t *samples,
                         size_t count,
                         uint32_t deadline_cycles,
                         bench_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!samples || !count) {
        return;
    }

    uint64_t total = 0;
    uint32_t misses = 0;
    for (size_t i = 0; i < count; ++i) {
        total += samples[i];
        if (samples[i] >= deadline_cycles) {
            ++misses;
        }
    }

    qsort(samples, count, sizeof(samples[0]), cmp_u32);

    out->min = samples[0];
    out->mean = (uint32_t)(total / count);
    out->p50 = percentile(samples, count, 50);
    out->p95 = percentile(samples, count, 95);
    out->p99 = percentile(samples, count, 99);
    out->max = samples[count - 1u];
    out->deadline_misses = misses;
}
