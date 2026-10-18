#include "fm6_control_runtime.h"

#include <stddef.h>
#include <stdint.h>
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

void fm6_control_queue_init(fm6_control_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    memset(queue, 0, sizeof(*queue));
    queue->next_sequence = 1u;
}

static bool reserve_slot(fm6_control_queue_t *queue,
                         fm6_control_command_t **command_out,
                         uint32_t *write_out)
{
    if (queue == NULL || command_out == NULL || write_out == NULL) {
        return false;
    }

    const uint32_t write = load_relaxed(&queue->write_count);
    const uint32_t read = load_acquire(&queue->read_count);
    if ((uint32_t)(write - read) >= FM6_CONTROL_QUEUE_CAPACITY) {
        __atomic_add_fetch(&queue->rejected_count, 1u, __ATOMIC_RELAXED);
        return false;
    }

    *write_out = write;
    *command_out = &queue->slots[write % FM6_CONTROL_QUEUE_CAPACITY];
    return true;
}

static void publish_slot(fm6_control_queue_t *queue,
                         fm6_control_command_t *command,
                         uint32_t write,
                         fm6_control_type_t type,
                         uint32_t *sequence_out)
{
    command->type = type;
    command->sequence = next_nonzero(&queue->next_sequence);
    if (sequence_out != NULL) {
        *sequence_out = command->sequence;
    }
    store_release(&queue->write_count, write + 1u);
}

bool fm6_control_queue_submit_program(fm6_control_queue_t *queue,
                                      const uint8_t voice[FM6_RUNTIME_VOICE_SIZE],
                                      unsigned voice_capacity,
                                      uint32_t *sequence_out)
{
    if (queue == NULL || voice == NULL ||
        voice_capacity == 0u || voice_capacity > FM6_BENCH_MAX_VOICES) {
        if (queue != NULL) {
            __atomic_add_fetch(&queue->rejected_count, 1u, __ATOMIC_RELAXED);
        }
        return false;
    }

    fm6_control_command_t *command = NULL;
    uint32_t write = 0u;
    if (!reserve_slot(queue, &command, &write)) {
        return false;
    }

    memcpy(command->data.program.voice, voice, FM6_RUNTIME_VOICE_SIZE);
    command->data.program.voice_capacity = (uint8_t)voice_capacity;
    publish_slot(queue, command, write, FM6_CONTROL_PROGRAM, sequence_out);
    return true;
}

static bool submit_note_command(fm6_control_queue_t *queue,
                                fm6_control_type_t type,
                                uint8_t note,
                                uint8_t velocity,
                                uint32_t *sequence_out)
{
    if (queue == NULL || note > 127u) {
        if (queue != NULL) {
            __atomic_add_fetch(&queue->rejected_count, 1u, __ATOMIC_RELAXED);
        }
        return false;
    }

    fm6_control_command_t *command = NULL;
    uint32_t write = 0u;
    if (!reserve_slot(queue, &command, &write)) {
        return false;
    }

    command->data.note.note = note;
    command->data.note.velocity = velocity;
    publish_slot(queue, command, write, type, sequence_out);
    return true;
}

static bool submit_control_command(fm6_control_queue_t *queue,
                                   fm6_control_type_t type,
                                   uint8_t value,
                                   uint32_t *sequence_out)
{
    if (queue == NULL) {
        return false;
    }

    fm6_control_command_t *command = NULL;
    uint32_t write = 0u;
    if (!reserve_slot(queue, &command, &write)) {
        return false;
    }

    command->data.control.value = value;
    publish_slot(queue, command, write, type, sequence_out);
    return true;
}

bool fm6_control_queue_submit_note_on(fm6_control_queue_t *queue,
                                      uint8_t note,
                                      uint8_t velocity,
                                      uint32_t *sequence_out)
{
    if (velocity == 0u) {
        return fm6_control_queue_submit_note_off(queue, note, sequence_out);
    }
    return submit_note_command(queue, FM6_CONTROL_NOTE_ON, note, velocity, sequence_out);
}

