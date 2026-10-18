#include "fm6_midi_router_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_control_runtime.h"
#include "fm6_midi_router.h"
#include "fm6_port.h"
#include "fm6_program_bank.h"

#define MIDI_DETERMINISM_BLOCKS 128u

typedef struct {
    uint32_t checksum;
    uint64_t energy;
    fm6_midi_router_stats_t stats;
    uint32_t commands;
    uint32_t programs;
    uint32_t sustain_events;
    uint32_t all_sound_off_events;
    uint32_t steals;
    uint32_t active_program_sequence;
    unsigned notes_down;
    unsigned sustained_notes;
    unsigned algorithm;
    bool sustain_down;
} midi_sequence_result_t;

static unsigned s_tests;
static unsigned s_failures;
static fm6_control_queue_t s_queue;
static fm6_control_runtime_t s_runtime;
static fm6_program_bank_t s_bank;
static fm6_midi_router_t s_router;
static uint8_t s_voice_a[DX7_VCED_SIZE];
static uint8_t s_voice_b[DX7_VCED_SIZE];
static uint8_t s_voice_c[DX7_VCED_SIZE];

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("FM6_MIDI_ROUTER_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
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
        o[1] = 68u;
        o[2] = 58u;
        o[3] = 74u;
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
    size_t n = strlen(name);
    if (n > sizeof(patch_name)) {
        n = sizeof(patch_name);
    }
    memcpy(patch_name, name, n);
    memcpy(voice + DX7_VCED_NAME_OFFSET, patch_name, sizeof(patch_name));
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voice_a, 5u, 5u, "MIDI-A");
    make_voice(s_voice_b, 17u, 21u, "MIDI-B");
    make_voice(s_voice_c, 29u, 31u, "MIDI-127");
}

static bool setup(uint8_t channel)
{
    fm6_control_queue_init(&s_queue);
    fm6_program_bank_init(&s_bank);
    if (!fm6_program_bank_set(&s_bank, 0u, s_voice_a) ||
        !fm6_program_bank_set(&s_bank, 1u, s_voice_b) ||
        !fm6_program_bank_set(&s_bank, 127u, s_voice_c)) {
        return false;
    }
    if (!fm6_control_runtime_init(&s_runtime, &s_queue)) {
        return false;
    }
    return fm6_midi_router_init(&s_router, &s_queue, &s_bank, channel, 12u);
}

static bool render_one(unsigned *applied)
{
    int32_t out[FM6_BENCH_BLOCK_FRAMES];
    return fm6_control_runtime_render_block(&s_runtime, out, applied);
}

static void feed(const uint8_t *data, size_t size)
{
    fm6_midi_router_feed(&s_router, data, size);
}

static void test_program_bank_128_slots(void)
{
    const bool started = setup(0u);
    const uint8_t *slot127 = fm6_program_bank_get(&s_bank, 127u);
    const bool before = started && fm6_program_bank_count(&s_bank) == 3u &&
                        slot127 != NULL &&
                        memcmp(slot127, s_voice_c, DX7_VCED_SIZE) == 0;
    const bool cleared = fm6_program_bank_clear(&s_bank, 127u);
    check("program_bank_128_slots",
          before && cleared && !fm6_program_bank_valid(&s_bank, 127u) &&
          fm6_program_bank_count(&s_bank) == 2u &&
          fm6_program_bank_get(&s_bank, 127u) == NULL);
}

static void test_explicit_note_on_off(void)
{
    const bool started = setup(0u);
    static const uint8_t on[] = {0xc0u, 0u, 0x90u, 60u, 110u};
    feed(on, sizeof(on));
    unsigned applied = 0u;
    bool ok = render_one(&applied);
    const bool on_state = applied == 2u && fm6_port_patch_algorithm() == 5u &&
                          fm6_control_runtime_note_is_down(&s_runtime, 60u);
    static const uint8_t off[] = {0x80u, 60u, 0u};
    feed(off, sizeof(off));
    ok &= render_one(NULL);
    check("explicit_note_on_off",
          started && ok && on_state &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u);
}

static void test_velocity_zero_is_note_off(void)
{
    const bool started = setup(0u);
    static const uint8_t bytes[] = {
        0xc0u, 0u,
        0x90u, 64u, 100u,
        64u, 0u,
    };
    feed(bytes, sizeof(bytes));
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    check("velocity_zero_note_off",
          started && rendered && applied == 3u &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u);
}

