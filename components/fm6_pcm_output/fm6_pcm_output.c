#include "fm6_pcm_output.h"

#include <limits.h>
#include <string.h>

static bool valid(const fm6_pcm_output_t *output)
{
    return output != NULL && output->initialized &&
           output->headroom_bits <= FM6_PCM_HEADROOM_MAX;
}

bool fm6_pcm_output_init(fm6_pcm_output_t *output, unsigned headroom_bits)
{
    if (output == NULL || headroom_bits > FM6_PCM_HEADROOM_MAX) {
        return false;
    }
    memset(output, 0, sizeof(*output));
    output->initialized = true;
    output->headroom_bits = headroom_bits;
    return true;
}

bool fm6_pcm_output_reset_stats(fm6_pcm_output_t *output)
{
    if (!valid(output)) {
        return false;
    }
    memset(&output->stats, 0, sizeof(output->stats));
    return true;
}

bool fm6_pcm_output_convert_block(fm6_pcm_output_t *output,
                                   const int32_t in[FM6_BENCH_BLOCK_FRAMES],
                                   int16_t out[FM6_BENCH_BLOCK_FRAMES])
{
    if (!valid(output) || in == NULL || out == NULL) {
        return false;
    }
    const unsigned shift = FM6_PCM_Q24_TO_Q15_SHIFT + output->headroom_bits;
    const uint64_t half = UINT64_C(1) << (shift - 1u);
    bool clipped = false;
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        /* Widen BEFORE abs, including INT32_MIN. Only unsigned values shift. */
        const int64_t sample = in[i];
        const uint64_t magnitude = (uint64_t)(sample < 0 ? -sample : sample);
        const uint64_t rounded = (magnitude + half) >> shift;
        int64_t quantized = sample < 0 ? -(int64_t)rounded : (int64_t)rounded;
        if (quantized > INT16_MAX) {
            quantized = INT16_MAX;
            ++output->stats.clipped_positive;
            clipped = true;
        } else if (quantized < INT16_MIN) {
            quantized = INT16_MIN;
            ++output->stats.clipped_negative;
            clipped = true;
        }
        out[i] = (int16_t)quantized;
        const uint32_t peak = (uint32_t)(quantized < 0 ? -quantized : quantized);
        if (magnitude > output->stats.peak_input_abs) {
            output->stats.peak_input_abs = (uint32_t)magnitude;
        }
        if (peak > output->stats.peak_output_abs) {
            output->stats.peak_output_abs = peak;
        }
    }
    ++output->stats.blocks;
    output->stats.frames += FM6_BENCH_BLOCK_FRAMES;
    output->stats.clipped_blocks += clipped;
    return true;
}

bool fm6_pcm_output_render_block(fm6_pcm_output_t *output,
                                  fm6_control_runtime_t *runtime,
                                  int16_t out[FM6_BENCH_BLOCK_FRAMES],
                                  unsigned *commands_applied_out)
{
    if (!valid(output) || runtime == NULL || runtime->queue == NULL || out == NULL) {
        return false;
    }
    int32_t wide[FM6_BENCH_BLOCK_FRAMES];
    unsigned applied = 0u;
    if (!fm6_control_runtime_render_block(runtime, wide, &applied)) {
        return false;
    }
    if (!fm6_pcm_output_convert_block(output, wide, out)) {
        return false;
    }
    if (runtime->program_active) {
        output->stats.mix_saturated_frames += fm6_port_last_mix_saturated();
    }
    if (commands_applied_out != NULL) {
        *commands_applied_out = applied;
    }
    return true;
}

bool fm6_pcm16_encode_le(const int16_t *in, size_t frames,
                         uint8_t *out, size_t out_bytes)
{
    if (frames > SIZE_MAX / 2u || out_bytes < frames * 2u ||
        (frames != 0u && (in == NULL || out == NULL))) {
        return false;
    }
    for (size_t i = 0; i < frames; ++i) {
        const uint16_t value = (uint16_t)in[i];
        out[2u * i] = (uint8_t)(value & 0xffu);
        out[2u * i + 1u] = (uint8_t)(value >> 8u);
    }
    return true;
}
