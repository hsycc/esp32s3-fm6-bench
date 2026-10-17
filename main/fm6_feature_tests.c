#include "fm6_feature_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "fm6_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    uint32_t checksum;
    uint64_t energy;
    unsigned sounded;
} feature_result_t;

static fm6_port_test_config_t base_config(void)
{
    const fm6_port_test_config_t config = {
        .voice_count = 1,
        .algorithm = 31,
        .base_note = 60,
        .note_step = 0,
        .velocity = 127,
        .feedback = 0,
        .fixed_mode = 0,
        .rate_scaling = 0,
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
        .lfo_wave = 0,
    };
    return config;
}

static feature_result_t render_case(const fm6_port_test_config_t *config, unsigned blocks)
{
    int32_t mix[FM6_BENCH_BLOCK_FRAMES];
    fm6_port_prepare_test(config);
    for (unsigned i = 0; i < blocks; ++i) {
        fm6_port_render_block(config->voice_count, config->algorithm, mix);
    }

    const feature_result_t result = {
        .checksum = fm6_port_checksum(),
        .energy = fm6_port_abs_energy(),
        .sounded = fm6_port_last_sounded(),
    };
    return result;
}

static unsigned check_changed(const char *name,
                              const feature_result_t *a,
                              const feature_result_t *b,
                              bool require_both_sounding)
{
    const bool changed = a->checksum != b->checksum;
    const bool active = a->energy != 0u && b->energy != 0u;
    const bool sounding = !require_both_sounding || (a->sounded != 0u && b->sounded != 0u);
    const bool pass = changed && active && sounding;

    printf("FEATURE name=%s status=%s a=0x%08" PRIx32 " b=0x%08" PRIx32
           " energy_a=%" PRIu64 " energy_b=%" PRIu64 " sounded_a=%u sounded_b=%u\n",
           name,
           pass ? "PASS" : "FAIL",
           a->checksum,
           b->checksum,
           a->energy,
           b->energy,
           a->sounded,
           b->sounded);
    return pass ? 0u : 1u;
}

static unsigned test_deterministic_baseline(void)
{
    fm6_port_test_config_t config = base_config();
    const feature_result_t a = render_case(&config, 96u);
    const feature_result_t b = render_case(&config, 96u);
    const bool pass = a.checksum == b.checksum &&
                      a.energy == b.energy &&
                      a.energy != 0u &&
                      a.sounded == 1u &&
                      b.sounded == 1u;

    printf("FEATURE name=baseline_determinism status=%s checksum=0x%08" PRIx32
           " energy=%" PRIu64 " sounded=%u\n",
           pass ? "PASS" : "FAIL",
           a.checksum,
           a.energy,
           a.sounded);
    return pass ? 0u : 1u;
}

static unsigned test_pitch(void)
{
    fm6_port_test_config_t low = base_config();
    fm6_port_test_config_t high = low;
    low.base_note = 24;
    high.base_note = 108;
    const feature_result_t a = render_case(&low, 96u);
    const feature_result_t b = render_case(&high, 96u);
    return check_changed("pitch_low_high", &a, &b, true);
}

static unsigned test_velocity(void)
{
    fm6_port_test_config_t low = base_config();
    fm6_port_test_config_t high = low;
    low.velocity_sensitivity = 7;
    high.velocity_sensitivity = 7;
    low.velocity = 32;
    high.velocity = 127;

    const feature_result_t a = render_case(&low, 96u);
    const feature_result_t b = render_case(&high, 96u);
    const bool pass = a.checksum != b.checksum &&
                      a.energy != 0u &&
                      b.energy > a.energy &&
                      b.sounded != 0u;

    printf("FEATURE name=velocity_sensitivity status=%s low=0x%08" PRIx32
           " high=0x%08" PRIx32 " energy_low=%" PRIu64
           " energy_high=%" PRIu64 "\n",
           pass ? "PASS" : "FAIL",
           a.checksum,
           b.checksum,
           a.energy,
           b.energy);
    return pass ? 0u : 1u;
}

static unsigned test_feedback(void)
{
    fm6_port_test_config_t off = base_config();
    fm6_port_test_config_t on = off;
    off.feedback = 0;
    on.feedback = 7;
    const feature_result_t a = render_case(&off, 96u);
    const feature_result_t b = render_case(&on, 96u);
    return check_changed("feedback_0_7", &a, &b, true);
}

