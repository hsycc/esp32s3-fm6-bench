#!/usr/bin/env python3
import pathlib
import re
import sys

SUMMARY_RE = re.compile(
    r"QEMU_DX7_PATCH: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) failures=(?P<failures>\d+) "
    r"single_bytes=(?P<single>\d+) bank_bytes=(?P<bank>\d+) bank_voices=(?P<voices>\d+)"
)
RENDER_RE = re.compile(
    r"DX7_PATCH_RENDER checksum=(?P<checksum>0x[0-9a-fA-F]+) energy=(?P<energy>\d+) "
    r"sounded=(?P<sounded>\d+) transpose_checksum=(?P<transpose>0x[0-9a-fA-F]+)"
)
TEST_RE = re.compile(r"DX7_PATCH_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)")

EXPECTED_TESTS = {
    "vced_vmem_vced_roundtrip",
    "vmem_is_7bit",
    "invalid_vmem_sanitizes_to_voice_ranges",
    "invalid_vmem_repack_is_7bit",
    "single_sysex_build",
    "single_sysex_inspect",
    "single_sysex_voice_roundtrip",
    "single_sysex_bad_checksum_rejected",
    "bad_checksum_cannot_commit_voice",
    "truncated_single_sysex_rejected",
    "raw_vced_inspect",
    "raw_vmem_inspect",
    "raw_vmem_get",
    "bank_sysex_build",
    "bank_sysex_inspect",
    "bank_32_voice_roundtrip",
    "bank_index_out_of_range",
    "raw_bank_inspect",
    "bank_bad_checksum_rejected",
    "imported_voice_renders_12_voices",
    "imported_voice_render_deterministic",
    "vced_vmem_sysex_render_equivalence",
    "imported_transpose_changes_audio",
}


def fail(message: str) -> int:
    print(f"DX7_PATCH_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_dx7_patch_log.py <qemu.log>", file=sys.stderr)
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
        return fail("missing QEMU_DX7_PATCH summary")
    if summary.group("status") != "PASS":
        return fail("QEMU_DX7_PATCH did not pass")
    if int(summary.group("tests")) != len(EXPECTED_TESTS) or int(summary.group("failures")) != 0:
        return fail(
            f"unexpected summary tests={summary.group('tests')} failures={summary.group('failures')}"
        )
    if int(summary.group("single")) != 163 or int(summary.group("bank")) != 4104 or int(summary.group("voices")) != 32:
        return fail("unexpected canonical SysEx dimensions")

    if "QEMU_DX7_PATCH_GATE: PASS failures=0" not in text:
        return fail("missing top-level DX7 patch gate pass")

    render = RENDER_RE.search(text)
    if not render:
        return fail("missing imported render result")
    checksum = int(render.group("checksum"), 16)
    transpose = int(render.group("transpose"), 16)
    if checksum == 0 or int(render.group("energy")) == 0 or int(render.group("sounded")) != 12:
        return fail("imported DX7 voice did not produce an active 12-voice render")
    if transpose == 0 or transpose == checksum:
        return fail("transpose did not change imported render signature")

    print("DX7_PATCH_LOG_CHECK: PASS")
    print(f"  codec      : {len(EXPECTED_TESTS)}/23 directed checks")
    print("  formats    : VCED 155, VMEM 128, single SysEx 163, bank SysEx 4104 / 32 voices")
    print("  checksum   : bad Yamaha checksums rejected before voice commit")
    print("  render     : imported VCED/VMEM/SysEx signatures identical and deterministic at 12 voices")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
