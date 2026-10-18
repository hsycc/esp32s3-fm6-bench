#include "fm6_control_runtime_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_control_runtime.h"
#include "fm6_port.h"

#define CONTROL_DETERMINISM_BLOCKS 256u

typedef struct {
    uint32_t checksum;
    uint64_t energy;
    uint32_t commands;
    uint32_t programs;
    uint32_t steals;
    uint32_t retriggers;
    uint32_t unmatched;
    uint32_t ignored;
    uint32_t active_program_sequence;
    uint32_t blocks;
    unsigned notes_down;
    unsigned algorithm;
} control_sequence_result_t;

static unsigned s_tests;
static unsigned s_failures;
static fm6_control_queue_t s_queue;
static fm6_control_runtime_t s_runtime;
static uint8_t s_voice_a[DX7_VCED_SIZE];
static uint8_t s_voice_b[DX7_VCED_SIZE];

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("FM6_CONTROL_RUNTIME_TEST name=%s status=%s\n",
           name, pass ? "PASS" : "FAIL");
}

static void make_voice(uint8_t voice[DX7_VCED_SIZE],
                       unsigned variant,
                       uint8_t algorithm,
                       const char *name)
{
    memset(voice, 0, DX7_VCED_SIZE);
    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = voice + op * 21u;
        o[0] = 92u;
        o[1] = 70u;
        o[2] = 58u;
        o[3] = 76u;
        o[4] = 99u;
        o[5] = 78u;
        o[6] = 58u;
        o[7] = 0u;
        o[8] = 50u;
        o[9] = (uint8_t)(10u + ((variant + op) % 20u));
        o[10] = (uint8_t)(10u + ((variant + op * 3u) % 20u));
        o[11] = 1u;
        o[12] = 1u;
        o[13] = (uint8_t)((variant + op) & 7u);
        o[14] = 0u;
        o[15] = (uint8_t)((variant + op * 2u) & 7u);
        o[16] = 99u;
        o[17] = 0u;
        o[18] = (uint8_t)(1u + ((variant + op) % 8u));
        o[19] = (uint8_t)((variant * 9u + op * 13u) % 100u);
        o[20] = 7u;
    }

    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = 92u;
        voice[130u + i] = 50u;
    }
    voice[DX7_VCED_ALGORITHM_OFFSET] = algorithm;
    voice[DX7_VCED_FEEDBACK_OFFSET] = (uint8_t)(variant & 7u);
    voice[136] = 1u;
    voice[137] = (uint8_t)(45u + variant % 30u);
    voice[138] = 8u;
    voice[139] = 12u;
    voice[140] = 6u;
    voice[141] = 1u;
    voice[142] = 0u;
    voice[143] = 2u;
    voice[DX7_VCED_TRANSPOSE_OFFSET] = 24u;

    uint8_t patch_name[DX7_VCED_NAME_SIZE];
    memset(patch_name, ' ', sizeof(patch_name));
    const size_t name_len = strlen(name);
    const size_t copy_len = name_len < sizeof(patch_name) ? name_len : sizeof(patch_name);
    memcpy(patch_name, name, copy_len);
    memcpy(voice + DX7_VCED_NAME_OFFSET, patch_name, sizeof(patch_name));
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voice_a, 5u, 5u, "CTRL-A");
    make_voice(s_voice_b, 17u, 21u, "CTRL-B");
}

static bool start_runtime(void)
{
    fm6_control_queue_init(&s_queue);
    return fm6_control_runtime_init(&s_runtime, &s_queue);
}

static bool render_one(unsigned *applied)
{
    int32_t out[FM6_BENCH_BLOCK_FRAMES];
    return fm6_control_runtime_render_block(&s_runtime, out, applied);
}

static void render_until_energy(unsigned limit)
{
    for (unsigned i = 0; i < limit && fm6_port_abs_energy() == 0u; ++i) {
        (void)render_one(NULL);
    }
}

static void test_preprogram_note_is_ignored_safely(void)
{
    const bool started = start_runtime();
    const bool queued = fm6_control_queue_submit_note_on(&s_queue, 60u, 100u, NULL);
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    check("preprogram_note_ignored",
          started && queued && rendered && applied == 1u &&
          !fm6_control_runtime_program_active(&s_runtime) &&
          fm6_control_runtime_ignored_without_program(&s_runtime) == 1u &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u &&
          fm6_port_abs_energy() == 0u);
}

