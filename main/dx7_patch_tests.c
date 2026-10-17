#include "dx7_patch_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"
#include "fm6_port.h"

#define TEST_BLOCKS 128u

static unsigned s_failures;
static unsigned s_tests;

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("DX7_PATCH_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
}

static void make_voice(uint8_t voice[DX7_VCED_SIZE], unsigned variant)
{
    memset(voice, 0, DX7_VCED_SIZE);

    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = voice + op * 21u;
        for (unsigned stage = 0; stage < 4u; ++stage) {
            o[stage] = (uint8_t)(99u - stage * 5u);
            o[4u + stage] = 99u;
        }
        o[8] = (uint8_t)(40u + (op * 3u) % 40u);
        o[9] = (uint8_t)(10u + op);
        o[10] = (uint8_t)(20u + op);
        o[11] = (uint8_t)(op & 3u);
        o[12] = (uint8_t)((op + 1u) & 3u);
        o[13] = (uint8_t)(op & 7u);
        o[14] = (uint8_t)(op & 3u);
        o[15] = (uint8_t)((op + variant) & 7u);
        o[16] = (uint8_t)(99u - op * 3u);
        o[17] = (uint8_t)((variant + op) & 1u);
        o[18] = (uint8_t)(1u + ((op + variant) % 12u));
        o[19] = (uint8_t)((variant * 9u + op * 11u) % 100u);
        o[20] = (uint8_t)(4u + ((op + variant) % 7u));
    }

    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = (uint8_t)(85u + i * 3u);
        voice[130u + i] = 50u;
    }
    voice[134] = (uint8_t)(variant & 31u);
    voice[135] = (uint8_t)(variant & 7u);
    voice[136] = 1u;
    voice[137] = (uint8_t)(35u + (variant % 50u));
    voice[138] = (uint8_t)(variant % 30u);
    voice[139] = (uint8_t)(variant % 40u);
    voice[140] = (uint8_t)(variant % 25u);
    voice[141] = 1u;
    voice[142] = (uint8_t)(variant % 6u);
    voice[143] = (uint8_t)(variant % 8u);
    voice[144] = 24u;

    char name[11];
    snprintf(name, sizeof(name), "QEMU%02uFM6", variant % 100u);
    for (unsigned i = 0; i < DX7_VCED_NAME_SIZE; ++i) {
        voice[DX7_VCED_NAME_OFFSET + i] = (uint8_t)(name[i] ? name[i] : ' ');
    }

    dx7_vced_sanitize(voice);
}

static bool voice_in_range(const uint8_t voice[DX7_VCED_SIZE])
{
    for (unsigned op = 0; op < 6u; ++op) {
        const uint8_t *o = voice + op * 21u;
        for (unsigned i = 0; i < 11u; ++i) {
            if (o[i] > 99u) return false;
        }
        if (o[11] > 3u || o[12] > 3u || o[13] > 7u || o[14] > 3u ||
            o[15] > 7u || o[16] > 99u || o[17] > 1u || o[18] > 31u ||
            o[19] > 99u || o[20] > 14u) {
            return false;
        }
    }

    for (unsigned i = 126u; i < 134u; ++i) {
        if (voice[i] > 99u) return false;
    }
    if (voice[134] > 31u || voice[135] > 7u || voice[136] > 1u ||
        voice[137] > 99u || voice[138] > 99u || voice[139] > 99u || voice[140] > 99u ||
        voice[141] > 1u || voice[142] > 5u || voice[143] > 7u || voice[144] > 48u) {
        return false;
    }
    for (unsigned i = 145u; i < 155u; ++i) {
        if (voice[i] < 32u || voice[i] > 126u) return false;
    }
    return true;
}

static void test_vmem_roundtrip(void)
{
    uint8_t original[DX7_VCED_SIZE];
    uint8_t decoded[DX7_VCED_SIZE];
    uint8_t packed[DX7_VMEM_SIZE];

    make_voice(original, 19u);
    dx7_vmem_pack(original, packed);
    dx7_vmem_unpack(packed, decoded);

    check("vced_vmem_vced_roundtrip", memcmp(original, decoded, sizeof(original)) == 0);

    bool seven_bit = true;
    for (unsigned i = 0; i < DX7_VMEM_SIZE; ++i) {
        seven_bit &= packed[i] <= 127u;
    }
    check("vmem_is_7bit", seven_bit);
}

