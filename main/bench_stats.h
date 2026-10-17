#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t min;
    uint32_t mean;
    uint32_t p50;
    uint32_t p95;
    uint32_t p99;
    uint32_t max;
    uint32_t deadline_misses;
} bench_stats_t;

void bench_stats_compute(uint32_t *samples,
                         size_t count,
                         uint32_t deadline_cycles,
                         bench_stats_t *out);
