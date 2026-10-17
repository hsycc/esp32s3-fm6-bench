#include "dx7_patch.h"

#include <string.h>

#define OP_COUNT 6u
#define VCED_OP_SIZE 21u
#define VMEM_OP_SIZE 17u

#define OP_R1 0u
#define OP_L1 4u
#define OP_BP 8u
#define OP_LD 9u
#define OP_RD 10u
#define OP_LC 11u
#define OP_RC 12u
#define OP_RS 13u
#define OP_AMS 14u
#define OP_KVS 15u
#define OP_OL 16u
#define OP_MODE 17u
#define OP_COARSE 18u
#define OP_FINE 19u
#define OP_DETUNE 20u

#define VCED_PITCH_R1 126u
#define VCED_PITCH_L1 130u
#define VCED_OSC_KEY_SYNC 136u
#define VCED_LFO_SPEED 137u
#define VCED_LFO_DELAY 138u
#define VCED_LFO_PMD 139u
#define VCED_LFO_AMD 140u
#define VCED_LFO_KEY_SYNC 141u
#define VCED_LFO_WAVE 142u
#define VCED_LFO_PMS 143u

static uint8_t clamp_u8(uint8_t value, uint8_t maximum)
{
    return value > maximum ? maximum : value;
}

static void sanitize_operator(uint8_t *op)
{
    for (unsigned i = 0; i < 11u; ++i) {
        op[i] = clamp_u8(op[i], 99u);
    }
    op[OP_LC] = clamp_u8(op[OP_LC], 3u);
    op[OP_RC] = clamp_u8(op[OP_RC], 3u);
    op[OP_RS] = clamp_u8(op[OP_RS], 7u);
    op[OP_AMS] = clamp_u8(op[OP_AMS], 3u);
    op[OP_KVS] = clamp_u8(op[OP_KVS], 7u);
    op[OP_OL] = clamp_u8(op[OP_OL], 99u);
    op[OP_MODE] = clamp_u8(op[OP_MODE], 1u);
    op[OP_COARSE] = clamp_u8(op[OP_COARSE], 31u);
    op[OP_FINE] = clamp_u8(op[OP_FINE], 99u);
    op[OP_DETUNE] = clamp_u8(op[OP_DETUNE], 14u);
}

void dx7_vced_sanitize(uint8_t voice[DX7_VCED_SIZE])
{
    if (voice == NULL) {
        return;
    }

    for (unsigned op = 0; op < OP_COUNT; ++op) {
        sanitize_operator(voice + op * VCED_OP_SIZE);
    }

    for (unsigned i = VCED_PITCH_R1; i <= VCED_PITCH_L1 + 3u; ++i) {
        voice[i] = clamp_u8(voice[i], 99u);
    }
    voice[DX7_VCED_ALGORITHM_OFFSET] = clamp_u8(voice[DX7_VCED_ALGORITHM_OFFSET], 31u);
    voice[DX7_VCED_FEEDBACK_OFFSET] = clamp_u8(voice[DX7_VCED_FEEDBACK_OFFSET], 7u);
    voice[VCED_OSC_KEY_SYNC] = clamp_u8(voice[VCED_OSC_KEY_SYNC], 1u);
    voice[VCED_LFO_SPEED] = clamp_u8(voice[VCED_LFO_SPEED], 99u);
    voice[VCED_LFO_DELAY] = clamp_u8(voice[VCED_LFO_DELAY], 99u);
    voice[VCED_LFO_PMD] = clamp_u8(voice[VCED_LFO_PMD], 99u);
    voice[VCED_LFO_AMD] = clamp_u8(voice[VCED_LFO_AMD], 99u);
    voice[VCED_LFO_KEY_SYNC] = clamp_u8(voice[VCED_LFO_KEY_SYNC], 1u);
    voice[VCED_LFO_WAVE] = clamp_u8(voice[VCED_LFO_WAVE], 5u);
    voice[VCED_LFO_PMS] = clamp_u8(voice[VCED_LFO_PMS], 7u);
    voice[DX7_VCED_TRANSPOSE_OFFSET] = clamp_u8(voice[DX7_VCED_TRANSPOSE_OFFSET], 48u);

    for (unsigned i = 0; i < DX7_VCED_NAME_SIZE; ++i) {
        uint8_t c = voice[DX7_VCED_NAME_OFFSET + i];
        if (c < 32u || c > 126u) {
            c = (uint8_t)' ';
        }
        voice[DX7_VCED_NAME_OFFSET + i] = c;
    }
}

