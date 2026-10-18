#include "fm6_program_queue_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_port.h"
#include "fm6_program_queue.h"

#define SWITCH_RENDER_BLOCKS 64u

static unsigned s_tests;
static unsigned s_failures;
static fm6_program_queue_t s_queue;
static uint8_t s_voice_a[DX7_VCED_SIZE];
static uint8_t s_voice_b[DX7_VCED_SIZE];
static uint8_t s_voice_c[DX7_VCED_SIZE];

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("FM6_PROGRAM_QUEUE_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
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
        o[9] = 0u;
        o[10] = 0u;
        o[11] = 0u;
        o[12] = 0u;
        o[13] = (uint8_t)(variant & 7u);
        o[14] = 0u;
        o[15] = (uint8_t)((variant + op) & 7u);
        o[16] = 99u;
        o[17] = (uint8_t)((variant + op) & 1u);
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
    voice[138] = 0u;
    voice[139] = 0u;
    voice[140] = 0u;
    voice[141] = 0u;
    voice[142] = 0u;
    voice[143] = 0u;
    voice[DX7_VCED_TRANSPOSE_OFFSET] = 24u;

    char name[11];
    snprintf(name, sizeof(name), "PQ%02uFM6XYZ", variant % 100u);
    memcpy(voice + DX7_VCED_NAME_OFFSET, name, DX7_VCED_NAME_SIZE);
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voice_a, 3u, 5u);
    make_voice(s_voice_b, 11u, 17u);
    make_voice(s_voice_c, 19u, 29u);
}

static void test_submit_waits_for_commit(void)
{
    fm6_program_queue_init(&s_queue);
    fm6_port_prepare(12u, 31u);

    uint32_t generation = 0u;
    const bool submitted = fm6_program_queue_submit(
        &s_queue, s_voice_a, 12u, 36u, 3u, 110u, &generation);
    const bool unchanged = fm6_port_patch_algorithm() == 31u &&
                           fm6_program_queue_active_generation(&s_queue) == 0u &&
                           fm6_program_queue_pending(&s_queue) == 1u;

    uint32_t committed_generation = 0u;
    const bool committed = fm6_program_queue_commit_next(&s_queue, &committed_generation);
    const bool switched = committed &&
                          generation == 1u && committed_generation == generation &&
                          fm6_program_queue_active_generation(&s_queue) == generation &&
                          fm6_port_patch_algorithm() == 5u &&
                          fm6_program_queue_pending(&s_queue) == 0u;
    check("submit_waits_for_block_commit", submitted && unchanged && switched);
}

static void test_input_copy_isolation(void)
{
    fm6_program_queue_init(&s_queue);
    uint8_t source[DX7_VCED_SIZE];
    memcpy(source, s_voice_a, sizeof(source));

    const bool submitted = fm6_program_queue_submit(
        &s_queue, source, 12u, 36u, 3u, 100u, NULL);
    source[DX7_VCED_ALGORITHM_OFFSET] = 22u;
    memset(source, 0xa5, 21u);

    const bool committed = fm6_program_queue_commit_next(&s_queue, NULL);
    check("input_copy_isolation",
          submitted && committed && fm6_port_patch_algorithm() == 5u);
}

static void test_submit_canonicalizes_voice(void)
{
    fm6_program_queue_init(&s_queue);
    uint8_t invalid[DX7_VCED_SIZE];
    memcpy(invalid, s_voice_a, sizeof(invalid));
    invalid[DX7_VCED_ALGORITHM_OFFSET] = 0xffu;
    invalid[DX7_VCED_FEEDBACK_OFFSET] = 0xffu;
    invalid[17u] = 0xffu;
    invalid[18u] = 0xffu;

    const bool submitted = fm6_program_queue_submit(
        &s_queue, invalid, 12u, 36u, 3u, 100u, NULL);
    const bool canonical = s_queue.slots[0].voice[DX7_VCED_ALGORITHM_OFFSET] == 31u &&
                           s_queue.slots[0].voice[DX7_VCED_FEEDBACK_OFFSET] == 7u &&
                           s_queue.slots[0].voice[17u] == 1u &&
                           s_queue.slots[0].voice[18u] == 31u;
    const bool committed = fm6_program_queue_commit_next(&s_queue, NULL);
    check("submit_canonicalizes_voice",
          submitted && canonical && committed && fm6_port_patch_algorithm() == 31u);
}

