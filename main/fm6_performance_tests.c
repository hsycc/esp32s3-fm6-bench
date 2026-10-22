#include "fm6_performance_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fm6_control_runtime.h"
#include "fm6_midi_router.h"
#include "fm6_program_bank.h"

static fm6_control_queue_t queue;
static fm6_control_runtime_t runtime;
static fm6_program_bank_t bank;
static fm6_midi_router_t router;
static uint8_t voice[FM6_RUNTIME_VOICE_SIZE];
static int32_t pcm[FM6_BENCH_BLOCK_FRAMES];
static unsigned tests, failures;

static void check(const char *name, bool pass)
{
    ++tests;
    failures += !pass;
    printf("FM6_PERFORMANCE_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
}

static bool setup(bool fixed)
{
    memset(voice, 0, sizeof(voice));
    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = voice + op * 21u;
        o[0] = o[1] = o[2] = o[3] = 99u;
        o[4] = o[5] = o[6] = 99u;
        o[7] = 0u;
        o[16] = 80u;
        o[17] = fixed ? 1u : 0u;
        o[18] = 1u;
        o[20] = 7u;
    }
    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = 99u;
        voice[130u + i] = 50u;
    }
    voice[134] = 31u; /* Six parallel carriers, unmodulated sine for pitch tests. */
    voice[136] = 1u;
    voice[144] = 24u;
    memset(voice + 145, ' ', 10u);
    fm6_control_queue_init(&queue);
    fm6_program_bank_init(&bank);
    bool ok = fm6_program_bank_set(&bank, 0u, voice);
    voice[134] = 5u;
    ok &= fm6_program_bank_set(&bank, 1u, voice);
    voice[134] = 31u;
    ok &= fm6_control_runtime_init(&runtime, &queue);
    ok &= fm6_midi_router_init(&router, &queue, &bank, 0u, 12u);
    return ok;
}

static bool render(void)
{
    return fm6_control_runtime_render_block(&runtime, pcm, NULL);
}

static bool silent(void)
{
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        if (pcm[i] != 0) {
            return false;
        }
    }
    return true;
}

static void message(uint8_t status, uint8_t a, uint8_t b)
{
    const uint8_t bytes[] = {status, a, b};
    fm6_midi_router_feed(&router, bytes, sizeof(bytes));
}

static bool start_note(void)
{
    bool ok = fm6_control_queue_submit_program(&queue, voice, 12u, NULL);
    ok &= fm6_control_queue_submit_note_on(&queue, 69u, 100u, NULL);
    return ok;
}

static uint32_t hash_pcm(uint32_t hash)
{
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        hash = (hash * 16777619u) ^ (uint32_t)pcm[i];
    }
    return hash;
}

static uint64_t energy_pcm(void)
{
    uint64_t energy = 0;
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        const int64_t sample = pcm[i];
        energy += (uint64_t)(sample < 0 ? -sample : sample);
    }
    return energy;
}

typedef struct {
    bool ok;
    uint32_t hash;
    uint64_t energy;
    unsigned crossings;
} measurement_t;

static measurement_t measure(bool fixed, uint16_t bend, uint8_t volume,
                              uint8_t expression, bool raw)
{
    measurement_t r = {.ok = setup(fixed), .hash = 2166136261u};
    if (raw) {
        r.ok &= fm6_port_load_program(voice, 12u);
        r.ok &= fm6_port_note_on(0u, 69u, 100u);
    } else {
        r.ok &= start_note();
        r.ok &= fm6_control_queue_submit_pitch_bend(&queue, bend, NULL);
        r.ok &= fm6_control_queue_submit_volume(&queue, volume, NULL);
        r.ok &= fm6_control_queue_submit_expression(&queue, expression, NULL);
    }
    int32_t previous = 0;
    for (unsigned block = 0; block < 320u; ++block) {
        if (raw) {
            fm6_port_render_block(12u, 31u, pcm);
        } else {
            r.ok &= render();
        }
        if (block >= 64u) {
            r.hash = hash_pcm(r.hash);
            r.energy += energy_pcm();
            for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
                if (previous <= 0 && pcm[i] > 0) {
                    ++r.crossings;
                }
                previous = pcm[i];
            }
        }
    }
    return r;
}

