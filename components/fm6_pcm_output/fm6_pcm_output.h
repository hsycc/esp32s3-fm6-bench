#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm6_control_runtime.h"

#define FM6_PCM_HEADROOM_DEFAULT 4u
#define FM6_PCM_HEADROOM_MAX 15u
#define FM6_PCM_Q24_TO_Q15_SHIFT 9u
#define FM6_PCM_BLOCK_BYTES (FM6_BENCH_BLOCK_FRAMES * 2u)

typedef struct {
    uint64_t blocks;
    uint64_t frames;
    uint64_t clipped_positive;
    uint64_t clipped_negative;
    uint64_t clipped_blocks;
    uint64_t mix_saturated_frames;
    uint32_t peak_input_abs;
    uint32_t peak_output_abs;
} fm6_pcm_output_stats_t;

typedef struct {
    bool initialized;
    unsigned headroom_bits;
    fm6_pcm_output_stats_t stats;
} fm6_pcm_output_t;

/* Single audio-owner object. Initialize before use; no concurrent mutation.
 * Q24 input -> signed PCM16: divide by 2^(9 + headroom_bits), round to nearest
 * (ties away from zero), then saturate. Default /16 headroom is a project policy,
 * NOT normalization by active voices or a guarantee that every patch is unclipped.
 * Keep headroom fixed while playing. Runtime CC7/CC11 already provide gain ramps.
 * Invalid calls return false without changing output/configuration/statistics.
 * Buffers must be non-overlapping and have the documented extent. */
bool fm6_pcm_output_init(fm6_pcm_output_t *output, unsigned headroom_bits);
bool fm6_pcm_output_reset_stats(fm6_pcm_output_t *output);
bool fm6_pcm_output_convert_block(fm6_pcm_output_t *output,
                                   const int32_t in[FM6_BENCH_BLOCK_FRAMES],
                                   int16_t out[FM6_BENCH_BLOCK_FRAMES]);

/* Preferred complete path: one control boundary -> one FM6 block -> MIDI gain
 * -> PCM16. No malloc, file I/O, blocking, extra voices or audio device required.
 * Underlying runtime errors return false without exposing partial PCM; already
 * applied runtime commands cannot be rolled back. Discard output on failure.
 * mix_saturated_frames is collected only by this wrapper, before MIDI gain. */
bool fm6_pcm_output_render_block(fm6_pcm_output_t *output,
                                  fm6_control_runtime_t *runtime,
                                  int16_t out[FM6_BENCH_BLOCK_FRAMES],
                                  unsigned *commands_applied_out);

/* Explicit little-endian bytes for file/test sinks. Not an I2S driver. */
bool fm6_pcm16_encode_le(const int16_t *in, size_t frames,
                         uint8_t *out, size_t out_bytes);
