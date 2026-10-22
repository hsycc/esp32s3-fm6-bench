#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fm6_port.h"

#define FM6_CONTROL_QUEUE_CAPACITY 16u
#define FM6_CONTROL_GAIN_UNITY 65536
#define FM6_CONTROL_GAIN_RAMP_FRAMES 128u
#define FM6_CONTROL_BEND_CENTER 8192u

typedef enum {
    FM6_CONTROL_PROGRAM = 1,
    FM6_CONTROL_NOTE_ON = 2,
    FM6_CONTROL_NOTE_OFF = 3,
    FM6_CONTROL_ALL_NOTES_OFF = 4,
    FM6_CONTROL_SUSTAIN = 5,
    FM6_CONTROL_ALL_SOUND_OFF = 6,
    FM6_CONTROL_PITCH_BEND = 7,
    FM6_CONTROL_VOLUME = 8,
    FM6_CONTROL_EXPRESSION = 9,
    FM6_CONTROL_RESET_CONTROLLERS = 10,
} fm6_control_type_t;

typedef struct {
    fm6_control_type_t type;
    uint32_t sequence;
    union {
        struct {
            uint8_t voice[FM6_RUNTIME_VOICE_SIZE];
            uint8_t voice_capacity;
        } program;
        struct {
            uint8_t note;
            uint8_t velocity;
        } note;
        struct {
            uint16_t value;
        } control;
    } data;
} fm6_control_command_t;

/* Exactly one producer and one audio consumer. Runtime state/accessors are
 * consumer-owned, not concurrent UI snapshots. Port/DSP state is a singleton. */
typedef struct {
    fm6_control_command_t slots[FM6_CONTROL_QUEUE_CAPACITY];
    uint32_t write_count;
    uint32_t read_count;
    uint32_t next_sequence;
    uint32_t rejected_count;
} fm6_control_queue_t;

typedef struct {
    bool assigned;
    bool down;
    bool sustained;
    uint8_t note;
    uint32_t age;
} fm6_control_voice_slot_t;

typedef struct {
    fm6_control_queue_t *queue;
    bool program_active;
    bool sustain_down;
    unsigned voice_capacity;
    fm6_control_voice_slot_t voices[FM6_BENCH_MAX_VOICES];
    uint32_t age_counter;
    uint32_t rendered_blocks;
    uint32_t commands_applied;
    uint32_t program_commits;
    uint32_t note_on_events;
    uint32_t note_off_events;
    uint32_t all_notes_off_events;
    uint32_t sustain_events;
    uint32_t all_sound_off_events;
    uint32_t retrigger_events;
    uint32_t voice_steals;
    uint32_t unmatched_note_offs;
    uint32_t ignored_without_program;
    uint32_t active_program_sequence;
    uint16_t pitch_bend;
    uint8_t volume;
    uint8_t expression;
    int32_t gain_q16;
    int32_t gain_start_q16;
    int32_t gain_target_q16;
    unsigned gain_ramp_position;
    uint32_t performance_events;
} fm6_control_runtime_t;

void fm6_control_queue_init(fm6_control_queue_t *queue);
bool fm6_control_queue_submit_program(fm6_control_queue_t *queue,
                                      const uint8_t voice[FM6_RUNTIME_VOICE_SIZE],
                                      unsigned voice_capacity,
                                      uint32_t *sequence_out);
bool fm6_control_queue_submit_note_on(fm6_control_queue_t *queue,
                                      uint8_t note, uint8_t velocity,
                                      uint32_t *sequence_out);
bool fm6_control_queue_submit_note_off(fm6_control_queue_t *queue,
                                       uint8_t note, uint32_t *sequence_out);
bool fm6_control_queue_submit_all_notes_off(fm6_control_queue_t *queue,
                                            uint32_t *sequence_out);
bool fm6_control_queue_submit_sustain(fm6_control_queue_t *queue,
                                      bool down, uint32_t *sequence_out);
bool fm6_control_queue_submit_all_sound_off(fm6_control_queue_t *queue,
                                            uint32_t *sequence_out);
/* MIDI 14-bit pitch bend, center=8192; receiver range is fixed at +/-2 semitones.
 * Invalid values are rejected without publishing or changing sequence_out. */
bool fm6_control_queue_submit_pitch_bend(fm6_control_queue_t *queue,
                                         uint16_t value, uint32_t *sequence_out);
bool fm6_control_queue_submit_volume(fm6_control_queue_t *queue,
                                     uint8_t value, uint32_t *sequence_out);
bool fm6_control_queue_submit_expression(fm6_control_queue_t *queue,
                                         uint8_t value, uint32_t *sequence_out);
bool fm6_control_queue_submit_reset_controllers(fm6_control_queue_t *queue,
                                                uint32_t *sequence_out);
unsigned fm6_control_queue_pending(const fm6_control_queue_t *queue);
uint32_t fm6_control_queue_rejected(const fm6_control_queue_t *queue);

bool fm6_control_runtime_init(fm6_control_runtime_t *runtime,
                              fm6_control_queue_t *queue);
/* Commands published before block entry are consumed in strict FIFO order before
 * that 32-frame block renders. Gain is applied AFTER FM synthesis; muting never
 * freezes envelopes or phases. Gain changes settle over 128 rendered frames. */
bool fm6_control_runtime_render_block(fm6_control_runtime_t *runtime,
                                      int32_t out[FM6_BENCH_BLOCK_FRAMES],
                                      unsigned *commands_applied_out);

bool fm6_control_runtime_program_active(const fm6_control_runtime_t *runtime);
bool fm6_control_runtime_sustain_down(const fm6_control_runtime_t *runtime);
unsigned fm6_control_runtime_voice_capacity(const fm6_control_runtime_t *runtime);
unsigned fm6_control_runtime_notes_down(const fm6_control_runtime_t *runtime);
unsigned fm6_control_runtime_sustained_notes(const fm6_control_runtime_t *runtime);
bool fm6_control_runtime_note_is_down(const fm6_control_runtime_t *runtime, uint8_t note);
int fm6_control_runtime_voice_for_note(const fm6_control_runtime_t *runtime, uint8_t note);
uint32_t fm6_control_runtime_program_commits(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_voice_steals(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_retriggers(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_unmatched_note_offs(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_ignored_without_program(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_sustain_events(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_all_sound_off_events(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_commands_applied(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_rendered_blocks(const fm6_control_runtime_t *runtime);
uint32_t fm6_control_runtime_active_program_sequence(const fm6_control_runtime_t *runtime);
