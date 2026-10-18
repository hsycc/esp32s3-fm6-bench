#include "fm6_note_runtime.h"

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

static uint32_t next_nonzero(uint32_t *next_value)
{
    uint32_t value = (*next_value)++;
    if (value == 0u) {
        value = (*next_value)++;
    }
    return value;
}

void fm6_note_queue_init(fm6_note_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    memset(queue, 0, sizeof(*queue));
    queue->next_sequence = 1u;
}

static bool submit_event(fm6_note_queue_t *queue,
                         fm6_note_event_type_t type,
                         uint8_t note,
                         uint8_t velocity,
                         uint32_t *sequence_out)
{
    if (queue == NULL) {
        return false;
    }
    if ((type == FM6_NOTE_EVENT_NOTE_ON || type == FM6_NOTE_EVENT_NOTE_OFF) && note > 127u) {
        __atomic_add_fetch(&queue->rejected_count, 1u, __ATOMIC_RELAXED);
        return false;
    }

    const uint32_t write = load_relaxed(&queue->write_count);
    const uint32_t read = load_acquire(&queue->read_count);
    if ((uint32_t)(write - read) >= FM6_NOTE_QUEUE_CAPACITY) {
        __atomic_add_fetch(&queue->rejected_count, 1u, __ATOMIC_RELAXED);
        return false;
    }

    fm6_note_event_t *event = &queue->slots[write % FM6_NOTE_QUEUE_CAPACITY];
    event->type = type;
    event->note = note;
    event->velocity = velocity;
    event->reserved = 0u;
    event->sequence = next_nonzero(&queue->next_sequence);

    if (sequence_out != NULL) {
        *sequence_out = event->sequence;
    }
    store_release(&queue->write_count, write + 1u);
    return true;
}

bool fm6_note_queue_submit_note_on(fm6_note_queue_t *queue,
                                   uint8_t note,
                                   uint8_t velocity,
                                   uint32_t *sequence_out)
{
    if (velocity == 0u) {
        return fm6_note_queue_submit_note_off(queue, note, sequence_out);
    }
    return submit_event(queue, FM6_NOTE_EVENT_NOTE_ON, note, velocity, sequence_out);
}

bool fm6_note_queue_submit_note_off(fm6_note_queue_t *queue,
                                    uint8_t note,
                                    uint32_t *sequence_out)
{
    return submit_event(queue, FM6_NOTE_EVENT_NOTE_OFF, note, 0u, sequence_out);
}

bool fm6_note_queue_submit_all_notes_off(fm6_note_queue_t *queue,
                                         uint32_t *sequence_out)
{
    return submit_event(queue, FM6_NOTE_EVENT_ALL_NOTES_OFF, 0u, 0u, sequence_out);
}

unsigned fm6_note_queue_pending(const fm6_note_queue_t *queue)
{
    if (queue == NULL) {
        return 0u;
    }
    const uint32_t write = load_acquire(&queue->write_count);
    const uint32_t read = load_acquire(&queue->read_count);
    const uint32_t pending = write - read;
    return pending > FM6_NOTE_QUEUE_CAPACITY
        ? FM6_NOTE_QUEUE_CAPACITY
        : (unsigned)pending;
}

uint32_t fm6_note_queue_rejected(const fm6_note_queue_t *queue)
{
    return queue == NULL ? 0u : load_acquire(&queue->rejected_count);
}

void fm6_note_queue_discard_pending(fm6_note_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    const uint32_t write = load_acquire(&queue->write_count);
    store_release(&queue->read_count, write);
}

static bool pop_event(fm6_note_queue_t *queue, fm6_note_event_t *event_out)
{
    if (queue == NULL || event_out == NULL) {
        return false;
    }

    const uint32_t read = load_relaxed(&queue->read_count);
    const uint32_t write = load_acquire(&queue->write_count);
    if (read == write) {
        return false;
    }

    *event_out = queue->slots[read % FM6_NOTE_QUEUE_CAPACITY];
    store_release(&queue->read_count, read + 1u);
    return true;
}