static void test_running_status_with_realtime(void)
{
    const bool started = setup(0u);
    static const uint8_t bytes[] = {
        0xc0u, 0u,
        0x90u, 60u, 100u,
        0xf8u,
        62u, 110u,
        64u, 120u,
    };
    feed(bytes, sizeof(bytes));
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    const fm6_midi_router_stats_t *stats = fm6_midi_router_stats(&s_router);
    check("running_status_realtime_interleave",
          started && rendered && applied == 4u && stats != NULL &&
          stats->realtime_bytes == 1u && stats->messages_seen == 4u &&
          stats->messages_routed == 4u &&
          fm6_control_runtime_notes_down(&s_runtime) == 3u);
}

static void test_channel_filter(void)
{
    const bool started = setup(2u);
    static const uint8_t bytes[] = {
        0xc0u, 0u,
        0xc2u, 0u,
        0x90u, 60u, 100u,
        0x92u, 65u, 110u,
    };
    feed(bytes, sizeof(bytes));
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    const fm6_midi_router_stats_t *stats = fm6_midi_router_stats(&s_router);
    check("channel_filter",
          started && rendered && applied == 2u && stats != NULL &&
          stats->channel_ignored == 2u && stats->messages_routed == 2u &&
          fm6_control_runtime_notes_down(&s_runtime) == 1u &&
          fm6_control_runtime_note_is_down(&s_runtime, 65u));
}

static void test_omni_channel(void)
{
    const bool started = setup(FM6_MIDI_CHANNEL_OMNI);
    static const uint8_t bytes[] = {
        0xc5u, 1u,
        0x97u, 70u, 115u,
    };
    feed(bytes, sizeof(bytes));
    const bool rendered = render_one(NULL);
    check("omni_channel",
          started && rendered && fm6_port_patch_algorithm() == 21u &&
          fm6_control_runtime_note_is_down(&s_runtime, 70u));
}

static void test_missing_program(void)
{
    const bool started = setup(0u);
    static const uint8_t bytes[] = {0xc0u, 10u};
    feed(bytes, sizeof(bytes));
    const fm6_midi_router_stats_t *stats = fm6_midi_router_stats(&s_router);
    check("missing_program_rejected",
          started && stats != NULL && stats->missing_program == 1u &&
          stats->messages_seen == 1u && stats->messages_routed == 0u &&
          fm6_control_queue_pending(&s_queue) == 0u);
}

static void test_sustain_hold_and_release(void)
{
    const bool started = setup(0u);
    static const uint8_t hold[] = {
        0xc0u, 0u,
        0x90u, 60u, 110u,
        0xb0u, 64u, 127u,
        0x80u, 60u, 0u,
    };
    feed(hold, sizeof(hold));
    unsigned applied = 0u;
    bool ok = render_one(&applied);
    const bool held = applied == 4u && fm6_control_runtime_sustain_down(&s_runtime) &&
                      fm6_control_runtime_notes_down(&s_runtime) == 0u &&
                      fm6_control_runtime_sustained_notes(&s_runtime) == 1u;

    static const uint8_t release[] = {0xb0u, 64u, 0u};
    feed(release, sizeof(release));
    ok &= render_one(NULL);
    check("sustain_hold_release",
          started && ok && held && !fm6_control_runtime_sustain_down(&s_runtime) &&
          fm6_control_runtime_sustained_notes(&s_runtime) == 0u &&
          fm6_control_runtime_sustain_events(&s_runtime) == 2u);
}

static void test_all_notes_off_respects_sustain(void)
{
    const bool started = setup(0u);
    static const uint8_t bytes[] = {
        0xc0u, 0u,
        0x90u, 60u, 100u,
        62u, 105u,
        0xb0u, 64u, 127u,
        123u, 0u,
    };
    feed(bytes, sizeof(bytes));
    const bool rendered = render_one(NULL);
    check("all_notes_off_respects_sustain",
          started && rendered && fm6_control_runtime_sustain_down(&s_runtime) &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u &&
          fm6_control_runtime_sustained_notes(&s_runtime) == 2u);
}

static void test_all_sound_off_is_hard_silence(void)
{
    const bool started = setup(0u);
    static const uint8_t bytes[] = {
        0xc0u, 0u,
        0x90u, 60u, 110u,
        0xb0u, 64u, 127u,
        0x80u, 60u, 0u,
        0xb0u, 120u, 0u,
    };
    feed(bytes, sizeof(bytes));
    const bool rendered = render_one(NULL);
    check("all_sound_off_hard_silence",
          started && rendered && fm6_control_runtime_sustain_down(&s_runtime) &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u &&
          fm6_control_runtime_sustained_notes(&s_runtime) == 0u &&
          fm6_control_runtime_all_sound_off_events(&s_runtime) == 1u &&
          fm6_port_last_sounded() == 0u);
}

