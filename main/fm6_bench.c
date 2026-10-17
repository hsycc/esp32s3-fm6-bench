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

#define BENCH_WARMUP_BLOCKS 512u
#define BENCH_SAMPLES 4096u
#define SCAN_WARMUP_BLOCKS 128u
#define SCAN_SAMPLES 512u

static uint32_t s_samples[BENCH_SAMPLES];
static int32_t s_mix[FM6_BENCH_BLOCK_FRAMES];

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

static double cycles_to_us(uint32_t cycles)
{
    return (double)cycles / (double)cpu_mhz();
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

void fm6_bench_run(void)
{
    printf("\n=== ESP32-S3 FM6 benchmark ===\n");
    printf("CPU=%u MHz, sample_rate=%u Hz, block=%u frames\n",
           cpu_mhz(), FM6_BENCH_SAMPLE_RATE, FM6_BENCH_BLOCK_FRAMES);
    printf("deadline=%" PRIu32 " cycles = %.2f us\n",
           deadline_cycles(), cycles_to_us(deadline_cycles()));
    printf("upstream Felucca revision=%s\n", fm6_port_upstream_revision());

    if (!fm6_port_available()) {
        printf("FM6 upstream is not staged. Run: python3 tools/import_felucca.py\n");
        return;
    }

    printf("\n[1/2] scanning all 32 algorithms at 12 voices...\n");
    unsigned worst_alg = 0;
    uint32_t worst_mean = 0;
    for (unsigned alg = 0; alg < 32u; ++alg) {
        const uint32_t mean = scan_algorithm(12u, alg);
        printf("  alg=%2u mean=%6" PRIu32 " cycles (%7.2f us)\n",
               alg + 1u, mean, cycles_to_us(mean));
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
    for (unsigned i = 0; i < sizeof(voice_counts) / sizeof(voice_counts[0]); ++i) {
        const unsigned voices = voice_counts[i];
        const bench_stats_t stats = run_case(voices, worst_alg);
        print_stats(voices, worst_alg, &stats);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    printf("checksum=0x%08" PRIx32 "\n", fm6_port_checksum());
    printf("=== benchmark complete ===\n\n");
}
