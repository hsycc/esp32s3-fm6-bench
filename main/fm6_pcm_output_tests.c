#include "fm6_pcm_output_tests.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "fm6_mix.h"
#include "fm6_midi_router.h"
#include "fm6_pcm_output.h"

#define SEQUENCE_BLOCKS 1024u
#define COHERENT_BLOCKS 64u

static unsigned tests, failures;
static fm6_pcm_output_t output;
static fm6_control_queue_t queue;
static fm6_control_runtime_t runtime;
static fm6_program_bank_t bank;
static fm6_midi_router_t router;
static uint8_t voice[FM6_RUNTIME_VOICE_SIZE];
static int32_t input[FM6_BENCH_BLOCK_FRAMES];
static int16_t pcm[FM6_BENCH_BLOCK_FRAMES];
static int32_t single[COHERENT_BLOCKS][FM6_BENCH_BLOCK_FRAMES];

static void check(const char *name, bool pass)
{
    ++tests;
    failures += !pass;
    printf("FM6_PCM_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
}

static bool zero_pcm(void)
{
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        if (pcm[i] != 0) {
            return false;
        }
    }
    return true;
}

static void fill_input(int32_t value)
{
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        input[i] = value;
    }
}

static void test_conversion(void)
{
    bool ok = fm6_pcm_output_init(&output, FM6_PCM_HEADROOM_DEFAULT);
    const fm6_pcm_output_t before = output;
    ok &= !fm6_pcm_output_init(&output, FM6_PCM_HEADROOM_MAX + 1u);
    ok &= !fm6_pcm_output_init(NULL, 0u);
    check("init_reject_atomic", ok && memcmp(&before, &output, sizeof(output)) == 0);

    memset(pcm, 0x55, sizeof(pcm));
    int16_t saved[FM6_BENCH_BLOCK_FRAMES];
    memcpy(saved, pcm, sizeof(saved));
    fm6_pcm_output_t uninitialized = {0};
    ok = !fm6_pcm_output_convert_block(&output, NULL, pcm);
    ok &= !fm6_pcm_output_convert_block(&output, input, NULL);
    ok &= !fm6_pcm_output_convert_block(&uninitialized, input, pcm);
    check("invalid_buffers_no_mutation", ok &&
          memcmp(saved, pcm, sizeof(saved)) == 0 &&
          memcmp(&before, &output, sizeof(output)) == 0);

    fill_input(0);
    ok = fm6_pcm_output_convert_block(&output, input, pcm);
    check("silence_exact", ok && zero_pcm() && output.stats.frames == 32u &&
          output.stats.blocks == 1u && output.stats.peak_output_abs == 0u);

    ok = fm6_pcm_output_init(&output, 0u);
    fill_input(0);
    input[0] = 8388608; input[1] = -8388608;
    input[2] = 512; input[3] = -512;
    ok &= fm6_pcm_output_convert_block(&output, input, pcm);
    check("q24_scale_known", ok && pcm[0] == 16384 && pcm[1] == -16384 &&
          pcm[2] == 1 && pcm[3] == -1);

    fill_input(0);
    input[0] = 255; input[1] = -255; input[2] = 256; input[3] = -256;
    input[4] = 768; input[5] = -768;
    ok = fm6_pcm_output_convert_block(&output, input, pcm);
    check("rounding_ties_away", ok && pcm[0] == 0 && pcm[1] == 0 &&
          pcm[2] == 1 && pcm[3] == -1 && pcm[4] == 2 && pcm[5] == -2);

    ok = fm6_pcm_output_reset_stats(&output);
    fill_input(0);
    input[0] = 32767 * 512;
    input[1] = -32768 * 512;
    input[2] = 32767 * 512 + 256;
    input[3] = -32768 * 512 - 256;
    ok &= fm6_pcm_output_convert_block(&output, input, pcm);
    check("saturation_endpoints", ok && pcm[0] == INT16_MAX && pcm[1] == INT16_MIN &&
          pcm[2] == INT16_MAX && pcm[3] == INT16_MIN &&
          output.stats.clipped_positive == 1u && output.stats.clipped_negative == 1u &&
          output.stats.clipped_blocks == 1u);

    ok = fm6_pcm_output_reset_stats(&output);
    fill_input(0); input[0] = INT32_MAX; input[1] = INT32_MIN;
    ok &= fm6_pcm_output_convert_block(&output, input, pcm);
    check("extreme_input_peaks", ok && pcm[0] == INT16_MAX && pcm[1] == INT16_MIN &&
          output.stats.peak_input_abs == UINT32_C(2147483648) &&
          output.stats.peak_output_abs == 32768u);

    ok = true;
    for (unsigned bits = 0; bits <= FM6_PCM_HEADROOM_MAX; ++bits) {
        ok &= fm6_pcm_output_init(&output, bits);
        fill_input(16777216);
        ok &= fm6_pcm_output_convert_block(&output, input, pcm);
        const int expected = bits == 0u ? INT16_MAX : (32768 >> bits);
        ok &= pcm[0] == expected;
    }
    check("headroom_bounds", ok);

    ok = fm6_pcm_output_init(&output, FM6_PCM_HEADROOM_DEFAULT);
    fill_input(12 * 16777216);
    ok &= fm6_pcm_output_convert_block(&output, input, pcm);
    check("fixed_headroom_twelve_units", ok && pcm[0] == 24576 &&
          output.stats.clipped_positive == 0u);

    ok = fm6_pcm_output_init(&output, 0u);
    int previous = INT16_MIN;
    for (unsigned n = 0; n <= 1024u; ++n) {
        const int64_t value = INT64_C(-2147483648) + (INT64_C(4294967295) * n) / 1024;
        fill_input((int32_t)value);
        ok &= fm6_pcm_output_convert_block(&output, input, pcm);
        ok &= pcm[0] >= previous;
        previous = pcm[0];
    }
    check("monotonic_no_wrap", ok && previous == INT16_MAX);
    ok = fm6_pcm_output_reset_stats(&output);
    const fm6_pcm_output_stats_t empty = {0};
    check("statistics_reset", ok && output.headroom_bits == 0u &&
          memcmp(&output.stats, &empty, sizeof(empty)) == 0);

    const int16_t vector[] = {0, 0x1234, -1, INT16_MIN, INT16_MAX};
    const uint8_t expected[] = {0, 0, 0x34, 0x12, 0xff, 0xff, 0, 0x80, 0xff, 0x7f};
    uint8_t bytes[sizeof(expected)];
    ok = fm6_pcm16_encode_le(vector, 5u, bytes, sizeof(bytes));
    check("little_endian_bytes", ok && memcmp(bytes, expected, sizeof(bytes)) == 0);
    memset(bytes, 0x5a, sizeof(bytes));
    ok = !fm6_pcm16_encode_le(vector, 5u, bytes, sizeof(bytes) - 1u);
    ok &= !fm6_pcm16_encode_le(NULL, 5u, bytes, sizeof(bytes));
    ok &= !fm6_pcm16_encode_le(vector, SIZE_MAX, bytes, SIZE_MAX);
    ok &= fm6_pcm16_encode_le(NULL, 0u, NULL, 0u);
    for (unsigned i = 0; i < sizeof(bytes); ++i) { ok &= bytes[i] == 0x5a; }
    check("pack_bounds_atomic", ok);
}

