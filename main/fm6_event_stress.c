#include "fm6_event_stress.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "fm6_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EVENT_STRESS_BLOCKS 4096u
#define EVENT_INTERVAL_BLOCKS 3u
#define EVENT_RELEASE_LIMIT 2048u

typedef struct {
    uint32_t checksum;
    uint64_t energy;
    unsigned note_on_events;
    unsigned note_off_events;
    unsigned retrigger_events;
    unsigned api_failures;
    unsigned drain_blocks;
    unsigned final_sounded;
} event_stress_result_t;

static uint32_t prng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static fm6_port_test_config_t event_config(void)
{
    const fm6_port_test_config_t config = {
        .voice_count = FM6_BENCH_MAX_VOICES,
        .algorithm = 6,
        .base_note = 36,
        .note_step = 4,
        .velocity = 110,
        .feedback = 7,
        .fixed_mode = 0,
        .rate_scaling = 4,
        .velocity_sensitivity = 5,
        .key_scale_depth = 35,
        .dynamic_envelope = 1,
        .pitch_eg_depth = 18,
        .lfo_speed = 58,
        .lfo_delay = 12,
        .lfo_pmd = 24,
        .lfo_amd = 12,
        .lfo_pms = 3,
        .lfo_ams = 1,
        .lfo_wave = 0,
    };
    return config;
}

static event_stress_result_t run_event_pass(void)
{
    const fm6_port_test_config_t config = event_config();
    bool key_down[FM6_BENCH_MAX_VOICES];
    int32_t mix[FM6_BENCH_BLOCK_FRAMES];
    uint32_t rng = 0x13579bdfu;
    event_stress_result_t result = {0};

    fm6_port_prepare_test(&config);
    for (unsigned v = 0; v < FM6_BENCH_MAX_VOICES; ++v) {
        key_down[v] = true;
    }

    for (unsigned block = 0; block < EVENT_STRESS_BLOCKS; ++block) {
        if ((block % EVENT_INTERVAL_BLOCKS) == 0u) {
            const uint32_t r = prng_next(&rng);
            const unsigned voice = r % FM6_BENCH_MAX_VOICES;
            const unsigned action = (r >> 8) % 3u;

            if (action == 0u) {
                if (!fm6_port_note_off(voice)) {
                    ++result.api_failures;
                } else {
                    key_down[voice] = false;
                    ++result.note_off_events;
                }
            } else {
                const uint8_t note = (uint8_t)(24u + ((r >> 12) % 72u));
                const uint8_t velocity = (uint8_t)(24u + ((r >> 20) % 104u));
                if (!fm6_port_note_on(voice, note, velocity)) {
                    ++result.api_failures;
                } else {
                    if (key_down[voice]) {
                        ++result.retrigger_events;
                    }
                    key_down[voice] = true;
                    ++result.note_on_events;
                }
            }
        }

        fm6_port_render_block(FM6_BENCH_MAX_VOICES, config.algorithm, mix);
        if ((block & 127u) == 0u) {
            taskYIELD();
        }
    }

    fm6_port_key_all(false);
    for (unsigned i = 0; i < EVENT_RELEASE_LIMIT; ++i) {
        fm6_port_render_block(FM6_BENCH_MAX_VOICES, config.algorithm, mix);
        result.drain_blocks = i + 1u;
        if (fm6_port_last_sounded() == 0u) {
            break;
        }
    }

    result.checksum = fm6_port_checksum();
    result.energy = fm6_port_abs_energy();
    result.final_sounded = fm6_port_last_sounded();
    return result;
}

static bool same_result(const event_stress_result_t *a, const event_stress_result_t *b)
{
    return a->checksum == b->checksum &&
           a->energy == b->energy &&
           a->note_on_events == b->note_on_events &&
           a->note_off_events == b->note_off_events &&
           a->retrigger_events == b->retrigger_events &&
           a->api_failures == b->api_failures &&
           a->drain_blocks == b->drain_blocks &&
           a->final_sounded == b->final_sounded;
}

unsigned fm6_event_stress_run(void)
{
    const event_stress_result_t a = run_event_pass();
    const event_stress_result_t b = run_event_pass();

    const bool activity_ok = a.energy != 0u &&
                             a.checksum != 0u &&
                             a.note_on_events != 0u &&
                             a.note_off_events != 0u &&
                             a.retrigger_events != 0u;
    const bool lifecycle_ok = a.api_failures == 0u &&
                              a.final_sounded == 0u &&
                              a.drain_blocks > 0u &&
                              a.drain_blocks <= EVENT_RELEASE_LIMIT;
    const bool deterministic = same_result(&a, &b);
    const bool pass = activity_ok && lifecycle_ok && deterministic;

    printf("QEMU_EVENT_STRESS: %s blocks=%u passes=2 interval=%u note_on=%u note_off=%u retrigger=%u "
           "drain_blocks=%u final_sounded=%u checksum=0x%08" PRIx32 " energy=%" PRIu64
           " api_failures=%u deterministic=%u\n",
           pass ? "PASS" : "FAIL",
           EVENT_STRESS_BLOCKS,
           EVENT_INTERVAL_BLOCKS,
           a.note_on_events,
           a.note_off_events,
           a.retrigger_events,
           a.drain_blocks,
           a.final_sounded,
           a.checksum,
           a.energy,
           a.api_failures,
           deterministic ? 1u : 0u);

    if (!pass) {
        printf("EVENT_STRESS_DETAIL second checksum=0x%08" PRIx32 " energy=%" PRIu64
               " note_on=%u note_off=%u retrigger=%u drain=%u final=%u api_failures=%u\n",
               b.checksum,
               b.energy,
               b.note_on_events,
               b.note_off_events,
               b.retrigger_events,
               b.drain_blocks,
               b.final_sounded,
               b.api_failures);
    }

    return pass ? 0u : 1u;
}
