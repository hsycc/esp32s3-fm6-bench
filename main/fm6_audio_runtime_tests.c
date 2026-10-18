#include "fm6_audio_runtime_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_audio_runtime.h"
#include "fm6_port.h"
#include "fm6_program_queue.h"

#define RUNTIME_SEQUENCE_BLOCKS 8u

static unsigned s_tests;
static unsigned s_failures;
static fm6_program_queue_t s_queue;
static fm6_audio_runtime_t s_runtime;
static uint8_t s_voices[4][DX7_VCED_SIZE];

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("FM6_AUDIO_RUNTIME_TEST name=%s status=%s\n",
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
        o[8] = (uint8_t)(40u + ((variant + op * 5u) % 40u));
        o[9] = (uint8_t)((variant + op) % 20u);
        o[10] = (uint8_t)((variant + op * 2u) % 20u);
        o[11] = (uint8_t)(op & 3u);
        o[12] = (uint8_t)((op + 1u) & 3u);
        o[13] = (uint8_t)((variant + op) & 7u);
        o[14] = (uint8_t)(op & 3u);
        o[15] = (uint8_t)((variant + op * 2u) & 7u);
        o[16] = 99u;
        o[17] = (uint8_t)((variant + op) & 1u);
        o[18] = (uint8_t)(1u + ((variant + op) % 8u));
        o[19] = (uint8_t)((variant * 9u + op * 13u) % 100u);
        o[20] = 7u;
    }

    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = 99u;
        voice[130u + i] = 50u;
    }
    voice[DX7_VCED_ALGORITHM_OFFSET] = algorithm;
    voice[DX7_VCED_FEEDBACK_OFFSET] = (uint8_t)(variant & 7u);
    voice[136] = 1u;
    voice[137] = (uint8_t)(40u + variant % 45u);
    voice[138] = (uint8_t)(variant % 15u);
    voice[139] = (uint8_t)(variant % 25u);
    voice[140] = (uint8_t)(variant % 15u);
    voice[141] = 1u;
    voice[142] = (uint8_t)(variant % 6u);
    voice[143] = (uint8_t)(variant % 8u);
    voice[DX7_VCED_TRANSPOSE_OFFSET] = 24u;

    char name[11];
    snprintf(name, sizeof(name), "RT%02uFM6XYZ", variant % 100u);
    memcpy(voice + DX7_VCED_NAME_OFFSET, name, DX7_VCED_NAME_SIZE);
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voices[0], 2u, 3u);
    make_voice(s_voices[1], 8u, 12u);
    make_voice(s_voices[2], 14u, 21u);
    make_voice(s_voices[3], 20u, 30u);
}

static bool submit(unsigned index, uint8_t voice_count, uint32_t *generation)
{
    return fm6_program_queue_submit(&s_queue,
                                    s_voices[index & 3u],
                                    voice_count,
                                    (uint8_t)(36u + index * 2u),
                                    (uint8_t)(1u + (index % 3u)),
                                    (uint8_t)(96u + index * 5u),
                                    generation);
}

static void test_invalid_init_rejected(void)
{
    fm6_program_queue_init(&s_queue);
    const bool null_runtime = !fm6_audio_runtime_init(NULL, &s_queue, 12u, 0u);
    const bool null_queue = !fm6_audio_runtime_init(&s_runtime, NULL, 12u, 0u);
    const bool zero_voices = !fm6_audio_runtime_init(&s_runtime, &s_queue, 0u, 0u);
    const bool too_many = !fm6_audio_runtime_init(
        &s_runtime, &s_queue, FM6_BENCH_MAX_VOICES + 1u, 0u);
    const bool bad_algorithm = !fm6_audio_runtime_init(&s_runtime, &s_queue, 12u, 32u);
    check("invalid_init_rejected",
          null_runtime && null_queue && zero_voices && too_many && bad_algorithm);
}