static void clear_allocator(fm6_note_runtime_t *runtime)
{
    memset(runtime->voices, 0, sizeof(runtime->voices));
    runtime->age_counter = 0u;
}

bool fm6_note_runtime_init(fm6_note_runtime_t *runtime,
                           fm6_note_queue_t *queue,
                           unsigned voice_capacity)
{
    if (runtime == NULL || queue == NULL ||
        voice_capacity == 0u || voice_capacity > FM6_BENCH_MAX_VOICES ||
        fm6_port_voice_capacity() != voice_capacity) {
        return false;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->queue = queue;
    runtime->voice_capacity = voice_capacity;
    clear_allocator(runtime);
    return true;
}

bool fm6_note_runtime_reset(fm6_note_runtime_t *runtime,
                            unsigned voice_capacity)
{
    if (runtime == NULL || runtime->queue == NULL ||
        voice_capacity == 0u || voice_capacity > FM6_BENCH_MAX_VOICES ||
        fm6_port_voice_capacity() != voice_capacity) {
        return false;
    }

    runtime->voice_capacity = voice_capacity;
    clear_allocator(runtime);
    fm6_note_queue_discard_pending(runtime->queue);
    return true;
}

static int find_down_note(const fm6_note_runtime_t *runtime, uint8_t note)
{
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        if (runtime->voices[i].assigned && runtime->voices[i].down &&
            runtime->voices[i].note == note) {
            return (int)i;
        }
    }
    return -1;
}

static int find_unassigned(const fm6_note_runtime_t *runtime)
{
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        if (!runtime->voices[i].assigned) {
            return (int)i;
        }
    }
    return -1;
}

static int find_oldest_released(const fm6_note_runtime_t *runtime)
{
    int found = -1;
    uint32_t oldest = 0u;
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        const fm6_voice_slot_t *slot = &runtime->voices[i];
        if (!slot->assigned || slot->down) {
            continue;
        }
        if (found < 0 || slot->age < oldest) {
            found = (int)i;
            oldest = slot->age;
        }
    }
    return found;
}

static int find_oldest_down(const fm6_note_runtime_t *runtime)
{
    int found = -1;
    uint32_t oldest = 0u;
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        const fm6_voice_slot_t *slot = &runtime->voices[i];
        if (!slot->assigned || !slot->down) {
            continue;
        }
        if (found < 0 || slot->age < oldest) {
            found = (int)i;
            oldest = slot->age;
        }
    }
    return found;
}

static uint32_t next_age(fm6_note_runtime_t *runtime)
{
    ++runtime->age_counter;
    if (runtime->age_counter == 0u) {
        /* Age wrap is rare; preserve ordering by compacting active slots into a
         * fresh monotonic sequence in voice-index order. */
        uint32_t age = 0u;
        for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
            if (runtime->voices[i].assigned) {
                runtime->voices[i].age = ++age;
            }
        }
        runtime->age_counter = age + 1u;
        if (runtime->age_counter == 0u) {
            runtime->age_counter = 1u;
        }
    }
    return runtime->age_counter;
}

static bool apply_note_on(fm6_note_runtime_t *runtime, uint8_t note, uint8_t velocity)
{
    int voice = find_down_note(runtime, note);
    if (voice >= 0) {
        if (!fm6_port_note_on((unsigned)voice, note, velocity)) {
            return false;
        }
        runtime->voices[voice].age = next_age(runtime);
        ++runtime->retrigger_events;
        ++runtime->note_on_events;
        return true;
    }

    voice = find_unassigned(runtime);
    if (voice < 0) {
        voice = find_oldest_released(runtime);
    }
    if (voice < 0) {
        voice = find_oldest_down(runtime);
        if (voice >= 0) {
            ++runtime->voice_steals;
        }
    }
    if (voice < 0 || !fm6_port_note_on((unsigned)voice, note, velocity)) {
        return false;
    }

    runtime->voices[voice].assigned = true;
    runtime->voices[voice].down = true;
    runtime->voices[voice].note = note;
    runtime->voices[voice].age = next_age(runtime);
    ++runtime->note_on_events;
    return true;
}