static unsigned test_ratio_fixed(void)
{
    fm6_port_test_config_t ratio = base_config();
    fm6_port_test_config_t fixed = ratio;
    ratio.fixed_mode = 0;
    fixed.fixed_mode = 1;
    const feature_result_t a = render_case(&ratio, 96u);
    const feature_result_t b = render_case(&fixed, 96u);
    return check_changed("ratio_fixed_frequency", &a, &b, true);
}

static unsigned test_pitch_envelope(void)
{
    fm6_port_test_config_t flat = base_config();
    fm6_port_test_config_t moving = flat;
    flat.pitch_eg_depth = 0;
    moving.pitch_eg_depth = 40;
    const feature_result_t a = render_case(&flat, 128u);
    const feature_result_t b = render_case(&moving, 128u);
    return check_changed("pitch_envelope", &a, &b, true);
}

static unsigned test_lfo_pitch(void)
{
    fm6_port_test_config_t off = base_config();
    fm6_port_test_config_t on = off;
    on.lfo_speed = 78;
    on.lfo_pmd = 90;
    on.lfo_pms = 7;
    on.lfo_wave = 0;
    const feature_result_t a = render_case(&off, 160u);
    const feature_result_t b = render_case(&on, 160u);
    return check_changed("lfo_pitch_mod", &a, &b, true);
}

static unsigned test_lfo_amplitude(void)
{
    fm6_port_test_config_t off = base_config();
    fm6_port_test_config_t on = off;
    on.lfo_speed = 73;
    on.lfo_amd = 70;
    on.lfo_ams = 3;
    on.lfo_wave = 0;
    const feature_result_t a = render_case(&off, 160u);
    const feature_result_t b = render_case(&on, 160u);
    return check_changed("lfo_amplitude_mod", &a, &b, false);
}

static unsigned test_key_scaling(void)
{
    fm6_port_test_config_t off = base_config();
    fm6_port_test_config_t on = off;
    off.base_note = 108;
    on.base_note = 108;
    on.key_scale_depth = 99;
    const feature_result_t a = render_case(&off, 96u);
    const feature_result_t b = render_case(&on, 96u);
    return check_changed("keyboard_level_scaling", &a, &b, false);
}

static unsigned test_rate_scaling(void)
{
    fm6_port_test_config_t slow = base_config();
    fm6_port_test_config_t fast = slow;
    slow.base_note = 108;
    fast.base_note = 108;
    slow.dynamic_envelope = 1;
    fast.dynamic_envelope = 1;
    slow.rate_scaling = 0;
    fast.rate_scaling = 7;
    const feature_result_t a = render_case(&slow, 128u);
    const feature_result_t b = render_case(&fast, 128u);
    return check_changed("envelope_rate_scaling", &a, &b, false);
}

static unsigned test_release(void)
{
    fm6_port_test_config_t config = base_config();
    config.dynamic_envelope = 1;
    int32_t mix[FM6_BENCH_BLOCK_FRAMES];

    fm6_port_prepare_test(&config);
    for (unsigned i = 0; i < 32u; ++i) {
        fm6_port_render_block(config.voice_count, config.algorithm, mix);
    }

    const uint32_t before = fm6_port_checksum();
    const uint64_t before_energy = fm6_port_abs_energy();
    const unsigned before_sounded = fm6_port_last_sounded();
    fm6_port_key_all(false);

    unsigned release_blocks = 0;
    for (; release_blocks < 2048u; ++release_blocks) {
        fm6_port_render_block(config.voice_count, config.algorithm, mix);
        if (fm6_port_last_sounded() == 0u) {
            ++release_blocks;
            break;
        }
    }

    const bool pass = before != 0u &&
                      before_energy != 0u &&
                      before_sounded == 1u &&
                      fm6_port_last_sounded() == 0u &&
                      release_blocks <= 2048u;

    printf("FEATURE name=note_off_release status=%s before=0x%08" PRIx32
           " release_blocks=%u final_sounded=%u\n",
           pass ? "PASS" : "FAIL",
           before,
           release_blocks,
           fm6_port_last_sounded());
    return pass ? 0u : 1u;
}