static void test_fifo_generation_order(void)
{
    fm6_program_queue_init(&s_queue);
    uint32_t g1 = 0u;
    uint32_t g2 = 0u;
    uint32_t g3 = 0u;
    const bool submitted =
        fm6_program_queue_submit(&s_queue, s_voice_a, 12u, 36u, 3u, 100u, &g1) &&
        fm6_program_queue_submit(&s_queue, s_voice_b, 10u, 40u, 2u, 105u, &g2) &&
        fm6_program_queue_submit(&s_queue, s_voice_c, 8u, 48u, 1u, 120u, &g3);

    uint32_t c1 = 0u;
    uint32_t c2 = 0u;
    uint32_t c3 = 0u;
    const bool first = fm6_program_queue_commit_next(&s_queue, &c1) &&
                       c1 == g1 && fm6_port_patch_algorithm() == 5u;
    const bool second = fm6_program_queue_commit_next(&s_queue, &c2) &&
                        c2 == g2 && fm6_port_patch_algorithm() == 17u;
    const bool third = fm6_program_queue_commit_next(&s_queue, &c3) &&
                       c3 == g3 && fm6_port_patch_algorithm() == 29u;

    check("fifo_generation_order",
          submitted && g1 == 1u && g2 == 2u && g3 == 3u &&
          first && second && third &&
          fm6_program_queue_committed(&s_queue) == 3u &&
          fm6_program_queue_pending(&s_queue) == 0u);
}

static void test_queue_full_backpressure(void)
{
    fm6_program_queue_init(&s_queue);
    bool accepted = true;
    for (unsigned i = 0; i < FM6_PROGRAM_QUEUE_CAPACITY; ++i) {
        const uint8_t *voice = (i & 1u) ? s_voice_b : s_voice_a;
        accepted &= fm6_program_queue_submit(
            &s_queue, voice, 12u, 36u, 3u, 100u, NULL);
    }
    const bool rejected = !fm6_program_queue_submit(
        &s_queue, s_voice_c, 12u, 36u, 3u, 100u, NULL);
    const bool full_state = fm6_program_queue_pending(&s_queue) == FM6_PROGRAM_QUEUE_CAPACITY &&
                            fm6_program_queue_rejected(&s_queue) == 1u;

    unsigned drained = 0u;
    while (fm6_program_queue_commit_next(&s_queue, NULL)) {
        ++drained;
    }
    check("queue_full_backpressure",
          accepted && rejected && full_state &&
          drained == FM6_PROGRAM_QUEUE_CAPACITY &&
          fm6_program_queue_pending(&s_queue) == 0u &&
          fm6_program_queue_committed(&s_queue) == FM6_PROGRAM_QUEUE_CAPACITY);
}

static void test_empty_and_invalid_noop(void)
{
    fm6_program_queue_init(&s_queue);
    fm6_port_prepare(12u, 7u);
    const bool empty = !fm6_program_queue_commit_next(&s_queue, NULL) &&
                       fm6_program_queue_active_generation(&s_queue) == 0u &&
                       fm6_port_patch_algorithm() == 7u;
    const bool invalid_zero = !fm6_program_queue_submit(
        &s_queue, s_voice_a, 0u, 36u, 3u, 100u, NULL);
    const bool invalid_large = !fm6_program_queue_submit(
        &s_queue, s_voice_a, FM6_BENCH_MAX_VOICES + 1u, 36u, 3u, 100u, NULL);
    check("empty_invalid_noop",
          empty && invalid_zero && invalid_large &&
          fm6_program_queue_rejected(&s_queue) == 2u &&
          fm6_program_queue_pending(&s_queue) == 0u);
}

