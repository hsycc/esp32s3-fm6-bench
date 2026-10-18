#include "fm6_program_queue_dualcore_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_port.h"
#include "fm6_program_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DUALCORE_COMMANDS 128u
#define DUALCORE_RENDER_BLOCKS 4u
#define DUALCORE_WAIT_LOOPS 10000u
#define DUALCORE_PRODUCER_CORE 1u
#define DUALCORE_CONSUMER_CORE 0u

typedef struct {
    fm6_program_queue_t *queue;
    volatile uint32_t ready;
    volatile uint32_t start;
    volatile uint32_t done;
    volatile uint32_t producer_core;
    volatile uint32_t accepted;
    volatile uint32_t producer_errors;
    volatile uint32_t forced_backpressure;
} producer_ctx_t;

typedef struct {
    bool pass;
    uint32_t signature;
    uint32_t rejected;
    uint32_t producer_core;
    uint32_t consumer_core;
    uint32_t accepted;
    uint32_t committed;
    uint32_t active_generation;
    unsigned active_steps;
} dualcore_result_t;

static unsigned s_tests;
static unsigned s_failures;
static fm6_program_queue_t s_queue;
static producer_ctx_t s_producer;
static uint8_t s_voices[4][DX7_VCED_SIZE];

static uint32_t load_acquire(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void store_release(volatile uint32_t *value, uint32_t next)
{
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static void add_relaxed(volatile uint32_t *value, uint32_t amount)
{
    (void)__atomic_add_fetch(value, amount, __ATOMIC_RELAXED);
}

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("FM6_PROGRAM_QUEUE_DUALCORE_TEST name=%s status=%s\n",
           name, pass ? "PASS" : "FAIL");
}

static void make_voice(uint8_t voice[DX7_VCED_SIZE], unsigned variant, uint8_t algorithm)
{
    memset(voice, 0, DX7_VCED_SIZE);
    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = voice + op * 21u;
        for (unsigned stage = 0; stage < 4u; ++stage) {
            o[stage] = 99u;
            o[4u + stage] = 99u;
        }
        o[8] = (uint8_t)(35u + ((variant + op * 7u) % 50u));
        o[9] = (uint8_t)((variant + op) % 25u);
        o[10] = (uint8_t)((variant + op * 3u) % 25u);
        o[11] = (uint8_t)(op & 3u);
        o[12] = (uint8_t)((op + 1u) & 3u);
        o[13] = (uint8_t)((variant + op) & 7u);
        o[14] = (uint8_t)(op & 3u);
        o[15] = (uint8_t)((variant + op * 2u) & 7u);
        o[16] = 99u;
        o[17] = (uint8_t)((variant + op) & 1u);
        o[18] = (uint8_t)(1u + ((variant + op) % 10u));
        o[19] = (uint8_t)((variant * 13u + op * 9u) % 100u);
        o[20] = (uint8_t)(4u + ((variant + op) % 7u));
    }

    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = 99u;
        voice[130u + i] = 50u;
    }
    voice[DX7_VCED_ALGORITHM_OFFSET] = algorithm;
    voice[DX7_VCED_FEEDBACK_OFFSET] = (uint8_t)(variant & 7u);
    voice[136] = 1u;
    voice[137] = (uint8_t)(35u + variant % 50u);
    voice[138] = (uint8_t)(variant % 20u);
    voice[139] = (uint8_t)(variant % 30u);
    voice[140] = (uint8_t)(variant % 20u);
    voice[141] = 1u;
    voice[142] = (uint8_t)(variant % 6u);
    voice[143] = (uint8_t)(variant % 8u);
    voice[DX7_VCED_TRANSPOSE_OFFSET] = 24u;

    char name[11];
    snprintf(name, sizeof(name), "DC%02uFM6XYZ", variant % 100u);
    memcpy(voice + DX7_VCED_NAME_OFFSET, name, DX7_VCED_NAME_SIZE);
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voices[0], 3u, 2u);
    make_voice(s_voices[1], 9u, 11u);
    make_voice(s_voices[2], 17u, 20u);
    make_voice(s_voices[3], 25u, 31u);
}

static const uint8_t *voice_for(unsigned command_index)
{
    return s_voices[command_index & 3u];
}

static uint8_t voice_count_for(unsigned command_index)
{
    static const uint8_t counts[4] = {12u, 10u, 8u, 6u};
    return counts[command_index & 3u];
}

