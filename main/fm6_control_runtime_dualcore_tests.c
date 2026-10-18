#include "fm6_control_runtime_dualcore_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_control_runtime.h"
#include "fm6_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CONTROL_DUALCORE_COMMANDS 256u
#define CONTROL_DUALCORE_WAIT_LOOPS 12000u
#define CONTROL_DUALCORE_PRODUCER_CORE 1u
#define CONTROL_DUALCORE_CONSUMER_CORE 0u

typedef struct {
    fm6_control_queue_t *queue;
    volatile uint32_t ready;
    volatile uint32_t start;
    volatile uint32_t cancel;
    volatile uint32_t done;
    volatile uint32_t producer_core;
    volatile uint32_t accepted;
    volatile uint32_t producer_errors;
    volatile uint32_t forced_backpressure;
} control_producer_ctx_t;

typedef struct {
    bool pass;
    uint32_t producer_core;
    uint32_t consumer_core;
    uint32_t accepted;
    uint32_t applied;
    uint32_t rejected;
    uint32_t programs;
    uint32_t active_program_sequence;
    uint32_t blocks;
    uint32_t checksum;
    uint64_t energy;
    unsigned notes_down;
    unsigned algorithm;
} control_dualcore_result_t;

static unsigned s_tests;
static unsigned s_failures;
static fm6_control_queue_t s_queue;
static fm6_control_runtime_t s_runtime;
static control_producer_ctx_t s_producer;
static uint8_t s_voice_a[DX7_VCED_SIZE];
static uint8_t s_voice_b[DX7_VCED_SIZE];

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
    printf("FM6_CONTROL_RUNTIME_DUALCORE_TEST name=%s status=%s\n",
           name, pass ? "PASS" : "FAIL");
}

static void make_voice(uint8_t voice[DX7_VCED_SIZE],
                       unsigned variant,
                       uint8_t algorithm,
                       const char *name)
{
    memset(voice, 0, DX7_VCED_SIZE);
    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = voice + op * 21u;
        for (unsigned stage = 0; stage < 4u; ++stage) {
            o[stage] = 99u;
            o[4u + stage] = stage == 3u ? 0u : 99u;
        }
        o[8] = 50u;
        o[9] = (uint8_t)((variant + op) % 20u);
        o[10] = (uint8_t)((variant + op * 3u) % 20u);
        o[11] = 1u;
        o[12] = 1u;
        o[13] = (uint8_t)((variant + op) & 7u);
        o[14] = 0u;
        o[15] = (uint8_t)((variant + op * 2u) & 7u);
        o[16] = 99u;
        o[17] = 0u;
        o[18] = (uint8_t)(1u + ((variant + op) % 10u));
        o[19] = (uint8_t)((variant * 11u + op * 9u) % 100u);
        o[20] = 7u;
    }
    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = 99u;
        voice[130u + i] = 50u;
    }
    voice[DX7_VCED_ALGORITHM_OFFSET] = algorithm;
    voice[DX7_VCED_FEEDBACK_OFFSET] = (uint8_t)(variant & 7u);
    voice[136] = 1u;
    voice[137] = 55u;
    voice[138] = 8u;
    voice[139] = 14u;
    voice[140] = 5u;
    voice[141] = 1u;
    voice[142] = 0u;
    voice[143] = 2u;
    voice[DX7_VCED_TRANSPOSE_OFFSET] = 24u;

    uint8_t patch_name[DX7_VCED_NAME_SIZE];
    memset(patch_name, ' ', sizeof(patch_name));
    const size_t name_len = strlen(name);
    const size_t copy_len = name_len < sizeof(patch_name) ? name_len : sizeof(patch_name);
    memcpy(patch_name, name, copy_len);
    memcpy(voice + DX7_VCED_NAME_OFFSET, patch_name, sizeof(patch_name));
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voice_a, 7u, 5u, "DC-CTRL-A");
    make_voice(s_voice_b, 19u, 21u, "DC-CTRL-B");
}

