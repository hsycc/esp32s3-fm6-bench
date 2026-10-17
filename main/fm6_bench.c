#include "fm6_bench.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "bench_stats.h"
#include "fm6_port.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifdef FM6_QEMU_CI
#define BENCH_WARMUP_BLOCKS 32u
#define BENCH_SAMPLES 128u
#define SCAN_WARMUP_BLOCKS 32u
#define SCAN_SAMPLES 16u
#else
#define BENCH_WARMUP_BLOCKS 512u
#define BENCH_SAMPLES 4096u
#define SCAN_WARMUP_BLOCKS 128u
#define SCAN_SAMPLES 512u
#endif

#define TARGET_MEAN_US 400u
#define TARGET_P95_US 450u
#define TARGET_P99_US 500u
#define TARGET_MAX_US 600u

static uint32_t s_samples[BENCH_SAMPLES];
static int32_t s_mix[FM6_BENCH_BLOCK_FRAMES];
static unsigned s_functional_failures;

static uint32_t cpu_mhz(void)
{
    return CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
}

static uint32_t deadline_cycles(void)
{
    const uint64_t hz = (uint64_t)cpu_mhz() * 1000000ull;
    return (uint32_t)((hz * FM6_BENCH_BLOCK_FRAMES + FM6_BENCH_SAMPLE_RATE - 1u) /
                      FM6_BENCH_SAMPLE_RATE);
}

static uint32_t us_to_cycles(uint32_t us)
{
    return us * cpu_mhz();
}

static double cycles_to_us(uint32_t cycles)
{
    return (double)cycles / (double)cpu_mhz();
}

static void verify_sounded(unsigned voices, unsigned algorithm, const char *phase)
{
    const unsigned sounded = fm6_port_last_sounded();
    if (sounded == voices) {
        return;
    }

    ++s_functional_failures;
    printf("FUNCTIONAL_FAIL phase=%s alg=%u expected_sounded=%u actual_sounded=%u\n",
           phase, algorithm + 1u, voices, sounded);
}

static void warmup(unsigned voices, unsigned algorithm, unsigned blocks)
{
    fm6_port_prepare(voices, algorithm);
    for (unsigned i = 0; i < blocks; ++i) {
        fm6_port_render_block(voices, algorithm, s_mix);
    }
}

static uint32_t scan_algorithm(unsigned voices, unsigned algorithm)
{
    warmup(voices, algorithm, SCAN_WARMUP_BLOCKS);

    uint64_t total = 0;
    for (unsigned i = 0; i < SCAN_SAMPLES; ++i) {
        const uint32_t begin = esp_cpu_get_cycle_count();
        fm6_port_render_block(voices, algorithm, s_mix);
        const uint32_t end = esp_cpu_get_cycle_count();
        verify_sounded(voices, algorithm, "scan");
        total += (uint32_t)(end - begin);
    }
    return (uint32_t)(total / SCAN_SAMPLES);
}

static bench_stats_t run_case(unsigned voices, unsigned algorithm)
{
    warmup(voices, algorithm, BENCH_WARMUP_BLOCKS);

    for (unsigned i = 0; i < BENCH_SAMPLES; ++i) {
        const uint32_t begin = esp_cpu_get_cycle_count();
        fm6_port_render_block(voices, algorithm, s_mix);
        const uint32_t end = esp_cpu_get_cycle_count();
        verify_sounded(voices, algorithm, "percentile");
        s_samples[i] = end - begin;
    }

    bench_stats_t stats;
    bench_stats_compute(s_samples, BENCH_SAMPLES, deadline_cycles(), &stats);
    return stats;
}

static void print_stats(unsigned voices, unsigned algorithm, const bench_stats_t *s)
{
    printf("FM6 voices=%u alg=%u | cycles min=%" PRIu32
           " mean=%" PRIu32 " p50=%" PRIu32 " p95=%" PRIu32
           " p99=%" PRIu32 " max=%" PRIu32 " misses=%" PRIu32 "\n",
           voices, algorithm + 1u,
           s->min, s->mean, s->p50, s->p95, s->p99, s->max, s->deadline_misses);

    printf("                      us min=%.2f mean=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f\n",
           cycles_to_us(s->min), cycles_to_us(s->mean), cycles_to_us(s->p50),
           cycles_to_us(s->p95), cycles_to_us(s->p99), cycles_to_us(s->max));
}

