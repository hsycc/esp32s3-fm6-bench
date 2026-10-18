#include "fm6_audio_runtime.h"

#include <stddef.h>
#include <string.h>

bool fm6_audio_runtime_init(fm6_audio_runtime_t *runtime,
                            fm6_program_queue_t *program_queue,
                            unsigned initial_voice_count,
                            unsigned initial_algorithm)
{
    if (runtime == NULL || program_queue == NULL ||
        initial_voice_count == 0u || initial_voice_count > FM6_BENCH_MAX_VOICES ||
        initial_algorithm >= 32u || !fm6_port_available()) {
        return false;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->program_queue = program_queue;
    runtime->voice_count = initial_voice_count;
    runtime->algorithm = initial_algorithm;
    fm6_port_prepare(initial_voice_count, initial_algorithm);
    return true;
}

bool fm6_audio_runtime_render_block(fm6_audio_runtime_t *runtime,
                                    int32_t out[FM6_BENCH_BLOCK_FRAMES],
                                    bool *program_committed_out,
                                    uint32_t *generation_out)
{
    if (runtime == NULL || runtime->program_queue == NULL || out == NULL ||
        runtime->voice_count == 0u || runtime->voice_count > FM6_BENCH_MAX_VOICES ||
        runtime->algorithm >= 32u) {
        return false;
    }

    if (program_committed_out != NULL) {
        *program_committed_out = false;
    }
    if (generation_out != NULL) {
        *generation_out = 0u;
    }

    const unsigned pending_before = fm6_program_queue_pending(runtime->program_queue);
    if (pending_before != 0u) {
        fm6_program_applied_t applied = {0};
        if (!fm6_program_queue_commit_next_ex(runtime->program_queue, &applied)) {
            return false;
        }

        runtime->voice_count = applied.voice_count;
        runtime->algorithm = fm6_port_patch_algorithm();
        runtime->active_generation = applied.generation;
        ++runtime->program_commits;

        if (program_committed_out != NULL) {
            *program_committed_out = true;
        }
        if (generation_out != NULL) {
            *generation_out = applied.generation;
        }
    }

    fm6_port_render_block(runtime->voice_count, runtime->algorithm, out);
    ++runtime->rendered_blocks;
    return true;
}

unsigned fm6_audio_runtime_voice_count(const fm6_audio_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->voice_count;
}

unsigned fm6_audio_runtime_algorithm(const fm6_audio_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->algorithm;
}

uint32_t fm6_audio_runtime_active_generation(const fm6_audio_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->active_generation;
}

uint32_t fm6_audio_runtime_rendered_blocks(const fm6_audio_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->rendered_blocks;
}

uint32_t fm6_audio_runtime_program_commits(const fm6_audio_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->program_commits;
}
