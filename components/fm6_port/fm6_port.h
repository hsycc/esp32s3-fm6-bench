#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FM6_BENCH_SAMPLE_RATE 44100u
#define FM6_BENCH_BLOCK_FRAMES 32u
#define FM6_BENCH_MAX_VOICES 12u

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
void fm6_port_render_block(unsigned voice_count, unsigned algorithm, int32_t *mix_out);
void fm6_port_key_all(bool down);
bool fm6_port_note_on(unsigned voice_index, uint8_t note, uint8_t velocity);
bool fm6_port_note_off(unsigned voice_index);
unsigned fm6_port_last_sounded(void);
uint32_t fm6_port_checksum(void);
uint64_t fm6_port_abs_energy(void);