#ifndef FM6_QEMU_CI
static int hardware_accepts(const bench_stats_t *s)
{
    return s_functional_failures == 0u &&
           s->mean < us_to_cycles(TARGET_MEAN_US) &&
           s->p95 < us_to_cycles(TARGET_P95_US) &&
           s->p99 < us_to_cycles(TARGET_P99_US) &&
           s->max < us_to_cycles(TARGET_MAX_US) &&
           s->deadline_misses == 0u;
}
#endif

void fm6_bench_run(void)
{
    printf("\n=== ESP32-S3 FM6 benchmark ===\n");
    printf("CPU=%" PRIu32 " MHz, sample_rate=%" PRIu32 " Hz, block=%" PRIu32 " frames\n",
           cpu_mhz(),
           (uint32_t)FM6_BENCH_SAMPLE_RATE,
           (uint32_t)FM6_BENCH_BLOCK_FRAMES);
    printf("deadline=%" PRIu32 " cycles = %.2f us\n",
           deadline_cycles(), cycles_to_us(deadline_cycles()));
    printf("upstream Felucca revision=%s\n", fm6_port_upstream_revision());
#ifdef FM6_QEMU_CI
    printf("QEMU_MODE=1: timing values are emulator-only; use them for regression, not physical ESP32-S3 performance.\n");
#else
    printf("BENCH_MODE=HARDWARE core=1\n");
#endif

    s_functional_failures = 0;

    if (!fm6_port_available()) {
        printf("FM6 upstream is not staged. Run: python3 tools/import_felucca.py\n");
#ifdef FM6_QEMU_CI
        printf("QEMU_VALIDATION: FAIL failures=1 reason=upstream_missing\n");
#else
        printf("HARDWARE_ACCEPTANCE: FAIL failures=1 reason=upstream_missing\n");
#endif
        return;
    }

    printf("\n[1/2] scanning all 32 algorithms at 12 voices...\n");
    unsigned worst_alg = 0;
    uint32_t worst_mean = 0;
    for (unsigned alg = 0; alg < 32u; ++alg) {
        const uint32_t mean = scan_algorithm(12u, alg);
        printf("  alg=%2u mean=%6" PRIu32 " cycles (%7.2f us) sounded=%u\n",
               alg + 1u, mean, cycles_to_us(mean), fm6_port_last_sounded());
        if (mean > worst_mean) {
            worst_mean = mean;
            worst_alg = alg;
        }
        taskYIELD();
    }

    printf("worst scan result: algorithm %u, mean=%" PRIu32 " cycles (%.2f us)\n",
           worst_alg + 1u, worst_mean, cycles_to_us(worst_mean));

    printf("\n[2/2] full percentile runs using worst scanned algorithm...\n");
    static const unsigned voice_counts[] = {6u, 8u, 10u, 12u};
    bench_stats_t stats12 = {0};
    for (unsigned i = 0; i < sizeof(voice_counts) / sizeof(voice_counts[0]); ++i) {
        const unsigned voices = voice_counts[i];
        const bench_stats_t stats = run_case(voices, worst_alg);
        print_stats(voices, worst_alg, &stats);
        if (voices == 12u) {
            stats12 = stats;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    const uint32_t checksum = fm6_port_checksum();
    if (checksum == 0u) {
        ++s_functional_failures;
        printf("FUNCTIONAL_FAIL phase=checksum value=0\n");
    }
    printf("checksum=0x%08" PRIx32 "\n", checksum);
#ifdef FM6_QEMU_CI
    printf("QEMU_VALIDATION: %s failures=%u\n",
           s_functional_failures == 0 ? "PASS" : "FAIL",
           s_functional_failures);
#else
    printf("HARDWARE_RESULT voices=12 alg=%u mean_cycles=%" PRIu32
           " p95_cycles=%" PRIu32 " p99_cycles=%" PRIu32
           " max_cycles=%" PRIu32 " deadline_misses=%" PRIu32
           " checksum=0x%08" PRIx32 "\n",
           worst_alg + 1u,
           stats12.mean, stats12.p95, stats12.p99, stats12.max,
           stats12.deadline_misses, checksum);
    printf("HARDWARE_ACCEPTANCE: %s failures=%u targets_us=%u/%u/%u/%u\n",
           hardware_accepts(&stats12) ? "PASS" : "FAIL",
           s_functional_failures,
           TARGET_MEAN_US, TARGET_P95_US, TARGET_P99_US, TARGET_MAX_US);
#endif
    printf("=== benchmark complete ===\n\n");
}