static void test_boundary_and_wire(void)
{
    bool ok = setup(false);
    check("controller_defaults", ok && runtime.pitch_bend == 8192u &&
          runtime.volume == 127u && runtime.expression == 127u &&
          runtime.gain_q16 == 65536 && !runtime.sustain_down);
    message(0xe0u, 127u, 127u);
    message(0xb0u, 7u, 64u);
    message(0xb0u, 11u, 96u);
    const bool before = runtime.pitch_bend == 8192u && runtime.volume == 127u &&
                        runtime.expression == 127u;
    ok &= render();
    check("controller_block_boundary", ok && before && runtime.pitch_bend == 16383u &&
          runtime.volume == 64u && runtime.expression == 96u &&
          runtime.commands_applied == 3u && silent());

    ok = setup(false) && start_note() && render();
    const uint8_t part[] = {0xe0u, 1u, 0xf8u};
    fm6_midi_router_feed(&router, part, sizeof(part));
    ok &= render();
    const bool incomplete = runtime.pitch_bend == 8192u;
    fm6_midi_router_feed_byte(&router, 64u);
    ok &= render();
    check("bend_fragmented_realtime", ok && incomplete && runtime.pitch_bend == 8193u &&
          fm6_port_pitch_bend_offset_q24() == 341 && router.stats.realtime_bytes == 1u);
    const uint8_t running[] = {0u, 0u, 127u, 127u};
    fm6_midi_router_feed(&router, running, sizeof(running));
    ok &= render();
    check("bend_running_status_endpoints", ok && runtime.pitch_bend == 16383u &&
          runtime.performance_events == 3u && router.stats.messages_routed == 3u);

    ok = fm6_port_set_pitch_bend(0u) && fm6_port_pitch_bend_offset_q24() == -2796202;
    ok &= fm6_port_set_pitch_bend(8192u) && fm6_port_pitch_bend_offset_q24() == 0;
    ok &= fm6_port_set_pitch_bend(16383u) && fm6_port_pitch_bend_offset_q24() == 2795861;
    ok &= !fm6_port_set_pitch_bend(16384u) && fm6_port_pitch_bend_offset_q24() == 2795861;
    check("bend_q24_endpoints", ok);
}

static void test_audio_measurements(void)
{
    const measurement_t raw = measure(false, 8192u, 127u, 127u, true);
    const measurement_t center = measure(false, 8192u, 127u, 127u, false);
    check("unity_pcm_equivalence", raw.ok && center.ok && center.energy != 0u &&
          raw.hash == center.hash && raw.energy == center.energy);
    const measurement_t down = measure(false, 0u, 127u, 127u, false);
    const measurement_t up = measure(false, 16383u, 127u, 127u, false);
    check("bend_audio_frequency", down.ok && center.ok && up.ok &&
          down.crossings >= 71u && down.crossings <= 75u &&
          center.crossings >= 80u && center.crossings <= 84u &&
          up.crossings >= 90u && up.crossings <= 94u &&
          down.hash != center.hash && up.hash != center.hash);
    printf("FM6_BEND_AUDIO down_crossings=%u center_crossings=%u up_crossings=%u samples=8192\n",
           down.crossings, center.crossings, up.crossings);
    const measurement_t fixed_a = measure(true, 0u, 127u, 127u, false);
    const measurement_t fixed_b = measure(true, 16383u, 127u, 127u, false);
    check("fixed_frequency_invariance", fixed_a.ok && fixed_b.ok &&
          fixed_a.energy != 0u && fixed_a.hash == fixed_b.hash && fixed_a.energy == fixed_b.energy);
    const measurement_t half = measure(false, 8192u, 64u, 127u, false);
    const measurement_t quarter = measure(false, 8192u, 64u, 64u, false);
    check("volume_expression_gain", half.ok && quarter.ok &&
          half.energy * 100u > center.energy * 49u &&
          half.energy * 100u < center.energy * 52u &&
          quarter.energy * 100u > center.energy * 24u &&
          quarter.energy * 100u < center.energy * 27u);
    const measurement_t mute = measure(false, 8192u, 0u, 127u, false);
    check("mute_output_zero", mute.ok && mute.energy == 0u &&
          fm6_control_runtime_notes_down(&runtime) == 1u && fm6_port_abs_energy() != 0u);
}