static void test_mix_math(void)
{
    int64_t sum[FM6_BENCH_BLOCK_FRAMES] = {0};
    bool saturated = false;
    fill_input(INT32_MAX);
    for (unsigned i = 0; i < 12u; ++i) { fm6_mix_add(sum, input); }
    bool ok = sum[0] == INT64_C(25769803764) &&
              fm6_mix_narrow(sum[0], &saturated) == INT32_MAX && saturated;
    memset(sum, 0, sizeof(sum));
    fill_input(INT32_MIN);
    for (unsigned i = 0; i < 12u; ++i) { fm6_mix_add(sum, input); }
    ok &= sum[0] == INT64_C(-25769803776) &&
          fm6_mix_narrow(sum[0], &saturated) == INT32_MIN && saturated;
    ok &= fm6_mix_narrow(INT32_MAX, &saturated) == INT32_MAX && !saturated;
    ok &= fm6_mix_narrow(INT32_MIN, &saturated) == INT32_MIN && !saturated;
    check("wide_mix_extremes", ok);

    memset(sum, 0, sizeof(sum));
    fill_input(INT32_MAX);
    for (unsigned i = 0; i < 6u; ++i) { fm6_mix_add(sum, input); }
    fill_input(-INT32_MAX);
    for (unsigned i = 0; i < 6u; ++i) { fm6_mix_add(sum, input); }
    check("wide_mix_cancellation", fm6_mix_narrow(sum[0], &saturated) == 0 && !saturated);
}

