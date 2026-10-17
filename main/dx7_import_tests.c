#include "dx7_import_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_import.h"
#include "dx7_patch.h"
#include "fm6_port.h"

#define COLLECT_MAX 32u
#define RENDER_BLOCKS 96u

typedef struct {
    uint8_t voices[COLLECT_MAX][DX7_VCED_SIZE];
    uint32_t flags[COLLECT_MAX];
    size_t offsets[COLLECT_MAX];
    size_t count;
} collector_t;

typedef struct {
    uint32_t checksum;
    uint64_t energy;
    unsigned sounded;
} render_result_t;

static collector_t s_collector;
static uint8_t s_voice_a[DX7_VCED_SIZE];
static uint8_t s_voice_b[DX7_VCED_SIZE];
static uint8_t s_single_a[DX7_SINGLE_SYSEX_SIZE];
static uint8_t s_single_b[DX7_SINGLE_SYSEX_SIZE];
static uint8_t s_bank_raw[DX7_BANK_DATA_SIZE];
static uint8_t s_bank_sysex[DX7_BANK_SYSEX_SIZE];
static uint8_t s_scratch[DX7_BANK_SYSEX_SIZE + DX7_SINGLE_SYSEX_SIZE + 32u];

static unsigned s_tests;
static unsigned s_failures;

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("DX7_IMPORT_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
}

static void make_voice(uint8_t voice[DX7_VCED_SIZE], unsigned variant)
{
    memset(voice, 0, DX7_VCED_SIZE);
    for (unsigned op = 0; op < 6u; ++op) {
        uint8_t *o = voice + op * 21u;
        for (unsigned stage = 0; stage < 4u; ++stage) {
            o[stage] = 99u;
            o[4u + stage] = 99u;
        }
        o[8] = 50u;
        o[9] = 0u;
        o[10] = 0u;
        o[11] = 0u;
        o[12] = 0u;
        o[13] = (uint8_t)(variant & 7u);
        o[14] = 0u;
        o[15] = (uint8_t)((variant + op) & 7u);
        o[16] = 99u;
        o[17] = 0u;
        o[18] = (uint8_t)(1u + ((variant + op) % 8u));
        o[19] = (uint8_t)((variant * 7u + op * 9u) % 100u);
        o[20] = 7u;
    }

    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = 99u;
        voice[130u + i] = 50u;
    }
    voice[134] = (uint8_t)(variant & 31u);
    voice[135] = (uint8_t)(variant & 7u);
    voice[136] = 1u;
    voice[137] = 45u;
    voice[138] = 0u;
    voice[139] = 0u;
    voice[140] = 0u;
    voice[141] = 0u;
    voice[142] = 0u;
    voice[143] = 0u;
    voice[144] = 24u;

    char name[11];
    snprintf(name, sizeof(name), "IMP%02uTEST", variant % 100u);
    for (unsigned i = 0; i < DX7_VCED_NAME_SIZE; ++i) {
        voice[145u + i] = (uint8_t)(name[i] ? name[i] : ' ');
    }
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voice_a, 5u);
    make_voice(s_voice_b, 17u);
    (void)dx7_build_single_sysex(s_voice_a, 0u, s_single_a);
    (void)dx7_build_single_sysex(s_voice_b, 4u, s_single_b);

    for (unsigned i = 0; i < DX7_BANK_VOICES; ++i) {
        uint8_t voice[DX7_VCED_SIZE];
        make_voice(voice, i);
        dx7_vmem_pack(voice, s_bank_raw + i * DX7_VMEM_SIZE);
    }
    (void)dx7_build_bank_sysex(s_bank_raw, 0u, s_bank_sysex);
}

static bool collect_voice(const uint8_t voice[DX7_VCED_SIZE],
                          size_t voice_index,
                          size_t source_offset,
                          uint32_t flags,
                          void *ctx)
{
    (void)voice_index;
    collector_t *collector = (collector_t *)ctx;
    if (collector->count >= COLLECT_MAX) {
        return false;
    }
    memcpy(collector->voices[collector->count], voice, DX7_VCED_SIZE);
    collector->flags[collector->count] = flags;
    collector->offsets[collector->count] = source_offset;
    ++collector->count;
    return true;
}

static dx7_patch_status_t scan(const uint8_t *data, size_t size, dx7_import_report_t *report)
{
    memset(&s_collector, 0, sizeof(s_collector));
    return dx7_import_scan(data, size, collect_voice, &s_collector, report);
}