void dx7_vmem_unpack(const uint8_t packed[DX7_VMEM_SIZE], uint8_t voice[DX7_VCED_SIZE])
{
    if (packed == NULL || voice == NULL) {
        return;
    }

    memset(voice, 0, DX7_VCED_SIZE);

    for (unsigned op = 0; op < OP_COUNT; ++op) {
        const uint8_t *src = packed + op * VMEM_OP_SIZE;
        uint8_t *dst = voice + op * VCED_OP_SIZE;

        for (unsigned i = 0; i < 11u; ++i) {
            dst[i] = src[i] & 0x7fu;
        }
        dst[OP_LC] = src[11] & 0x03u;
        dst[OP_RC] = (src[11] >> 2) & 0x03u;
        dst[OP_RS] = src[12] & 0x07u;
        dst[OP_DETUNE] = (src[12] >> 3) & 0x0fu;
        dst[OP_AMS] = src[13] & 0x03u;
        dst[OP_KVS] = (src[13] >> 2) & 0x07u;
        dst[OP_OL] = src[14] & 0x7fu;
        dst[OP_MODE] = src[15] & 0x01u;
        dst[OP_COARSE] = (src[15] >> 1) & 0x1fu;
        dst[OP_FINE] = src[16] & 0x7fu;
    }

    for (unsigned i = 0; i < 8u; ++i) {
        voice[VCED_PITCH_R1 + i] = packed[102u + i] & 0x7fu;
    }
    voice[DX7_VCED_ALGORITHM_OFFSET] = packed[110] & 0x1fu;
    voice[DX7_VCED_FEEDBACK_OFFSET] = packed[111] & 0x07u;
    voice[VCED_OSC_KEY_SYNC] = (packed[111] >> 3) & 0x01u;
    voice[VCED_LFO_SPEED] = packed[112] & 0x7fu;
    voice[VCED_LFO_DELAY] = packed[113] & 0x7fu;
    voice[VCED_LFO_PMD] = packed[114] & 0x7fu;
    voice[VCED_LFO_AMD] = packed[115] & 0x7fu;
    voice[VCED_LFO_KEY_SYNC] = packed[116] & 0x01u;
    voice[VCED_LFO_WAVE] = (packed[116] >> 1) & 0x07u;
    voice[VCED_LFO_PMS] = (packed[116] >> 4) & 0x07u;
    voice[DX7_VCED_TRANSPOSE_OFFSET] = packed[117] & 0x7fu;
    for (unsigned i = 0; i < DX7_VCED_NAME_SIZE; ++i) {
        voice[DX7_VCED_NAME_OFFSET + i] = packed[118u + i] & 0x7fu;
    }

    dx7_vced_sanitize(voice);
}

void dx7_vmem_pack(const uint8_t voice[DX7_VCED_SIZE], uint8_t packed[DX7_VMEM_SIZE])
{
    if (voice == NULL || packed == NULL) {
        return;
    }

    uint8_t canonical[DX7_VCED_SIZE];
    memcpy(canonical, voice, sizeof(canonical));
    dx7_vced_sanitize(canonical);
    memset(packed, 0, DX7_VMEM_SIZE);

    for (unsigned op = 0; op < OP_COUNT; ++op) {
        const uint8_t *src = canonical + op * VCED_OP_SIZE;
        uint8_t *dst = packed + op * VMEM_OP_SIZE;

        for (unsigned i = 0; i < 11u; ++i) {
            dst[i] = src[i] & 0x7fu;
        }
        dst[11] = (uint8_t)((src[OP_LC] & 0x03u) | ((src[OP_RC] & 0x03u) << 2));
        dst[12] = (uint8_t)((src[OP_RS] & 0x07u) | ((src[OP_DETUNE] & 0x0fu) << 3));
        dst[13] = (uint8_t)((src[OP_AMS] & 0x03u) | ((src[OP_KVS] & 0x07u) << 2));
        dst[14] = src[OP_OL] & 0x7fu;
        dst[15] = (uint8_t)((src[OP_MODE] & 0x01u) | ((src[OP_COARSE] & 0x1fu) << 1));
        dst[16] = src[OP_FINE] & 0x7fu;
    }

    for (unsigned i = 0; i < 8u; ++i) {
        packed[102u + i] = canonical[VCED_PITCH_R1 + i] & 0x7fu;
    }
    packed[110] = canonical[DX7_VCED_ALGORITHM_OFFSET] & 0x1fu;
    packed[111] = (uint8_t)((canonical[DX7_VCED_FEEDBACK_OFFSET] & 0x07u) |
                            ((canonical[VCED_OSC_KEY_SYNC] & 0x01u) << 3));
    packed[112] = canonical[VCED_LFO_SPEED] & 0x7fu;
    packed[113] = canonical[VCED_LFO_DELAY] & 0x7fu;
    packed[114] = canonical[VCED_LFO_PMD] & 0x7fu;
    packed[115] = canonical[VCED_LFO_AMD] & 0x7fu;
    packed[116] = (uint8_t)((canonical[VCED_LFO_KEY_SYNC] & 0x01u) |
                            ((canonical[VCED_LFO_WAVE] & 0x07u) << 1) |
                            ((canonical[VCED_LFO_PMS] & 0x07u) << 4));
    packed[117] = canonical[DX7_VCED_TRANSPOSE_OFFSET] & 0x7fu;
    for (unsigned i = 0; i < DX7_VCED_NAME_SIZE; ++i) {
        packed[118u + i] = canonical[DX7_VCED_NAME_OFFSET + i] & 0x7fu;
    }
}

