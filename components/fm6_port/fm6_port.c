#include "fm6_port.h"
#include "fm6_mix.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FELUCCA_REVISION "5738617968a22b5d4c0d3470cfcc95559052f446"

#if defined(__has_include)
#  if __has_include("upstream/felucca_tables.h") && __has_include("upstream/fm6_core.c")
#    define FM6_UPSTREAM_PRESENT 1
#  endif
#endif

#ifndef FM6_UPSTREAM_PRESENT
#define FM6_UPSTREAM_PRESENT 0
#endif

#if FM6_UPSTREAM_PRESENT

#ifndef FELUCCA_CPU2
#define FELUCCA_CPU2 1
#endif

#include "upstream/felucca_tables.h"
#include "upstream/fm6_core.c"

_Static_assert(FS == FM6_BENCH_SAMPLE_RATE, "Felucca table sample rate mismatch");
_Static_assert(CTL == FM6_BENCH_BLOCK_FRAMES, "Felucca table block size mismatch");
_Static_assert(FP_SIZE == FM6_RUNTIME_VOICE_SIZE, "DX7 runtime voice size mismatch");

static fm6_note_t s_voice[FM6_BENCH_MAX_VOICES];
static int32_t s_note_logfreq[FM6_BENCH_MAX_VOICES];
static uint8_t s_patch[FP_SIZE + 1u];
static fm6_lfo_t s_lfo;
static int32_t s_detune[6];
static volatile uint32_t s_checksum;
static volatile uint64_t s_abs_energy;
static unsigned s_last_sounded;
static unsigned s_last_mix_saturated;
static unsigned s_prepared_voices;
static uint8_t s_feedback;
static int32_t s_pitch_bend_q24;

static uint8_t clamp_u8(unsigned value, unsigned max_value)
{
    return (uint8_t)(value > max_value ? max_value : value);
}

static uint8_t clamp_note(unsigned value)
{
    return clamp_u8(value, 127u);
}

static int32_t patch_note_logfreq(uint8_t note)
{
    const int32_t transpose = (int32_t)s_patch[FP_TRNSP] - 24;
    return fm6_note_logfreq(((int32_t)note + transpose) * 16, 0);
}

