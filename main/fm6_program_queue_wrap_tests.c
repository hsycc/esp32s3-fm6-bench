#include "fm6_program_queue_wrap_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_port.h"
#include "fm6_program_queue.h"

static unsigned s_tests;
static unsigned s_failures;
static fm6_program_queue_t s_queue;
static uint8_t s_voice_a[DX7_VCED_SIZE];
static uint8_t s_voice_b[DX7_VCED_SIZE];
static uint8_t s_voice_c[DX7_VCED_SIZE];
static uint32_t s_counter_after;
static uint32_t s_first_generation;
static uint32_t s_second_generation;

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("FM6_PROGRAM_QUEUE_WRAP_TEST name=%s status=%s\n",
           name, pass ? "PASS" : "FAIL");
}

static void make_voice(uint8_t voice[DX7_VCED_SIZE], unsigned variant, uint8_t algorithm)
{
    memset(voice, 0, DX7_VCED_SIZE);
    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = voice + op * 21u;
        for (unsigned stage = 0; stage < 4u; ++stage) {
            o[stage] = 99u;
            o[4u + stage] = 99u;
        }
        o[8] = 50u;
        o[13] = (uint8_t)(variant & 7u);
        o[15] = (uint8_t)((variant + op) & 7u);
        o[16] = 99u;
        o[17] = 0u;
        o[18] = (uint8_t)(1u + ((variant + op) % 8u));
        o[19] = (uint8_t)((variant * 7u + op * 11u) % 100u);
        o[20] = 7u;
    }

    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = 99u;
        voice[130u + i] = 50u;
    }
    voice[DX7_VCED_ALGORITHM_OFFSET] = algorithm;
    voice[DX7_VCED_FEEDBACK_OFFSET] = (uint8_t)(variant & 7u);
    voice[136] = 1u;
    voice[137] = 45u;
    voice[DX7_VCED_TRANSPOSE_OFFSET] = 24u;

    char name[11] = {0};
    snprintf(name, sizeof(name), "WR%02uFM6XYZ", variant % 100u);
    memcpy(voice + DX7_VCED_NAME_OFFSET, name, DX7_VCED_NAME_SIZE);
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voice_a, 3u, 5u);
    make_voice(s_voice_b, 11u, 17u);
    make_voice(s_voice_c, 19u, 29u);
}

static uint32_t load_relaxed(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static void store_relaxed(uint32_t *value, uint32_t next)
{
    __atomic_store_n(value, next, __ATOMIC_RELAXED);
}

static void test_counter_wrap_preserves_fifo(void)
{
    fm6_program_queue_init(&s_queue);
    fm6_port_prepare(12u, 0u);

    const uint32_t start = UINT32_MAX - 1u;
    store_relaxed(&s_queue.write_count, start);
    store_relaxed(&s_queue.read_count, start);
    store_relaxed(&s_queue.next_generation, 100u);

    uint32_t submitted[4] = {0};
    bool accepted =
        fm6_program_queue_submit(&s_queue, s_voice_a, 12u, 36u, 3u, 100u, &submitted[0]) &&
        fm6_program_queue_submit(&s_queue, s_voice_b, 10u, 40u, 2u, 105u, &submitted[1]) &&
        fm6_program_queue_submit(&s_queue, s_voice_c, 8u, 48u, 1u, 110u, &submitted[2]) &&
        fm6_program_queue_submit(&s_queue, s_voice_a, 6u, 52u, 1u, 115u, &submitted[3]);

    const bool full_across_wrap = accepted &&
                                  fm6_program_queue_pending(&s_queue) == FM6_PROGRAM_QUEUE_CAPACITY &&
                                  load_relaxed(&s_queue.write_count) == 2u;

    static const uint32_t expected_generation[4] = {100u, 101u, 102u, 103u};
    static const unsigned expected_algorithm[4] = {5u, 17u, 29u, 5u};
    bool fifo = true;
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t generation = 0u;
        fifo &= fm6_program_queue_commit_next(&s_queue, &generation);
        fifo &= generation == expected_generation[i];
        fifo &= fm6_port_patch_algorithm() == expected_algorithm[i];
        fifo &= fm6_program_queue_pending(&s_queue) == (3u - i);
    }

    uint32_t reused_generation = 0u;
    const bool reused = fm6_program_queue_submit(
                            &s_queue, s_voice_b, 12u, 36u, 3u, 120u, &reused_generation) &&
                        fm6_program_queue_commit_next(&s_queue, NULL);

    s_counter_after = load_relaxed(&s_queue.read_count);
    const bool pass = full_across_wrap && fifo && reused &&
                      reused_generation == 104u &&
                      fm6_port_patch_algorithm() == 17u &&
                      fm6_program_queue_pending(&s_queue) == 0u &&
                      load_relaxed(&s_queue.write_count) == 3u &&
                      s_counter_after == 3u &&
                      fm6_program_queue_committed(&s_queue) == 5u;
    check("counter_wrap_preserves_fifo", pass);
}

static void test_generation_wrap_skips_zero(void)
{
    fm6_program_queue_init(&s_queue);
    store_relaxed(&s_queue.next_generation, UINT32_MAX);

    uint32_t g1 = 0u;
    uint32_t g2 = 0u;
    uint32_t g3 = 0u;
    const bool submitted =
        fm6_program_queue_submit(&s_queue, s_voice_a, 12u, 36u, 3u, 100u, &g1) &&
        fm6_program_queue_submit(&s_queue, s_voice_b, 12u, 36u, 3u, 100u, &g2) &&
        fm6_program_queue_submit(&s_queue, s_voice_c, 12u, 36u, 3u, 100u, &g3);

    uint32_t c1 = 0u;
    uint32_t c2 = 0u;
    uint32_t c3 = 0u;
    const bool committed =
        fm6_program_queue_commit_next(&s_queue, &c1) &&
        fm6_program_queue_commit_next(&s_queue, &c2) &&
        fm6_program_queue_commit_next(&s_queue, &c3);

    s_first_generation = g1;
    s_second_generation = g2;
    check("generation_wrap_skips_zero",
          submitted && committed &&
          g1 == UINT32_MAX && g2 == 1u && g3 == 2u &&
          c1 == g1 && c2 == g2 && c3 == g3 &&
          fm6_program_queue_active_generation(&s_queue) == 2u &&
          fm6_program_queue_pending(&s_queue) == 0u);
}

unsigned fm6_program_queue_wrap_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    s_counter_after = 0u;
    s_first_generation = 0u;
    s_second_generation = 0u;
    build_fixtures();

    test_counter_wrap_preserves_fifo();
    test_generation_wrap_skips_zero();

    printf("FM6_PROGRAM_QUEUE_WRAP_RESULT: %s counter_start=%" PRIu32
           " counter_after=%" PRIu32 " first_generation=%" PRIu32
           " second_generation=%" PRIu32 "\n",
           s_failures == 0u ? "PASS" : "FAIL",
           UINT32_MAX - 1u,
           s_counter_after,
           s_first_generation,
           s_second_generation);
    printf("QEMU_FM6_PROGRAM_QUEUE_WRAP: %s tests=%u failures=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures);
    return s_failures;
}