static void test_concatenated_singles_with_junk(void)
{
    size_t n = 0u;
    s_scratch[n++] = 0x55u;
    s_scratch[n++] = 0x00u;
    s_scratch[n++] = 0x7fu;
    memcpy(s_scratch + n, s_single_a, sizeof(s_single_a));
    n += sizeof(s_single_a);
    s_scratch[n++] = 0x12u;
    s_scratch[n++] = 0x34u;
    memcpy(s_scratch + n, s_single_b, sizeof(s_single_b));
    n += sizeof(s_single_b);

    dx7_import_report_t report;
    const dx7_patch_status_t status = scan(s_scratch, n, &report);
    check("concat_single_with_junk",
          status == DX7_PATCH_OK &&
          s_collector.count == 2u &&
          report.messages_seen == 2u &&
          report.bytes_skipped == 5u &&
          memcmp(s_collector.voices[0], s_voice_a, DX7_VCED_SIZE) == 0 &&
          memcmp(s_collector.voices[1], s_voice_b, DX7_VCED_SIZE) == 0 &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_CHECKSUM_VALID) != 0u &&
          (s_collector.flags[1] & DX7_IMPORT_FLAG_F7_PRESENT) != 0u);
}

static void test_single_missing_checksum_variants(void)
{
    memcpy(s_scratch, s_single_a, 161u);
    s_scratch[161] = 0xf7u;
    dx7_import_report_t report;
    dx7_patch_status_t status = scan(s_scratch, 162u, &report);
    check("single_missing_checksum_with_f7",
          status == DX7_PATCH_OK && s_collector.count == 1u &&
          report.checksum_missing == 1u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_CHECKSUM_MISSING) != 0u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_F7_PRESENT) != 0u &&
          memcmp(s_collector.voices[0], s_voice_a, DX7_VCED_SIZE) == 0);

    status = scan(s_scratch, 161u, &report);
    check("single_missing_checksum_no_f7",
          status == DX7_PATCH_OK && s_collector.count == 1u &&
          report.checksum_missing == 1u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_F7_PRESENT) == 0u);
}

static void test_bad_checksum_is_flagged_not_hidden(void)
{
    memcpy(s_scratch, s_single_a, sizeof(s_single_a));
    s_scratch[161] ^= 1u;

    dx7_import_report_t report;
    const dx7_patch_status_t status = scan(s_scratch, sizeof(s_single_a), &report);
    check("single_bad_checksum_flagged",
          status == DX7_PATCH_OK && s_collector.count == 1u &&
          report.checksum_bad == 1u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_CHECKSUM_BAD) != 0u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_CHECKSUM_VALID) == 0u);
}

static void test_bank_count_and_missing_suffixes(void)
{
    dx7_import_report_t report;

    memcpy(s_scratch, s_bank_sysex, sizeof(s_bank_sysex));
    s_scratch[4] = 0x10u;
    dx7_patch_status_t status = scan(s_scratch, sizeof(s_bank_sysex), &report);
    check("bank_nonstandard_count",
          status == DX7_PATCH_OK && s_collector.count == 32u &&
          report.nonstandard_count == 1u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_NONSTANDARD_COUNT) != 0u);

    memcpy(s_scratch, s_bank_sysex, 4102u);
    s_scratch[4102] = 0xf7u;
    status = scan(s_scratch, 4103u, &report);
    check("bank_missing_checksum_with_f7",
          status == DX7_PATCH_OK && s_collector.count == 32u &&
          report.checksum_missing == 1u &&
          (s_collector.flags[31] & DX7_IMPORT_FLAG_F7_PRESENT) != 0u);

    status = scan(s_scratch, 4102u, &report);
    check("bank_missing_checksum_no_f7",
          status == DX7_PATCH_OK && s_collector.count == 32u &&
          report.checksum_missing == 1u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_F7_PRESENT) == 0u);
}

static void test_truncated_bank_keeps_only_complete_voices(void)
{
    const size_t complete = 10u;
    const size_t partial = 17u;
    const size_t payload_bytes = complete * DX7_VMEM_SIZE + partial;
    memcpy(s_scratch, s_bank_sysex, 6u);
    memcpy(s_scratch + 6u, s_bank_raw, payload_bytes);
    s_scratch[6u + payload_bytes] = 0xf7u;

    dx7_import_report_t report;
    const dx7_patch_status_t status = scan(s_scratch, 7u + payload_bytes, &report);
    check("truncated_bank_keeps_complete_voices",
          status == DX7_PATCH_OK && s_collector.count == complete &&
          report.truncated_messages == 1u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_TRUNCATED) != 0u);
}