static void test_program_change_preserves_controller_order(void)
{
    const bool started = setup(0u);
    static const uint8_t bytes[] = {
        0xc0u, 0u,
        0xb0u, 64u, 127u,
        0x90u, 60u, 100u,
        0xc0u, 1u,
        0x90u, 72u, 120u,
        0x80u, 72u, 0u,
    };
    feed(bytes, sizeof(bytes));
    unsigned applied = 0u;
    const bool rendered = render_one(&applied);
    uint8_t last_program = 0xffu;
    check("program_change_preserves_sustain_order",
          started && rendered && applied == 6u &&
          fm6_midi_router_last_program(&s_router, &last_program) && last_program == 1u &&
          fm6_port_patch_algorithm() == 21u &&
          fm6_control_runtime_sustain_down(&s_runtime) &&
          fm6_control_runtime_notes_down(&s_runtime) == 0u &&
          fm6_control_runtime_sustained_notes(&s_runtime) == 1u);
}

static void test_sysex_isolation_clears_running_status(void)
{
    const bool started = setup(0u);
    static const uint8_t bytes[] = {
        0xc0u, 0u,
        0x90u, 60u, 100u,
        0xf0u, 0x43u, 0x00u, 0x09u, 0x01u, 0x02u, 0xf8u, 0xf7u,
        62u, 110u,
        0x90u, 64u, 120u,
    };
    feed(bytes, sizeof(bytes));
    const bool rendered = render_one(NULL);
    const fm6_midi_router_stats_t *stats = fm6_midi_router_stats(&s_router);
    check("sysex_isolation_running_status",
          started && rendered && stats != NULL &&
          stats->realtime_bytes == 1u && stats->orphan_data == 2u &&
          stats->sysex_bytes == 7u &&
          fm6_control_runtime_notes_down(&s_runtime) == 2u &&
          fm6_control_runtime_note_is_down(&s_runtime, 60u) &&
          fm6_control_runtime_note_is_down(&s_runtime, 64u));
}

static void test_unsupported_channel_messages(void)
{
    const bool started = setup(0u);
    static const uint8_t bytes[] = {
        0xc0u, 0u,
        0xa0u, 60u, 20u,
        0xb0u, 1u, 64u,
        0xd0u, 50u,
    };
    feed(bytes, sizeof(bytes));
    const fm6_midi_router_stats_t *stats = fm6_midi_router_stats(&s_router);
    check("unsupported_messages_ignored",
          started && stats != NULL && stats->messages_seen == 4u &&
          stats->messages_routed == 1u && stats->unsupported_messages == 3u &&
          fm6_control_queue_pending(&s_queue) == 1u);
}

static void test_queue_backpressure_is_reported(void)
{
    const bool started = setup(0u);
    for (unsigned i = 0; i < 20u; ++i) {
        const uint8_t msg[3] = {0x90u, (uint8_t)(36u + i), 100u};
        feed(msg, sizeof(msg));
    }
    const fm6_midi_router_stats_t *stats = fm6_midi_router_stats(&s_router);
    check("queue_backpressure_reported",
          started && stats != NULL && stats->messages_seen == 20u &&
          stats->messages_routed == FM6_CONTROL_QUEUE_CAPACITY &&
          stats->queue_rejected == 4u &&
          fm6_control_queue_pending(&s_queue) == FM6_CONTROL_QUEUE_CAPACITY);
}

static midi_sequence_result_t run_deterministic_sequence(void)
{
    midi_sequence_result_t result;
    memset(&result, 0, sizeof(result));
    if (!setup(0u)) {
        return result;
    }

    for (unsigned block = 0; block < MIDI_DETERMINISM_BLOCKS; ++block) {
        if ((block % 32u) == 0u) {
            const uint8_t msg[2] = {0xc0u, (uint8_t)((block / 32u) & 1u)};
            feed(msg, sizeof(msg));
        }
        if ((block % 5u) == 0u) {
            const uint8_t msg[3] = {
                0x90u,
                (uint8_t)(36u + ((block / 5u) % 24u)),
                (uint8_t)(80u + block % 40u),
            };
            feed(msg, sizeof(msg));
        }
        if ((block % 7u) == 3u) {
            const uint8_t msg[3] = {
                0x80u,
                (uint8_t)(36u + ((block / 7u) % 24u)),
                0u,
            };
            feed(msg, sizeof(msg));
        }
        if ((block % 29u) == 10u) {
            const uint8_t msg[3] = {0xb0u, 64u, (uint8_t)(((block / 29u) & 1u) ? 0u : 127u)};
            feed(msg, sizeof(msg));
        }
        if ((block % 47u) == 20u) {
            static const uint8_t msg[] = {0xb0u, 123u, 0u};
            feed(msg, sizeof(msg));
        }
        (void)render_one(NULL);
    }

    result.checksum = fm6_port_checksum();
    result.energy = fm6_port_abs_energy();
    result.stats = s_router.stats;
    result.commands = fm6_control_runtime_commands_applied(&s_runtime);
    result.programs = fm6_control_runtime_program_commits(&s_runtime);
    result.sustain_events = fm6_control_runtime_sustain_events(&s_runtime);
    result.all_sound_off_events = fm6_control_runtime_all_sound_off_events(&s_runtime);
    result.steals = fm6_control_runtime_voice_steals(&s_runtime);
    result.active_program_sequence = fm6_control_runtime_active_program_sequence(&s_runtime);
    result.notes_down = fm6_control_runtime_notes_down(&s_runtime);
    result.sustained_notes = fm6_control_runtime_sustained_notes(&s_runtime);
    result.algorithm = fm6_port_patch_algorithm();
    result.sustain_down = fm6_control_runtime_sustain_down(&s_runtime);
    return result;
}

