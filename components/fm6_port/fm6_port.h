#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FM6_BENCH_SAMPLE_RATE 44100u
#define FM6_BENCH_BLOCK_FRAMES 32u
#define FM6_BENCH_MAX_VOICES 12u
#define FM6_RUNTIME_VOICE_SIZE 155u

typedef struct {
    unsigned voice_count;
    unsigned algorithm;
    uint8_t base_note;
    uint8_t note_step;
    uint8_t velocity;
    uint8_t feedback;
    uint8_t fixed_mode;
    uint8_t rate_scaling;
    uint8_t velocity_sensitivity;
    uint8_t key_scale_depth;
    uint8_t dynamic_envelope;
    uint8_t pitch_eg_depth;
    uint8_t lfo_speed;
    uint8_t lfo_delay;
    uint8_t lfo_pmd;
    uint8_t lfo_amd;
    uint8_t lfo_pms;
    uint8_t lfo_ams;
    uint8_t lfo_wave;
} fm6_port_test_config_t;

bool fm6_port_available(void);
const char *fm6_port_upstream_revision(void);
void fm6_port_prepare(unsigned voice_count, unsigned algorithm);
void fm6_port_prepare_test(const fm6_port_test_config_t *config);

/* Benchmark helper pre-fills voices. Product load_program starts silent. */
bool fm6_port_prepare_voice(const uint8_t voice[FM6_RUNTIME_VOICE_SIZE],
                            unsigned voice_count,
                            uint8_t base_note,
                            uint8_t note_step,
                            uint8_t velocity);
bool fm6_port_load_program(const uint8_t voice[FM6_RUNTIME_VOICE_SIZE],
                           unsigned voice_capacity);

unsigned fm6_port_voice_capacity(void);
unsigned fm6_port_patch_algorithm(void);
/* Q24 PCM. Accumulate voices in int64, then saturate once to int32. */
void fm6_port_render_block(unsigned voice_count, unsigned algorithm, int32_t *mix_out);
void fm6_port_key_all(bool down);
bool fm6_port_note_on(unsigned voice_index, uint8_t note, uint8_t velocity);
bool fm6_port_note_off(unsigned voice_index);

/* Audio-owner-only controls. 14-bit bend uses +/-2 semitones, no EG/phase reset.
 * It offsets ratio operators; the upstream fixed-frequency operators stay fixed.
 * Loading/preparing a program resets bend to center; control_runtime restores it.
 * 16383 is one wheel step short of the mathematical +2-semitone endpoint. */
bool fm6_port_set_pitch_bend(uint16_t value);
int32_t fm6_port_pitch_bend_offset_q24(void);

/* CC120: hard-kill voices without changing patch, bend or reserved capacity. */
void fm6_port_kill_all(void);
unsigned fm6_port_last_sounded(void);
/* Saturated frames in the most recent raw block (0..32), before MIDI gain.
 * Program load and kill_all clear this diagnostic. Audio-owner access only. */
unsigned fm6_port_last_mix_saturated(void);
/* Raw engine diagnostics, BEFORE control_runtime's volume/expression gain.
 * Output-chain tests must hash the returned runtime PCM, not these counters. */
uint32_t fm6_port_checksum(void);
uint64_t fm6_port_abs_energy(void);
