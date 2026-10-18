#include "fm6_note_runtime_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_note_runtime.h"
#include "fm6_port.h"

#define NOTE_RUNTIME_RELEASE_LIMIT 2048u
#define NOTE_RUNTIME_DETERMINISM_BLOCKS 256u

typedef struct {
    uint32_t checksum;
    uint64_t energy;
    uint32_t steals;
    uint32_t retriggers;
    uint32_t unmatched_offs;
    uint32_t events;
    uint32_t blocks;
    unsigned notes_down;
    unsigned final_sounded;
} sequence_result_t;

static unsigned s_tests;
static unsigned s_failures;
static fm6_note_queue_t s_queue;
static fm6_note_runtime_t s_runtime;
static uint8_t s_voice[DX7_VCED_SIZE];

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("FM6_NOTE_RUNTIME_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
}

static void make_voice(void)
{
    memset(s_voice, 0, sizeof(s_voice));
    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = s_voice + op * 21u;
        o[0] = 90u;
        o[1] = 65u;
        o[2] = 55u;
        o[3] = 75u;
        o[4] = 99u;
        o[5] = 75u;
        o[6] = 55u;
        o[7] = 0u;
        o[8] = 50u;
        o[9] = 20u;
        o[10] = 20u;
        o[11] = 1u;
        o[12] = 1u;
        o[13] = 3u;
        o[14] = 0u;
        o[15] = 4u;
        o[16] = 99u;
        o[17] = 0u;
        o[18] = (uint8_t)(1u + op);
        o[19] = (uint8_t)(op * 11u);
        o[20] = 7u;
    }

    for (unsigned i = 0; i < 4u; ++i) {
        s_voice[126u + i] = 90u;
        s_voice[130u + i] = 50u;
    }
    s_voice[DX7_VCED_ALGORITHM_OFFSET] = 5u;
    s_voice[DX7_VCED_FEEDBACK_OFFSET] = 6u;
    s_voice[136] = 1u;
    s_voice[137] = 55u;
    s_voice[138] = 10u;
    s_voice[139] = 18u;
    s_voice[140] = 8u;
    s_voice[141] = 1u;
    s_voice[142] = 0u;
    s_voice[143] = 2u;
    s_voice[DX7_VCED_TRANSPOSE_OFFSET] = 24u;
    memcpy(s_voice + DX7_VCED_NAME_OFFSET, "NOTE RT V1", DX7_VCED_NAME_SIZE);
    dx7_vced_sanitize(s_voice);
}

static bool start_runtime(unsigned voices)
{
    fm6_note_queue_init(&s_queue);
    return fm6_port_load_program(s_voice, voices) &&
           fm6_note_runtime_init(&s_runtime, &s_queue, voices);
}

static bool render_one(unsigned *applied)
{
    int32_t out[FM6_BENCH_BLOCK_FRAMES];
    return fm6_note_runtime_render_block(&s_runtime, out, applied);
}

static void test_silent_program_load(void)
{
    const bool started = start_runtime(12u);
    unsigned applied = 99u;
    const bool rendered = render_one(&applied);
    check("silent_program_load",
          started && rendered && applied == 0u &&
          fm6_port_voice_capacity() == 12u &&
          fm6_port_patch_algorithm() == 5u &&
          fm6_port_last_sounded() == 0u &&
          fm6_port_abs_energy() == 0u &&
          fm6_note_runtime_notes_down(&s_runtime) == 0u);
}

static void test_note_on_applies_at_block_boundary(void)
{
    const bool started = start_runtime(12u);
    uint32_t sequence = 0u;
    const bool queued = fm6_note_queue_submit_note_on(&s_queue, 60u, 110u, &sequence);
    const bool unchanged_before = fm6_note_runtime_notes_down(&s_runtime) == 0u &&
                                  fm6_note_queue_pending(&s_queue) == 1u;
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);

    for (unsigned i = 0; i < 8u && fm6_port_abs_energy() == 0u; ++i) {
        (void)render_one(NULL);
    }

    check("note_on_block_boundary",
          started && queued && sequence == 1u && unchanged_before && rendered &&
          applied == 1u && fm6_note_queue_pending(&s_queue) == 0u &&
          fm6_note_runtime_notes_down(&s_runtime) == 1u &&
          fm6_note_runtime_note_is_down(&s_runtime, 60u) &&
          fm6_note_runtime_voice_for_note(&s_runtime, 60u) == 0 &&
          fm6_port_abs_energy() != 0u);
}

static void test_velocity_zero_maps_to_note_off(void)
{
    const bool started = start_runtime(12u);
    bool queued = fm6_note_queue_submit_note_on(&s_queue, 64u, 100u, NULL);
    queued &= render_one(NULL);
    queued &= fm6_note_queue_submit_note_on(&s_queue, 64u, 0u, NULL);
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    check("velocity_zero_is_note_off",
          started && queued && rendered && applied == 1u &&
          fm6_note_runtime_notes_down(&s_runtime) == 0u &&
          !fm6_note_runtime_note_is_down(&s_runtime, 64u));
}

