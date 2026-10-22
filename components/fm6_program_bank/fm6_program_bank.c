#include "fm6_program_bank.h"

#include <stddef.h>
#include <string.h>

static bool in_range(unsigned program)
{
    return program < FM6_PROGRAM_BANK_SLOTS;
}

static uint32_t bit_mask(unsigned program)
{
    return 1u << (program & 31u);
}

static unsigned word_index(unsigned program)
{
    return program >> 5u;
}

void fm6_program_bank_init(fm6_program_bank_t *bank)
{
    if (bank != NULL) {
        memset(bank, 0, sizeof(*bank));
    }
}

bool fm6_program_bank_set(fm6_program_bank_t *bank,
                          unsigned program,
                          const uint8_t voice[FM6_RUNTIME_VOICE_SIZE])
{
    if (bank == NULL || voice == NULL || !in_range(program)) {
        return false;
    }

    const unsigned word = word_index(program);
    const uint32_t mask = bit_mask(program);
    const bool was_valid = (bank->valid_bits[word] & mask) != 0u;
    memcpy(bank->voices[program], voice, FM6_RUNTIME_VOICE_SIZE);
    bank->valid_bits[word] |= mask;
    if (!was_valid) {
        ++bank->valid_count;
    }
    return true;
}

bool fm6_program_bank_clear(fm6_program_bank_t *bank, unsigned program)
{
    if (bank == NULL || !in_range(program)) {
        return false;
    }

    const unsigned word = word_index(program);
    const uint32_t mask = bit_mask(program);
    if ((bank->valid_bits[word] & mask) != 0u) {
        bank->valid_bits[word] &= ~mask;
        if (bank->valid_count != 0u) {
            --bank->valid_count;
        }
    }
    memset(bank->voices[program], 0, FM6_RUNTIME_VOICE_SIZE);
    return true;
}

bool fm6_program_bank_valid(const fm6_program_bank_t *bank, unsigned program)
{
    if (bank == NULL || !in_range(program)) {
        return false;
    }
    return (bank->valid_bits[word_index(program)] & bit_mask(program)) != 0u;
}

const uint8_t *fm6_program_bank_get(const fm6_program_bank_t *bank, unsigned program)
{
    return fm6_program_bank_valid(bank, program) ? bank->voices[program] : NULL;
}

unsigned fm6_program_bank_count(const fm6_program_bank_t *bank)
{
    return bank == NULL ? 0u : bank->valid_count;
}
