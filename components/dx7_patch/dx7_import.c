#include "dx7_import.h"

#include <string.h>

static bool looks_like_sysex(const uint8_t *data, size_t size)
{
    return size >= 2u && data[0] == 0xf0u && data[1] == 0x43u;
}

static bool emit_voice(const uint8_t voice[DX7_VCED_SIZE],
                       size_t source_offset,
                       uint32_t flags,
                       dx7_import_voice_cb callback,
                       void *ctx,
                       dx7_import_report_t *report)
{
    const size_t index = report->voices_found;
    ++report->voices_found;
    return callback == NULL || callback(voice, index, source_offset, flags, ctx);
}

static bool emit_raw(const uint8_t *data,
                     size_t size,
                     dx7_import_voice_cb callback,
                     void *ctx,
                     dx7_import_report_t *report)
{
    uint8_t voice[DX7_VCED_SIZE];

    if (size == DX7_VCED_SIZE) {
        memcpy(voice, data, sizeof(voice));
        dx7_vced_sanitize(voice);
        ++report->messages_seen;
        return emit_voice(voice,
                          0u,
                          DX7_IMPORT_FLAG_RAW | DX7_IMPORT_FLAG_SINGLE,
                          callback,
                          ctx,
                          report);
    }

    if (size == DX7_VMEM_SIZE) {
        dx7_vmem_unpack(data, voice);
        ++report->messages_seen;
        return emit_voice(voice,
                          0u,
                          DX7_IMPORT_FLAG_RAW | DX7_IMPORT_FLAG_SINGLE,
                          callback,
                          ctx,
                          report);
    }

    if (size == DX7_BANK_DATA_SIZE) {
        ++report->messages_seen;
        for (size_t i = 0; i < DX7_BANK_VOICES; ++i) {
            dx7_vmem_unpack(data + i * DX7_VMEM_SIZE, voice);
            if (!emit_voice(voice,
                            i * DX7_VMEM_SIZE,
                            DX7_IMPORT_FLAG_RAW | DX7_IMPORT_FLAG_BANK,
                            callback,
                            ctx,
                            report)) {
                return false;
            }
        }
        return true;
    }

    return false;
}

static size_t find_message_marker(const uint8_t *data, size_t size, size_t start)
{
    for (size_t i = start; i < size; ++i) {
        if (data[i] == 0xf0u || data[i] == 0xf7u) {
            return i;
        }
    }
    return size;
}

static bool payload_has_marker(const uint8_t *data, size_t start, size_t end, size_t *marker)
{
    for (size_t i = start; i < end; ++i) {
        if (data[i] == 0xf0u || data[i] == 0xf7u) {
            if (marker != NULL) {
                *marker = i;
            }
            return true;
        }
    }
    return false;
}

static bool scan_single(const uint8_t *data,
                        size_t size,
                        size_t start,
                        dx7_import_voice_cb callback,
                        void *ctx,
                        dx7_import_report_t *report,
                        size_t *next_offset)
{
    const size_t payload_start = start + 6u;
    const size_t payload_end = payload_start + DX7_VCED_SIZE;
    uint32_t flags = DX7_IMPORT_FLAG_SYSEX | DX7_IMPORT_FLAG_SINGLE;

    ++report->messages_seen;
    if (data[start + 4u] != 0x01u || data[start + 5u] != 0x1bu) {
        flags |= DX7_IMPORT_FLAG_NONSTANDARD_COUNT;
        ++report->nonstandard_count;
    }

    if (payload_end > size) {
        flags |= DX7_IMPORT_FLAG_TRUNCATED;
        ++report->truncated_messages;
        *next_offset = size;
        return true;
    }

    size_t embedded_marker = 0u;
    if (payload_has_marker(data, payload_start, payload_end, &embedded_marker)) {
        flags |= DX7_IMPORT_FLAG_TRUNCATED;
        ++report->truncated_messages;
        *next_offset = data[embedded_marker] == 0xf0u ? embedded_marker : embedded_marker + 1u;
        return true;
    }

    size_t cursor = payload_end;
    const uint8_t expected_checksum = dx7_yamaha_checksum(data + payload_start, DX7_VCED_SIZE);

    if (cursor < size && data[cursor] != 0xf0u && data[cursor] != 0xf7u) {
        flags |= DX7_IMPORT_FLAG_CHECKSUM_PRESENT;
        if (data[cursor] == expected_checksum) {
            flags |= DX7_IMPORT_FLAG_CHECKSUM_VALID;
        } else {
            flags |= DX7_IMPORT_FLAG_CHECKSUM_BAD;
            ++report->checksum_bad;
        }
        ++cursor;
    } else {
        flags |= DX7_IMPORT_FLAG_CHECKSUM_MISSING;
        ++report->checksum_missing;
    }

    if (cursor < size && data[cursor] == 0xf7u) {
        flags |= DX7_IMPORT_FLAG_F7_PRESENT;
        ++cursor;
    }

    uint8_t voice[DX7_VCED_SIZE];
    memcpy(voice, data + payload_start, sizeof(voice));
    dx7_vced_sanitize(voice);

    *next_offset = cursor;
    return emit_voice(voice, start, flags, callback, ctx, report);
}