static uint32_t prng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t run_soak_pass(unsigned cases, unsigned *failures)
{
    uint32_t rng = 0x6d2b79f5u;
    uint32_t aggregate = 0;
    int32_t mix[FM6_BENCH_BLOCK_FRAMES];

    for (unsigned i = 0; i < cases; ++i) {
        const uint32_t r0 = prng_next(&rng);
        const uint32_t r1 = prng_next(&rng);
        const uint32_t r2 = prng_next(&rng);
        const uint32_t r3 = prng_next(&rng);

        fm6_port_test_config_t config = base_config();
        config.voice_count = 1u + (r0 % FM6_BENCH_MAX_VOICES);
        config.algorithm = r1 & 31u;
        config.base_note = (uint8_t)(24u + (r2 % 60u));
        config.note_step = (uint8_t)(1u + (r3 % 5u));
        config.velocity = (uint8_t)(80u + (r0 % 48u));
        config.feedback = (uint8_t)(r1 & 7u);
        config.fixed_mode = (uint8_t)((r2 >> 8) & 1u);
        config.pitch_eg_depth = (uint8_t)((r3 >> 8) % 20u);
        config.lfo_speed = (uint8_t)(30u + ((r0 >> 8) % 60u));
        config.lfo_pmd = (uint8_t)((r1 >> 8) % 40u);
        config.lfo_pms = (uint8_t)((r2 >> 16) & 7u);
        config.lfo_wave = (uint8_t)((r3 >> 16) % 6u);

        fm6_port_prepare_test(&config);
        for (unsigned block = 0; block < 24u; ++block) {
            fm6_port_render_block(config.voice_count, config.algorithm, mix);
        }

        const uint32_t signature = fm6_port_checksum();
        const uint64_t energy = fm6_port_abs_energy();
        const unsigned sounded = fm6_port_last_sounded();
        if (energy == 0u || sounded != config.voice_count) {
            ++*failures;
            printf("FEATURE_SOAK_FAIL case=%u alg=%u voices=%u sounded=%u energy=%" PRIu64
                   " checksum=0x%08" PRIx32 "\n",
                   i,
                   config.algorithm + 1u,
                   config.voice_count,
                   sounded,
                   energy,
                   signature);
        }

        aggregate = (aggregate * 16777619u) ^ signature;
        aggregate ^= (uint32_t)energy;
        aggregate ^= (uint32_t)(energy >> 32);
        taskYIELD();
    }

    return aggregate;
}

static unsigned test_deterministic_soak(void)
{
    const unsigned cases = 64u;
    unsigned failures_a = 0;
    unsigned failures_b = 0;
    const uint32_t a = run_soak_pass(cases, &failures_a);
    const uint32_t b = run_soak_pass(cases, &failures_b);
    const bool pass = failures_a == 0u && failures_b == 0u && a == b && a != 0u;

    printf("QEMU_SOAK: %s cases=%u passes=2 checksum=0x%08" PRIx32
           " failures=%u\n",
           pass ? "PASS" : "FAIL",
           cases,
           a,
           failures_a + failures_b + (a != b ? 1u : 0u));
    return pass ? 0u : 1u;
}

unsigned fm6_feature_tests_run(void)
{
    unsigned failures = 0;
    unsigned tests = 0;

#define RUN_FEATURE(fn) do { ++tests; failures += (fn)(); } while (0)
    RUN_FEATURE(test_deterministic_baseline);
    RUN_FEATURE(test_pitch);
    RUN_FEATURE(test_velocity);
    RUN_FEATURE(test_feedback);
    RUN_FEATURE(test_ratio_fixed);
    RUN_FEATURE(test_pitch_envelope);
    RUN_FEATURE(test_lfo_pitch);
    RUN_FEATURE(test_lfo_amplitude);
    RUN_FEATURE(test_key_scaling);
    RUN_FEATURE(test_rate_scaling);
    RUN_FEATURE(test_release);
    RUN_FEATURE(test_deterministic_soak);
#undef RUN_FEATURE

    printf("QEMU_FEATURE_MATRIX: %s tests=%u failures=%u\n",
           failures == 0u ? "PASS" : "FAIL",
           tests,
           failures);
    return failures;
}