static void test_same_note_retriggers_same_voice(void)
{
    const bool started = start_runtime(12u);
    bool ok = fm6_note_queue_submit_note_on(&s_queue, 67u, 90u, NULL) && render_one(NULL);
    const int first_voice = fm6_note_runtime_voice_for_note(&s_runtime, 67u);
    ok &= fm6_note_queue_submit_note_on(&s_queue, 67u, 120u, NULL) && render_one(NULL);
    const int second_voice = fm6_note_runtime_voice_for_note(&s_runtime, 67u);
    check("same_note_retrigger_same_voice",
          started && ok && first_voice >= 0 && first_voice == second_voice &&
          fm6_note_runtime_notes_down(&s_runtime) == 1u &&
          fm6_note_runtime_retriggers(&s_runtime) == 1u &&
          fm6_note_runtime_voice_steals(&s_runtime) == 0u);
}

static void test_polyphony_steals_oldest_down_voice(void)
{
    const bool started = start_runtime(12u);
    bool queued = true;
    for (unsigned i = 0; i < 12u; ++i) {
        queued &= fm6_note_queue_submit_note_on(
            &s_queue, (uint8_t)(36u + i), (uint8_t)(80u + i), NULL);
    }
    unsigned applied = 0u;
    bool rendered = render_one(&applied);
    const int oldest_voice = fm6_note_runtime_voice_for_note(&s_runtime, 36u);

    queued &= fm6_note_queue_submit_note_on(&s_queue, 72u, 127u, NULL);
    rendered &= render_one(NULL);

    check("polyphony_steals_oldest_down_voice",
          started && queued && rendered && applied == 12u &&
          oldest_voice == 0 &&
          fm6_note_runtime_notes_down(&s_runtime) == 12u &&
          !fm6_note_runtime_note_is_down(&s_runtime, 36u) &&
          fm6_note_runtime_note_is_down(&s_runtime, 72u) &&
          fm6_note_runtime_voice_for_note(&s_runtime, 72u) == oldest_voice &&
          fm6_note_runtime_voice_steals(&s_runtime) == 1u);
}

static void test_released_voice_reused_before_steal(void)
{
    const bool started = start_runtime(12u);
    bool ok = true;
    for (unsigned i = 0; i < 12u; ++i) {
        ok &= fm6_note_queue_submit_note_on(&s_queue, (uint8_t)(48u + i), 100u, NULL);
    }
    ok &= render_one(NULL);
    const int released_voice = fm6_note_runtime_voice_for_note(&s_runtime, 53u);
    ok &= fm6_note_queue_submit_note_off(&s_queue, 53u, NULL) && render_one(NULL);
    const uint32_t steals_before = fm6_note_runtime_voice_steals(&s_runtime);
    ok &= fm6_note_queue_submit_note_on(&s_queue, 84u, 120u, NULL) && render_one(NULL);

    check("released_voice_reused_before_steal",
          started && ok && released_voice >= 0 &&
          fm6_note_runtime_voice_for_note(&s_runtime, 84u) == released_voice &&
          fm6_note_runtime_notes_down(&s_runtime) == 12u &&
          fm6_note_runtime_voice_steals(&s_runtime) == steals_before);
}

static void test_all_notes_off_and_release_drain(void)
{
    const bool started = start_runtime(12u);
    bool ok = true;
    for (unsigned i = 0; i < 6u; ++i) {
        ok &= fm6_note_queue_submit_note_on(&s_queue, (uint8_t)(60u + i), 110u, NULL);
    }
    ok &= render_one(NULL);
    ok &= fm6_note_queue_submit_all_notes_off(&s_queue, NULL);
    unsigned applied = 0u;
    ok &= render_one(&applied);

    unsigned drain = 0u;
    while (fm6_port_last_sounded() != 0u && drain < NOTE_RUNTIME_RELEASE_LIMIT) {
        ok &= render_one(NULL);
        ++drain;
    }

    check("all_notes_off_release_drain",
          started && ok && applied == 1u &&
          fm6_note_runtime_notes_down(&s_runtime) == 0u &&
          fm6_port_last_sounded() == 0u &&
          drain < NOTE_RUNTIME_RELEASE_LIMIT);
}

static void test_program_reset_discards_stale_events(void)
{
    const bool started = start_runtime(12u);
    const bool queued = fm6_note_queue_submit_note_on(&s_queue, 91u, 127u, NULL) &&
                        fm6_note_queue_pending(&s_queue) == 1u;
    const bool reloaded = fm6_port_load_program(s_voice, 12u);
    const bool reset = fm6_note_runtime_reset(&s_runtime, 12u);
    unsigned applied = 99u;
    const bool rendered = render_one(&applied);
    check("program_reset_discards_stale_events",
          started && queued && reloaded && reset && rendered &&
          applied == 0u && fm6_note_queue_pending(&s_queue) == 0u &&
          fm6_note_runtime_notes_down(&s_runtime) == 0u &&
          fm6_port_last_sounded() == 0u);
}