static bool scan_bank(const uint8_t *data,
                      size_t size,
                      size_t start,
                      dx7_import_voice_cb callback,
                      void *ctx,
                      dx7_import_report_t *report,
                      size_t *next_offset)
{
    const size_t payload_start = start + 6u;
    uint32_t flags = DX7_IMPORT_FLAG_SYSEX | DX7_IMPORT_FLAG_BANK;
    ++report->messages_seen;

    if (data[start + 4u] != 0x20u || data[start + 5u] != 0x00u) {
        flags |= DX7_IMPORT_FLAG_NONSTANDARD_COUNT;
        ++report->nonstandard_count;
    }

    const size_t boundary = find_message_marker(data, size, payload_start);
    const bool has_f7 = boundary < size && data[boundary] == 0xf7u;
    const size_t bytes_before_boundary = boundary - payload_start;

    size_t voice_bytes = bytes_before_boundary;
    if (voice_bytes > DX7_BANK_DATA_SIZE) {
        voice_bytes = DX7_BANK_DATA_SIZE;
    }

    size_t complete_voices = voice_bytes / DX7_VMEM_SIZE;
    if (complete_voices > DX7_BANK_VOICES) {
        complete_voices = DX7_BANK_VOICES;
    }

    const bool full_bank = complete_voices == DX7_BANK_VOICES;
    if (!full_bank || (voice_bytes % DX7_VMEM_SIZE) != 0u) {
        flags |= DX7_IMPORT_FLAG_TRUNCATED;
        ++report->truncated_messages;
    }

    size_t cursor = payload_start + complete_voices * DX7_VMEM_SIZE;
    if (full_bank) {
        const uint8_t expected_checksum = dx7_yamaha_checksum(data + payload_start, DX7_BANK_DATA_SIZE);
        if (cursor < size && data[cursor] != 0xf0u && data[cursor] != 0xf7u) {
            flags |= DX7_IMPORT_FLAG_CHECKSUM_PRESENT;
            if (data[cursor] == expected_checksum) {
                flags |= DX7_IMPORT_FLAG_CHECKSUM_VALID;
            } else {
                flags |= DX7_IMPORT_FLAG_CHECKSUM_BAD;
                ++report->checksum_bad;
            }
            ++cursor;
        } else {
            flags |= DX7_IMPORT_FLAG_CHECKSUM_MISSING;
            ++report->checksum_missing;
        }
    } else {
        flags |= DX7_IMPORT_FLAG_CHECKSUM_MISSING;
        ++report->checksum_missing;
        cursor = boundary;
    }

    if (has_f7) {
        flags |= DX7_IMPORT_FLAG_F7_PRESENT;
    }

    uint8_t voice[DX7_VCED_SIZE];
    for (size_t i = 0; i < complete_voices; ++i) {
        dx7_vmem_unpack(data + payload_start + i * DX7_VMEM_SIZE, voice);
        if (!emit_voice(voice,
                        start + 6u + i * DX7_VMEM_SIZE,
                        flags,
                        callback,
                        ctx,
                        report)) {
            *next_offset = cursor;
            return false;
        }
    }

    if (has_f7) {
        *next_offset = boundary + 1u;
    } else if (boundary < size && data[boundary] == 0xf0u) {
        *next_offset = boundary;
    } else {
        *next_offset = cursor > boundary ? cursor : boundary;
    }
    return true;
}

static size_t skip_unknown_message(const uint8_t *data,
                                   size_t size,
                                   size_t start,
                                   dx7_import_report_t *report)
{
    ++report->messages_seen;
    ++report->unknown_messages;

    const size_t boundary = find_message_marker(data, size, start + 1u);
    if (boundary >= size) {
        return size;
    }
    return data[boundary] == 0xf7u ? boundary + 1u : boundary;
}

dx7_patch_status_t dx7_import_scan(const uint8_t *data,
                                   size_t size,
                                   dx7_import_voice_cb callback,
                                   void *ctx,
                                   dx7_import_report_t *report)
{
    if (data == NULL || report == NULL) {
        return DX7_PATCH_BAD_ARGUMENT;
    }

    memset(report, 0, sizeof(*report));
    if (size == 0u) {
        return DX7_PATCH_BAD_FORMAT;
    }

    if (!looks_like_sysex(data, size) &&
        (size == DX7_VCED_SIZE || size == DX7_VMEM_SIZE || size == DX7_BANK_DATA_SIZE)) {
        (void)emit_raw(data, size, callback, ctx, report);
        return report->voices_found != 0u ? DX7_PATCH_OK : DX7_PATCH_BAD_FORMAT;
    }

    size_t cursor = 0u;
    while (cursor < size) {
        if (data[cursor] != 0xf0u) {
            ++report->bytes_skipped;
            ++cursor;
            continue;
        }

        if (cursor + 6u > size) {
            ++report->messages_seen;
            ++report->truncated_messages;
            break;
        }

        if (data[cursor + 1u] != 0x43u || (data[cursor + 2u] & 0xf0u) != 0u) {
            cursor = skip_unknown_message(data, size, cursor, report);
            continue;
        }

        const uint8_t format = data[cursor + 3u];
        size_t next = cursor + 1u;
        bool keep_scanning = true;

        if (format == 0x00u) {
            keep_scanning = scan_single(data, size, cursor, callback, ctx, report, &next);
        } else if (format == 0x09u) {
            keep_scanning = scan_bank(data, size, cursor, callback, ctx, report, &next);
        } else {
            next = skip_unknown_message(data, size, cursor, report);
        }

        if (!keep_scanning) {
            return DX7_PATCH_OK;
        }
        if (next <= cursor) {
            next = cursor + 1u;
        }
        cursor = next;
    }

    return report->voices_found != 0u ? DX7_PATCH_OK : DX7_PATCH_BAD_FORMAT;
}
