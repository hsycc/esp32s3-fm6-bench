#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DX7_VCED_SIZE 155u
#define DX7_VMEM_SIZE 128u
#define DX7_BANK_VOICES 32u
#define DX7_BANK_DATA_SIZE (DX7_VMEM_SIZE * DX7_BANK_VOICES)
#define DX7_SINGLE_SYSEX_SIZE 163u
#define DX7_BANK_SYSEX_SIZE 4104u

#define DX7_VCED_ALGORITHM_OFFSET 134u
#define DX7_VCED_FEEDBACK_OFFSET 135u
#define DX7_VCED_TRANSPOSE_OFFSET 144u
#define DX7_VCED_NAME_OFFSET 145u
#define DX7_VCED_NAME_SIZE 10u

typedef enum {
    DX7_PATCH_OK = 0,
    DX7_PATCH_BAD_ARGUMENT,
    DX7_PATCH_BAD_FORMAT,
    DX7_PATCH_BAD_CHECKSUM,
    DX7_PATCH_OUT_OF_RANGE,
} dx7_patch_status_t;

typedef enum {
    DX7_INPUT_NONE = 0,
    DX7_INPUT_RAW_VCED,
    DX7_INPUT_RAW_VMEM,
    DX7_INPUT_RAW_BANK,
    DX7_INPUT_SINGLE_SYSEX,
    DX7_INPUT_BANK_SYSEX,
} dx7_input_kind_t;

typedef struct {
    dx7_input_kind_t kind;
    size_t voice_count;
    bool has_checksum;
    bool checksum_valid;
} dx7_patch_info_t;

void dx7_vced_sanitize(uint8_t voice[DX7_VCED_SIZE]);
void dx7_vmem_unpack(const uint8_t packed[DX7_VMEM_SIZE], uint8_t voice[DX7_VCED_SIZE]);
void dx7_vmem_pack(const uint8_t voice[DX7_VCED_SIZE], uint8_t packed[DX7_VMEM_SIZE]);
uint8_t dx7_yamaha_checksum(const uint8_t *data, size_t size);

dx7_patch_status_t dx7_patch_inspect(const uint8_t *data, size_t size, dx7_patch_info_t *info);
dx7_patch_status_t dx7_patch_get_voice(const uint8_t *data,
                                       size_t size,
                                       size_t voice_index,
                                       uint8_t voice[DX7_VCED_SIZE]);

dx7_patch_status_t dx7_build_single_sysex(const uint8_t voice[DX7_VCED_SIZE],
                                           uint8_t channel,
                                           uint8_t out[DX7_SINGLE_SYSEX_SIZE]);
dx7_patch_status_t dx7_build_bank_sysex(const uint8_t packed_bank[DX7_BANK_DATA_SIZE],
                                         uint8_t channel,
                                         uint8_t out[DX7_BANK_SYSEX_SIZE]);

const char *dx7_patch_status_name(dx7_patch_status_t status);
