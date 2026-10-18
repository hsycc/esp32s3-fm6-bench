#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fm6_port.h"

#define FM6_NOTE_QUEUE_CAPACITY 32u

typedef enum {
    FM6_NOTE_EVENT_NOTE_ON = 1,
    FM6_NOTE_EVENT_NOTE_OFF = 2,
    FM6_NOTE_EVENT_ALL_NOTES_OFF = 3,
} fm6_note_event_type_t;

typedef struct {
    fm6_note_event_type_t type;
    uint8_t note;
    uint8_t velocity;
    uint16_t reserved;
    uint32_t sequence;
} fm6_note_event_t;

typedef struct {
    fm6_note_event_t slots[FM6_NOTE_QUEUE_CAPACITY];
    uint32_t write_count;
    uint32_t read_count;
    uint32_t next_sequence;
    uint32_t rejected_count;
} fm6_note_queue_t;

typedef struct {
    bool assigned;
    bool down;
    uint8_t note;
    uint8_t reserved;
    uint32_t age;
} fm6_voice_slot_t;

typedef struct {
    fm6_note_queue_t *queue;
    unsigned voice_capacity;
    fm6_voice_slot_t voices[FM6_BENCH_MAX_VOICES];
    uint32_t age_counter;
    uint32_t rendered_blocks;
    uint32_t events_applied;
    uint32_t note_on_events;
    uint32_t note_off_events;
    uint32_t retrigger_events;
    uint32_t voice_steals;
    uint32_t unmatched_note_offs;
    uint32_t all_notes_off_events;
} fm6_note_runtime_t;

/* Single-producer/single-consumer event queue. Producer may run on the UI/MIDI
 * core. Consumer is the audio thread and should drain only at block boundaries. */
void fm6_note_queue_init(fm6_note_queue_t *queue);
bool fm6_note_queue_submit_note_on(fm6_note_queue_t *queue,
                                   uint8_t note,
                                   uint8_t velocity,
                                   uint32_t *sequence_out);
bool fm6_note_queue_submit_note_off(fm6_note_queue_t *queue,
                                    uint8_t note,
                                    uint32_t *sequence_out);
bool fm6_note_queue_submit_all_notes_off(fm6_note_queue_t *queue,
                                         uint32_t *sequence_out);
unsigned fm6_note_queue_pending(const fm6_note_queue_t *queue);
uint32_t fm6_note_queue_rejected(const fm6_note_queue_t *queue);

/* Consumer-side flush used at a program change so note events generated for the
 * previous patch cannot leak into the freshly reset voice pool. */
void fm6_note_queue_discard_pending(fm6_note_queue_t *queue);

/* Runtime assumes fm6_port_load_program() has already prepared a silent patch
 * with voice_capacity slots. All note events are applied at the beginning of
 * render_block(), before the corresponding 32-frame block is synthesized. */
bool fm6_note_runtime_init(fm6_note_runtime_t *runtime,
                           fm6_note_queue_t *queue,
                           unsigned voice_capacity);

/* Reset allocator metadata after a program change. The FM port should already
 * have been reset by fm6_port_load_program(). Pending note events are discarded. */
bool fm6_note_runtime_reset(fm6_note_runtime_t *runtime,
                            unsigned voice_capacity);

/* Applies a snapshot of events that were pending at block entry. Events produced
 * concurrently while that snapshot is being drained remain queued for the next
 * audio block. */
bool fm6_note_runtime_render_block(fm6_note_runtime_t *runtime,
                                   int32_t out[FM6_BENCH_BLOCK_FRAMES],
                                   unsigned *events_applied_out);

unsigned fm6_note_runtime_notes_down(const fm6_note_runtime_t *runtime);
bool fm6_note_runtime_note_is_down(const fm6_note_runtime_t *runtime, uint8_t note);
int fm6_note_runtime_voice_for_note(const fm6_note_runtime_t *runtime, uint8_t note);
uint32_t fm6_note_runtime_voice_steals(const fm6_note_runtime_t *runtime);
uint32_t fm6_note_runtime_retriggers(const fm6_note_runtime_t *runtime);
uint32_t fm6_note_runtime_unmatched_note_offs(const fm6_note_runtime_t *runtime);
uint32_t fm6_note_runtime_events_applied(const fm6_note_runtime_t *runtime);
uint32_t fm6_note_runtime_rendered_blocks(const fm6_note_runtime_t *runtime);