static void build_test_patch(const fm6_port_test_config_t *config)
{
    memset(s_patch, 0, sizeof(s_patch));
    const uint8_t key_scale_depth = clamp_u8(config->key_scale_depth, 99u);
    const uint8_t rate_scaling = clamp_u8(config->rate_scaling, 7u);
    const uint8_t velocity_sensitivity = clamp_u8(config->velocity_sensitivity, 7u);
    const uint8_t lfo_ams = clamp_u8(config->lfo_ams, 3u);

    for (unsigned op_index = 0; op_index < 6u; ++op_index) {
        uint8_t *op = s_patch + op_index * FP_OP;
        if (config->dynamic_envelope) {
            op[FP_R1 + 0u] = 90;
            op[FP_R1 + 1u] = 65;
            op[FP_R1 + 2u] = 55;
            op[FP_R1 + 3u] = 75;
            op[FP_L1 + 0u] = 99;
            op[FP_L1 + 1u] = 75;
            op[FP_L1 + 2u] = 55;
            op[FP_L1 + 3u] = 0;
        } else {
            for (unsigned stage = 0; stage < 4u; ++stage) {
                op[FP_R1 + stage] = 99;
                op[FP_L1 + stage] = 99;
            }
        }
        op[FP_BP] = 50;
        op[FP_LD] = key_scale_depth;
        op[FP_RD] = key_scale_depth;
        op[FP_LC] = key_scale_depth ? 1u : 0u;
        op[FP_RC] = key_scale_depth ? 1u : 0u;
        op[FP_RS] = rate_scaling;
        op[FP_AMS] = lfo_ams;
        op[FP_KVS] = velocity_sensitivity;
        op[FP_OL] = 99;
        op[FP_MODE] = config->fixed_mode ? 1u : 0u;
        op[FP_FC] = (uint8_t)(1u + op_index);
        op[FP_FF] = (uint8_t)((25u + op_index * 11u) % 100u);
        op[FP_DET] = 7;
    }
    const uint8_t pitch_depth = clamp_u8(config->pitch_eg_depth, 49u);
    for (unsigned i = 0; i < 4u; ++i) {
        s_patch[FP_PR1 + i] = pitch_depth ? 90u : 99u;
        s_patch[FP_PL1 + i] = 50u;
    }
    if (pitch_depth) {
        s_patch[FP_PL1 + 0u] = (uint8_t)(50u + pitch_depth);
        s_patch[FP_PL1 + 1u] = (uint8_t)(50u - pitch_depth);
        s_patch[FP_PL1 + 2u] = (uint8_t)(50u + pitch_depth / 2u);
        s_patch[FP_PL1 + 3u] = 50u;
    }
    s_patch[FP_ALG] = clamp_u8(config->algorithm, 31u);
    s_patch[FP_FB] = clamp_u8(config->feedback, 7u);
    s_patch[FP_OKS] = 1;
    s_patch[FP_LFS] = clamp_u8(config->lfo_speed, 99u);
    s_patch[FP_LFD] = clamp_u8(config->lfo_delay, 99u);
    s_patch[FP_LPMD] = clamp_u8(config->lfo_pmd, 99u);
    s_patch[FP_LAMD] = clamp_u8(config->lfo_amd, 99u);
    s_patch[FP_LKS] = 0;
    s_patch[FP_LFW] = clamp_u8(config->lfo_wave, 5u);
    s_patch[FP_LPMS] = clamp_u8(config->lfo_pms, 7u);
    s_patch[FP_TRNSP] = 24;
}

static bool reset_runtime_state(unsigned voice_count)
{
    if (voice_count == 0u || voice_count > FM6_BENCH_MAX_VOICES) {
        return false;
    }
    memset(s_voice, 0, sizeof(s_voice));
    memset(s_note_logfreq, 0, sizeof(s_note_logfreq));
    memset(s_detune, 0, sizeof(s_detune));
    memset(&s_lfo, 0, sizeof(s_lfo));
    fm6_lfo_reset(&s_lfo, s_patch);
    s_prepared_voices = voice_count;
    s_feedback = s_patch[FP_FB] & 7u;
    s_checksum = 0;
    s_abs_energy = 0;
    s_last_sounded = 0;
    s_last_mix_saturated = 0;
    s_pitch_bend_q24 = 0;
    return true;
}

static bool prepare_current_patch(unsigned voice_count,
                                  uint8_t base_note, uint8_t note_step,
                                  uint8_t velocity)
{
    if (!reset_runtime_state(voice_count)) {
        return false;
    }
    fm6_lfo_key(&s_lfo);
    for (unsigned v = 0; v < voice_count; ++v) {
        const uint8_t note = clamp_note((unsigned)base_note + v * (unsigned)note_step);
        const int vel = clamp_u8(velocity, 127u);
        fm6_note_init(&s_voice[v], s_patch, note, vel, 1);
        s_note_logfreq[v] = patch_note_logfreq(note);
    }
    return true;
}

bool fm6_port_available(void)
{
    return true;
}

const char *fm6_port_upstream_revision(void)
{
    return FELUCCA_REVISION;
}

void fm6_port_prepare_test(const fm6_port_test_config_t *config)
{
    if (config == NULL) {
        return;
    }
    build_test_patch(config);
    (void)prepare_current_patch(config->voice_count, config->base_note,
                                config->note_step, config->velocity);
}