bool fm6_control_queue_submit_note_off(fm6_control_queue_t *queue,
                                       uint8_t note,
                                       uint32_t *sequence_out)
{
    return submit_note_command(queue, FM6_CONTROL_NOTE_OFF, note, 0u, sequence_out);
}

bool fm6_control_queue_submit_all_notes_off(fm6_control_queue_t *queue,
                                            uint32_t *sequence_out)
{
    return submit_control_command(queue, FM6_CONTROL_ALL_NOTES_OFF, 0u, sequence_out);
}

bool fm6_control_queue_submit_sustain(fm6_control_queue_t *queue,
                                      bool down,
                                      uint32_t *sequence_out)
{
    return submit_control_command(queue, FM6_CONTROL_SUSTAIN, down ? 1u : 0u, sequence_out);
}

bool fm6_control_queue_submit_all_sound_off(fm6_control_queue_t *queue,
                                            uint32_t *sequence_out)
{
    return submit_control_command(queue, FM6_CONTROL_ALL_SOUND_OFF, 0u, sequence_out);
}

unsigned fm6_control_queue_pending(const fm6_control_queue_t *queue)
{
    if (queue == NULL) {
        return 0u;
    }
    const uint32_t write = load_acquire(&queue->write_count);
    const uint32_t read = load_acquire(&queue->read_count);
    const uint32_t pending = write - read;
    return pending > FM6_CONTROL_QUEUE_CAPACITY
        ? FM6_CONTROL_QUEUE_CAPACITY
        : (unsigned)pending;
}

uint32_t fm6_control_queue_rejected(const fm6_control_queue_t *queue)
{
    return queue == NULL ? 0u : load_acquire(&queue->rejected_count);
}

static bool pop_command(fm6_control_queue_t *queue,
                        fm6_control_command_t *command_out)
{
    if (queue == NULL || command_out == NULL) {
        return false;
    }

    const uint32_t read = load_relaxed(&queue->read_count);
    const uint32_t write = load_acquire(&queue->write_count);
    if (read == write) {
        return false;
    }

    *command_out = queue->slots[read % FM6_CONTROL_QUEUE_CAPACITY];
    store_release(&queue->read_count, read + 1u);
    return true;
}

static void clear_allocator(fm6_control_runtime_t *runtime)
{
    memset(runtime->voices, 0, sizeof(runtime->voices));
    runtime->age_counter = 0u;
}

bool fm6_control_runtime_init(fm6_control_runtime_t *runtime,
                              fm6_control_queue_t *queue)
{
    if (runtime == NULL || queue == NULL) {
        return false;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->queue = queue;
    return true;
}

static int find_down_note(const fm6_control_runtime_t *runtime, uint8_t note)
{
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        const fm6_control_voice_slot_t *slot = &runtime->voices[i];
        if (slot->assigned && slot->down && slot->note == note) {
            return (int)i;
        }
    }
    return -1;
}

static int find_active_note(const fm6_control_runtime_t *runtime, uint8_t note)
{
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        const fm6_control_voice_slot_t *slot = &runtime->voices[i];
        if (slot->assigned && (slot->down || slot->sustained) && slot->note == note) {
            return (int)i;
        }
    }
    return -1;
}

static int find_unassigned(const fm6_control_runtime_t *runtime)
{
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        if (!runtime->voices[i].assigned) {
            return (int)i;
        }
    }
    return -1;
}

static bool age_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static int find_oldest_released(const fm6_control_runtime_t *runtime)
{
    int found = -1;
    uint32_t oldest = 0u;
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        const fm6_control_voice_slot_t *slot = &runtime->voices[i];
        if (!slot->assigned || slot->down || slot->sustained) {
            continue;
        }
        if (found < 0 || age_before(slot->age, oldest)) {
            found = (int)i;
            oldest = slot->age;
        }
    }
    return found;
}

static int find_oldest_sustained(const fm6_control_runtime_t *runtime)
{
    int found = -1;
    uint32_t oldest = 0u;
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        const fm6_control_voice_slot_t *slot = &runtime->voices[i];
        if (!slot->assigned || slot->down || !slot->sustained) {
            continue;
        }
        if (found < 0 || age_before(slot->age, oldest)) {
            found = (int)i;
            oldest = slot->age;
        }
    }
    return found;
}

