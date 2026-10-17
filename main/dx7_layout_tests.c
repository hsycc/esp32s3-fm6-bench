#include "dx7_layout_tests.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dx7_patch.h"

static unsigned s_tests;
static unsigned s_failures;

static void check(const char *name, bool pass)
{
    ++s_tests;
    if (!pass) {
        ++s_failures;
    }
    printf("DX7_LAYOUT_TEST name=%s status=%s\n", name, pass ? "PASS" : "FAIL");
}

static void make_known_voice(uint8_t voice[DX7_VCED_SIZE])
{
    memset(voice, 0, DX7_VCED_SIZE);

    for (unsigned i = 0; i < DX7_VCED_NAME_SIZE; ++i) {
        voice[DX7_VCED_NAME_OFFSET + i] = (uint8_t)' ';
    }

    uint8_t *op = voice;
    for (unsigned i = 0; i < 11u; ++i) {
        op[i] = (uint8_t)(i + 1u);
    }
    op[11] = 2u;   /* LC */
    op[12] = 3u;   /* RC */
    op[13] = 5u;   /* rate scaling */
    op[14] = 3u;   /* AMS */
    op[15] = 6u;   /* KVS */
    op[16] = 88u;  /* output level */
    op[17] = 1u;   /* fixed mode */
    op[18] = 17u;  /* coarse */
    op[19] = 77u;  /* fine */
    op[20] = 11u;  /* detune */

    for (unsigned i = 0; i < 8u; ++i) {
        voice[126u + i] = (uint8_t)(20u + i);
    }
    voice[134] = 27u;
    voice[135] = 6u;
    voice[136] = 1u;
    voice[137] = 12u;
    voice[138] = 34u;
    voice[139] = 56u;
    voice[140] = 78u;
    voice[141] = 1u;
    voice[142] = 5u;
    voice[143] = 6u;
    voice[144] = 36u;

    static const char name[DX7_VCED_NAME_SIZE + 1u] = "VECTOR1234";
    memcpy(voice + DX7_VCED_NAME_OFFSET, name, DX7_VCED_NAME_SIZE);
}

static void test_known_pack_vector(void)
{
    uint8_t voice[DX7_VCED_SIZE];
    uint8_t packed[DX7_VMEM_SIZE];
    make_known_voice(voice);
    dx7_vmem_pack(voice, packed);

    bool direct = true;
    for (unsigned i = 0; i < 11u; ++i) {
        direct &= packed[i] == (uint8_t)(i + 1u);
    }

    check("known_pack_operator_direct_bytes", direct);
    check("known_pack_lc_rc", packed[11] == 0x0eu);
    check("known_pack_rs_detune", packed[12] == 0x5du);
    check("known_pack_ams_kvs", packed[13] == 0x1bu);
    check("known_pack_output_level", packed[14] == 88u);
    check("known_pack_mode_coarse", packed[15] == 0x23u);
    check("known_pack_fine", packed[16] == 77u);

    bool pitch = true;
    for (unsigned i = 0; i < 8u; ++i) {
        pitch &= packed[102u + i] == (uint8_t)(20u + i);
    }
    check("known_pack_pitch_eg", pitch);
    check("known_pack_algorithm", packed[110] == 27u);
    check("known_pack_feedback_sync", packed[111] == 0x0eu);
    check("known_pack_lfo_scalars",
          packed[112] == 12u && packed[113] == 34u &&
          packed[114] == 56u && packed[115] == 78u);
    check("known_pack_lfo_bits", packed[116] == 0x6bu);
    check("known_pack_transpose", packed[117] == 36u);
    check("known_pack_name", memcmp(packed + 118u, "VECTOR1234", 10u) == 0);
}

static void test_known_unpack_vector(void)
{
    uint8_t packed[DX7_VMEM_SIZE] = {0};
    uint8_t voice[DX7_VCED_SIZE];

    for (unsigned i = 0; i < 11u; ++i) {
        packed[i] = (uint8_t)(i + 1u);
    }
    packed[11] = 0x0eu;
    packed[12] = 0x5du;
    packed[13] = 0x1bu;
    packed[14] = 88u;
    packed[15] = 0x23u;
    packed[16] = 77u;

    for (unsigned i = 0; i < 8u; ++i) {
        packed[102u + i] = (uint8_t)(20u + i);
    }
    packed[110] = 27u;
    packed[111] = 0x0eu;
    packed[112] = 12u;
    packed[113] = 34u;
    packed[114] = 56u;
    packed[115] = 78u;
    packed[116] = 0x6bu;
    packed[117] = 36u;
    memcpy(packed + 118u, "VECTOR1234", 10u);

    dx7_vmem_unpack(packed, voice);
    const uint8_t *op = voice;

    bool direct = true;
    for (unsigned i = 0; i < 11u; ++i) {
        direct &= op[i] == (uint8_t)(i + 1u);
    }
    check("known_unpack_operator_direct_bytes", direct);
    check("known_unpack_operator_bits",
          op[11] == 2u && op[12] == 3u && op[13] == 5u &&
          op[14] == 3u && op[15] == 6u && op[16] == 88u &&
          op[17] == 1u && op[18] == 17u && op[19] == 77u && op[20] == 11u);
    check("known_unpack_global_bits",
          voice[134] == 27u && voice[135] == 6u && voice[136] == 1u &&
          voice[141] == 1u && voice[142] == 5u && voice[143] == 6u &&
          voice[144] == 36u);
    check("known_unpack_name", memcmp(voice + 145u, "VECTOR1234", 10u) == 0);
}

static void test_known_checksum_vector(void)
{
    static const uint8_t data[] = {0x01u, 0x02u, 0x03u};
    check("known_yamaha_checksum", dx7_yamaha_checksum(data, sizeof(data)) == 0x7au);
}

unsigned dx7_layout_tests_run(void)
{
    s_tests = 0u;
    s_failures = 0u;
    test_known_pack_vector();
    test_known_unpack_vector();
    test_known_checksum_vector();

    printf("QEMU_DX7_LAYOUT: %s tests=%u failures=%u\n",
           s_failures == 0u ? "PASS" : "FAIL",
           s_tests,
           s_failures);
    return s_failures;
}
