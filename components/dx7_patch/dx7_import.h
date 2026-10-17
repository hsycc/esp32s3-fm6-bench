#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dx7_patch.h"

typedef enum {
    DX7_IMPORT_FLAG_NONE = 0,
    DX7_IMPORT_FLAG_RAW = 1u << 0,
    DX7_IMPORT_FLAG_SYSEX = 1u << 1,
    DX7_IMPORT_FLAG_SINGLE = 1u << 2,
    DX7_IMPORT_FLAG_BANK = 1u << 3,
    DX7_IMPORT_FLAG_CHECKSUM_PRESENT = 1u << 4,
    DX7_IMPORT_FLAG_CHECKSUM_VALID = 1u << 5,
    DX7_IMPORT_FLAG_CHECKSUM_MISSING = 1u << 6,
    DX7_IMPORT_FLAG_CHECKSUM_BAD = 1u << 7,
    DX7_IMPORT_FLAG_F7_PRESENT = 1u << 8,
    DX7_IMPORT_FLAG_TRUNCATED = 1u << 9,
    DX7_IMPORT_FLAG_NONSTANDARD_COUNT = 1u << 10,
} dx7_import_flags_t;

typedef struct {
    size_t messages_seen;
    size_t voices_found;
    size_t unknown_messages;
    size_t truncated_messages;
    size_t checksum_missing;
    size_t checksum_bad;
    size_t nonstandard_count;
    size_t bytes_skipped;
} dx7_import_report_t;

typedef bool (*dx7_import_voice_cb)(const uint8_t voice[DX7_VCED_SIZE],
                                    size_t voice_index,
                                    size_t source_offset,
                                    uint32_t flags,
                                    void *ctx);

/* Tolerant external-file scanner. It never weakens dx7_patch_inspect(); every
 * emitted voice is canonicalized to the strict 155-byte VCED runtime form.
 * Returning false from the callback stops scanning successfully. */
dx7_patch_status_t dx7_import_scan(const uint8_t *data,
                                   size_t size,
                                   dx7_import_voice_cb callback,
                                   void *ctx,
                                   dx7_import_report_t *report);