static void test_sanitize(void)
{
    uint8_t packed[DX7_VMEM_SIZE];
    uint8_t voice[DX7_VCED_SIZE];
    uint8_t canonical[DX7_VMEM_SIZE];
    memset(packed, 0xff, sizeof(packed));

    dx7_vmem_unpack(packed, voice);
    dx7_vmem_pack(voice, canonical);

    bool seven_bit = true;
    for (unsigned i = 0; i < DX7_VMEM_SIZE; ++i) {
        seven_bit &= canonical[i] <= 127u;
    }
    check("invalid_vmem_sanitizes_to_voice_ranges", voice_in_range(voice));
    check("invalid_vmem_repack_is_7bit", seven_bit);
}

static void test_single_sysex(void)
{
    uint8_t voice[DX7_VCED_SIZE];
    uint8_t got[DX7_VCED_SIZE];
    uint8_t sysex[DX7_SINGLE_SYSEX_SIZE];
    dx7_patch_info_t info;

    make_voice(voice, 11u);
    dx7_patch_status_t status = dx7_build_single_sysex(voice, 3u, sysex);
    check("single_sysex_build", status == DX7_PATCH_OK &&
          sysex[0] == 0xf0u && sysex[1] == 0x43u && sysex[2] == 3u &&
          sysex[3] == 0u && sysex[4] == 1u && sysex[5] == 0x1bu &&
          sysex[DX7_SINGLE_SYSEX_SIZE - 1u] == 0xf7u);

    status = dx7_patch_inspect(sysex, sizeof(sysex), &info);
    check("single_sysex_inspect", status == DX7_PATCH_OK &&
          info.kind == DX7_INPUT_SINGLE_SYSEX && info.voice_count == 1u &&
          info.has_checksum && info.checksum_valid);

    status = dx7_patch_get_voice(sysex, sizeof(sysex), 0u, got);
    check("single_sysex_voice_roundtrip", status == DX7_PATCH_OK &&
          memcmp(voice, got, sizeof(voice)) == 0);

    sysex[161] ^= 1u;
    status = dx7_patch_inspect(sysex, sizeof(sysex), &info);
    check("single_sysex_bad_checksum_rejected", status == DX7_PATCH_BAD_CHECKSUM &&
          info.has_checksum && !info.checksum_valid);

    status = dx7_patch_get_voice(sysex, sizeof(sysex), 0u, got);
    check("bad_checksum_cannot_commit_voice", status == DX7_PATCH_BAD_CHECKSUM);
    check("truncated_single_sysex_rejected",
          dx7_patch_inspect(sysex, sizeof(sysex) - 1u, &info) == DX7_PATCH_BAD_FORMAT);
}

static void test_raw_inputs(void)
{
    uint8_t voice[DX7_VCED_SIZE];
    uint8_t packed[DX7_VMEM_SIZE];
    uint8_t got[DX7_VCED_SIZE];
    dx7_patch_info_t info;

    make_voice(voice, 7u);
    dx7_vmem_pack(voice, packed);

    check("raw_vced_inspect",
          dx7_patch_inspect(voice, sizeof(voice), &info) == DX7_PATCH_OK &&
          info.kind == DX7_INPUT_RAW_VCED && info.voice_count == 1u);
    check("raw_vmem_inspect",
          dx7_patch_inspect(packed, sizeof(packed), &info) == DX7_PATCH_OK &&
          info.kind == DX7_INPUT_RAW_VMEM && info.voice_count == 1u);
    check("raw_vmem_get",
          dx7_patch_get_voice(packed, sizeof(packed), 0u, got) == DX7_PATCH_OK &&
          memcmp(voice, got, sizeof(voice)) == 0);
}