static uint32_t resume_hash(bool mute, bool *ok)
{
    *ok = setup(false) && start_note();
    uint32_t hash = 2166136261u;
    for (unsigned block = 0; block < 128u; ++block) {
        if (mute && (block == 16u || block == 48u)) {
            *ok &= fm6_control_queue_submit_expression(&queue, block == 16u ? 0u : 127u, NULL);
        }
        *ok &= render();
        if (block >= 64u) {
            hash = hash_pcm(hash);
        }
    }
    *ok &= runtime.note_on_events == 1u && runtime.retrigger_events == 0u;
    return hash;
}

static void test_gain_ramps(void)
{
    bool ok = setup(false) && start_note();
    for (unsigned i = 0; i < 64u; ++i) {
        ok &= render();
    }
    ok &= fm6_control_queue_submit_expression(&queue, 0u, NULL);
    for (unsigned i = 0; i < 4u; ++i) {
        ok &= render();
        ok &= runtime.gain_q16 == 65536 - (int32_t)(i + 1u) * 16384;
    }
    ok &= render();
    check("gain_ramp_128_frames", ok && silent() && runtime.gain_ramp_position == 128u &&
          fm6_control_runtime_notes_down(&runtime) == 1u);

    ok = setup(false);
    ok &= fm6_control_queue_submit_expression(&queue, 0u, NULL) && render();
    const bool first = runtime.gain_q16 == 49152;
    ok &= fm6_control_queue_submit_expression(&queue, 127u, NULL) && render();
    const bool retarget = runtime.gain_q16 == 53248;
    for (unsigned i = 0; i < 3u; ++i) {
        ok &= render();
    }
    check("gain_retarget_continuity", ok && first && retarget && runtime.gain_q16 == 65536);

    ok = setup(false);
    for (unsigned i = 0; i < 4u; ++i) {
        ok &= fm6_control_queue_submit_expression(&queue, 0u, NULL) && render();
    }
    check("gain_repeated_target_progress", ok && runtime.gain_q16 == 0 &&
          runtime.gain_ramp_position == 128u);
    bool a_ok = false, b_ok = false;
    const uint32_t a = resume_hash(false, &a_ok);
    const uint32_t b = resume_hash(true, &b_ok);
    check("mute_advances_synth", a_ok && b_ok && a == b);
}

static void test_reset_and_program(void)
{
    bool ok = setup(false);
    const uint8_t input[] = {0xc0u, 0u, 0x90u, 60u, 100u, 64u, 100u,
                            0xb0u, 64u, 127u, 0x80u, 60u, 0u,
                            0xb0u, 7u, 75u, 11u, 20u, 0xe0u, 1u, 64u};
    fm6_midi_router_feed(&router, input, sizeof(input));
    ok &= render();
    const bool held = fm6_control_runtime_sustained_notes(&runtime) == 1u;
    message(0xb0u, 121u, 0u);
    ok &= render();
    check("reset_preserves_held_notes", ok && held && runtime.pitch_bend == 8192u &&
          runtime.expression == 127u && runtime.volume == 75u && !runtime.sustain_down &&
          fm6_control_runtime_sustained_notes(&runtime) == 0u &&
          fm6_control_runtime_note_is_down(&runtime, 64u) &&
          runtime.program_commits == 1u && runtime.all_sound_off_events == 0u &&
          fm6_port_pitch_bend_offset_q24() == 0);

    ok = setup(false);
    const uint8_t before[] = {0xe0u, 0u, 0u, 0xb0u, 7u, 75u, 11u, 30u, 64u, 127u,
                             0xc0u, 0u, 0x90u, 60u, 100u, 0x80u, 60u, 0u};
    fm6_midi_router_feed(&router, before, sizeof(before));
    ok &= render();
    const uint8_t next[] = {0xc0u, 1u, 0x90u, 72u, 100u, 0x80u, 72u, 0u};
    fm6_midi_router_feed(&router, next, sizeof(next));
    ok &= render();
    check("program_preserves_controls", ok && runtime.pitch_bend == 0u && runtime.volume == 75u &&
          runtime.expression == 30u && runtime.sustain_down && runtime.program_commits == 2u &&
          fm6_control_runtime_sustained_notes(&runtime) == 1u && fm6_port_patch_algorithm() == 5u &&
          fm6_port_pitch_bend_offset_q24() == -2796202);

    ok = setup(false);
    message(0xe0u, 0u, 0u);
    message(0xb0u, 11u, 0u);
    message(0xb0u, 121u, 0u);
    ok &= start_note() && render();
    check("reset_before_program", ok && runtime.pitch_bend == 8192u && runtime.expression == 127u &&
          runtime.gain_q16 == 65536 && fm6_port_pitch_bend_offset_q24() == 0);

    message(0xe0u, 0u, 0u);
    message(0xb0u, 7u, 64u);
    message(0xb0u, 11u, 32u);
    message(0xb0u, 120u, 0u);
    ok &= render();
    check("all_sound_off_keeps_controls", ok && silent() && runtime.volume == 64u &&
          runtime.expression == 32u && runtime.pitch_bend == 0u &&
          fm6_port_pitch_bend_offset_q24() == -2796202 && runtime.all_sound_off_events == 1u);
}

