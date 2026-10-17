#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FM6_BENCH_SAMPLE_RATE 44100u
#define FM6_BENCH_BLOCK_FRAMES 32u
#define FM6_BENCH_MAX_VOICES 12u

bool fm6_port_available(void);
const char *fm6_port_upstream_revision(void);

void fm6_port_prepare(unsigned voice_count, unsigned algorithm);
void fm6_port_render_block(unsigned voice_count, unsigned algorithm, int32_t *mix_out);
unsigned fm6_port_last_sounded(void);
uint32_t fm6_port_checksum(void);