static void test_bank_sysex(void)
{
    static uint8_t bank[DX7_BANK_DATA_SIZE];
    static uint8_t sysex[DX7_BANK_SYSEX_SIZE];
    uint8_t expected[DX7_VCED_SIZE];
    uint8_t got[DX7_VCED_SIZE];
    dx7_patch_info_t info;

    for (unsigned i = 0; i < DX7_BANK_VOICES; ++i) {
        make_voice(expected, i);
        dx7_vmem_pack(expected, bank + i * DX7_VMEM_SIZE);
    }

    dx7_patch_status_t status = dx7_build_bank_sysex(bank, 9u, sysex);
    check("bank_sysex_build", status == DX7_PATCH_OK &&
          sysex[0] == 0xf0u && sysex[1] == 0x43u && sysex[2] == 9u &&
          sysex[3] == 0x09u && sysex[4] == 0x20u && sysex[5] == 0x00u &&
          sysex[DX7_BANK_SYSEX_SIZE - 1u] == 0xf7u);

    status = dx7_patch_inspect(sysex, sizeof(sysex), &info);
    check("bank_sysex_inspect", status == DX7_PATCH_OK &&
          info.kind == DX7_INPUT_BANK_SYSEX && info.voice_count == 32u &&
          info.has_checksum && info.checksum_valid);

    bool all_match = true;
    for (unsigned i = 0; i < DX7_BANK_VOICES; ++i) {
        make_voice(expected, i);
        if (dx7_patch_get_voice(sysex, sizeof(sysex), i, got) != DX7_PATCH_OK ||
            memcmp(expected, got, sizeof(expected)) != 0) {
            all_match = false;
            break;
        }
    }
    check("bank_32_voice_roundtrip", all_match);
    check("bank_index_out_of_range",
          dx7_patch_get_voice(sysex, sizeof(sysex), 32u, got) == DX7_PATCH_OUT_OF_RANGE);

    check("raw_bank_inspect",
          dx7_patch_inspect(bank, sizeof(bank), &info) == DX7_PATCH_OK &&
          info.kind == DX7_INPUT_RAW_BANK && info.voice_count == 32u);

    sysex[4102] ^= 1u;
    check("bank_bad_checksum_rejected",
          dx7_patch_inspect(sysex, sizeof(sysex), &info) == DX7_PATCH_BAD_CHECKSUM);
}

typedef struct {
    uint32_t checksum;
    uint64_t energy;
    unsigned sounded;
} render_result_t;

typedef struct {
    uint32_t aggregate;
    unsigned active;
    unsigned failures;
    render_result_t voices[DX7_BANK_VOICES];
} bank_render_result_t;

static render_result_t render_imported(const uint8_t voice[DX7_VCED_SIZE])
{
    int32_t mix[FM6_BENCH_BLOCK_FRAMES];
    const bool prepared = fm6_port_prepare_voice(voice, 12u, 36u, 3u, 110u);
    if (!prepared) {
        const render_result_t failed = {0};
        return failed;
    }

    const unsigned algorithm = fm6_port_patch_algorithm();
    unsigned peak_sounded = 0u;
    for (unsigned i = 0; i < TEST_BLOCKS; ++i) {
        fm6_port_render_block(12u, algorithm, mix);
        const unsigned sounded = fm6_port_last_sounded();
        if (sounded > peak_sounded) {
            peak_sounded = sounded;
        }
    }

    const render_result_t result = {
        .checksum = fm6_port_checksum(),
        .energy = fm6_port_abs_energy(),
        .sounded = peak_sounded,
    };
    return result;
}

static bank_render_result_t render_bank_input(const uint8_t *input, size_t input_size)
{
    bank_render_result_t result = {.aggregate = 2166136261u};
    uint8_t voice[DX7_VCED_SIZE];

    for (unsigned i = 0; i < DX7_BANK_VOICES; ++i) {
        if (dx7_patch_get_voice(input, input_size, i, voice) != DX7_PATCH_OK) {
            ++result.failures;
            continue;
        }

        result.voices[i] = render_imported(voice);
        const render_result_t *r = &result.voices[i];
        if (r->checksum == 0u || r->energy == 0u || r->sounded != 12u) {
            ++result.failures;
        } else {
            ++result.active;
        }

        result.aggregate ^= r->checksum;
        result.aggregate *= 16777619u;
        result.aggregate ^= (uint32_t)r->energy;
        result.aggregate *= 16777619u;
        result.aggregate ^= (uint32_t)(r->energy >> 32);
        result.aggregate *= 16777619u;
        result.aggregate ^= r->sounded;
        result.aggregate *= 16777619u;
    }

    return result;
}

static bool bank_results_equal(const bank_render_result_t *a, const bank_render_result_t *b)
{
    if (a->aggregate != b->aggregate || a->active != b->active || a->failures != b->failures) {
        return false;
    }
    return memcmp(a->voices, b->voices, sizeof(a->voices)) == 0;
}