static void test_unknown_message_is_skipped(void)
{
    static const uint8_t unknown[] = {0xf0u, 0x43u, 0x00u, 0x7du, 0x01u, 0x02u, 0x03u, 0x04u, 0xf7u};
    size_t n = 0u;
    memcpy(s_scratch + n, unknown, sizeof(unknown));
    n += sizeof(unknown);
    s_scratch[n++] = 0x66u;
    memcpy(s_scratch + n, s_single_a, sizeof(s_single_a));
    n += sizeof(s_single_a);

    dx7_import_report_t report;
    const dx7_patch_status_t status = scan(s_scratch, n, &report);
    check("unknown_yamaha_block_skipped",
          status == DX7_PATCH_OK && s_collector.count == 1u &&
          report.unknown_messages == 1u &&
          memcmp(s_collector.voices[0], s_voice_a, DX7_VCED_SIZE) == 0);
}

static void test_raw_inputs(void)
{
    uint8_t packed[DX7_VMEM_SIZE];
    dx7_vmem_pack(s_voice_a, packed);
    dx7_import_report_t report;

    dx7_patch_status_t status = scan(s_voice_a, DX7_VCED_SIZE, &report);
    check("raw_vced_tolerant_import",
          status == DX7_PATCH_OK && s_collector.count == 1u &&
          (s_collector.flags[0] & DX7_IMPORT_FLAG_RAW) != 0u);

    status = scan(packed, DX7_VMEM_SIZE, &report);
    check("raw_vmem_tolerant_import",
          status == DX7_PATCH_OK && s_collector.count == 1u &&
          memcmp(s_collector.voices[0], s_voice_a, DX7_VCED_SIZE) == 0);

    status = scan(s_bank_raw, DX7_BANK_DATA_SIZE, &report);
    check("raw_bank_tolerant_import",
          status == DX7_PATCH_OK && s_collector.count == 32u &&
          (s_collector.flags[31] & DX7_IMPORT_FLAG_BANK) != 0u);
}

static render_result_t render_voice(const uint8_t voice[DX7_VCED_SIZE])
{
    int32_t mix[FM6_BENCH_BLOCK_FRAMES];
    render_result_t result = {0};
    if (!fm6_port_prepare_voice(voice, 12u, 36u, 3u, 120u)) {
        return result;
    }

    const unsigned alg = fm6_port_patch_algorithm();
    for (unsigned i = 0; i < RENDER_BLOCKS; ++i) {
        fm6_port_render_block(12u, alg, mix);
    }
    result.checksum = fm6_port_checksum();
    result.energy = fm6_port_abs_energy();
    result.sounded = fm6_port_last_sounded();
    return result;
}

static void test_tolerant_import_render_equivalence(void)
{
    memcpy(s_scratch, s_single_a, 161u);
    s_scratch[161] = 0xf7u;
    dx7_import_report_t report;
    const dx7_patch_status_t status = scan(s_scratch, 162u, &report);

    const render_result_t direct = render_voice(s_voice_a);
    const render_result_t imported = s_collector.count == 1u
        ? render_voice(s_collector.voices[0])
        : (render_result_t){0};

    const bool pass = status == DX7_PATCH_OK &&
                      direct.checksum != 0u && direct.energy != 0u && direct.sounded == 12u &&
                      imported.checksum == direct.checksum &&
                      imported.energy == direct.energy &&
                      imported.sounded == direct.sounded;
    check("tolerant_import_render_equivalence", pass);

    printf("DX7_IMPORT_RENDER checksum=0x%08" PRIx32 " energy=%" PRIu64 " sounded=%u\n",
           imported.checksum, imported.energy, imported.sounded);
}

unsigned dx7_import_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    build_fixtures();

    test_concatenated_singles_with_junk();
    test_single_missing_checksum_variants();
    test_bad_checksum_is_flagged_not_hidden();
    test_bank_count_and_missing_suffixes();
    test_truncated_bank_keeps_only_complete_voices();
    test_unknown_message_is_skipped();
    test_raw_inputs();
    test_tolerant_import_render_equivalence();

    printf("QEMU_DX7_IMPORT: %s tests=%u failures=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures);
    return s_failures;
}