uint8_t dx7_yamaha_checksum(const uint8_t *data, size_t size)
{
    if (data == NULL) {
        return 0u;
    }

    uint32_t sum = 0u;
    for (size_t i = 0; i < size; ++i) {
        sum += data[i];
    }
    return (uint8_t)((0u - sum) & 0x7fu);
}

static bool valid_yamaha_substatus(uint8_t value)
{
    return (value & 0xf0u) == 0u;
}

static dx7_patch_status_t inspect_single_sysex(const uint8_t *data, dx7_patch_info_t *info)
{
    if (data[0] != 0xf0u || data[1] != 0x43u || !valid_yamaha_substatus(data[2]) ||
        data[3] != 0x00u || data[4] != 0x01u || data[5] != 0x1bu ||
        data[DX7_SINGLE_SYSEX_SIZE - 1u] != 0xf7u) {
        return DX7_PATCH_BAD_FORMAT;
    }

    info->kind = DX7_INPUT_SINGLE_SYSEX;
    info->voice_count = 1u;
    info->has_checksum = true;
    info->checksum_valid = dx7_yamaha_checksum(data + 6u, DX7_VCED_SIZE) == data[161u];
    return info->checksum_valid ? DX7_PATCH_OK : DX7_PATCH_BAD_CHECKSUM;
}

static dx7_patch_status_t inspect_bank_sysex(const uint8_t *data, dx7_patch_info_t *info)
{
    if (data[0] != 0xf0u || data[1] != 0x43u || !valid_yamaha_substatus(data[2]) ||
        data[3] != 0x09u || data[4] != 0x20u || data[5] != 0x00u ||
        data[DX7_BANK_SYSEX_SIZE - 1u] != 0xf7u) {
        return DX7_PATCH_BAD_FORMAT;
    }

    info->kind = DX7_INPUT_BANK_SYSEX;
    info->voice_count = DX7_BANK_VOICES;
    info->has_checksum = true;
    info->checksum_valid = dx7_yamaha_checksum(data + 6u, DX7_BANK_DATA_SIZE) == data[4102u];
    return info->checksum_valid ? DX7_PATCH_OK : DX7_PATCH_BAD_CHECKSUM;
}

dx7_patch_status_t dx7_patch_inspect(const uint8_t *data, size_t size, dx7_patch_info_t *info)
{
    if (data == NULL || info == NULL) {
        return DX7_PATCH_BAD_ARGUMENT;
    }

    memset(info, 0, sizeof(*info));

    if (size == DX7_VCED_SIZE) {
        info->kind = DX7_INPUT_RAW_VCED;
        info->voice_count = 1u;
        return DX7_PATCH_OK;
    }
    if (size == DX7_VMEM_SIZE) {
        info->kind = DX7_INPUT_RAW_VMEM;
        info->voice_count = 1u;
        return DX7_PATCH_OK;
    }
    if (size == DX7_BANK_DATA_SIZE) {
        info->kind = DX7_INPUT_RAW_BANK;
        info->voice_count = DX7_BANK_VOICES;
        return DX7_PATCH_OK;
    }
    if (size == DX7_SINGLE_SYSEX_SIZE) {
        return inspect_single_sysex(data, info);
    }
    if (size == DX7_BANK_SYSEX_SIZE) {
        return inspect_bank_sysex(data, info);
    }

    return DX7_PATCH_BAD_FORMAT;
}

