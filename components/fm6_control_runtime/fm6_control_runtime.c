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

static bool reject(fm6_control_queue_t *queue)
{
    if (queue != NULL) {
        __atomic_add_fetch(&queue->rejected_count, 1u, __ATOMIC_RELAXED);
    }
    return false;
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
        return reject(queue);
    }
    *write_out = write;
    *command_out = &queue->slots[write % FM6_CONTROL_QUEUE_CAPACITY];
    return true;
}

static void publish_slot(fm6_control_queue_t *queue,
                         fm6_control_command_t *command,
                         uint32_t write, fm6_control_type_t type,
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
        return reject(queue);
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
                                uint8_t note, uint8_t velocity,
                                uint32_t *sequence_out)
{
    if (queue == NULL || note > 127u || velocity > 127u) {
        return reject(queue);
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
                                   uint16_t value, uint32_t *sequence_out)
{
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
                                      uint8_t note, uint8_t velocity,
                                      uint32_t *sequence_out)
{
    if (velocity == 0u) {
        return fm6_control_queue_submit_note_off(queue, note, sequence_out);
    }
    return submit_note_command(queue, FM6_CONTROL_NOTE_ON, note, velocity, sequence_out);
}

bool fm6_control_queue_submit_note_off(fm6_control_queue_t *queue,
                                       uint8_t note, uint32_t *sequence_out)
{
    return submit_note_command(queue, FM6_CONTROL_NOTE_OFF, note, 0u, sequence_out);
}

bool fm6_control_queue_submit_all_notes_off(fm6_control_queue_t *queue,
                                            uint32_t *sequence_out)
{
    return submit_control_command(queue, FM6_CONTROL_ALL_NOTES_OFF, 0u, sequence_out);
}

bool fm6_control_queue_submit_sustain(fm6_control_queue_t *queue,
                                      bool down, uint32_t *sequence_out)
{
    return submit_control_command(queue, FM6_CONTROL_SUSTAIN, down ? 1u : 0u, sequence_out);
}

bool fm6_control_queue_submit_all_sound_off(fm6_control_queue_t *queue,
                                            uint32_t *sequence_out)
{
    return submit_control_command(queue, FM6_CONTROL_ALL_SOUND_OFF, 0u, sequence_out);
}

bool fm6_control_queue_submit_pitch_bend(fm6_control_queue_t *queue,
                                         uint16_t value, uint32_t *sequence_out)
{
    if (value > 16383u) {
        return reject(queue);
    }
    return submit_control_command(queue, FM6_CONTROL_PITCH_BEND, value, sequence_out);
}

bool fm6_control_queue_submit_volume(fm6_control_queue_t *queue,
                                     uint8_t value, uint32_t *sequence_out)
{
    if (value > 127u) {
        return reject(queue);
    }
    return submit_control_command(queue, FM6_CONTROL_VOLUME, value, sequence_out);
}

bool fm6_control_queue_submit_expression(fm6_control_queue_t *queue,
                                         uint8_t value, uint32_t *sequence_out)
{
    if (value > 127u) {
        return reject(queue);
    }
    return submit_control_command(queue, FM6_CONTROL_EXPRESSION, value, sequence_out);
}

bool fm6_control_queue_submit_reset_controllers(fm6_control_queue_t *queue,
                                                uint32_t *sequence_out)
{
    return submit_control_command(queue, FM6_CONTROL_RESET_CONTROLLERS, 0u, sequence_out);
}

unsigned fm6_control_queue_pending(const fm6_control_queue_t *queue)
{
    if (queue == NULL) {
        return 0u;
    }
    const uint32_t write = load_acquire(&queue->write_count);
    const uint32_t read = load_acquire(&queue->read_count);
    const uint32_t pending = write - read;
    return pending > FM6_CONTROL_QUEUE_CAPACITY ? FM6_CONTROL_QUEUE_CAPACITY : (unsigned)pending;
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
    runtime->pitch_bend = FM6_CONTROL_BEND_CENTER;
    runtime->volume = 127u;
    runtime->expression = 127u;
    runtime->gain_q16 = FM6_CONTROL_GAIN_UNITY;
    runtime->gain_start_q16 = FM6_CONTROL_GAIN_UNITY;
    runtime->gain_target_q16 = FM6_CONTROL_GAIN_UNITY;
    runtime->gain_ramp_position = FM6_CONTROL_GAIN_RAMP_FRAMES;
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

/* Select by existing allocation priority, then stable age order. */
static int find_oldest(const fm6_control_runtime_t *runtime, unsigned state)
{
    int found = -1;
    uint32_t oldest = 0u;
    for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
        const fm6_control_voice_slot_t *slot = &runtime->voices[i];
        const unsigned slot_state = slot->down ? 2u : slot->sustained ? 1u : 0u;
        if (!slot->assigned || slot_state != state) {
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
    /* Loading a patch resets DSP state. Restore receiver controls without
     * restarting voices again or changing the running output-gain ramp. */
    if (!fm6_port_set_pitch_bend(runtime->pitch_bend)) {
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
                          uint8_t note, uint8_t velocity)
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
        voice = find_oldest(runtime, 0u);
    }
    if (voice < 0) {
        voice = find_oldest(runtime, 1u);
        if (voice >= 0) {
            ++runtime->voice_steals;
        }
    }
    if (voice < 0) {
        voice = find_oldest(runtime, 2u);
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
    const bool was_down = runtime->sustain_down;
    runtime->sustain_down = down;
    ++runtime->sustain_events;
    if (runtime->program_active && was_down && !down) {
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

static void update_gain_target(fm6_control_runtime_t *runtime)
{
    /* Project policy: linear amplitude for each 7-bit control, multiplicative
     * volume/expression, not a claim of GM2 logarithmic volume-law compliance. */
    const uint32_t product = (uint32_t)runtime->volume * runtime->expression;
    const int32_t target = (int32_t)((product * 65536u + 8064u) / 16129u);
    if (target != runtime->gain_target_q16) {
        runtime->gain_start_q16 = runtime->gain_q16;
        runtime->gain_target_q16 = target;
        runtime->gain_ramp_position = 0u;
    }
}

static bool apply_pitch_bend(fm6_control_runtime_t *runtime, uint16_t value)
{
    if (value > 16383u) {
        return false;
    }
    runtime->pitch_bend = value;
    return !runtime->program_active || fm6_port_set_pitch_bend(value);
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
    case FM6_CONTROL_PITCH_BEND:
        ++runtime->performance_events;
        return apply_pitch_bend(runtime, command->data.control.value);
    case FM6_CONTROL_VOLUME:
    case FM6_CONTROL_EXPRESSION:
        if (command->data.control.value > 127u) {
            return false;
        }
        if (command->type == FM6_CONTROL_VOLUME) {
            runtime->volume = (uint8_t)command->data.control.value;
        } else {
            runtime->expression = (uint8_t)command->data.control.value;
        }
        ++runtime->performance_events;
        update_gain_target(runtime);
        return true;
    case FM6_CONTROL_RESET_CONTROLLERS:
        /* CC121 is not All Notes Off and does not reset program or CC7. */
        ++runtime->performance_events;
        runtime->expression = 127u;
        update_gain_target(runtime);
        if (!apply_pitch_bend(runtime, FM6_CONTROL_BEND_CENTER)) {
            return false;
        }
        return apply_sustain(runtime, false);
    default:
        return false;
    }
}

static void apply_output_gain(fm6_control_runtime_t *runtime, int32_t *out)
{
    if (runtime->gain_ramp_position == FM6_CONTROL_GAIN_RAMP_FRAMES &&
        runtime->gain_q16 == FM6_CONTROL_GAIN_UNITY) {
        return; /* Exact legacy PCM at unity, no extra sample multiplies. */
    }
    for (unsigned i = 0; i < FM6_BENCH_BLOCK_FRAMES; ++i) {
        if (runtime->gain_ramp_position < FM6_CONTROL_GAIN_RAMP_FRAMES) {
            ++runtime->gain_ramp_position;
            const int32_t delta = runtime->gain_target_q16 - runtime->gain_start_q16;
            runtime->gain_q16 = runtime->gain_start_q16 +
                (delta * (int32_t)runtime->gain_ramp_position) /
                    (int32_t)FM6_CONTROL_GAIN_RAMP_FRAMES;
        }
        /* 64-bit product prevents overflow, signed division avoids relying on
         * negative right-shift semantics. Gain never exceeds unity. */
        out[i] = (int32_t)(((int64_t)out[i] * runtime->gain_q16) / 65536);
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
        fm6_port_render_block(runtime->voice_capacity, fm6_port_patch_algorithm(), out);
    } else {
        memset(out, 0, sizeof(int32_t) * FM6_BENCH_BLOCK_FRAMES);
    }
    apply_output_gain(runtime, out);
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
    unsigned count = 0u;
    if (runtime != NULL) {
        for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
            count += runtime->voices[i].assigned && runtime->voices[i].down;
        }
    }
    return count;
}

unsigned fm6_control_runtime_sustained_notes(const fm6_control_runtime_t *runtime)
{
    unsigned count = 0u;
    if (runtime != NULL) {
        for (unsigned i = 0; i < runtime->voice_capacity; ++i) {
            const fm6_control_voice_slot_t *slot = &runtime->voices[i];
            count += slot->assigned && !slot->down && slot->sustained;
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