static uint32_t mix_signature(uint32_t signature, uint32_t checksum, uint64_t energy,
                              unsigned sounded, uint32_t generation, unsigned algorithm)
{
    signature ^= checksum;
    signature *= 16777619u;
    signature ^= (uint32_t)energy;
    signature *= 16777619u;
    signature ^= (uint32_t)(energy >> 32);
    signature *= 16777619u;
    signature ^= sounded;
    signature *= 16777619u;
    signature ^= generation;
    signature *= 16777619u;
    signature ^= algorithm;
    signature *= 16777619u;
    return signature;
}

static uint32_t run_switch_sequence(unsigned *active_steps)
{
    fm6_program_queue_init(&s_queue);
    const uint8_t *voices[] = {s_voice_a, s_voice_b, s_voice_c};
    const uint8_t counts[] = {12u, 10u, 8u};
    uint32_t signature = 2166136261u;
    *active_steps = 0u;

    for (unsigned step = 0; step < 3u; ++step) {
        uint32_t submitted_generation = 0u;
        if (!fm6_program_queue_submit(&s_queue,
                                      voices[step],
                                      counts[step],
                                      (uint8_t)(36u + step * 4u),
                                      (uint8_t)(3u - step),
                                      (uint8_t)(100u + step * 8u),
                                      &submitted_generation)) {
            return 0u;
        }

        /* The program must remain pending until this explicit block-boundary commit. */
        if (fm6_program_queue_active_generation(&s_queue) == submitted_generation) {
            return 0u;
        }

        uint32_t committed_generation = 0u;
        if (!fm6_program_queue_commit_next(&s_queue, &committed_generation) ||
            committed_generation != submitted_generation) {
            return 0u;
        }

        int32_t mix[FM6_BENCH_BLOCK_FRAMES];
        const unsigned algorithm = fm6_port_patch_algorithm();
        unsigned peak_sounded = 0u;
        for (unsigned block = 0; block < SWITCH_RENDER_BLOCKS; ++block) {
            fm6_port_render_block(counts[step], algorithm, mix);
            const unsigned sounded = fm6_port_last_sounded();
            if (sounded > peak_sounded) {
                peak_sounded = sounded;
            }
        }
        if (fm6_port_abs_energy() != 0u || peak_sounded != 0u) {
            ++*active_steps;
        }
        signature = mix_signature(signature,
                                  fm6_port_checksum(),
                                  fm6_port_abs_energy(),
                                  peak_sounded,
                                  committed_generation,
                                  algorithm);
    }

    return signature;
}

static void test_deterministic_switch_sequence(void)
{
    unsigned active_a = 0u;
    unsigned active_b = 0u;
    const uint32_t a = run_switch_sequence(&active_a);
    const uint32_t b = run_switch_sequence(&active_b);
    const bool pass = a != 0u && a == b && active_a > 0u && active_a == active_b;
    check("deterministic_program_switch_sequence", pass);
    printf("FM6_PROGRAM_QUEUE_SWITCH: %s steps=3 passes=2 active_steps=%u signature=0x%08" PRIx32 "\n",
           pass ? "PASS" : "FAIL", active_a, a);
}

unsigned fm6_program_queue_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    build_fixtures();

    test_submit_waits_for_commit();
    test_input_copy_isolation();
    test_submit_canonicalizes_voice();
    test_fifo_generation_order();
    test_queue_full_backpressure();
    test_empty_and_invalid_noop();
    test_deterministic_switch_sequence();

    printf("QEMU_FM6_PROGRAM_QUEUE: %s tests=%u failures=%u capacity=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures,
           (unsigned)FM6_PROGRAM_QUEUE_CAPACITY);
    return s_failures;
}
