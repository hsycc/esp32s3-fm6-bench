#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dx7_patch.h"
#include "fm6_port.h"

#define FM6_PROGRAM_QUEUE_CAPACITY 4u

typedef struct {
    uint8_t voice[DX7_VCED_SIZE];
    uint8_t voice_count;
    uint8_t base_note;
    uint8_t note_step;
    uint8_t velocity;
    uint32_t generation;
} fm6_program_command_t;

typedef struct {
    uint32_t generation;
    uint8_t voice_count;
    uint8_t base_note;
    uint8_t note_step;
    uint8_t velocity;
} fm6_program_applied_t;

typedef struct {
    fm6_program_command_t slots[FM6_PROGRAM_QUEUE_CAPACITY];
    uint32_t write_count;
    uint32_t read_count;
    uint32_t next_generation;
    uint32_t active_generation;
    uint32_t committed_count;
    uint32_t rejected_count;
} fm6_program_queue_t;

/* Single-producer/single-consumer queue. Producer may run on the storage/UI core;
 * consumer must call commit_next() only at an audio block boundary. No dynamic
 * allocation, filesystem I/O, locks or callbacks occur in the consumer path. */
void fm6_program_queue_init(fm6_program_queue_t *queue);

bool fm6_program_queue_submit(fm6_program_queue_t *queue,
                              const uint8_t voice[DX7_VCED_SIZE],
                              uint8_t voice_count,
                              uint8_t base_note,
                              uint8_t note_step,
                              uint8_t velocity,
                              uint32_t *generation_out);

/* Commits at most one pending program into fm6_port and reports the exact runtime
 * metadata that was applied. The caller defines the block boundary. */
bool fm6_program_queue_commit_next_ex(fm6_program_queue_t *queue,
                                      fm6_program_applied_t *applied_out);

/* Backward-compatible generation-only wrapper around commit_next_ex(). */
bool fm6_program_queue_commit_next(fm6_program_queue_t *queue,
                                   uint32_t *generation_out);

unsigned fm6_program_queue_pending(const fm6_program_queue_t *queue);
uint32_t fm6_program_queue_active_generation(const fm6_program_queue_t *queue);
uint32_t fm6_program_queue_committed(const fm6_program_queue_t *queue);
uint32_t fm6_program_queue_rejected(const fm6_program_queue_t *queue);
