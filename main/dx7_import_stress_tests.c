#include "dx7_import_stress_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_import.h"
#include "dx7_patch.h"

#define STRESS_COLLECT_MAX 16u
#define FUZZ_STREAM_MAX 2048u
#define FUZZ_SEED 0x6d5a56e9u

typedef struct {
    uint8_t voices[STRESS_COLLECT_MAX][DX7_VCED_SIZE];
    uint32_t flags[STRESS_COLLECT_MAX];
    size_t offsets[STRESS_COLLECT_MAX];
    size_t count;
} stress_collector_t;

typedef struct {
    stress_collector_t collector;
    size_t stop_after;
} early_stop_ctx_t;

static unsigned s_tests;
static unsigned s_failures;

static uint8_t s_voice_a[DX7_VCED_SIZE];
static uint8_t s_voice_b[DX7_VCED_SIZE];
static uint8_t s_single_a[DX7_SINGLE_SYSEX_SIZE];
static uint8_t s_single_b[DX7_SINGLE_SYSEX_SIZE];
static uint8_t s_bank_raw[DX7_BANK_DATA_SIZE];
static uint8_t s_bank_sysex[DX7_BANK_SYSEX_SIZE];
static uint8_t s_early_stream[DX7_BANK_SYSEX_SIZE + DX7_SINGLE_SYSEX_SIZE + 8u];
static uint8_t s_fuzz_a[FUZZ_STREAM_MAX];
static uint8_t s_fuzz_b[FUZZ_STREAM_MAX];

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("DX7_IMPORT_STRESS_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
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
        o[8] = (uint8_t)(40u + ((variant + op * 3u) % 40u));
        o[9] = (uint8_t)((variant + op) % 30u);
        o[10] = (uint8_t)((variant + op * 2u) % 30u);
        o[11] = (uint8_t)(op & 3u);
        o[12] = (uint8_t)((op + 1u) & 3u);
        o[13] = (uint8_t)((variant + op) & 7u);
        o[14] = (uint8_t)(op & 3u);
        o[15] = (uint8_t)((variant + op * 2u) & 7u);
        o[16] = 99u;
        o[17] = (uint8_t)((variant + op) & 1u);
        o[18] = (uint8_t)(1u + ((variant + op) % 12u));
        o[19] = (uint8_t)((variant * 11u + op * 7u) % 100u);
        o[20] = (uint8_t)(4u + ((variant + op) % 7u));
    }

    for (unsigned i = 0; i < 4u; ++i) {
        voice[126u + i] = (uint8_t)(88u + i * 3u);
        voice[130u + i] = 50u;
    }
    voice[134] = (uint8_t)(variant & 31u);
    voice[135] = (uint8_t)(variant & 7u);
    voice[136] = 1u;
    voice[137] = (uint8_t)(35u + variant % 50u);
    voice[138] = (uint8_t)(variant % 30u);
    voice[139] = (uint8_t)(variant % 40u);
    voice[140] = (uint8_t)(variant % 25u);
    voice[141] = 1u;
    voice[142] = (uint8_t)(variant % 6u);
    voice[143] = (uint8_t)(variant % 8u);
    voice[144] = 24u;

    char name[11];
    snprintf(name, sizeof(name), "STR%02uFM6XY", variant % 100u);
    memcpy(voice + DX7_VCED_NAME_OFFSET, name, DX7_VCED_NAME_SIZE);
    dx7_vced_sanitize(voice);
}

static void build_fixtures(void)
{
    make_voice(s_voice_a, 9u);
    make_voice(s_voice_b, 23u);
    (void)dx7_build_single_sysex(s_voice_a, 0u, s_single_a);
    (void)dx7_build_single_sysex(s_voice_b, 7u, s_single_b);

    for (unsigned i = 0; i < DX7_BANK_VOICES; ++i) {
        uint8_t voice[DX7_VCED_SIZE];
        make_voice(voice, i);
        dx7_vmem_pack(voice, s_bank_raw + i * DX7_VMEM_SIZE);
    }
    (void)dx7_build_bank_sysex(s_bank_raw, 0u, s_bank_sysex);
}

static bool collect_cb(const uint8_t voice[DX7_VCED_SIZE],
                       size_t voice_index,
                       size_t source_offset,
                       uint32_t flags,
                       void *ctx)
{
    (void)voice_index;
    stress_collector_t *collector = (stress_collector_t *)ctx;
    if (collector->count >= STRESS_COLLECT_MAX) {
        return false;
    }
    memcpy(collector->voices[collector->count], voice, DX7_VCED_SIZE);
    collector->flags[collector->count] = flags;
    collector->offsets[collector->count] = source_offset;
    ++collector->count;
    return true;
}