static void test_commit_happens_before_render(void)
{
    fm6_program_queue_init(&s_queue);
    const bool initialized = fm6_audio_runtime_init(&s_runtime, &s_queue, 12u, 31u);
    uint32_t submitted_generation = 0u;
    const bool submitted = submit(0u, 12u, &submitted_generation);
    const bool still_old = fm6_audio_runtime_algorithm(&s_runtime) == 31u &&
                           fm6_port_patch_algorithm() == 31u &&
                           fm6_program_queue_pending(&s_queue) == 1u;

    int32_t out[FM6_BENCH_BLOCK_FRAMES];
    bool committed = false;
    uint32_t rendered_generation = 0u;
    const bool rendered = fm6_audio_runtime_render_block(
        &s_runtime, out, &committed, &rendered_generation);

    const bool switched = rendered && committed &&
                          submitted_generation == 1u &&
                          rendered_generation == submitted_generation &&
                          fm6_audio_runtime_active_generation(&s_runtime) == submitted_generation &&
                          fm6_audio_runtime_algorithm(&s_runtime) == 3u &&
                          fm6_port_patch_algorithm() == 3u &&
                          fm6_audio_runtime_rendered_blocks(&s_runtime) == 1u &&
                          fm6_audio_runtime_program_commits(&s_runtime) == 1u &&
                          fm6_program_queue_pending(&s_queue) == 0u;
    check("commit_before_render", initialized && submitted && still_old && switched);
}

static void test_one_program_per_block(void)
{
    static const uint8_t counts[4] = {12u, 10u, 8u, 6u};
    static const unsigned algorithms[4] = {3u, 12u, 21u, 30u};
    fm6_program_queue_init(&s_queue);
    const bool initialized = fm6_audio_runtime_init(&s_runtime, &s_queue, 12u, 0u);

    bool submitted = true;
    for (unsigned i = 0; i < 4u; ++i) {
        submitted &= submit(i, counts[i], NULL);
    }

    bool structural = submitted && fm6_program_queue_pending(&s_queue) == 4u;
    for (unsigned block = 0; block < 4u && structural; ++block) {
        int32_t out[FM6_BENCH_BLOCK_FRAMES];
        bool committed = false;
        uint32_t generation = 0u;
        structural &= fm6_audio_runtime_render_block(
            &s_runtime, out, &committed, &generation);
        structural &= committed && generation == block + 1u;
        structural &= fm6_program_queue_pending(&s_queue) == 3u - block;
        structural &= fm6_audio_runtime_program_commits(&s_runtime) == block + 1u;
        structural &= fm6_audio_runtime_rendered_blocks(&s_runtime) == block + 1u;
        structural &= fm6_audio_runtime_voice_count(&s_runtime) == counts[block];
        structural &= fm6_audio_runtime_algorithm(&s_runtime) == algorithms[block];
    }

    check("one_program_per_block",
          initialized && structural && fm6_program_queue_pending(&s_queue) == 0u);
}

static void test_empty_queue_render_keeps_program(void)
{
    fm6_program_queue_init(&s_queue);
    const bool initialized = fm6_audio_runtime_init(&s_runtime, &s_queue, 10u, 7u);
    int32_t out[FM6_BENCH_BLOCK_FRAMES];
    bool committed = true;
    uint32_t generation = 99u;
    const bool rendered = fm6_audio_runtime_render_block(
        &s_runtime, out, &committed, &generation);
    check("empty_queue_render_keeps_program",
          initialized && rendered && !committed && generation == 0u &&
          fm6_audio_runtime_voice_count(&s_runtime) == 10u &&
          fm6_audio_runtime_algorithm(&s_runtime) == 7u &&
          fm6_audio_runtime_active_generation(&s_runtime) == 0u &&
          fm6_audio_runtime_program_commits(&s_runtime) == 0u &&
          fm6_audio_runtime_rendered_blocks(&s_runtime) == 1u);
}

static void test_applied_voice_count_tracks_command(void)
{
    fm6_program_queue_init(&s_queue);
    const bool initialized = fm6_audio_runtime_init(&s_runtime, &s_queue, 12u, 0u);
    const bool submitted_a = submit(1u, 8u, NULL);
    const bool submitted_b = submit(2u, 6u, NULL);

    int32_t out[FM6_BENCH_BLOCK_FRAMES];
    const bool render_a = fm6_audio_runtime_render_block(&s_runtime, out, NULL, NULL);
    const bool tracks_a = fm6_audio_runtime_voice_count(&s_runtime) == 8u &&
                          fm6_audio_runtime_algorithm(&s_runtime) == 12u;
    const bool render_b = fm6_audio_runtime_render_block(&s_runtime, out, NULL, NULL);
    const bool tracks_b = fm6_audio_runtime_voice_count(&s_runtime) == 6u &&
                          fm6_audio_runtime_algorithm(&s_runtime) == 21u;
    check("applied_voice_count_tracks_command",
          initialized && submitted_a && submitted_b &&
          render_a && tracks_a && render_b && tracks_b);
}

