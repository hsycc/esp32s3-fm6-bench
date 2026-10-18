#include "fm6_program_queue.h"

#include <stddef.h>
#include <string.h>

static uint32_t load_acquire(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static uint32_t load_relaxed(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static void store_release(uint32_t *value, uint32_t next)
{
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

void fm6_program_queue_init(fm6_program_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    memset(queue, 0, sizeof(*queue));
    queue->next_generation = 1u;
}

bool fm6_program_queue_submit(fm6_program_queue_t *queue,
                              const uint8_t voice[DX7_VCED_SIZE],
                              uint8_t voice_count,
                              uint8_t base_note,
                              uint8_t note_step,
                              uint8_t velocity,
                              uint32_t *generation_out)
{
    if (queue == NULL || voice == NULL ||
        voice_count == 0u || voice_count > FM6_BENCH_MAX_VOICES) {
        if (queue != NULL) {
            __atomic_add_fetch(&queue->rejected_count, 1u, __ATOMIC_RELAXED);
        }
        return false;
    }

    const uint32_t write = load_relaxed(&queue->write_count);
    const uint32_t read = load_acquire(&queue->read_count);
    if ((uint32_t)(write - read) >= FM6_PROGRAM_QUEUE_CAPACITY) {
        __atomic_add_fetch(&queue->rejected_count, 1u, __ATOMIC_RELAXED);
        return false;
    }

    fm6_program_command_t *command = &queue->slots[write % FM6_PROGRAM_QUEUE_CAPACITY];
    memcpy(command->voice, voice, DX7_VCED_SIZE);
    dx7_vced_sanitize(command->voice);
    command->voice_count = voice_count;
    command->base_note = base_note;
    command->note_step = note_step;
    command->velocity = velocity;

    uint32_t generation = queue->next_generation++;
    if (generation == 0u) {
        generation = queue->next_generation++;
    }
    command->generation = generation;

    if (generation_out != NULL) {
        *generation_out = generation;
    }
    store_release(&queue->write_count, write + 1u);
    return true;
}

bool fm6_program_queue_commit_next_ex(fm6_program_queue_t *queue,
                                      fm6_program_applied_t *applied_out)
{
    if (queue == NULL) {
        return false;
    }

    const uint32_t read = load_relaxed(&queue->read_count);
    const uint32_t write = load_acquire(&queue->write_count);
    if (read == write) {
        return false;
    }

    const fm6_program_command_t *command =
        &queue->slots[read % FM6_PROGRAM_QUEUE_CAPACITY];

    if (!fm6_port_prepare_voice(command->voice,
                                command->voice_count,
                                command->base_note,
                                command->note_step,
                                command->velocity)) {
        return false;
    }

    if (applied_out != NULL) {
        applied_out->generation = command->generation;
        applied_out->voice_count = command->voice_count;
        applied_out->base_note = command->base_note;
        applied_out->note_step = command->note_step;
        applied_out->velocity = command->velocity;
    }

    store_release(&queue->active_generation, command->generation);
    __atomic_add_fetch(&queue->committed_count, 1u, __ATOMIC_RELAXED);
    store_release(&queue->read_count, read + 1u);
    return true;
}

bool fm6_program_queue_commit_next(fm6_program_queue_t *queue,
                                   uint32_t *generation_out)
{
    fm6_program_applied_t applied = {0};
    const bool committed = fm6_program_queue_commit_next_ex(queue, &applied);
    if (committed && generation_out != NULL) {
        *generation_out = applied.generation;
    }
    return committed;
}

unsigned fm6_program_queue_pending(const fm6_program_queue_t *queue)
{
    if (queue == NULL) {
        return 0u;
    }
    const uint32_t write = load_acquire(&queue->write_count);
    const uint32_t read = load_acquire(&queue->read_count);
    const uint32_t pending = write - read;
    return pending > FM6_PROGRAM_QUEUE_CAPACITY
        ? FM6_PROGRAM_QUEUE_CAPACITY
        : (unsigned)pending;
}

uint32_t fm6_program_queue_active_generation(const fm6_program_queue_t *queue)
{
    return queue == NULL ? 0u : load_acquire(&queue->active_generation);
}

uint32_t fm6_program_queue_committed(const fm6_program_queue_t *queue)
{
    return queue == NULL ? 0u : load_acquire(&queue->committed_count);
}

uint32_t fm6_program_queue_rejected(const fm6_program_queue_t *queue)
{
    return queue == NULL ? 0u : load_acquire(&queue->rejected_count);
}