dx7_patch_status_t dx7_patch_get_voice(const uint8_t *data,
                                       size_t size,
                                       size_t voice_index,
                                       uint8_t voice[DX7_VCED_SIZE])
{
    if (data == NULL || voice == NULL) {
        return DX7_PATCH_BAD_ARGUMENT;
    }

    dx7_patch_info_t info;
    const dx7_patch_status_t status = dx7_patch_inspect(data, size, &info);
    if (status != DX7_PATCH_OK) {
        return status;
    }
    if (voice_index >= info.voice_count) {
        return DX7_PATCH_OUT_OF_RANGE;
    }

    switch (info.kind) {
    case DX7_INPUT_RAW_VCED:
        memcpy(voice, data, DX7_VCED_SIZE);
        dx7_vced_sanitize(voice);
        return DX7_PATCH_OK;
    case DX7_INPUT_RAW_VMEM:
        dx7_vmem_unpack(data, voice);
        return DX7_PATCH_OK;
    case DX7_INPUT_RAW_BANK:
        dx7_vmem_unpack(data + voice_index * DX7_VMEM_SIZE, voice);
        return DX7_PATCH_OK;
    case DX7_INPUT_SINGLE_SYSEX:
        memcpy(voice, data + 6u, DX7_VCED_SIZE);
        dx7_vced_sanitize(voice);
        return DX7_PATCH_OK;
    case DX7_INPUT_BANK_SYSEX:
        dx7_vmem_unpack(data + 6u + voice_index * DX7_VMEM_SIZE, voice);
        return DX7_PATCH_OK;
    default:
        return DX7_PATCH_BAD_FORMAT;
    }
}

dx7_patch_status_t dx7_build_single_sysex(const uint8_t voice[DX7_VCED_SIZE],
                                           uint8_t channel,
                                           uint8_t out[DX7_SINGLE_SYSEX_SIZE])
{
    if (voice == NULL || out == NULL) {
        return DX7_PATCH_BAD_ARGUMENT;
    }

    uint8_t canonical[DX7_VCED_SIZE];
    memcpy(canonical, voice, sizeof(canonical));
    dx7_vced_sanitize(canonical);

    out[0] = 0xf0u;
    out[1] = 0x43u;
    out[2] = channel & 0x0fu;
    out[3] = 0x00u;
    out[4] = 0x01u;
    out[5] = 0x1bu;
    memcpy(out + 6u, canonical, DX7_VCED_SIZE);
    out[161] = dx7_yamaha_checksum(out + 6u, DX7_VCED_SIZE);
    out[162] = 0xf7u;
    return DX7_PATCH_OK;
}

dx7_patch_status_t dx7_build_bank_sysex(const uint8_t packed_bank[DX7_BANK_DATA_SIZE],
                                         uint8_t channel,
                                         uint8_t out[DX7_BANK_SYSEX_SIZE])
{
    if (packed_bank == NULL || out == NULL) {
        return DX7_PATCH_BAD_ARGUMENT;
    }

    out[0] = 0xf0u;
    out[1] = 0x43u;
    out[2] = channel & 0x0fu;
    out[3] = 0x09u;
    out[4] = 0x20u;
    out[5] = 0x00u;

    for (unsigned voice_index = 0; voice_index < DX7_BANK_VOICES; ++voice_index) {
        uint8_t voice[DX7_VCED_SIZE];
        uint8_t canonical[DX7_VMEM_SIZE];
        dx7_vmem_unpack(packed_bank + voice_index * DX7_VMEM_SIZE, voice);
        dx7_vmem_pack(voice, canonical);
        memcpy(out + 6u + voice_index * DX7_VMEM_SIZE, canonical, DX7_VMEM_SIZE);
    }

    out[4102] = dx7_yamaha_checksum(out + 6u, DX7_BANK_DATA_SIZE);
    out[4103] = 0xf7u;
    return DX7_PATCH_OK;
}

const char *dx7_patch_status_name(dx7_patch_status_t status)
{
    switch (status) {
    case DX7_PATCH_OK:
        return "ok";
    case DX7_PATCH_BAD_ARGUMENT:
        return "bad_argument";
    case DX7_PATCH_BAD_FORMAT:
        return "bad_format";
    case DX7_PATCH_BAD_CHECKSUM:
        return "bad_checksum";
    case DX7_PATCH_OUT_OF_RANGE:
        return "out_of_range";
    default:
        return "unknown";
    }
}
