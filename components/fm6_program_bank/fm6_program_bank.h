#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fm6_port.h"

#define FM6_PROGRAM_BANK_SLOTS 128u

typedef struct {
    uint8_t voices[FM6_PROGRAM_BANK_SLOTS][FM6_RUNTIME_VOICE_SIZE];
    uint32_t valid_bits[FM6_PROGRAM_BANK_SLOTS / 32u];
    unsigned valid_count;
} fm6_program_bank_t;

void fm6_program_bank_init(fm6_program_bank_t *bank);
bool fm6_program_bank_set(fm6_program_bank_t *bank,
                          unsigned program,
                          const uint8_t voice[FM6_RUNTIME_VOICE_SIZE]);
bool fm6_program_bank_clear(fm6_program_bank_t *bank, unsigned program);
bool fm6_program_bank_valid(const fm6_program_bank_t *bank, unsigned program);
const uint8_t *fm6_program_bank_get(const fm6_program_bank_t *bank, unsigned program);
unsigned fm6_program_bank_count(const fm6_program_bank_t *bank);