static void make_voice(bool loud, bool fixed)
{
    memset(voice, 0, sizeof(voice));
    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = voice + op * 21u;
        o[0] = o[1] = o[2] = o[3] = 99u;
        o[4] = o[5] = o[6] = 99u;
        o[7] = 0u;
        o[15] = loud ? 7u : 0u;
        o[16] = loud ? 99u : 80u;
        o[17] = fixed ? 1u : 0u;
        o[18] = fixed ? 2u : 1u;
        o[20] = 7u;
    }
    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = 99u; voice[130u + i] = 50u;
    }
    voice[134] = 31u;
    voice[136] = 1u;
    voice[144] = 24u;
    memset(voice + 145u, ' ', 10u);
}

static bool setup(bool loud, bool fixed, unsigned headroom)
{
    make_voice(loud, fixed);
    fm6_control_queue_init(&queue);
    fm6_program_bank_init(&bank);
    bool ok = fm6_program_bank_set(&bank, 0u, voice);
    ok &= fm6_control_runtime_init(&runtime, &queue);
    ok &= fm6_midi_router_init(&router, &queue, &bank, 0u, 12u);
    ok &= fm6_pcm_output_init(&output, headroom);
    return ok;
}

static bool render(unsigned *applied)
{
    return fm6_pcm_output_render_block(&output, &runtime, pcm, applied);
}

static void message(uint8_t status, uint8_t a, uint8_t b)
{
    const uint8_t data[] = {status, a, b};
    fm6_midi_router_feed(&router, data, (status & 0xf0u) == 0xc0u ? 2u : 3u);
}

static void test_coherent_mix(void)
{
    make_voice(true, false);
    bool ok = fm6_port_prepare_voice(voice, 1u, 69u, 0u, 127u);
    for (unsigned b = 0; b < COHERENT_BLOCKS; ++b) {
        fm6_port_render_block(1u, 31u, single[b]);
    }
    ok &= fm6_port_prepare_voice(voice, 12u, 69u, 0u, 127u);
    unsigned positive = 0u, negative = 0u, observed = 0u;
    for (unsigned b = 0; b < COHERENT_BLOCKS; ++b) {
        fm6_port_render_block(12u, 31u, input);
        observed += fm6_port_last_mix_saturated();
        for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
            const int64_t expected = (int64_t)single[b][i] * 12;
            positive += expected > INT32_MAX;
            negative += expected < INT32_MIN;
            const int32_t narrowed = expected > INT32_MAX ? INT32_MAX :
                                     expected < INT32_MIN ? INT32_MIN : (int32_t)expected;
            ok &= input[i] == narrowed;
        }
    }
    check("coherent_twelve_voice_reference", ok && positive > 0u && negative > 0u &&
          observed == positive + negative && fm6_port_last_sounded() == 12u);
    printf("FM6_PCM_OVERLOAD positive=%u negative=%u observed=%u frames=%u\n",
           positive, negative, observed, COHERENT_BLOCKS * FM6_BENCH_BLOCK_FRAMES);
    fm6_port_kill_all();
    ok = fm6_port_last_mix_saturated() == 0u;
    fm6_port_render_block(12u, 31u, input);
    ok &= fm6_port_last_mix_saturated() == 0u;
    ok &= fm6_port_load_program(voice, 12u);
    check("raw_saturation_diagnostics_reset", ok && fm6_port_last_mix_saturated() == 0u);
}