static void test_program_only_starts_silent(void)
{
    const bool started = start_runtime();
    uint32_t program_sequence = 0u;
    const bool queued = fm6_control_queue_submit_program(
        &s_queue, s_voice_a, 12u, &program_sequence);
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    check("program_only_starts_silent",
          started && queued && rendered && applied == 1u &&
          program_sequence == 1u &&
          fm6_control_runtime_program_active(&s_runtime) &&
          fm6_control_runtime_voice_capacity(&s_runtime) == 12u &&
          fm6_control_runtime_active_program_sequence(&s_runtime) == program_sequence &&
          fm6_port_patch_algorithm() == 5u &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u &&
          fm6_port_last_sounded() == 0u &&
          fm6_port_abs_energy() == 0u);
}

static void test_program_then_note_same_boundary(void)
{
    const bool started = start_runtime();
    uint32_t program_sequence = 0u;
    uint32_t note_sequence = 0u;
    const bool queued = fm6_control_queue_submit_program(
                            &s_queue, s_voice_a, 12u, &program_sequence) &&
                        fm6_control_queue_submit_note_on(
                            &s_queue, 60u, 110u, &note_sequence);
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    render_until_energy(8u);
    check("program_then_note_same_boundary",
          started && queued && rendered && applied == 2u &&
          program_sequence == 1u && note_sequence == 2u &&
          fm6_control_runtime_active_program_sequence(&s_runtime) == 1u &&
          fm6_control_runtime_notes_down(&s_runtime) == 1u &&
          fm6_control_runtime_note_is_down(&s_runtime, 60u) &&
          fm6_control_runtime_voice_for_note(&s_runtime, 60u) == 0 &&
          fm6_port_patch_algorithm() == 5u &&
          fm6_port_abs_energy() != 0u);
}

static void test_note_then_program_resets_old_state(void)
{
    const bool started = start_runtime();
    bool ok = fm6_control_queue_submit_program(&s_queue, s_voice_a, 12u, NULL) &&
              fm6_control_queue_submit_note_on(&s_queue, 60u, 110u, NULL) &&
              render_one(NULL);

    ok &= fm6_control_queue_submit_note_on(&s_queue, 62u, 100u, NULL);
    uint32_t program_b_sequence = 0u;
    ok &= fm6_control_queue_submit_program(
        &s_queue, s_voice_b, 12u, &program_b_sequence);
    unsigned applied = 0u;
    ok &= render_one(&applied);

    check("note_then_program_resets_old_state",
          started && ok && applied == 2u &&
          fm6_control_runtime_active_program_sequence(&s_runtime) == program_b_sequence &&
          fm6_port_patch_algorithm() == 21u &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u &&
          !fm6_control_runtime_note_is_down(&s_runtime, 60u) &&
          !fm6_control_runtime_note_is_down(&s_runtime, 62u) &&
          fm6_port_last_sounded() == 0u &&
          fm6_port_abs_energy() == 0u);
}

static void test_program_note_program_note_total_order(void)
{
    const bool started = start_runtime();
    uint32_t seq_a = 0u;
    uint32_t seq_b = 0u;
    const bool queued =
        fm6_control_queue_submit_program(&s_queue, s_voice_a, 12u, &seq_a) &&
        fm6_control_queue_submit_note_on(&s_queue, 60u, 100u, NULL) &&
        fm6_control_queue_submit_program(&s_queue, s_voice_b, 12u, &seq_b) &&
        fm6_control_queue_submit_note_on(&s_queue, 72u, 120u, NULL);
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    render_until_energy(8u);

    check("program_note_program_note_total_order",
          started && queued && rendered && applied == 4u &&
          seq_a == 1u && seq_b == 3u &&
          fm6_control_runtime_program_commits(&s_runtime) == 2u &&
          fm6_control_runtime_active_program_sequence(&s_runtime) == seq_b &&
          fm6_port_patch_algorithm() == 21u &&
          fm6_control_runtime_notes_down(&s_runtime) == 1u &&
          !fm6_control_runtime_note_is_down(&s_runtime, 60u) &&
          fm6_control_runtime_note_is_down(&s_runtime, 72u) &&
          fm6_port_abs_energy() != 0u);
}