static uint32_t mix_u32(uint32_t h, uint32_t value)
{
    h ^= value;
    h *= 16777619u;
    return h;
}

static uint32_t hash_block(uint32_t h, const int32_t out[FM6_BENCH_BLOCK_FRAMES])
{
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        h = mix_u32(h, (uint32_t)out[i]);
    }
    return h;
}

typedef struct {
    bool pass;
    uint32_t signature;
    unsigned active_blocks;
    uint32_t final_generation;
} sequence_result_t;

static sequence_result_t run_sequence(void)
{
    static const uint8_t counts[4] = {12u, 10u, 8u, 6u};
    sequence_result_t result = {0};
    fm6_program_queue_init(&s_queue);
    if (!fm6_audio_runtime_init(&s_runtime, &s_queue, 12u, 0u)) {
        return result;
    }

    for (unsigned i = 0; i < 4u; ++i) {
        if (!submit(i, counts[i], NULL)) {
            return result;
        }
    }

    uint32_t signature = 2166136261u;
    unsigned commits_seen = 0u;
    unsigned active_blocks = 0u;
    for (unsigned block = 0; block < RUNTIME_SEQUENCE_BLOCKS; ++block) {
        int32_t out[FM6_BENCH_BLOCK_FRAMES];
        bool committed = false;
        uint32_t generation = 0u;
        if (!fm6_audio_runtime_render_block(
                &s_runtime, out, &committed, &generation)) {
            return result;
        }

        unsigned nonzero = 0u;
        for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
            nonzero += out[i] != 0;
        }
        active_blocks += nonzero != 0u;
        commits_seen += committed ? 1u : 0u;

        signature = hash_block(signature, out);
        signature = mix_u32(signature, committed ? 1u : 0u);
        signature = mix_u32(signature, generation);
        signature = mix_u32(signature, fm6_audio_runtime_active_generation(&s_runtime));
        signature = mix_u32(signature, fm6_audio_runtime_voice_count(&s_runtime));
        signature = mix_u32(signature, fm6_audio_runtime_algorithm(&s_runtime));
        signature = mix_u32(signature, fm6_program_queue_pending(&s_queue));
    }

    result.signature = signature;
    result.active_blocks = active_blocks;
    result.final_generation = fm6_audio_runtime_active_generation(&s_runtime);
    result.pass = commits_seen == 4u &&
                  fm6_audio_runtime_program_commits(&s_runtime) == 4u &&
                  fm6_audio_runtime_rendered_blocks(&s_runtime) == RUNTIME_SEQUENCE_BLOCKS &&
                  result.final_generation == 4u &&
                  fm6_program_queue_pending(&s_queue) == 0u &&
                  result.active_blocks > 0u &&
                  result.signature != 0u;
    return result;
}

static void test_deterministic_runtime_sequence(void)
{
    const sequence_result_t a = run_sequence();
    const sequence_result_t b = run_sequence();
    const bool pass = a.pass && b.pass &&
                      a.signature == b.signature &&
                      a.active_blocks == b.active_blocks &&
                      a.final_generation == b.final_generation;
    check("deterministic_runtime_sequence", pass);
    printf("FM6_AUDIO_RUNTIME_SEQUENCE: %s blocks=%u commits=4 passes=2 final_generation=%" PRIu32
           " active_blocks=%u signature=0x%08" PRIx32 "\n",
           pass ? "PASS" : "FAIL",
           (unsigned)RUNTIME_SEQUENCE_BLOCKS,
           a.final_generation,
           a.active_blocks,
           a.signature);
}

unsigned fm6_audio_runtime_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    build_fixtures();

    test_invalid_init_rejected();
    test_commit_happens_before_render();
    test_one_program_per_block();
    test_empty_queue_render_keeps_program();
    test_applied_voice_count_tracks_command();
    test_deterministic_runtime_sequence();

    printf("QEMU_FM6_AUDIO_RUNTIME: %s tests=%u failures=%u block_frames=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures,
           (unsigned)FM6_BENCH_BLOCK_FRAMES);
    return s_failures;
}