static void test_runtime(void)
{
    bool ok = setup(false, false, FM6_PCM_HEADROOM_DEFAULT);
    unsigned applied = 999u;
    ok &= render(&applied);
    check("runtime_initial_silence", ok && zero_pcm() && applied == 0u &&
          runtime.rendered_blocks == 1u && output.stats.frames == 32u);

    ok = setup(false, false, FM6_PCM_HEADROOM_DEFAULT);
    ok &= fm6_control_queue_submit_program(&queue, voice, 12u, NULL);
    ok &= fm6_control_queue_submit_note_on(&queue, 69u, 127u, NULL);
    applied = 999u;
    ok &= !fm6_pcm_output_render_block(&output, &runtime, NULL, &applied);
    check("runtime_invalid_no_consume", ok && applied == 999u &&
          fm6_control_queue_pending(&queue) == 2u && runtime.rendered_blocks == 0u &&
          output.stats.frames == 0u);
    ok = render(&applied);
    check("runtime_one_block_boundary", ok && applied == 2u && runtime.rendered_blocks == 1u &&
          output.stats.blocks == 1u && fm6_control_runtime_notes_down(&runtime) == 1u);

    ok = fm6_control_queue_submit_volume(&queue, 0u, NULL);
    for (unsigned b = 0; b < 5u; ++b) { ok &= render(NULL); }
    check("runtime_mute_after_ramp", ok && zero_pcm() &&
          fm6_control_runtime_notes_down(&runtime) == 1u && fm6_port_last_sounded() == 1u);
    ok = fm6_control_queue_submit_volume(&queue, 127u, NULL);
    for (unsigned b = 0; b < 5u; ++b) { ok &= render(NULL); }
    ok &= !zero_pcm();
    ok &= fm6_control_queue_submit_all_sound_off(&queue, NULL);
    ok &= render(NULL);
    check("runtime_hard_silence", ok && zero_pcm() && fm6_port_last_sounded() == 0u);

    const uint64_t old_blocks = output.stats.blocks;
    ok = fm6_control_queue_submit_program(&queue, voice, 12u, NULL);
    ok &= render(NULL);
    check("output_stats_survive_program_change", ok && zero_pcm() &&
          output.stats.blocks == old_blocks + 1u &&
          output.stats.frames == output.stats.blocks * FM6_BENCH_BLOCK_FRAMES);

    /* Fixed-mode operators give different MIDI keys identical frequencies.
     * Huge output headroom makes PCM unclipped; upstream clipping must STILL
     * be visible rather than hidden by attenuation. */
    ok = setup(true, true, FM6_PCM_HEADROOM_MAX);
    ok &= fm6_control_queue_submit_program(&queue, voice, 12u, NULL);
    for (unsigned n = 0; n < 12u; ++n) {
        ok &= fm6_control_queue_submit_note_on(&queue, (uint8_t)(48u + n), 127u, NULL);
    }
    for (unsigned b = 0; b < 64u; ++b) { ok &= render(NULL); }
    check("raw_clip_visible_when_pcm_not_clipped", ok && output.stats.mix_saturated_frames > 0u &&
          output.stats.clipped_positive == 0u && output.stats.clipped_negative == 0u &&
          output.stats.peak_output_abs > 0u && fm6_port_last_sounded() == 12u);
}

typedef struct {
    bool ok, final_silent;
    uint32_t hash, commands, routed, rejected;
    uint64_t energy, frames, mix_clips, pcm_clips;
    unsigned max_notes;
} sequence_t;