static bool early_stop_cb(const uint8_t voice[DX7_VCED_SIZE],
                          size_t voice_index,
                          size_t source_offset,
                          uint32_t flags,
                          void *ctx)
{
    early_stop_ctx_t *state = (early_stop_ctx_t *)ctx;
    (void)voice_index;
    if (state->collector.count >= STRESS_COLLECT_MAX) {
        return false;
    }
    const size_t slot = state->collector.count;
    memcpy(state->collector.voices[slot], voice, DX7_VCED_SIZE);
    state->collector.flags[slot] = flags;
    state->collector.offsets[slot] = source_offset;
    ++state->collector.count;
    return state->collector.count < state->stop_after;
}

static bool reports_equal(const dx7_import_report_t *a, const dx7_import_report_t *b)
{
    return a->messages_seen == b->messages_seen &&
           a->voices_found == b->voices_found &&
           a->unknown_messages == b->unknown_messages &&
           a->truncated_messages == b->truncated_messages &&
           a->checksum_missing == b->checksum_missing &&
           a->checksum_bad == b->checksum_bad &&
           a->nonstandard_count == b->nonstandard_count &&
           a->bytes_skipped == b->bytes_skipped;
}

static bool collectors_equal(const stress_collector_t *a, const stress_collector_t *b)
{
    if (a->count != b->count) {
        return false;
    }
    for (size_t i = 0; i < a->count; ++i) {
        if (a->flags[i] != b->flags[i] || a->offsets[i] != b->offsets[i] ||
            memcmp(a->voices[i], b->voices[i], DX7_VCED_SIZE) != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t fnv1a32(const uint8_t *data, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t prng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static bool append_bytes(uint8_t *out, size_t capacity, size_t *length, const uint8_t *data, size_t size)
{
    if (*length > capacity || size > capacity - *length) {
        return false;
    }
    memcpy(out + *length, data, size);
    *length += size;
    return true;
}

static bool append_noise(uint8_t *out, size_t capacity, size_t *length, uint32_t *seed, size_t count)
{
    if (*length > capacity || count > capacity - *length) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        uint8_t value = (uint8_t)prng_next(seed);
        if (value == 0xf0u || value == 0xf7u) {
            value ^= 0x55u;
        }
        out[(*length)++] = value;
    }
    return true;
}

static size_t build_fuzz_stream(uint8_t *out, size_t capacity, uint32_t seed)
{
    static const uint8_t foreign[] = {0xf0u, 0x7du, 0x31u, 0x22u, 0x13u, 0xf7u};
    static const uint8_t trailing_partial[] = {0xf0u, 0x43u, 0x00u};
    size_t n = 0u;
    uint8_t bad_single[DX7_SINGLE_SYSEX_SIZE];
    memcpy(bad_single, s_single_a, sizeof(bad_single));
    bad_single[DX7_SINGLE_SYSEX_SIZE - 2u] ^= 0x01u;

    if (!append_noise(out, capacity, &n, &seed, 73u) ||
        !append_bytes(out, capacity, &n, s_single_a, sizeof(s_single_a)) ||
        !append_noise(out, capacity, &n, &seed, 51u) ||
        !append_bytes(out, capacity, &n, foreign, sizeof(foreign)) ||
        !append_noise(out, capacity, &n, &seed, 29u)) {
        return 0u;
    }

    /* Deliberately truncated Yamaha single: the next valid F0 appears inside the
     * nominal payload window and must become the resynchronization point. */
    if (!append_bytes(out, capacity, &n, s_single_a, 6u + 37u) ||
        !append_bytes(out, capacity, &n, s_single_b, sizeof(s_single_b)) ||
        !append_noise(out, capacity, &n, &seed, 61u) ||
        !append_bytes(out, capacity, &n, bad_single, sizeof(bad_single)) ||
        !append_noise(out, capacity, &n, &seed, 47u) ||
        !append_bytes(out, capacity, &n, s_single_b, sizeof(s_single_b)) ||
        !append_noise(out, capacity, &n, &seed, 59u) ||
        !append_bytes(out, capacity, &n, trailing_partial, sizeof(trailing_partial))) {
        return 0u;
    }
    return n;
}

static void test_callback_early_stop(void)
{
    size_t n = 0u;
    memcpy(s_early_stream + n, s_bank_sysex, sizeof(s_bank_sysex));
    n += sizeof(s_bank_sysex);
    s_early_stream[n++] = 0x42u;
    memcpy(s_early_stream + n, s_single_b, sizeof(s_single_b));
    n += sizeof(s_single_b);

    early_stop_ctx_t state = {.stop_after = 5u};
    dx7_import_report_t report;
    const dx7_patch_status_t status = dx7_import_scan(
        s_early_stream, n, early_stop_cb, &state, &report);

    uint8_t expected[DX7_VCED_SIZE];
    dx7_vmem_unpack(s_bank_raw + 4u * DX7_VMEM_SIZE, expected);
    const bool pass = status == DX7_PATCH_OK &&
                      state.collector.count == state.stop_after &&
                      report.voices_found == state.stop_after &&
                      report.messages_seen == 1u &&
                      report.bytes_skipped == 0u &&
                      state.collector.offsets[0] == 6u &&
                      state.collector.offsets[4] == 6u + 4u * DX7_VMEM_SIZE &&
                      (state.collector.flags[4] & DX7_IMPORT_FLAG_BANK) != 0u &&
                      memcmp(state.collector.voices[4], expected, DX7_VCED_SIZE) == 0;
    check("callback_early_stop_contract", pass);
}

static void test_deterministic_stream_fuzz(void)
{
    const size_t n_a = build_fuzz_stream(s_fuzz_a, sizeof(s_fuzz_a), FUZZ_SEED);
    const size_t n_b = build_fuzz_stream(s_fuzz_b, sizeof(s_fuzz_b), FUZZ_SEED);

    stress_collector_t a = {0};
    stress_collector_t b = {0};
    dx7_import_report_t report_a;
    dx7_import_report_t report_b;
    const dx7_patch_status_t status_a = n_a != 0u
        ? dx7_import_scan(s_fuzz_a, n_a, collect_cb, &a, &report_a)
        : DX7_PATCH_BAD_FORMAT;
    const dx7_patch_status_t status_b = n_b != 0u
        ? dx7_import_scan(s_fuzz_b, n_b, collect_cb, &b, &report_b)
        : DX7_PATCH_BAD_FORMAT;

    const bool expected_voices = a.count == 4u &&
                                 memcmp(a.voices[0], s_voice_a, DX7_VCED_SIZE) == 0 &&
                                 memcmp(a.voices[1], s_voice_b, DX7_VCED_SIZE) == 0 &&
                                 memcmp(a.voices[2], s_voice_a, DX7_VCED_SIZE) == 0 &&
                                 memcmp(a.voices[3], s_voice_b, DX7_VCED_SIZE) == 0 &&
                                 (a.flags[0] & DX7_IMPORT_FLAG_CHECKSUM_VALID) != 0u &&
                                 (a.flags[1] & DX7_IMPORT_FLAG_CHECKSUM_VALID) != 0u &&
                                 (a.flags[2] & DX7_IMPORT_FLAG_CHECKSUM_BAD) != 0u &&
                                 (a.flags[3] & DX7_IMPORT_FLAG_CHECKSUM_VALID) != 0u;

    const bool pass = n_a != 0u && n_a == n_b &&
                      memcmp(s_fuzz_a, s_fuzz_b, n_a) == 0 &&
                      status_a == DX7_PATCH_OK && status_b == DX7_PATCH_OK &&
                      collectors_equal(&a, &b) && reports_equal(&report_a, &report_b) &&
                      expected_voices &&
                      report_a.checksum_bad == 1u &&
                      report_a.unknown_messages >= 1u &&
                      report_a.truncated_messages >= 2u;
    check("deterministic_stream_fuzz", pass);

    uint32_t signature = fnv1a32(s_fuzz_a, n_a);
    for (size_t i = 0; i < a.count; ++i) {
        signature ^= fnv1a32(a.voices[i], DX7_VCED_SIZE);
        signature *= 16777619u;
        signature ^= a.flags[i];
        signature *= 16777619u;
        signature ^= (uint32_t)a.offsets[i];
        signature *= 16777619u;
    }
    printf("DX7_IMPORT_FUZZ seed=0x%08" PRIx32 " bytes=%u voices=%u passes=2 signature=0x%08" PRIx32
           " truncated=%u unknown=%u checksum_bad=%u\n",
           (uint32_t)FUZZ_SEED,
           (unsigned)n_a,
           (unsigned)a.count,
           signature,
           (unsigned)report_a.truncated_messages,
           (unsigned)report_a.unknown_messages,
           (unsigned)report_a.checksum_bad);
}

unsigned dx7_import_stress_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    build_fixtures();

    test_callback_early_stop();
    test_deterministic_stream_fuzz();

    printf("QEMU_DX7_IMPORT_STRESS: %s tests=%u failures=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures);
    return s_failures;
}
