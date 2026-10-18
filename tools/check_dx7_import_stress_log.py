#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(
    r"DX7_IMPORT_STRESS_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
SUMMARY_RE = re.compile(
    r"QEMU_DX7_IMPORT_STRESS: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) failures=(?P<failures>\d+)"
)
FUZZ_RE = re.compile(
    r"DX7_IMPORT_FUZZ seed=(?P<seed>0x[0-9a-fA-F]+) bytes=(?P<bytes>\d+) "
    r"voices=(?P<voices>\d+) passes=(?P<passes>\d+) signature=(?P<signature>0x[0-9a-fA-F]+) "
    r"truncated=(?P<truncated>\d+) unknown=(?P<unknown>\d+) checksum_bad=(?P<checksum_bad>\d+)"
)

EXPECTED_TESTS = {
    "callback_early_stop_contract",
    "deterministic_stream_fuzz",
}
EXPECTED_SEED = 0x6D5A56E9


def fail(message: str) -> int:
    print(f"DX7_IMPORT_STRESS_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_dx7_import_stress_log.py <qemu.log>", file=sys.stderr)
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
        return fail("missing QEMU_DX7_IMPORT_STRESS summary")
    if summary.group("status") != "PASS" or int(summary.group("failures")) != 0:
        return fail("DX7 importer stress summary did not pass")
    if int(summary.group("tests")) != len(EXPECTED_TESTS):
        return fail(f"unexpected stress test count {summary.group('tests')}")

    if "QEMU_DX7_IMPORT_STRESS_GATE: PASS failures=0" not in text:
        return fail("missing top-level DX7 importer stress gate pass")

    fuzz = FUZZ_RE.search(text)
    if not fuzz:
        return fail("missing deterministic fuzz summary")
    if int(fuzz.group("seed"), 16) != EXPECTED_SEED:
        return fail("unexpected deterministic fuzz seed")
    if int(fuzz.group("bytes")) <= 0 or int(fuzz.group("voices")) != 4:
        return fail("unexpected fuzz corpus dimensions")
    if int(fuzz.group("passes")) != 2:
        return fail("fuzz stream was not reproduced twice")
    if int(fuzz.group("signature"), 16) == 0:
        return fail("fuzz signature must be non-zero")
    if int(fuzz.group("truncated")) < 2 or int(fuzz.group("unknown")) < 1:
        return fail("fuzz corpus did not exercise recovery/isolation paths")
    if int(fuzz.group("checksum_bad")) != 1:
        return fail("fuzz corpus did not surface exactly one bad checksum")

    print("DX7_IMPORT_STRESS_LOG_CHECK: PASS")
    print("  callback   : consumer early-stop returns success and prevents later-message scanning")
    print("  fuzz       : fixed-seed byte stream reproduced twice with identical voices/flags/offsets/report")
    print("  recovery   : truncated and unknown messages coexist with valid voices without losing resynchronization")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
