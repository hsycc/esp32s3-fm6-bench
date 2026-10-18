#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fm6_port.h"
#include "fm6_program_queue.h"

typedef struct {
    fm6_program_queue_t *program_queue;
    unsigned voice_count;
    unsigned algorithm;
    uint32_t active_generation;
    uint32_t rendered_blocks;
    uint32_t program_commits;
} fm6_audio_runtime_t;

/* Owns the FM6 render-side program transition policy. A successful init prepares
 * the initial engine state. All subsequent program changes must enter through the
 * queue and are applied only by render_block() at the next block boundary. */
bool fm6_audio_runtime_init(fm6_audio_runtime_t *runtime,
                            fm6_program_queue_t *program_queue,
                            unsigned initial_voice_count,
                            unsigned initial_algorithm);

/* Applies at most one pending program before rendering exactly one 32-frame FM6
 * block. This makes the queue-to-engine boundary structural rather than a caller
 * convention: no render call can drain multiple queued programs. */
bool fm6_audio_runtime_render_block(fm6_audio_runtime_t *runtime,
                                    int32_t out[FM6_BENCH_BLOCK_FRAMES],
                                    bool *program_committed_out,
                                    uint32_t *generation_out);

unsigned fm6_audio_runtime_voice_count(const fm6_audio_runtime_t *runtime);
unsigned fm6_audio_runtime_algorithm(const fm6_audio_runtime_t *runtime);
uint32_t fm6_audio_runtime_active_generation(const fm6_audio_runtime_t *runtime);
uint32_t fm6_audio_runtime_rendered_blocks(const fm6_audio_runtime_t *runtime);
uint32_t fm6_audio_runtime_program_commits(const fm6_audio_runtime_t *runtime);