static void test_full_bank_render(void)
{
    static uint8_t bank[DX7_BANK_DATA_SIZE];
    static uint8_t sysex[DX7_BANK_SYSEX_SIZE];
    uint8_t voice[DX7_VCED_SIZE];

    for (unsigned i = 0; i < DX7_BANK_VOICES; ++i) {
        make_voice(voice, i);
        dx7_vmem_pack(voice, bank + i * DX7_VMEM_SIZE);
    }

    const bool built = dx7_build_bank_sysex(bank, 0u, sysex) == DX7_PATCH_OK;
    const bank_render_result_t sysex_a = built ? render_bank_input(sysex, sizeof(sysex)) : (bank_render_result_t){0};
    const bank_render_result_t sysex_b = built ? render_bank_input(sysex, sizeof(sysex)) : (bank_render_result_t){0};
    const bank_render_result_t raw = render_bank_input(bank, sizeof(bank));

    const bool all_active = built && sysex_a.failures == 0u && sysex_a.active == DX7_BANK_VOICES;
    const bool deterministic = all_active && bank_results_equal(&sysex_a, &sysex_b);
    const bool formats_equal = all_active && raw.failures == 0u &&
                               raw.active == DX7_BANK_VOICES &&
                               bank_results_equal(&sysex_a, &raw);

    check("bank_all_voices_render", all_active);
    check("bank_render_deterministic", deterministic);
    check("raw_bank_sysex_render_equivalence", formats_equal);

    printf("QEMU_DX7_BANK_RENDER: %s voices=%u active=%u passes=2 aggregate=0x%08" PRIx32
           " raw_aggregate=0x%08" PRIx32 " failures=%u\n",
           (all_active && deterministic && formats_equal) ? "PASS" : "FAIL",
           (unsigned)DX7_BANK_VOICES,
           sysex_a.active,
           sysex_a.aggregate,
           raw.aggregate,
           sysex_a.failures + sysex_b.failures + raw.failures);
}

static void test_imported_render(void)
{
    uint8_t original[DX7_VCED_SIZE];
    uint8_t from_vmem[DX7_VCED_SIZE];
    uint8_t from_sysex[DX7_VCED_SIZE];
    uint8_t transposed[DX7_VCED_SIZE];
    uint8_t packed[DX7_VMEM_SIZE];
    uint8_t sysex[DX7_SINGLE_SYSEX_SIZE];

    make_voice(original, 31u);
    dx7_vmem_pack(original, packed);
    dx7_vmem_unpack(packed, from_vmem);
    dx7_build_single_sysex(original, 0u, sysex);
    dx7_patch_get_voice(sysex, sizeof(sysex), 0u, from_sysex);

    const render_result_t a = render_imported(original);
    const render_result_t b = render_imported(original);
    const render_result_t c = render_imported(from_vmem);
    const render_result_t d = render_imported(from_sysex);

    const bool active = a.checksum != 0u && a.energy != 0u && a.sounded == 12u;
    const bool deterministic = a.checksum == b.checksum && a.energy == b.energy && a.sounded == b.sounded;
    const bool formats_equal = a.checksum == c.checksum && a.checksum == d.checksum &&
                               a.energy == c.energy && a.energy == d.energy;

    check("imported_voice_renders_12_voices", active);
    check("imported_voice_render_deterministic", deterministic);
    check("vced_vmem_sysex_render_equivalence", formats_equal);

    memcpy(transposed, original, sizeof(transposed));
    transposed[DX7_VCED_TRANSPOSE_OFFSET] = 36u;
    const render_result_t t = render_imported(transposed);
    check("imported_transpose_changes_audio",
          t.checksum != 0u && t.energy != 0u && t.checksum != a.checksum);

    printf("DX7_PATCH_RENDER checksum=0x%08" PRIx32 " energy=%" PRIu64
           " sounded=%u transpose_checksum=0x%08" PRIx32 "\n",
           a.checksum, a.energy, a.sounded, t.checksum);
}

unsigned dx7_patch_tests_run(void)
{
    s_failures = 0u;
    s_tests = 0u;

    test_vmem_roundtrip();
    test_sanitize();
    test_single_sysex();
    test_raw_inputs();
    test_bank_sysex();
    test_imported_render();
    test_full_bank_render();

    printf("QEMU_DX7_PATCH: %s tests=%u failures=%u single_bytes=%u bank_bytes=%u bank_voices=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures,
           (unsigned)DX7_SINGLE_SYSEX_SIZE,
           (unsigned)DX7_BANK_SYSEX_SIZE,
           (unsigned)DX7_BANK_VOICES);
    return s_failures;
}