static void test_queue_backpressure(void)
{
    fm6_note_queue_init(&s_queue);
    bool accepted = true;
    for (unsigned i = 0; i < FM6_NOTE_QUEUE_CAPACITY; ++i) {
        accepted &= fm6_note_queue_submit_note_on(
            &s_queue, (uint8_t)(24u + (i % 72u)), 100u, NULL);
    }
    const bool rejected = !fm6_note_queue_submit_note_on(&s_queue, 100u, 100u, NULL);
    const bool full = fm6_note_queue_pending(&s_queue) == FM6_NOTE_QUEUE_CAPACITY &&
                      fm6_note_queue_rejected(&s_queue) == 1u;
    fm6_note_queue_discard_pending(&s_queue);
    check("queue_backpressure_and_flush",
          accepted && rejected && full && fm6_note_queue_pending(&s_queue) == 0u);
}

static sequence_result_t run_deterministic_sequence(void)
{
    sequence_result_t result = {0};
    if (!start_runtime(12u)) {
        return result;
    }

    int32_t out[FM6_BENCH_BLOCK_FRAMES];
    for (unsigned block = 0; block < NOTE_RUNTIME_DETERMINISM_BLOCKS; ++block) {
        if ((block % 8u) == 0u) {
            const uint8_t note = (uint8_t)(36u + ((block / 8u) % 24u));
            (void)fm6_note_queue_submit_note_on(&s_queue, note, (uint8_t)(80u + (block % 40u)), NULL);
        }
        if ((block % 11u) == 5u) {
            const uint8_t note = (uint8_t)(36u + ((block / 11u) % 24u));
            (void)fm6_note_queue_submit_note_off(&s_queue, note, NULL);
        }
        if ((block % 67u) == 33u) {
            (void)fm6_note_queue_submit_all_notes_off(&s_queue, NULL);
        }
        (void)fm6_note_runtime_render_block(&s_runtime, out, NULL);
    }

    result.checksum = fm6_port_checksum();
    result.energy = fm6_port_abs_energy();
    result.steals = fm6_note_runtime_voice_steals(&s_runtime);
    result.retriggers = fm6_note_runtime_retriggers(&s_runtime);
    result.unmatched_offs = fm6_note_runtime_unmatched_note_offs(&s_runtime);
    result.events = fm6_note_runtime_events_applied(&s_runtime);
    result.blocks = fm6_note_runtime_rendered_blocks(&s_runtime);
    result.notes_down = fm6_note_runtime_notes_down(&s_runtime);
    result.final_sounded = fm6_port_last_sounded();
    return result;
}

static bool same_sequence(const sequence_result_t *a, const sequence_result_t *b)
{
    return a->checksum == b->checksum &&
           a->energy == b->energy &&
           a->steals == b->steals &&
           a->retriggers == b->retriggers &&
           a->unmatched_offs == b->unmatched_offs &&
           a->events == b->events &&
           a->blocks == b->blocks &&
           a->notes_down == b->notes_down &&
           a->final_sounded == b->final_sounded;
}

static void test_deterministic_note_sequence(void)
{
    const sequence_result_t a = run_deterministic_sequence();
    const sequence_result_t b = run_deterministic_sequence();
    const bool pass = a.checksum != 0u && a.energy != 0u &&
                      a.events != 0u && a.blocks == NOTE_RUNTIME_DETERMINISM_BLOCKS &&
                      same_sequence(&a, &b);
    check("deterministic_note_sequence", pass);
    printf("FM6_NOTE_RUNTIME_SEQUENCE: %s blocks=%u passes=2 events=%" PRIu32
           " steals=%" PRIu32 " retriggers=%" PRIu32 " unmatched=%" PRIu32
           " notes_down=%u sounded=%u checksum=0x%08" PRIx32 " energy=%" PRIu64 "\n",
           pass ? "PASS" : "FAIL",
           NOTE_RUNTIME_DETERMINISM_BLOCKS,
           a.events,
           a.steals,
           a.retriggers,
           a.unmatched_offs,
           a.notes_down,
           a.final_sounded,
           a.checksum,
           a.energy);
}

unsigned fm6_note_runtime_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    make_voice();

    test_silent_program_load();
    test_note_on_applies_at_block_boundary();
    test_velocity_zero_maps_to_note_off();
    test_same_note_retriggers_same_voice();
    test_polyphony_steals_oldest_down_voice();
    test_released_voice_reused_before_steal();
    test_all_notes_off_and_release_drain();
    test_program_reset_discards_stale_events();
    test_queue_backpressure();
    test_deterministic_note_sequence();

    printf("QEMU_FM6_NOTE_RUNTIME: %s tests=%u failures=%u queue_capacity=%u voices=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures,
           (unsigned)FM6_NOTE_QUEUE_CAPACITY,
           (unsigned)FM6_BENCH_MAX_VOICES);
    return s_failures;
}