void fm6_port_prepare(unsigned voice_count, unsigned algorithm)
{
    const fm6_port_test_config_t config = {
        .voice_count = voice_count,
        .algorithm = algorithm,
        .base_note = 36,
        .note_step = 5,
        .velocity = 127,
        .feedback = 7,
        .fixed_mode = 0,
        .rate_scaling = 7,
        .velocity_sensitivity = 0,
        .key_scale_depth = 0,
        .dynamic_envelope = 0,
        .pitch_eg_depth = 0,
        .lfo_speed = 50,
        .lfo_delay = 0,
        .lfo_pmd = 0,
        .lfo_amd = 0,
        .lfo_pms = 0,
        .lfo_ams = 0,
        .lfo_wave = 4,
    };
    fm6_port_prepare_test(&config);
}

bool fm6_port_load_program(const uint8_t voice[FM6_RUNTIME_VOICE_SIZE],
                           unsigned voice_capacity)
{
    if (voice == NULL || voice_capacity == 0u || voice_capacity > FM6_BENCH_MAX_VOICES) {
        return false;
    }
    memcpy(s_patch, voice, FP_SIZE);
    s_patch[FP_SIZE] = 0;
    return reset_runtime_state(voice_capacity);
}

bool fm6_port_prepare_voice(const uint8_t voice[FM6_RUNTIME_VOICE_SIZE],
                            unsigned voice_count, uint8_t base_note,
                            uint8_t note_step, uint8_t velocity)
{
    if (voice == NULL || voice_count == 0u || voice_count > FM6_BENCH_MAX_VOICES) {
        return false;
    }
    memcpy(s_patch, voice, FP_SIZE);
    s_patch[FP_SIZE] = 0;
    return prepare_current_patch(voice_count, base_note, note_step, velocity);
}

unsigned fm6_port_voice_capacity(void)
{
    return s_prepared_voices;
}

unsigned fm6_port_patch_algorithm(void)
{
    return s_patch[FP_ALG] & 31u;
}

void fm6_port_render_block(unsigned voice_count, unsigned algorithm, int32_t *mix_out)
{
    if (voice_count > s_prepared_voices) {
        voice_count = s_prepared_voices;
    }
    if (voice_count > FM6_BENCH_MAX_VOICES) {
        voice_count = FM6_BENCH_MAX_VOICES;
    }
    int64_t mix[FM6_BENCH_BLOCK_FRAMES] = {0};
    const int32_t lfo_value = fm6_lfo_sample(&s_lfo);
    const int32_t lfo_delay = fm6_lfo_delay(&s_lfo);
    const uint32_t alg = algorithm & 31u;
    unsigned sounded_voices = 0;

    for (unsigned v = 0; v < voice_count; ++v) {
        int32_t voice_out[FM6_BENCH_BLOCK_FRAMES] = {0};
        const int sounded = fm6_note_compute(&s_voice[v], s_patch, voice_out,
                                              lfo_value, lfo_delay,
                                              s_note_logfreq[v] + s_pitch_bend_q24,
                                              alg, s_feedback, s_detune, 0);
        if (!sounded) {
            continue;
        }
        ++sounded_voices;
        fm6_mix_add(mix, voice_out);
    }
    s_last_sounded = sounded_voices;
    s_last_mix_saturated = 0u;
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        bool saturated = false;
        mix_out[i] = fm6_mix_narrow(mix[i], &saturated);
        s_last_mix_saturated += saturated;
        const int64_t sample = mix_out[i];
        s_abs_energy += (uint64_t)(sample < 0 ? -sample : sample);
        s_checksum = (s_checksum * 16777619u) ^ (uint32_t)mix_out[i];
    }
}

void fm6_port_key_all(bool down)
{
    for (unsigned v = 0; v < s_prepared_voices; ++v) {
        fm6_note_key(&s_voice[v], s_patch, down ? 1 : 0);
    }
}

