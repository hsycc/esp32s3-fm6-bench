#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(
    r"FM6_PROGRAM_QUEUE_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
SUMMARY_RE = re.compile(
    r"QEMU_FM6_PROGRAM_QUEUE: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) "
    r"failures=(?P<failures>\d+) capacity=(?P<capacity>\d+)"
)
SWITCH_RE = re.compile(
    r"FM6_PROGRAM_QUEUE_SWITCH: (?P<status>PASS|FAIL) steps=(?P<steps>\d+) "
    r"passes=(?P<passes>\d+) active_steps=(?P<active>\d+) "
    r"signature=(?P<signature>0x[0-9a-fA-F]+)"
)

EXPECTED_TESTS = {
    "submit_waits_for_block_commit",
    "input_copy_isolation",
    "submit_canonicalizes_voice",
    "fifo_generation_order",
    "queue_full_backpressure",
    "empty_invalid_noop",
    "deterministic_program_switch_sequence",
}


def fail(message: str) -> int:
    print(f"FM6_PROGRAM_QUEUE_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_program_queue_log.py <qemu.log>", file=sys.stderr)
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
        return fail("missing QEMU_FM6_PROGRAM_QUEUE summary")
    if summary.group("status") != "PASS" or int(summary.group("failures")) != 0:
        return fail("FM6 program queue summary did not pass")
    if int(summary.group("tests")) != len(EXPECTED_TESTS):
        return fail(f"unexpected program queue test count {summary.group('tests')}")
    if int(summary.group("capacity")) != 4:
        return fail(f"unexpected queue capacity {summary.group('capacity')}")

    if "QEMU_FM6_PROGRAM_QUEUE_GATE: PASS failures=0" not in text:
        return fail("missing top-level FM6 program queue gate pass")

    switch = SWITCH_RE.search(text)
    if not switch:
        return fail("missing deterministic program switch summary")
    if switch.group("status") != "PASS":
        return fail("deterministic program switch sequence failed")
    if int(switch.group("steps")) != 3 or int(switch.group("passes")) != 2:
        return fail("unexpected deterministic switch dimensions")
    active = int(switch.group("active"))
    if active < 1 or active > 3:
        return fail(f"invalid active step count {active}")
    if int(switch.group("signature"), 16) == 0:
        return fail("program switch signature must be non-zero")

    print("FM6_PROGRAM_QUEUE_LOG_CHECK: PASS")
    print("  boundary   : submit cannot change the running patch before explicit block-boundary commit")
    print("  ownership  : submitted VCED is copied, canonicalized and isolated from producer-buffer mutation")
    print("  ordering   : FIFO generations preserved; capacity-4 backpressure rejects overflow deterministically")
    print("  safety     : empty/invalid operations are no-ops for the active FM engine")
    print(f"  switch     : 3 program changes reproduced exactly x2, active_steps={active}, signature={switch.group('signature')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