static bool submit_index(control_producer_ctx_t *ctx, unsigned index)
{
    uint32_t sequence = 0u;
    bool accepted = false;

    if ((index % 32u) == 0u) {
        const bool use_b = ((index / 32u) & 1u) != 0u;
        accepted = fm6_control_queue_submit_program(
            ctx->queue,
            use_b ? s_voice_b : s_voice_a,
            12u,
            &sequence);
    } else if ((index % 13u) == 0u) {
        accepted = fm6_control_queue_submit_all_notes_off(ctx->queue, &sequence);
    } else if ((index & 1u) != 0u) {
        accepted = fm6_control_queue_submit_note_on(
            ctx->queue,
            (uint8_t)(36u + (index % 24u)),
            (uint8_t)(80u + (index % 40u)),
            &sequence);
    } else {
        accepted = fm6_control_queue_submit_note_off(
            ctx->queue,
            (uint8_t)(36u + (index % 24u)),
            &sequence);
    }

    if (accepted) {
        if (sequence != index + 1u) {
            add_relaxed(&ctx->producer_errors, 1u);
        }
        add_relaxed(&ctx->accepted, 1u);
    }
    return accepted;
}

static void producer_finish(control_producer_ctx_t *ctx)
{
    store_release(&ctx->done, 1u);
    vTaskDelete(NULL);
}

static void producer_task(void *arg)
{
    control_producer_ctx_t *ctx = (control_producer_ctx_t *)arg;
    store_release(&ctx->producer_core, (uint32_t)xPortGetCoreID());

    for (unsigned i = 0; i < FM6_CONTROL_QUEUE_CAPACITY; ++i) {
        if (!submit_index(ctx, i)) {
            add_relaxed(&ctx->producer_errors, 1u);
        }
    }

    if (!submit_index(ctx, FM6_CONTROL_QUEUE_CAPACITY)) {
        store_release(&ctx->forced_backpressure, 1u);
    } else {
        add_relaxed(&ctx->producer_errors, 1u);
    }

    store_release(&ctx->ready, 1u);
    while (load_acquire(&ctx->start) == 0u) {
        if (load_acquire(&ctx->cancel) != 0u) {
            producer_finish(ctx);
            return;
        }
        taskYIELD();
    }

    for (unsigned i = FM6_CONTROL_QUEUE_CAPACITY; i < CONTROL_DUALCORE_COMMANDS; ++i) {
        unsigned guard = 0u;
        while (!submit_index(ctx, i)) {
            if (load_acquire(&ctx->cancel) != 0u) {
                producer_finish(ctx);
                return;
            }
            if (++guard >= CONTROL_DUALCORE_WAIT_LOOPS) {
                add_relaxed(&ctx->producer_errors, 1u);
                producer_finish(ctx);
                return;
            }
            taskYIELD();
        }
    }

    producer_finish(ctx);
}