static void test_multiple_programs_latest_wins(void)
{
    const bool started = start_runtime();
    uint32_t seq_b = 0u;
    const bool queued =
        fm6_control_queue_submit_program(&s_queue, s_voice_a, 6u, NULL) &&
        fm6_control_queue_submit_program(&s_queue, s_voice_b, 10u, &seq_b);
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    check("multiple_programs_latest_wins",
          started && queued && rendered && applied == 2u &&
          fm6_control_runtime_program_commits(&s_runtime) == 2u &&
          fm6_control_runtime_active_program_sequence(&s_runtime) == seq_b &&
          fm6_control_runtime_voice_capacity(&s_runtime) == 10u &&
          fm6_port_patch_algorithm() == 21u &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u);
}

static void test_thirteenth_note_steals_oldest(void)
{
    const bool started = start_runtime();
    bool ok = fm6_control_queue_submit_program(&s_queue, s_voice_a, 12u, NULL);
    for (unsigned i = 0; i < 12u; ++i) {
        ok &= fm6_control_queue_submit_note_on(
            &s_queue, (uint8_t)(36u + i), (uint8_t)(90u + i), NULL);
    }
    unsigned applied = 0u;
    ok &= render_one(&applied);
    const int oldest_voice = fm6_control_runtime_voice_for_note(&s_runtime, 36u);
    ok &= fm6_control_queue_submit_note_on(&s_queue, 72u, 127u, NULL);
    ok &= render_one(NULL);

    check("thirteenth_note_steals_oldest",
          started && ok && applied == 13u && oldest_voice == 0 &&
          fm6_control_runtime_notes_down(&s_runtime) == 12u &&
          !fm6_control_runtime_note_is_down(&s_runtime, 36u) &&
          fm6_control_runtime_note_is_down(&s_runtime, 72u) &&
          fm6_control_runtime_voice_for_note(&s_runtime, 72u) == oldest_voice &&
          fm6_control_runtime_voice_steals(&s_runtime) == 1u);
}

static void test_queue_backpressure(void)
{
    fm6_control_queue_init(&s_queue);
    bool accepted = true;
    for (unsigned i = 0; i < FM6_CONTROL_QUEUE_CAPACITY; ++i) {
        accepted &= fm6_control_queue_submit_note_on(
            &s_queue, (uint8_t)(24u + i), 100u, NULL);
    }
    const bool rejected = !fm6_control_queue_submit_note_on(&s_queue, 100u, 100u, NULL);
    check("queue_backpressure",
          accepted && rejected &&
          fm6_control_queue_pending(&s_queue) == FM6_CONTROL_QUEUE_CAPACITY &&
          fm6_control_queue_rejected(&s_queue) == 1u);
}

static void store_relaxed(uint32_t *value, uint32_t next)
{
    __atomic_store_n(value, next, __ATOMIC_RELAXED);
}

static uint32_t load_relaxed(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static void test_counter_and_sequence_wrap(void)
{
    const bool started = start_runtime();
    const uint32_t start = UINT32_MAX - 1u;
    store_relaxed(&s_queue.write_count, start);
    store_relaxed(&s_queue.read_count, start);
    store_relaxed(&s_queue.next_sequence, UINT32_MAX);

    uint32_t program_sequence = 0u;
    uint32_t note_sequence = 0u;
    uint32_t off_sequence = 0u;
    const bool queued =
        fm6_control_queue_submit_program(&s_queue, s_voice_a, 12u, &program_sequence) &&
        fm6_control_queue_submit_note_on(&s_queue, 60u, 100u, &note_sequence) &&
        fm6_control_queue_submit_note_off(&s_queue, 60u, &off_sequence);
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);

    check("counter_and_sequence_wrap",
          started && queued && rendered && applied == 3u &&
          program_sequence == UINT32_MAX && note_sequence == 1u && off_sequence == 2u &&
          load_relaxed(&s_queue.write_count) == 1u &&
          load_relaxed(&s_queue.read_count) == 1u &&
          fm6_control_queue_pending(&s_queue) == 0u &&
          fm6_control_runtime_active_program_sequence(&s_runtime) == UINT32_MAX &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u);
}