static uint8_t expected_algorithm_for(unsigned command_index)
{
    static const uint8_t algorithms[4] = {2u, 11u, 20u, 31u};
    return algorithms[command_index & 3u];
}

static bool submit_command(producer_ctx_t *ctx, unsigned command_index)
{
    uint32_t generation = 0u;
    const bool accepted = fm6_program_queue_submit(
        ctx->queue,
        voice_for(command_index),
        voice_count_for(command_index),
        (uint8_t)(36u + (command_index % 12u)),
        (uint8_t)(1u + (command_index % 3u)),
        (uint8_t)(90u + (command_index % 38u)),
        &generation);
    if (accepted) {
        if (generation != command_index + 1u) {
            add_relaxed(&ctx->producer_errors, 1u);
        }
        add_relaxed(&ctx->accepted, 1u);
    }
    return accepted;
}

static void producer_task(void *arg)
{
    producer_ctx_t *ctx = (producer_ctx_t *)arg;
    store_release(&ctx->producer_core, (uint32_t)xPortGetCoreID());

    for (unsigned i = 0; i < FM6_PROGRAM_QUEUE_CAPACITY; ++i) {
        if (!submit_command(ctx, i)) {
            add_relaxed(&ctx->producer_errors, 1u);
        }
    }

    /* The consumer is still waiting for ready, so command 5 must observe a full
     * queue and be rejected. This makes backpressure coverage deterministic. */
    if (!submit_command(ctx, FM6_PROGRAM_QUEUE_CAPACITY)) {
        store_release(&ctx->forced_backpressure, 1u);
    } else {
        add_relaxed(&ctx->producer_errors, 1u);
    }

    store_release(&ctx->ready, 1u);
    while (load_acquire(&ctx->start) == 0u) {
        taskYIELD();
    }

    for (unsigned i = FM6_PROGRAM_QUEUE_CAPACITY; i < DUALCORE_COMMANDS; ++i) {
        unsigned guard = 0u;
        while (!submit_command(ctx, i)) {
            if (++guard >= DUALCORE_WAIT_LOOPS) {
                add_relaxed(&ctx->producer_errors, 1u);
                store_release(&ctx->done, 1u);
                vTaskDelete(NULL);
                return;
            }
            taskYIELD();
        }
    }

    store_release(&ctx->done, 1u);
    vTaskDelete(NULL);
}

static uint32_t mix_signature(uint32_t signature,
                              uint32_t checksum,
                              uint64_t energy,
                              unsigned sounded,
                              uint32_t generation,
                              unsigned algorithm)
{
    signature ^= checksum;
    signature *= 16777619u;
    signature ^= (uint32_t)energy;
    signature *= 16777619u;
    signature ^= (uint32_t)(energy >> 32);
    signature *= 16777619u;
    signature ^= sounded;
    signature *= 16777619u;
    signature ^= generation;
    signature *= 16777619u;
    signature ^= algorithm;
    signature *= 16777619u;
    return signature;
}

