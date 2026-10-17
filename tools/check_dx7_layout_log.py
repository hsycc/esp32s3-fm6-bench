#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(r"DX7_LAYOUT_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)")
SUMMARY_RE = re.compile(r"QEMU_DX7_LAYOUT: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) failures=(?P<failures>\d+)")

EXPECTED_TESTS = {
    "known_pack_operator_direct_bytes",
    "known_pack_lc_rc",
    "known_pack_rs_detune",
    "known_pack_ams_kvs",
    "known_pack_output_level",
    "known_pack_mode_coarse",
    "known_pack_fine",
    "known_pack_pitch_eg",
    "known_pack_algorithm",
    "known_pack_feedback_sync",
    "known_pack_lfo_scalars",
    "known_pack_lfo_bits",
    "known_pack_transpose",
    "known_pack_name",
    "known_unpack_operator_direct_bytes",
    "known_unpack_operator_bits",
    "known_unpack_global_bits",
    "known_unpack_name",
    "known_yamaha_checksum",
}


def fail(message: str) -> int:
    print(f"DX7_LAYOUT_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_dx7_layout_log.py <qemu.log>", file=sys.stderr)
        return 2

    text = pathlib.Path(sys.argv[1]).read_text(errors="replace")
    tests = {m.group("name"): m.group("status") for m in TEST_RE.finditer(text)}

    if set(tests) != EXPECTED_TESTS:
        missing = sorted(EXPECTED_TESTS - set(tests))
        unexpected = sorted(set(tests) - EXPECTED_TESTS)
        return fail(f"coverage mismatch missing={missing} unexpected={unexpected}")

    failed = sorted(name for name, status in tests.items() if status != "PASS")
    if failed:
        return fail(f"tests failed: {failed}")

    summary = SUMMARY_RE.search(text)
    if not summary:
        return fail("missing QEMU_DX7_LAYOUT summary")
    if summary.group("status") != "PASS":
        return fail("layout summary failed")
    if int(summary.group("tests")) != len(EXPECTED_TESTS) or int(summary.group("failures")) != 0:
        return fail(
            f"unexpected summary tests={summary.group('tests')} failures={summary.group('failures')}"
        )

    if "QEMU_DX7_LAYOUT_GATE: PASS failures=0" not in text:
        return fail("missing top-level layout gate pass")

    print("DX7_LAYOUT_LOG_CHECK: PASS")
    print("  operator   : direct bytes and LC/RC, RS/detune, AMS/KVS, mode/coarse bit fields")
    print("  globals    : algorithm, feedback/sync, LFO packed bits, transpose and name")
    print("  checksum   : fixed Yamaha checksum vector")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
