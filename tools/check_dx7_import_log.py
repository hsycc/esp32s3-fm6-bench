#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(r"DX7_IMPORT_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)")
SUMMARY_RE = re.compile(r"QEMU_DX7_IMPORT: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) failures=(?P<failures>\d+)")
RENDER_RE = re.compile(
    r"DX7_IMPORT_RENDER checksum=(?P<checksum>0x[0-9a-fA-F]+) "
    r"energy=(?P<energy>\d+) sounded=(?P<sounded>\d+)"
)

EXPECTED_TESTS = {
    "concat_single_with_junk",
    "single_missing_checksum_with_f7",
    "single_missing_checksum_no_f7",
    "single_bad_checksum_flagged",
    "bank_nonstandard_count",
    "bank_missing_checksum_with_f7",
    "bank_missing_checksum_no_f7",
    "truncated_bank_keeps_complete_voices",
    "unknown_yamaha_block_skipped",
    "mixed_bank_single_stream",
    "truncated_single_recovers_next_sysex",
    "non_yamaha_block_skipped",
    "trailing_truncated_header_preserves_voice",
    "raw_vced_tolerant_import",
    "raw_vmem_tolerant_import",
    "raw_bank_tolerant_import",
    "tolerant_import_render_equivalence",
}


def fail(message: str) -> int:
    print(f"DX7_IMPORT_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_dx7_import_log.py <qemu.log>", file=sys.stderr)
        return 2

    text = pathlib.Path(sys.argv[1]).read_text(errors="replace")
    tests = {m.group("name"): m.group("status") for m in TEST_RE.finditer(text)}

    if set(tests) != EXPECTED_TESTS:
        missing = sorted(EXPECTED_TESTS - set(tests))
        unexpected = sorted(set(tests) - EXPECTED_TESTS)
        return fail(f"test coverage mismatch missing={missing} unexpected={unexpected}")

    failed = sorted(name for name, status in tests.items() if status != "PASS")
    if failed:
        return fail(f"tests failed: {failed}")

    summary = SUMMARY_RE.search(text)
    if not summary:
        return fail("missing QEMU_DX7_IMPORT summary")
    if summary.group("status") != "PASS":
        return fail("tolerant import summary failed")
    if int(summary.group("tests")) != len(EXPECTED_TESTS) or int(summary.group("failures")) != 0:
        return fail(
            f"unexpected summary tests={summary.group('tests')} failures={summary.group('failures')}"
        )

    if "QEMU_DX7_IMPORT_GATE: PASS failures=0" not in text:
        return fail("missing top-level tolerant import gate pass")

    render = RENDER_RE.search(text)
    if not render:
        return fail("missing tolerant imported render result")
    if int(render.group("checksum"), 16) == 0 or int(render.group("energy")) == 0:
        return fail("tolerant imported voice produced no observable audio")
    if int(render.group("sounded")) != 12:
        return fail(f"tolerant imported render sounded={render.group('sounded')} expected=12")

    print("DX7_IMPORT_LOG_CHECK: PASS")
    print(f"  corpus     : {len(EXPECTED_TESTS)}/{len(EXPECTED_TESTS)} directed tolerant-import checks")
    print("  tolerant   : concatenated messages, mixed bank/single streams, junk bytes, missing checksum/F7, count mismatch")
    print("  recovery   : truncated bank/single and trailing partial header preserve valid voices and resume at the next SysEx")
    print("  isolation  : unknown Yamaha/non-Yamaha blocks skipped; bad checksum surfaced as a warning flag")
    print("  raw input  : 155-byte VCED, 128-byte VMEM and 4096-byte bank")
    print("  render     : tolerant-imported voice matches canonical 12-voice render")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