static sequence_t run_sequence(bool adapter)
{
    sequence_t r = {.ok = setup(false, false, FM6_PCM_HEADROOM_DEFAULT), .hash = 2166136261u};
    for (unsigned block = 0; block <= SEQUENCE_BLOCKS; ++block) {
        if (block == SEQUENCE_BLOCKS) {
            message(0xb0u, 120u, 0u);
        } else if ((block % 256u) == 0u) {
            message(0xc0u, 0u, 0u);
            for (unsigned n = 0; n < 12u; ++n) {
                message(0x90u, (uint8_t)(48u + n), 110u);
            }
        } else {
            if ((block % 17u) == 0u) {
                message(0xe0u, (uint8_t)(block & 127u), (uint8_t)((block / 17u) & 127u));
            }
            if ((block % 23u) == 0u) { message(0xb0u, 7u, (uint8_t)(70u + block % 58u)); }
            if ((block % 29u) == 0u) { message(0xb0u, 11u, (uint8_t)(60u + block % 68u)); }
            if ((block % 37u) == 0u) { message(0xb0u, 64u, ((block / 37u) & 1u) ? 127u : 0u); }
            if ((block % 13u) == 5u) { message(0x80u, (uint8_t)(48u + block % 18u), 0u); }
            if ((block % 11u) == 7u) { message(0x90u, (uint8_t)(48u + block % 18u), 110u); }
            if ((block % 200u) == 119u) { message(0xb0u, 121u, 0u); }
        }
        unsigned applied = 0u;
        if (adapter) {
            r.ok &= render(&applied);
        } else {
            r.ok &= fm6_control_runtime_render_block(&runtime, input, &applied);
            r.ok &= fm6_pcm_output_convert_block(&output, input, pcm);
            output.stats.mix_saturated_frames += fm6_port_last_mix_saturated();
        }
        r.commands += applied;
        const unsigned notes = fm6_control_runtime_notes_down(&runtime);
        if (notes > r.max_notes) { r.max_notes = notes; }
        uint8_t bytes[FM6_PCM_BLOCK_BYTES];
        r.ok &= fm6_pcm16_encode_le(pcm, FM6_BENCH_BLOCK_FRAMES, bytes, sizeof(bytes));
        for (unsigned i = 0; i < sizeof(bytes); ++i) {
            r.hash = (r.hash ^ bytes[i]) * 16777619u;
        }
        for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
            const int32_t sample = pcm[i];
            r.energy += (uint64_t)(sample < 0 ? -sample : sample);
        }
    }
    r.routed = router.stats.messages_routed;
    r.rejected = router.stats.queue_rejected;
    r.frames = output.stats.frames;
    r.mix_clips = output.stats.mix_saturated_frames;
    r.pcm_clips = output.stats.clipped_positive + output.stats.clipped_negative;
    r.final_silent = zero_pcm() && fm6_port_last_sounded() == 0u &&
                     fm6_control_runtime_notes_down(&runtime) == 0u &&
                     fm6_control_runtime_sustained_notes(&runtime) == 0u;
    r.ok &= runtime.rendered_blocks == SEQUENCE_BLOCKS + 1u;
    r.ok &= fm6_control_runtime_commands_applied(&runtime) == r.commands;
    return r;
}

static bool equal_sequence(sequence_t a, sequence_t b)
{
    return a.ok && b.ok && a.final_silent && b.final_silent &&
           a.hash == b.hash && a.energy == b.energy && a.commands == b.commands &&
           a.routed == b.routed && a.rejected == b.rejected && a.frames == b.frames &&
           a.mix_clips == b.mix_clips && a.pcm_clips == b.pcm_clips &&
           a.max_notes == b.max_notes;
}

static void test_sequence(void)
{
    const sequence_t a = run_sequence(true);
    const sequence_t b = run_sequence(true);
    const sequence_t reference = run_sequence(false);
    const bool good = a.ok && a.hash != 0u && a.energy > 0u && a.rejected == 0u &&
                      a.commands == a.routed && a.commands > 100u && a.max_notes == 12u &&
                      a.frames == (SEQUENCE_BLOCKS + 1u) * FM6_BENCH_BLOCK_FRAMES;
    check("pcm_sequence_determinism", good && equal_sequence(a, b));
    check("pcm_reference_pipeline_equivalence", good && equal_sequence(a, reference));
    printf("FM6_PCM_SEQUENCE: %s blocks=%u passes=2 reference=1 frames=%" PRIu64
           " commands=%" PRIu32 " routed=%" PRIu32 " rejected=%" PRIu32
           " max_notes=%u checksum=0x%08" PRIx32 " energy=%" PRIu64
           " mix_clips=%" PRIu64 " pcm_clips=%" PRIu64 " final_silent=%u\n",
           good && equal_sequence(a, b) && equal_sequence(a, reference) ? "PASS" : "FAIL",
           SEQUENCE_BLOCKS + 1u, a.frames, a.commands, a.routed, a.rejected,
           a.max_notes, a.hash, a.energy, a.mix_clips, a.pcm_clips, (unsigned)a.final_silent);
}

unsigned fm6_pcm_output_tests_run(void)
{
    tests = failures = 0u;
    test_conversion();
    test_mix_math();
    test_coherent_mix();
    test_runtime();
    test_sequence();
    printf("QEMU_FM6_PCM: %s tests=%u failures=%u voices=%u block_frames=%u headroom=%u\n",
           failures == 0u ? "PASS" : "FAIL", tests, failures,
           FM6_BENCH_MAX_VOICES, FM6_BENCH_BLOCK_FRAMES, FM6_PCM_HEADROOM_DEFAULT);
    return failures;
}