bool fm6_port_note_on(unsigned voice_index, uint8_t note, uint8_t velocity)
{
    if (voice_index >= s_prepared_voices) {
        return false;
    }
    note = clamp_note(note);
    velocity = clamp_u8(velocity, 127u);
    const int fresh = !s_voice[voice_index].live;
    fm6_note_init(&s_voice[voice_index], s_patch, note, velocity, fresh);
    s_note_logfreq[voice_index] = patch_note_logfreq(note);
    fm6_lfo_key(&s_lfo);
    return true;
}

bool fm6_port_note_off(unsigned voice_index)
{
    if (voice_index >= s_prepared_voices) {
        return false;
    }
    fm6_note_key(&s_voice[voice_index], s_patch, 0);
    return true;
}

bool fm6_port_set_pitch_bend(uint16_t value)
{
    if (value > 16383u) {
        return false;
    }
    /* log2(freq ratio) in Q24. No floating point, no note or envelope restart. */
    const int64_t numerator = ((int32_t)value - 8192) * INT64_C(33554432);
    s_pitch_bend_q24 = (int32_t)(numerator / 98304);
    return true;
}

int32_t fm6_port_pitch_bend_offset_q24(void)
{
    return s_pitch_bend_q24;
}

void fm6_port_kill_all(void)
{
    memset(s_voice, 0, sizeof(s_voice));
    memset(s_note_logfreq, 0, sizeof(s_note_logfreq));
    s_last_sounded = 0u;
    s_last_mix_saturated = 0u;
}

unsigned fm6_port_last_sounded(void)
{
    return s_last_sounded;
}

unsigned fm6_port_last_mix_saturated(void)
{
    return s_last_mix_saturated;
}

uint32_t fm6_port_checksum(void)
{
    return s_checksum;
}

uint64_t fm6_port_abs_energy(void)
{
    return s_abs_energy;
}

#else

bool fm6_port_available(void) { return false; }
const char *fm6_port_upstream_revision(void) { return FELUCCA_REVISION; }
void fm6_port_prepare_test(const fm6_port_test_config_t *config) { (void)config; }
void fm6_port_prepare(unsigned voice_count, unsigned algorithm)
{
    (void)voice_count;
    (void)algorithm;
}
bool fm6_port_load_program(const uint8_t voice[FM6_RUNTIME_VOICE_SIZE], unsigned voice_capacity)
{
    (void)voice;
    (void)voice_capacity;
    return false;
}
bool fm6_port_prepare_voice(const uint8_t voice[FM6_RUNTIME_VOICE_SIZE],
                            unsigned voice_count, uint8_t base_note,
                            uint8_t note_step, uint8_t velocity)
{
    (void)voice;
    (void)voice_count;
    (void)base_note;
    (void)note_step;
    (void)velocity;
    return false;
}
unsigned fm6_port_voice_capacity(void) { return 0u; }
unsigned fm6_port_patch_algorithm(void) { return 0u; }
void fm6_port_render_block(unsigned voice_count, unsigned algorithm, int32_t *mix_out)
{
    (void)voice_count;
    (void)algorithm;
    memset(mix_out, 0, sizeof(int32_t) * FM6_BENCH_BLOCK_FRAMES);
}
void fm6_port_key_all(bool down) { (void)down; }
bool fm6_port_note_on(unsigned voice_index, uint8_t note, uint8_t velocity)
{
    (void)voice_index;
    (void)note;
    (void)velocity;
    return false;
}
bool fm6_port_note_off(unsigned voice_index) { (void)voice_index; return false; }
bool fm6_port_set_pitch_bend(uint16_t value) { (void)value; return false; }
int32_t fm6_port_pitch_bend_offset_q24(void) { return 0; }
void fm6_port_kill_all(void) { }
unsigned fm6_port_last_sounded(void) { return 0u; }
unsigned fm6_port_last_mix_saturated(void) { return 0u; }
uint32_t fm6_port_checksum(void) { return 0u; }
uint64_t fm6_port_abs_energy(void) { return 0u; }

#endif