static bool apply_note_off(fm6_note_runtime_t *runtime, uint8_t note)
{
    const int voice = find_down_note(runtime, note);
    ++runtime->note_off_events;
    if (voice < 0) {
        ++runtime->unmatched_note_offs;
        return true;
    }

    if (!fm6_port_note_off((unsigned)voice)) {
        return false;
    }
    runtime->voices[voice].down = false;
    return true;
}

static bool apply_all_notes_off(fm6_note_runtime_t *runtime)
{
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        if (runtime->voices[i].assigned && runtime->voices[i].down) {
            if (!fm6_port_note_off(i)) {
                return false;
            }
            runtime->voices[i].down = false;
        }
    }
    ++runtime->all_notes_off_events;
    return true;
}

static bool apply_event(fm6_note_runtime_t *runtime, const fm6_note_event_t *event)
{
    switch (event->type) {
    case FM6_NOTE_EVENT_NOTE_ON:
        return apply_note_on(runtime, event->note, event->velocity);
    case FM6_NOTE_EVENT_NOTE_OFF:
        return apply_note_off(runtime, event->note);
    case FM6_NOTE_EVENT_ALL_NOTES_OFF:
        return apply_all_notes_off(runtime);
    default:
        return false;
    }
}

bool fm6_note_runtime_render_block(fm6_note_runtime_t *runtime,
                                   int32_t out[FM6_BENCH_BLOCK_FRAMES],
                                   unsigned *events_applied_out)
{
    if (runtime == NULL || runtime->queue == NULL || out == NULL ||
        runtime->voice_capacity == 0u ||
        runtime->voice_capacity > FM6_BENCH_MAX_VOICES ||
        fm6_port_voice_capacity() != runtime->voice_capacity) {
        return false;
    }

    const unsigned snapshot = fm6_note_queue_pending(runtime->queue);
    unsigned applied = 0u;
    for (unsigned i = 0; i < snapshot; ++i) {
        fm6_note_event_t event;
        if (!pop_event(runtime->queue, &event) || !apply_event(runtime, &event)) {
            return false;
        }
        ++runtime->events_applied;
        ++applied;
    }

    fm6_port_render_block(runtime->voice_capacity, fm6_port_patch_algorithm(), out);
    ++runtime->rendered_blocks;

    if (events_applied_out != NULL) {
        *events_applied_out = applied;
    }
    return true;
}

unsigned fm6_note_runtime_notes_down(const fm6_note_runtime_t *runtime)
{
    if (runtime == NULL) {
        return 0u;
    }
    unsigned count = 0u;
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        if (runtime->voices[i].assigned && runtime->voices[i].down) {
            ++count;
        }
    }
    return count;
}

bool fm6_note_runtime_note_is_down(const fm6_note_runtime_t *runtime, uint8_t note)
{
    return runtime != NULL && find_down_note(runtime, note) >= 0;
}

int fm6_note_runtime_voice_for_note(const fm6_note_runtime_t *runtime, uint8_t note)
{
    return runtime == NULL ? -1 : find_down_note(runtime, note);
}

uint32_t fm6_note_runtime_voice_steals(const fm6_note_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->voice_steals;
}

uint32_t fm6_note_runtime_retriggers(const fm6_note_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->retrigger_events;
}

uint32_t fm6_note_runtime_unmatched_note_offs(const fm6_note_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->unmatched_note_offs;
}

uint32_t fm6_note_runtime_events_applied(const fm6_note_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->events_applied;
}

uint32_t fm6_note_runtime_rendered_blocks(const fm6_note_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->rendered_blocks;
}