static bool wait_flag(const volatile uint32_t *flag)
{
    for (unsigned i = 0; i < CONTROL_DUALCORE_WAIT_LOOPS; ++i) {
        if (load_acquire(flag) != 0u) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

static void cancel_producer(void)
{
    store_release(&s_producer.cancel, 1u);
    store_release(&s_producer.start, 1u);
    (void)wait_flag(&s_producer.done);
}

static control_dualcore_result_t run_dualcore(void)
{
    control_dualcore_result_t result = {0};
    fm6_control_queue_init(&s_queue);
    if (!fm6_control_runtime_init(&s_runtime, &s_queue)) {
        return result;
    }
    memset(&s_producer, 0, sizeof(s_producer));
    s_producer.queue = &s_queue;
    result.consumer_core = (uint32_t)xPortGetCoreID();

    const BaseType_t created = xTaskCreatePinnedToCore(
        producer_task,
        "fm6_ctrl_prod",
        4096,
        &s_producer,
        18,
        NULL,
        CONTROL_DUALCORE_PRODUCER_CORE);
    if (created != pdPASS) {
        return result;
    }

    if (!wait_flag(&s_producer.ready)) {
        cancel_producer();
        return result;
    }

    const bool precondition =
        fm6_control_queue_pending(&s_queue) == FM6_CONTROL_QUEUE_CAPACITY &&
        fm6_control_queue_rejected(&s_queue) >= 1u &&
        load_acquire(&s_producer.forced_backpressure) == 1u &&
        load_acquire(&s_producer.accepted) == FM6_CONTROL_QUEUE_CAPACITY;
    if (!precondition) {
        cancel_producer();
        return result;
    }

    store_release(&s_producer.start, 1u);

    unsigned idle_guard = 0u;
    while (fm6_control_runtime_commands_applied(&s_runtime) < CONTROL_DUALCORE_COMMANDS) {
        int32_t out[FM6_BENCH_BLOCK_FRAMES];
        unsigned applied = 0u;
        if (!fm6_control_runtime_render_block(&s_runtime, out, &applied)) {
            break;
        }
        if (applied == 0u) {
            if (load_acquire(&s_producer.done) != 0u &&
                fm6_control_queue_pending(&s_queue) == 0u) {
                break;
            }
            if (++idle_guard >= CONTROL_DUALCORE_WAIT_LOOPS) {
                break;
            }
            taskYIELD();
        } else {
            idle_guard = 0u;
        }
    }

    if (fm6_control_runtime_commands_applied(&s_runtime) != CONTROL_DUALCORE_COMMANDS) {
        cancel_producer();
    }
    const bool producer_done = wait_flag(&s_producer.done);

    result.producer_core = load_acquire(&s_producer.producer_core);
    result.accepted = load_acquire(&s_producer.accepted);
    result.applied = fm6_control_runtime_commands_applied(&s_runtime);
    result.rejected = fm6_control_queue_rejected(&s_queue);
    result.programs = fm6_control_runtime_program_commits(&s_runtime);
    result.active_program_sequence = fm6_control_runtime_active_program_sequence(&s_runtime);
    result.blocks = fm6_control_runtime_rendered_blocks(&s_runtime);
    result.checksum = fm6_port_checksum();
    result.energy = fm6_port_abs_energy();
    result.notes_down = fm6_control_runtime_notes_down(&s_runtime);
    result.algorithm = fm6_port_patch_algorithm();

    result.pass = producer_done &&
                  load_acquire(&s_producer.producer_errors) == 0u &&
                  load_acquire(&s_producer.forced_backpressure) == 1u &&
                  result.consumer_core == CONTROL_DUALCORE_CONSUMER_CORE &&
                  result.producer_core == CONTROL_DUALCORE_PRODUCER_CORE &&
                  result.accepted == CONTROL_DUALCORE_COMMANDS &&
                  result.applied == CONTROL_DUALCORE_COMMANDS &&
                  result.programs == 8u &&
                  result.active_program_sequence == 225u &&
                  result.algorithm == 21u &&
                  fm6_control_queue_pending(&s_queue) == 0u &&
                  result.rejected >= 1u &&
                  result.blocks > 0u &&
                  result.checksum != 0u &&
                  result.energy != 0u;
    return result;
}

static void test_dualcore_control_stream(void)
{
    const control_dualcore_result_t result = run_dualcore();
    check("dualcore_mixed_control_stream", result.pass);
    printf("FM6_CONTROL_RUNTIME_DUALCORE: %s commands=%u producer_core=%" PRIu32
           " consumer_core=%" PRIu32 " accepted=%" PRIu32 " applied=%" PRIu32
           " rejected=%" PRIu32 " programs=%" PRIu32 " active_program=%" PRIu32
           " blocks=%" PRIu32 " notes_down=%u algorithm=%u checksum=0x%08" PRIx32
           " energy=%" PRIu64 "\n",
           result.pass ? "PASS" : "FAIL",
           (unsigned)CONTROL_DUALCORE_COMMANDS,
           result.producer_core,
           result.consumer_core,
           result.accepted,
           result.applied,
           result.rejected,
           result.programs,
           result.active_program_sequence,
           result.blocks,
           result.notes_down,
           result.algorithm,
           result.checksum,
           result.energy);
}

unsigned fm6_control_runtime_dualcore_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    build_fixtures();

    test_dualcore_control_stream();

    printf("QEMU_FM6_CONTROL_RUNTIME_DUALCORE: %s tests=%u failures=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures);
    return s_failures;
}