static int find_oldest_down(const fm6_control_runtime_t *runtime)
{
    int found = -1;
    uint32_t oldest = 0u;
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        const fm6_control_voice_slot_t *slot = &runtime->voices[i];
        if (!slot->assigned || !slot->down) {
            continue;
        }
        if (found < 0 || age_before(slot->age, oldest)) {
            found = (int)i;
            oldest = slot->age;
        }
    }
    return found;
}

static uint32_t next_age(fm6_control_runtime_t *runtime)
{
    ++runtime->age_counter;
    if (runtime->age_counter == 0u) {
        ++runtime->age_counter;
    }
    return runtime->age_counter;
}

static bool apply_program(fm6_control_runtime_t *runtime,
                          const fm6_control_command_t *command)
{
    const unsigned capacity = command->data.program.voice_capacity;
    if (!fm6_port_load_program(command->data.program.voice, capacity)) {
        return false;
    }

    runtime->program_active = true;
    runtime->voice_capacity = capacity;
    runtime->active_program_sequence = command->sequence;
    clear_allocator(runtime);
    ++runtime->program_commits;
    return true;
}

static bool apply_note_on(fm6_control_runtime_t *runtime,
                          uint8_t note,
                          uint8_t velocity)
{
    if (!runtime->program_active) {
        ++runtime->ignored_without_program;
        return true;
    }

    int voice = find_active_note(runtime, note);
    if (voice >= 0) {
        if (!fm6_port_note_on((unsigned)voice, note, velocity)) {
            return false;
        }
        runtime->voices[voice].down = true;
        runtime->voices[voice].sustained = false;
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
        voice = find_oldest_sustained(runtime);
        if (voice >= 0) {
            ++runtime->voice_steals;
        }
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
    runtime->voices[voice].sustained = false;
    runtime->voices[voice].note = note;
    runtime->voices[voice].age = next_age(runtime);
    ++runtime->note_on_events;
    return true;
}

static bool apply_note_off(fm6_control_runtime_t *runtime, uint8_t note)
{
    if (!runtime->program_active) {
        ++runtime->ignored_without_program;
        return true;
    }

    ++runtime->note_off_events;
    const int voice = find_down_note(runtime, note);
    if (voice < 0) {
        ++runtime->unmatched_note_offs;
        return true;
    }

    runtime->voices[voice].down = false;
    if (runtime->sustain_down) {
        runtime->voices[voice].sustained = true;
        return true;
    }

    runtime->voices[voice].sustained = false;
    return fm6_port_note_off((unsigned)voice);
}

static bool release_sustained(fm6_control_runtime_t *runtime)
{
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        fm6_control_voice_slot_t *slot = &runtime->voices[i];
        if (slot->assigned && !slot->down && slot->sustained) {
            if (!fm6_port_note_off(i)) {
                return false;
            }
            slot->sustained = false;
        }
    }
    return true;
}

static bool apply_sustain(fm6_control_runtime_t *runtime, bool down)
{
    if (!runtime->program_active) {
        runtime->sustain_down = down;
        ++runtime->sustain_events;
        return true;
    }

    const bool was_down = runtime->sustain_down;
    runtime->sustain_down = down;
    ++runtime->sustain_events;
    if (was_down && !down) {
        return release_sustained(runtime);
    }
    return true;
}

static bool apply_all_notes_off(fm6_control_runtime_t *runtime)
{
    if (!runtime->program_active) {
        ++runtime->ignored_without_program;
        return true;
    }

    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        fm6_control_voice_slot_t *slot = &runtime->voices[i];
        if (!slot->assigned || !slot->down) {
            continue;
        }
        slot->down = false;
        if (runtime->sustain_down) {
            slot->sustained = true;
        } else {
            slot->sustained = false;
            if (!fm6_port_note_off(i)) {
                return false;
            }
        }
    }
    ++runtime->all_notes_off_events;
    return true;
}

static bool apply_all_sound_off(fm6_control_runtime_t *runtime)
{
    if (!runtime->program_active) {
        ++runtime->ignored_without_program;
        return true;
    }

    fm6_port_kill_all();
    clear_allocator(runtime);
    ++runtime->all_sound_off_events;
    return true;
}