static control_sequence_result_t run_deterministic_sequence(void)
{
    control_sequence_result_t result = {0};
    if (!start_runtime()) {
        return result;
    }

    int32_t out[FM6_BENCH_BLOCK_FRAMES];
    for (unsigned block = 0; block < CONTROL_DETERMINISM_BLOCKS; ++block) {
        if ((block % 64u) == 0u) {
            const bool use_b = ((block / 64u) & 1u) != 0u;
            (void)fm6_control_queue_submit_program(
                &s_queue, use_b ? s_voice_b : s_voice_a, 12u, NULL);
        }
        if ((block % 7u) == 1u) {
            (void)fm6_control_queue_submit_note_on(
                &s_queue,
                (uint8_t)(36u + ((block / 7u) % 30u)),
                (uint8_t)(80u + (block % 40u)),
                NULL);
        }
        if ((block % 11u) == 4u) {
            (void)fm6_control_queue_submit_note_off(
                &s_queue,
                (uint8_t)(36u + ((block / 11u) % 30u)),
                NULL);
        }
        if ((block % 53u) == 31u) {
            (void)fm6_control_queue_submit_all_notes_off(&s_queue, NULL);
        }
        (void)fm6_control_runtime_render_block(&s_runtime, out, NULL);
    }

    result.checksum = fm6_port_checksum();
    result.energy = fm6_port_abs_energy();
    result.commands = fm6_control_runtime_commands_applied(&s_runtime);
    result.programs = fm6_control_runtime_program_commits(&s_runtime);
    result.steals = fm6_control_runtime_voice_steals(&s_runtime);
    result.retriggers = fm6_control_runtime_retriggers(&s_runtime);
    result.unmatched = fm6_control_runtime_unmatched_note_offs(&s_runtime);
    result.ignored = fm6_control_runtime_ignored_without_program(&s_runtime);
    result.active_program_sequence = fm6_control_runtime_active_program_sequence(&s_runtime);
    result.blocks = fm6_control_runtime_rendered_blocks(&s_runtime);
    result.notes_down = fm6_control_runtime_notes_down(&s_runtime);
    result.algorithm = fm6_port_patch_algorithm();
    return result;
}

static bool same_sequence(const control_sequence_result_t *a,
                          const control_sequence_result_t *b)
{
    return a->checksum == b->checksum &&
           a->energy == b->energy &&
           a->commands == b->commands &&
           a->programs == b->programs &&
           a->steals == b->steals &&
           a->retriggers == b->retriggers &&
           a->unmatched == b->unmatched &&
           a->ignored == b->ignored &&
           a->active_program_sequence == b->active_program_sequence &&
           a->blocks == b->blocks &&
           a->notes_down == b->notes_down &&
           a->algorithm == b->algorithm;
}

static void test_deterministic_mixed_control_sequence(void)
{
    const control_sequence_result_t a = run_deterministic_sequence();
    const control_sequence_result_t b = run_deterministic_sequence();
    const bool pass = a.blocks == CONTROL_DETERMINISM_BLOCKS &&
                      a.programs == 4u &&
                      a.commands != 0u &&
                      a.checksum != 0u &&
                      a.energy != 0u &&
                      same_sequence(&a, &b);
    check("deterministic_mixed_control_sequence", pass);
    printf("FM6_CONTROL_RUNTIME_SEQUENCE: %s blocks=%u passes=2 commands=%" PRIu32
           " programs=%" PRIu32 " steals=%" PRIu32 " retriggers=%" PRIu32
           " unmatched=%" PRIu32 " active_program=%" PRIu32
           " notes_down=%u algorithm=%u checksum=0x%08" PRIx32 " energy=%" PRIu64 "\n",
           pass ? "PASS" : "FAIL",
           CONTROL_DETERMINISM_BLOCKS,
           a.commands,
           a.programs,
           a.steals,
           a.retriggers,
           a.unmatched,
           a.active_program_sequence,
           a.notes_down,
           a.algorithm,
           a.checksum,
           a.energy);
}

unsigned fm6_control_runtime_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    build_fixtures();

    test_preprogram_note_is_ignored_safely();
    test_program_only_starts_silent();
    test_program_then_note_same_boundary();
    test_note_then_program_resets_old_state();
    test_program_note_program_note_total_order();
    test_multiple_programs_latest_wins();
    test_thirteenth_note_steals_oldest();
    test_queue_backpressure();
    test_counter_and_sequence_wrap();
    test_deterministic_mixed_control_sequence();

    printf("QEMU_FM6_CONTROL_RUNTIME: %s tests=%u failures=%u queue_capacity=%u voices=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures,
           (unsigned)FM6_CONTROL_QUEUE_CAPACITY,
           (unsigned)FM6_BENCH_MAX_VOICES);
    return s_failures;
}
