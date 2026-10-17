#include "fm6_port.h"

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

/* Felucca uses this switch to decide whether the two temporary FM buses live
 * on the stack (CPU2 path) or in shared BSS. For this single-task benchmark,
 * stack-local buses are the cleaner and more re-entrant choice. */
#ifndef FELUCCA_CPU2
#define FELUCCA_CPU2 1
#endif

#include "upstream/felucca_tables.h"
#include "upstream/fm6_core.c"

_Static_assert(FS == FM6_BENCH_SAMPLE_RATE, "Felucca table sample rate mismatch");
_Static_assert(CTL == FM6_BENCH_BLOCK_FRAMES, "Felucca table block size mismatch");

static fm6_note_t s_voice[FM6_BENCH_MAX_VOICES];
static int32_t s_note_logfreq[FM6_BENCH_MAX_VOICES];
static uint8_t s_patch[FP_SIZE + 1u];
static fm6_lfo_t s_lfo;
static int32_t s_detune[6];
static volatile uint32_t s_checksum;
static unsigned s_last_sounded;

static void build_stress_patch(void)
{
    memset(s_patch, 0, sizeof(s_patch));

    for (unsigned op_index = 0; op_index < 6u; ++op_index) {
        uint8_t *op = s_patch + op_index * FP_OP;

        /* Four fast stages held at full level keep all six operators active
         * after warm-up, preventing an artificially cheap benchmark caused by
         * operators falling below Felucca's skip threshold. */
        for (unsigned stage = 0; stage < 4u; ++stage) {
            op[FP_R1 + stage] = 99;
            op[FP_L1 + stage] = 99;
        }

        op[FP_BP] = 50;
        op[FP_LD] = 0;
        op[FP_RD] = 0;
        op[FP_LC] = 0;
        op[FP_RC] = 0;
        op[FP_RS] = 7;
        op[FP_AMS] = 0;                          /* deterministic full-level stress */
        op[FP_KVS] = 0;
        op[FP_OL] = 99;
        op[FP_MODE] = 0;                         /* ratio mode */
        op[FP_FC] = (uint8_t)(1u + op_index);   /* 1x .. 6x */
        op[FP_FF] = 0;
        op[FP_DET] = 7;                         /* centre */
    }

    /* Pitch EG: centre pitch with fast rates. */
    for (unsigned i = 0; i < 4u; ++i) {
        s_patch[FP_PR1 + i] = 99;
        s_patch[FP_PL1 + i] = 50;
    }

    s_patch[FP_ALG] = 0;
    s_patch[FP_FB] = 7;
    s_patch[FP_OKS] = 1;
    s_patch[FP_LFS] = 50;
    s_patch[FP_LFD] = 0;

    /* LFO modulation is deliberately disabled in the benchmark patch. The
     * previous max-AMS test legitimately pushed every carrier below Felucca's
     * render threshold on some control blocks, which made "sounded" a poor
     * invariant. Algorithm/feedback cost is still exercised at full level. */
    s_patch[FP_LPMD] = 0;
    s_patch[FP_LAMD] = 0;
    s_patch[FP_LKS] = 0;
    s_patch[FP_LFW] = 4;
    s_patch[FP_LPMS] = 0;
    s_patch[FP_TRNSP] = 24;
}

bool fm6_port_available(void)
{
    return true;
}

const char *fm6_port_upstream_revision(void)
{
    return FELUCCA_REVISION;
}

void fm6_port_prepare(unsigned voice_count, unsigned algorithm)
{
    if (voice_count > FM6_BENCH_MAX_VOICES) {
        voice_count = FM6_BENCH_MAX_VOICES;
    }

    build_stress_patch();
    s_patch[FP_ALG] = (uint8_t)(algorithm & 31u);
    memset(s_voice, 0, sizeof(s_voice));
    memset(s_detune, 0, sizeof(s_detune));
    memset(&s_lfo, 0, sizeof(s_lfo));
    fm6_lfo_reset(&s_lfo, s_patch);
    fm6_lfo_key(&s_lfo);

    for (unsigned v = 0; v < voice_count; ++v) {
        /* Spread the chord over a useful musical range so oscillator frequency
         * paths are not all identical. */
        const int note = 36 + (int)((v * 5u) % 48u);
        fm6_note_init(&s_voice[v], s_patch, note, 127, 1);
        s_note_logfreq[v] = fm6_note_logfreq(note * 16, 0);
    }

    s_checksum = 0;
    s_last_sounded = 0;
}

void fm6_port_render_block(unsigned voice_count, unsigned algorithm, int32_t *mix_out)
{
    if (voice_count > FM6_BENCH_MAX_VOICES) {
        voice_count = FM6_BENCH_MAX_VOICES;
    }

    memset(mix_out, 0, sizeof(int32_t) * FM6_BENCH_BLOCK_FRAMES);

    const int32_t lfo_value = fm6_lfo_sample(&s_lfo);
    const int32_t lfo_delay = fm6_lfo_delay(&s_lfo);
    const uint32_t alg = algorithm & 31u;
    unsigned sounded_voices = 0;

    for (unsigned v = 0; v < voice_count; ++v) {
        int32_t voice_out[FM6_BENCH_BLOCK_FRAMES] = {0};
        const int sounded = fm6_note_compute(&s_voice[v],
                                              s_patch,
                                              voice_out,
                                              lfo_value,
                                              lfo_delay,
                                              s_note_logfreq[v],
                                              alg,
                                              7,
                                              s_detune,
                                              0);
        if (!sounded) {
            continue;
        }

        ++sounded_voices;
        for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
            mix_out[i] += voice_out[i];
        }
    }

    s_last_sounded = sounded_voices;

    /* Keep the render observable across optimization/LTO boundaries. */
    s_checksum ^= (uint32_t)mix_out[0];
    s_checksum = (s_checksum << 5) | (s_checksum >> 27);
    s_checksum ^= (uint32_t)mix_out[FM6_BENCH_BLOCK_FRAMES - 1u];
}

unsigned fm6_port_last_sounded(void)
{
    return s_last_sounded;
}

uint32_t fm6_port_checksum(void)
{
    return s_checksum;
}

#else

bool fm6_port_available(void)
{
    return false;
}

const char *fm6_port_upstream_revision(void)
{
    return FELUCCA_REVISION;
}

void fm6_port_prepare(unsigned voice_count, unsigned algorithm)
{
    (void)voice_count;
    (void)algorithm;
}

void fm6_port_render_block(unsigned voice_count, unsigned algorithm, int32_t *mix_out)
{
    (void)voice_count;
    (void)algorithm;
    memset(mix_out, 0, sizeof(int32_t) * FM6_BENCH_BLOCK_FRAMES);
}

unsigned fm6_port_last_sounded(void)
{
    return 0;
}

uint32_t fm6_port_checksum(void)
{
    return 0;
}

#endif
