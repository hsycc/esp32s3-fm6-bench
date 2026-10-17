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
#define BENCH_WARMUP_BLOCKS 64u
#define BENCH_SAMPLES 512u
#define SCAN_WARMUP_BLOCKS 64u
#define SCAN_SAMPLES 64u
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

#ifndef FM6_QEMU_CI
static uint32_t us_to_cycles(uint32_t us)
{
    return us * cpu_mhz();
}
#endif

static double cycles_to_us(uint32_t cycles)
{
    return (double)cycles / (double)cpu_mhz();
}

#ifndef FM6_QEMU_CI
/* A deliberately synthetic Core0 workload. It does integer arithmetic and
 * repeatedly touches 4 KiB of internal DRAM for ~500 us, then sleeps for one
 * RTOS tick. This is not intended to model a specific UI exactly; it gives us
 * a repeatable way to observe cross-core/cache/bus sensitivity while the FM6
 * render stays pinned to Core1. */
static volatile int s_core0_stress_enabled;
static volatile uint32_t s_core0_stress_sink;
static uint32_t s_core0_stress_mem[1024];

static void core0_stress_task(void *arg)
{
    (void)arg;
    uint32_t x = 0x6d2b79f5u;

    for (;;) {
        if (!s_core0_stress_enabled) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        const uint32_t begin = esp_cpu_get_cycle_count();
        const uint32_t busy_cycles = us_to_cycles(500u);
        unsigned i = 0;
        do {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            const unsigned index = i++ & 1023u;
            s_core0_stress_mem[index] = x + s_core0_stress_mem[(index + 257u) & 1023u];
        } while ((uint32_t)(esp_cpu_get_cycle_count() - begin) < busy_cycles);

        s_core0_stress_sink = x ^ s_core0_stress_mem[i & 1023u];
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void start_core0_stress_task(void)
{
    const BaseType_t ok = xTaskCreatePinnedToCore(
        core0_stress_task,
        "fm6_core0_stress",
        3072,
        NULL,
        5,
        NULL,
        0);
    if (ok != pdPASS) {
        ++s_functional_failures;
        printf("FUNCTIONAL_FAIL phase=core0_stress_task_create\n");
    }
}
#endif

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

static bench_stats_t run_case(unsigned voices, unsigned algorithm, const char *phase)
{
    warmup(voices, algorithm, BENCH_WARMUP_BLOCKS);

    for (unsigned i = 0; i < BENCH_SAMPLES; ++i) {
        const uint32_t begin = esp_cpu_get_cycle_count();
        fm6_port_render_block(voices, algorithm, s_mix);
        const uint32_t end = esp_cpu_get_cycle_count();
        verify_sounded(voices, algorithm, phase);
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

static void print_hardware_result(const char *name,
                                  unsigned algorithm,
                                  const bench_stats_t *s,
                                  uint32_t checksum)
{
    printf("%s voices=12 alg=%u mean_cycles=%" PRIu32
           " p95_cycles=%" PRIu32 " p99_cycles=%" PRIu32
           " max_cycles=%" PRIu32 " deadline_misses=%" PRIu32
           " checksum=0x%08" PRIx32 "\n",
           name,
           algorithm + 1u,
           s->mean, s->p95, s->p99, s->max,
           s->deadline_misses, checksum);
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
    printf("BENCH_MODE=HARDWARE audio_core=1 stress_core=0\n");
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

#ifndef FM6_QEMU_CI
    start_core0_stress_task();
#endif

    printf("\n[1/2] scanning all 32 algorithms at 12 voices...\n");
    unsigned worst_alg = 0;
    uint32_t worst_mean = 0;
    for (unsigned alg = 0; alg < 32u; ++alg) {
        const uint32_t mean = scan_algorithm(12u, alg);
        const uint32_t signature = fm6_port_checksum();
        printf("  alg=%2u mean=%6" PRIu32 " cycles (%7.2f us) sounded=%u checksum=0x%08" PRIx32 "\n",
               alg + 1u, mean, cycles_to_us(mean), fm6_port_last_sounded(), signature);
        if (signature == 0u) {
            ++s_functional_failures;
            printf("FUNCTIONAL_FAIL phase=algorithm_signature alg=%u value=0\n", alg + 1u);
        }
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
        const bench_stats_t stats = run_case(voices, worst_alg, "percentile");
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
    printf("QEMU_RESULT voices=12 alg=%u mean_cycles=%" PRIu32
           " p95_cycles=%" PRIu32 " p99_cycles=%" PRIu32
           " max_cycles=%" PRIu32 " deadline_misses=%" PRIu32
           " checksum=0x%08" PRIx32 "\n",
           worst_alg + 1u,
           stats12.mean, stats12.p95, stats12.p99, stats12.max,
           stats12.deadline_misses, checksum);
    printf("QEMU_VALIDATION: %s failures=%u algorithms=32 voices=6/8/10/12\n",
           s_functional_failures == 0 ? "PASS" : "FAIL",
           s_functional_failures);
#else
    print_hardware_result("HARDWARE_RESULT", worst_alg, &stats12, checksum);
    printf("HARDWARE_ACCEPTANCE: %s failures=%u targets_us=%u/%u/%u/%u\n",
           hardware_accepts(&stats12) ? "PASS" : "FAIL",
           s_functional_failures,
           TARGET_MEAN_US, TARGET_P95_US, TARGET_P99_US, TARGET_MAX_US);

    printf("\n[stress] 12 voices with synthetic Core0 DRAM/ALU load...\n");
    s_core0_stress_enabled = 1;
    vTaskDelay(pdMS_TO_TICKS(20));
    const bench_stats_t loaded12 = run_case(12u, worst_alg, "core0_stress");
    s_core0_stress_enabled = 0;
    const uint32_t loaded_checksum = fm6_port_checksum();
    print_stats(12u, worst_alg, &loaded12);
    print_hardware_result("HARDWARE_STRESS_RESULT", worst_alg, &loaded12, loaded_checksum);
    printf("HARDWARE_STRESS_ACCEPTANCE: %s failures=%u stress_sink=0x%08" PRIx32 "\n",
           hardware_accepts(&loaded12) ? "PASS" : "FAIL",
           s_functional_failures,
           s_core0_stress_sink);
#endif
    printf("=== benchmark complete ===\n\n");
}
