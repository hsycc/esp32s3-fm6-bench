#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(
    r"FM6_PROGRAM_QUEUE_WRAP_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
RESULT_RE = re.compile(
    r"FM6_PROGRAM_QUEUE_WRAP_RESULT: (?P<status>PASS|FAIL) "
    r"counter_start=(?P<start>\d+) counter_after=(?P<after>\d+) "
    r"first_generation=(?P<first>\d+) second_generation=(?P<second>\d+)"
)
SUMMARY_RE = re.compile(
    r"QEMU_FM6_PROGRAM_QUEUE_WRAP: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) failures=(?P<failures>\d+)"
)

EXPECTED_TESTS = {
    "counter_wrap_preserves_fifo",
    "generation_wrap_skips_zero",
}


def fail(message: str) -> int:
    print(f"FM6_PROGRAM_QUEUE_WRAP_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_program_queue_wrap_log.py <qemu.log>", file=sys.stderr)
        return 2

    text = pathlib.Path(sys.argv[1]).read_text(errors="replace")
    tests = {m.group("name"): m.group("status") for m in TEST_RE.finditer(text)}
    if set(tests) != EXPECTED_TESTS:
        return fail(
            f"test coverage mismatch missing={sorted(EXPECTED_TESTS - set(tests))} "
            f"unexpected={sorted(set(tests) - EXPECTED_TESTS)}"
        )
    failed = sorted(name for name, status in tests.items() if status != "PASS")
    if failed:
        return fail(f"tests failed: {failed}")

    result = RESULT_RE.search(text)
    if not result or result.group("status") != "PASS":
        return fail("missing passing wrap result")
    if int(result.group("start")) != 0xFFFFFFFE:
        return fail(f"unexpected counter start {result.group('start')}")
    if int(result.group("after")) != 3:
        return fail(f"counter did not wrap to expected post-reuse value: {result.group('after')}")
    if int(result.group("first")) != 0xFFFFFFFF or int(result.group("second")) != 1:
        return fail(
            f"generation zero was not skipped first={result.group('first')} second={result.group('second')}"
        )

    summary = SUMMARY_RE.search(text)
    if not summary:
        return fail("missing QEMU_FM6_PROGRAM_QUEUE_WRAP summary")
    if summary.group("status") != "PASS" or int(summary.group("failures")) != 0:
        return fail("wrap summary failed")
    if int(summary.group("tests")) != len(EXPECTED_TESTS):
        return fail(f"unexpected wrap test count {summary.group('tests')}")
    if "QEMU_FM6_PROGRAM_QUEUE_WRAP_GATE: PASS failures=0" not in text:
        return fail("missing top-level queue wrap gate pass")

    print("FM6_PROGRAM_QUEUE_WRAP_LOG_CHECK: PASS")
    print("  counters   : write/read cross UINT32_MAX while FIFO and capacity accounting remain correct")
    print("  slots      : ring slots are reused correctly after counter wrap")
    print("  generation : 0xFFFFFFFF wraps to 1; generation 0 remains reserved")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