static bool same_stats(const fm6_midi_router_stats_t *a, const fm6_midi_router_stats_t *b)
{
    return a->bytes_seen == b->bytes_seen &&
           a->messages_seen == b->messages_seen &&
           a->messages_routed == b->messages_routed &&
           a->channel_ignored == b->channel_ignored &&
           a->unsupported_messages == b->unsupported_messages &&
           a->realtime_bytes == b->realtime_bytes &&
           a->sysex_bytes == b->sysex_bytes &&
           a->orphan_data == b->orphan_data &&
           a->missing_program == b->missing_program &&
           a->queue_rejected == b->queue_rejected;
}

static bool same_sequence(const midi_sequence_result_t *a, const midi_sequence_result_t *b)
{
    return a->checksum == b->checksum && a->energy == b->energy &&
           same_stats(&a->stats, &b->stats) &&
           a->commands == b->commands && a->programs == b->programs &&
           a->sustain_events == b->sustain_events &&
           a->all_sound_off_events == b->all_sound_off_events &&
           a->steals == b->steals &&
           a->active_program_sequence == b->active_program_sequence &&
           a->notes_down == b->notes_down &&
           a->sustained_notes == b->sustained_notes &&
           a->algorithm == b->algorithm && a->sustain_down == b->sustain_down;
}

static void test_deterministic_midi_sequence(void)
{
    const midi_sequence_result_t a = run_deterministic_sequence();
    const midi_sequence_result_t b = run_deterministic_sequence();
    const bool pass = a.checksum != 0u && a.energy != 0u &&
                      a.programs == 4u && a.commands != 0u &&
                      a.stats.queue_rejected == 0u && same_sequence(&a, &b);
    check("deterministic_midi_sequence", pass);
    printf("FM6_MIDI_SEQUENCE: %s blocks=%u passes=2 messages=%" PRIu32
           " routed=%" PRIu32 " commands=%" PRIu32 " programs=%" PRIu32
           " sustain=%" PRIu32 " steals=%" PRIu32 " notes_down=%u sustained=%u"
           " algorithm=%u checksum=0x%08" PRIx32 " energy=%" PRIu64 "\n",
           pass ? "PASS" : "FAIL",
           MIDI_DETERMINISM_BLOCKS,
           a.stats.messages_seen,
           a.stats.messages_routed,
           a.commands,
           a.programs,
           a.sustain_events,
           a.steals,
           a.notes_down,
           a.sustained_notes,
           a.algorithm,
           a.checksum,
           a.energy);
}

unsigned fm6_midi_router_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    build_fixtures();

    test_program_bank_128_slots();
    test_explicit_note_on_off();
    test_velocity_zero_is_note_off();
    test_running_status_with_realtime();
    test_channel_filter();
    test_omni_channel();
    test_missing_program();
    test_sustain_hold_and_release();
    test_all_notes_off_respects_sustain();
    test_all_sound_off_is_hard_silence();
    test_program_change_preserves_controller_order();
    test_sysex_isolation_clears_running_status();
    test_unsupported_channel_messages();
    test_queue_backpressure_is_reported();
    test_deterministic_midi_sequence();

    printf("QEMU_FM6_MIDI_ROUTER: %s tests=%u failures=%u bank_slots=%u queue_capacity=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures,
           (unsigned)FM6_PROGRAM_BANK_SLOTS,
           (unsigned)FM6_CONTROL_QUEUE_CAPACITY);
    return s_failures;
}