static bool wait_flag(const volatile uint32_t *flag)
{
    for (unsigned i = 0; i < DUALCORE_WAIT_LOOPS; ++i) {
        if (load_acquire(flag) != 0u) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

static dualcore_result_t run_dualcore_sequence(void)
{
    dualcore_result_t result = {0};
    fm6_program_queue_init(&s_queue);
    memset(&s_producer, 0, sizeof(s_producer));
    s_producer.queue = &s_queue;
    result.consumer_core = (uint32_t)xPortGetCoreID();

    BaseType_t created = xTaskCreatePinnedToCore(
        producer_task,
        "fm6_pq_prod",
        4096,
        &s_producer,
        18,
        NULL,
        DUALCORE_PRODUCER_CORE);
    if (created != pdPASS) {
        return result;
    }

    if (!wait_flag(&s_producer.ready)) {
        return result;
    }

    const bool precondition = fm6_program_queue_pending(&s_queue) == FM6_PROGRAM_QUEUE_CAPACITY &&
                              fm6_program_queue_rejected(&s_queue) >= 1u &&
                              load_acquire(&s_producer.forced_backpressure) == 1u &&
                              load_acquire(&s_producer.accepted) == FM6_PROGRAM_QUEUE_CAPACITY;
    if (!precondition) {
        store_release(&s_producer.start, 1u);
        (void)wait_flag(&s_producer.done);
        return result;
    }

    store_release(&s_producer.start, 1u);

    uint32_t signature = 2166136261u;
    unsigned committed = 0u;
    unsigned active_steps = 0u;
    unsigned idle_guard = 0u;
    while (committed < DUALCORE_COMMANDS) {
        uint32_t generation = 0u;
        if (!fm6_program_queue_commit_next(&s_queue, &generation)) {
            if (load_acquire(&s_producer.done) != 0u &&
                fm6_program_queue_pending(&s_queue) == 0u) {
                break;
            }
            if (++idle_guard >= DUALCORE_WAIT_LOOPS) {
                break;
            }
            taskYIELD();
            continue;
        }
        idle_guard = 0u;

        const unsigned command_index = committed;
        const unsigned expected_algorithm = expected_algorithm_for(command_index);
        const unsigned voice_count = voice_count_for(command_index);
        if (generation != command_index + 1u ||
            fm6_program_queue_active_generation(&s_queue) != generation ||
            fm6_port_patch_algorithm() != expected_algorithm) {
            break;
        }

        int32_t mix[FM6_BENCH_BLOCK_FRAMES];
        unsigned peak_sounded = 0u;
        for (unsigned block = 0; block < DUALCORE_RENDER_BLOCKS; ++block) {
            fm6_port_render_block(voice_count, expected_algorithm, mix);
            const unsigned sounded = fm6_port_last_sounded();
            if (sounded > peak_sounded) {
                peak_sounded = sounded;
            }
        }
        if (fm6_port_abs_energy() != 0u || peak_sounded != 0u) {
            ++active_steps;
        }
        signature = mix_signature(signature,
                                  fm6_port_checksum(),
                                  fm6_port_abs_energy(),
                                  peak_sounded,
                                  generation,
                                  expected_algorithm);
        ++committed;
    }

    const bool producer_done = wait_flag(&s_producer.done);
    result.signature = signature;
    result.rejected = fm6_program_queue_rejected(&s_queue);
    result.producer_core = load_acquire(&s_producer.producer_core);
    result.accepted = load_acquire(&s_producer.accepted);
    result.committed = fm6_program_queue_committed(&s_queue);
    result.active_generation = fm6_program_queue_active_generation(&s_queue);
    result.active_steps = active_steps;

    result.pass = producer_done &&
                  load_acquire(&s_producer.producer_errors) == 0u &&
                  load_acquire(&s_producer.forced_backpressure) == 1u &&
                  result.consumer_core == DUALCORE_CONSUMER_CORE &&
                  result.producer_core == DUALCORE_PRODUCER_CORE &&
                  result.accepted == DUALCORE_COMMANDS &&
                  committed == DUALCORE_COMMANDS &&
                  result.committed == DUALCORE_COMMANDS &&
                  result.active_generation == DUALCORE_COMMANDS &&
                  fm6_program_queue_pending(&s_queue) == 0u &&
                  result.rejected >= 1u &&
                  result.active_steps > 0u &&
                  result.signature != 0u;
    return result;
}

static void test_dualcore_spsc_stress(void)
{
    const dualcore_result_t a = run_dualcore_sequence();
    const dualcore_result_t b = run_dualcore_sequence();
    const bool deterministic = a.pass && b.pass &&
                               a.signature == b.signature &&
                               a.active_steps == b.active_steps &&
                               a.producer_core == b.producer_core &&
                               a.consumer_core == b.consumer_core;
    check("dualcore_spsc_ordering_backpressure", deterministic);

    printf("FM6_PROGRAM_QUEUE_DUALCORE: %s commands=%u passes=2 producer_core=%" PRIu32
           " consumer_core=%" PRIu32 " accepted=%" PRIu32 " committed=%" PRIu32
           " rejected_a=%" PRIu32 " rejected_b=%" PRIu32 " active_steps=%u signature=0x%08" PRIx32 "\n",
           deterministic ? "PASS" : "FAIL",
           (unsigned)DUALCORE_COMMANDS,
           a.producer_core,
           a.consumer_core,
           a.accepted,
           a.committed,
           a.rejected,
           b.rejected,
           a.active_steps,
           a.signature);
}

unsigned fm6_program_queue_dualcore_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    build_fixtures();

    test_dualcore_spsc_stress();

    printf("QEMU_FM6_PROGRAM_QUEUE_DUALCORE: %s tests=%u failures=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures);
    return s_failures;
}