static void test_rejection_and_filter(void)
{
    bool ok = setup(false);
    uint32_t sequence = 0xabcdef01u;
    ok &= !fm6_control_queue_submit_pitch_bend(&queue, 16384u, &sequence);
    ok &= !fm6_control_queue_submit_volume(&queue, 128u, &sequence);
    ok &= !fm6_control_queue_submit_expression(&queue, 255u, &sequence);
    ok &= !fm6_control_queue_submit_note_on(&queue, 60u, 128u, &sequence);
    check("invalid_controller_ranges", ok && sequence == 0xabcdef01u &&
          fm6_control_queue_pending(&queue) == 0u && fm6_control_queue_rejected(&queue) == 4u);

    ok = setup(false) && start_note() && render();
    for (unsigned i = 0; i < FM6_CONTROL_QUEUE_CAPACITY; ++i) {
        ok &= fm6_control_queue_submit_sustain(&queue, false, NULL);
    }
    ok &= !fm6_control_queue_submit_pitch_bend(&queue, 0u, &sequence);
    ok &= !fm6_control_queue_submit_volume(&queue, 0u, &sequence);
    ok &= render();
    check("controller_backpressure", ok && sequence == 0xabcdef01u &&
          runtime.pitch_bend == 8192u && runtime.volume == 127u &&
          fm6_control_queue_rejected(&queue) == 2u && runtime.performance_events == 0u);

    ok = setup(false) && fm6_midi_router_set_channel(&router, 2u);
    const uint8_t bytes[] = {0xc2u, 0u, 0xe2u, 0u, 64u, 0xe0u, 0u, 0u,
                            0xb0u, 7u, 0u, 11u, 0u, 121u, 0u};
    fm6_midi_router_feed(&router, bytes, sizeof(bytes));
    ok &= render();
    check("controller_channel_filter", ok && router.stats.channel_ignored == 4u &&
          router.stats.messages_routed == 2u && runtime.pitch_bend == 8192u &&
          runtime.volume == 127u && runtime.expression == 127u);
}

static void test_polyphonic_controls(void)
{
    bool ok = setup(false) && fm6_control_queue_submit_program(&queue, voice, 12u, NULL);
    for (unsigned i = 0; i < 12u; ++i) {
        ok &= fm6_control_queue_submit_note_on(&queue, (uint8_t)(48u + i), 100u, NULL);
    }
    for (unsigned i = 0; i < 64u; ++i) {
        ok &= render();
    }
    fm6_control_voice_slot_t before[12];
    memcpy(before, runtime.voices, sizeof(before));
    message(0xe0u, 127u, 127u);
    message(0xb0u, 7u, 80u);
    message(0xb0u, 11u, 64u);
    ok &= render();
    check("twelve_voices_no_retrigger", ok && fm6_control_runtime_notes_down(&runtime) == 12u &&
          runtime.note_on_events == 12u && runtime.retrigger_events == 0u && runtime.voice_steals == 0u &&
          memcmp(before, runtime.voices, sizeof(before)) == 0 && energy_pcm() != 0u);
}

