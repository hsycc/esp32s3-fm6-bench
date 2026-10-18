#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(
    r"FM6_PROGRAM_QUEUE_DUALCORE_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
SUMMARY_RE = re.compile(
    r"QEMU_FM6_PROGRAM_QUEUE_DUALCORE: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) failures=(?P<failures>\d+)"
)
RUN_RE = re.compile(
    r"FM6_PROGRAM_QUEUE_DUALCORE: (?P<status>PASS|FAIL) commands=(?P<commands>\d+) passes=(?P<passes>\d+) "
    r"producer_core=(?P<producer>\d+) consumer_core=(?P<consumer>\d+) accepted=(?P<accepted>\d+) "
    r"committed=(?P<committed>\d+) rejected_a=(?P<rejected_a>\d+) rejected_b=(?P<rejected_b>\d+) "
    r"active_steps=(?P<active>\d+) signature=(?P<signature>0x[0-9a-fA-F]+)"
)

EXPECTED_TESTS = {"dualcore_spsc_ordering_backpressure"}


def fail(message: str) -> int:
    print(f"FM6_PROGRAM_QUEUE_DUALCORE_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_program_queue_dualcore_log.py <qemu.log>", file=sys.stderr)
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
        return fail("missing QEMU_FM6_PROGRAM_QUEUE_DUALCORE summary")
    if summary.group("status") != "PASS" or int(summary.group("failures")) != 0:
        return fail("dual-core program queue summary did not pass")
    if int(summary.group("tests")) != 1:
        return fail(f"unexpected dual-core test count {summary.group('tests')}")

    if "QEMU_FM6_PROGRAM_QUEUE_DUALCORE_GATE: PASS failures=0" not in text:
        return fail("missing top-level dual-core program queue gate pass")

    run = RUN_RE.search(text)
    if not run:
        return fail("missing dual-core stress run summary")
    if run.group("status") != "PASS":
        return fail("dual-core stress run did not pass")
    if int(run.group("commands")) != 128 or int(run.group("passes")) != 2:
        return fail("unexpected dual-core stress dimensions")
    if int(run.group("producer")) != 1 or int(run.group("consumer")) != 0:
        return fail(
            f"unexpected core placement producer={run.group('producer')} consumer={run.group('consumer')}"
        )
    if int(run.group("accepted")) != 128 or int(run.group("committed")) != 128:
        return fail("dual-core queue lost or failed to commit commands")
    if int(run.group("rejected_a")) < 1 or int(run.group("rejected_b")) < 1:
        return fail("forced full-queue backpressure was not observed on both passes")
    active = int(run.group("active"))
    if active < 1 or active > 128:
        return fail(f"invalid active step count {active}")
    if int(run.group("signature"), 16) == 0:
        return fail("dual-core render signature must be non-zero")

    print("FM6_PROGRAM_QUEUE_DUALCORE_LOG_CHECK: PASS")
    print("  cores      : producer pinned Core1, audio consumer pinned Core0")
    print("  ordering   : 128 accepted generations committed in exact FIFO order on both passes")
    print("  backpressure: queue-full rejection forced before consumer start and observed on both passes")
    print("  memory     : SPSC acquire/release path exercised across ESP32-S3 cores")
    print(f"  render     : deterministic x2, active_steps={active}, signature={run.group('signature')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