static bool apply_command(fm6_control_runtime_t *runtime,
                          const fm6_control_command_t *command)
{
    switch (command->type) {
    case FM6_CONTROL_PROGRAM:
        return apply_program(runtime, command);
    case FM6_CONTROL_NOTE_ON:
        return apply_note_on(runtime, command->data.note.note, command->data.note.velocity);
    case FM6_CONTROL_NOTE_OFF:
        return apply_note_off(runtime, command->data.note.note);
    case FM6_CONTROL_ALL_NOTES_OFF:
        return apply_all_notes_off(runtime);
    case FM6_CONTROL_SUSTAIN:
        return apply_sustain(runtime, command->data.control.value != 0u);
    case FM6_CONTROL_ALL_SOUND_OFF:
        return apply_all_sound_off(runtime);
    default:
        return false;
    }
}

bool fm6_control_runtime_render_block(fm6_control_runtime_t *runtime,
                                      int32_t out[FM6_BENCH_BLOCK_FRAMES],
                                      unsigned *commands_applied_out)
{
    if (runtime == NULL || runtime->queue == NULL || out == NULL) {
        return false;
    }

    const unsigned snapshot = fm6_control_queue_pending(runtime->queue);
    unsigned applied = 0u;
    for (unsigned i = 0; i < snapshot; ++i) {
        fm6_control_command_t command;
        if (!pop_command(runtime->queue, &command) || !apply_command(runtime, &command)) {
            return false;
        }
        ++runtime->commands_applied;
        ++applied;
    }

    if (runtime->program_active) {
        fm6_port_render_block(runtime->voice_capacity,
                              fm6_port_patch_algorithm(),
                              out);
    } else {
        memset(out, 0, sizeof(int32_t) * FM6_BENCH_BLOCK_FRAMES);
    }
    ++runtime->rendered_blocks;

    if (commands_applied_out != NULL) {
        *commands_applied_out = applied;
    }
    return true;
}

bool fm6_control_runtime_program_active(const fm6_control_runtime_t *runtime)
{
    return runtime != NULL && runtime->program_active;
}

bool fm6_control_runtime_sustain_down(const fm6_control_runtime_t *runtime)
{
    return runtime != NULL && runtime->sustain_down;
}

unsigned fm6_control_runtime_voice_capacity(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->voice_capacity;
}

unsigned fm6_control_runtime_notes_down(const fm6_control_runtime_t *runtime)
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

unsigned fm6_control_runtime_sustained_notes(const fm6_control_runtime_t *runtime)
{
    if (runtime == NULL) {
        return 0u;
    }
    unsigned count = 0u;
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        if (runtime->voices[i].assigned && !runtime->voices[i].down &&
            runtime->voices[i].sustained) {
            ++count;
        }
    }
    return count;
}

bool fm6_control_runtime_note_is_down(const fm6_control_runtime_t *runtime, uint8_t note)
{
    return runtime != NULL && find_down_note(runtime, note) >= 0;
}

int fm6_control_runtime_voice_for_note(const fm6_control_runtime_t *runtime, uint8_t note)
{
    return runtime == NULL ? -1 : find_down_note(runtime, note);
}

uint32_t fm6_control_runtime_program_commits(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->program_commits;
}

uint32_t fm6_control_runtime_voice_steals(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->voice_steals;
}

uint32_t fm6_control_runtime_retriggers(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->retrigger_events;
}

uint32_t fm6_control_runtime_unmatched_note_offs(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->unmatched_note_offs;
}

uint32_t fm6_control_runtime_ignored_without_program(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->ignored_without_program;
}

uint32_t fm6_control_runtime_sustain_events(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->sustain_events;
}

uint32_t fm6_control_runtime_all_sound_off_events(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->all_sound_off_events;
}

uint32_t fm6_control_runtime_commands_applied(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->commands_applied;
}

uint32_t fm6_control_runtime_rendered_blocks(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->rendered_blocks;
}

uint32_t fm6_control_runtime_active_program_sequence(const fm6_control_runtime_t *runtime)
{
    return runtime == NULL ? 0u : runtime->active_program_sequence;
}