typedef struct {
    bool ok;
    uint32_t hash, commands, events, rejected;
    uint64_t energy;
    unsigned max_notes;
    bool final_silent;
} sequence_t;

static sequence_t sequence(void)
{
    sequence_t r = {.ok = setup(false), .hash = 2166136261u};
    for (unsigned block = 0; block < 512u; ++block) {
        if (block == 0u || block == 256u) {
            const uint8_t program[] = {0xc0u, (uint8_t)(block == 0u ? 0u : 1u)};
            fm6_midi_router_feed(&router, program, sizeof(program));
            for (unsigned i = 0; i < 12u; ++i) {
                message(0x90u, (uint8_t)(48u + i), 100u);
            }
        } else {
            if (block % 7u == 0u) {
                const uint16_t bend = (uint16_t)((block * 79u) & 16383u);
                message(0xe0u, (uint8_t)(bend & 127u), (uint8_t)(bend >> 7));
            }
            if (block % 11u == 0u) {
                message(0xb0u, 7u, (uint8_t)((block * 3u) & 127u));
            }
            if (block % 13u == 0u) {
                message(0xb0u, 11u, (uint8_t)((block * 5u) & 127u));
            }
            if (block % 41u == 20u) {
                message(0xb0u, 64u, (uint8_t)((block & 1u) ? 127u : 0u));
            }
            if (block % 17u == 3u) {
                message(0x80u, (uint8_t)(48u + (block / 17u) % 12u), 0u);
            }
            if (block % 17u == 4u) {
                message(0x90u, (uint8_t)(48u + (block / 17u) % 12u), 100u);
            }
            if (block % 37u == 10u) {
                message(0xb0u, 121u, 0u);
            }
        }
        r.ok &= render();
        r.hash = hash_pcm(r.hash); /* Whole output, survives program changes. */
        r.energy += energy_pcm();
        const unsigned down = fm6_control_runtime_notes_down(&runtime);
        if (down > r.max_notes) {
            r.max_notes = down;
        }
    }
    message(0xb0u, 120u, 0u);
    r.ok &= render();
    r.final_silent = silent() && fm6_control_runtime_notes_down(&runtime) == 0u &&
                     fm6_control_runtime_sustained_notes(&runtime) == 0u;
    r.commands = runtime.commands_applied;
    r.events = runtime.performance_events;
    r.rejected = router.stats.queue_rejected;
    r.ok &= r.commands == router.stats.messages_routed && runtime.program_commits == 2u;
    return r;
}

static void test_sequence(void)
{
    const sequence_t a = sequence();
    const sequence_t b = sequence();
    const bool pass = a.ok && b.ok && a.hash == b.hash && a.energy == b.energy &&
                      a.energy != 0u && a.commands == b.commands && a.events == b.events &&
                      a.events > 0u && a.rejected == 0u && b.rejected == 0u &&
                      a.max_notes == 12u && b.max_notes == 12u && a.final_silent && b.final_silent;
    check("controls_sequence_determinism", pass);
    printf("FM6_PERFORMANCE_SEQUENCE: %s blocks=512 passes=2 commands=%" PRIu32
           " events=%" PRIu32 " max_notes=%u rejected=%" PRIu32
           " checksum=0x%08" PRIx32 " energy=%" PRIu64 " final_silent=%u\n",
           pass ? "PASS" : "FAIL", a.commands, a.events, a.max_notes, a.rejected,
           a.hash, a.energy, (unsigned)a.final_silent);
}

unsigned fm6_performance_tests_run(void)
{
    tests = failures = 0u;
    test_boundary_and_wire();
    test_audio_measurements();
    test_gain_ramps();
    test_reset_and_program();
    test_rejection_and_filter();
    test_polyphonic_controls();
    test_sequence();
    printf("QEMU_FM6_PERFORMANCE: %s tests=%u failures=%u voices=12 ramp_frames=128\n",
           failures == 0u ? "PASS" : "FAIL", tests, failures);
    return failures;
}
