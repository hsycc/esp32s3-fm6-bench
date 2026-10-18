#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(
    r"FM6_CONTROL_RUNTIME_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
SUMMARY_RE = re.compile(
    r"QEMU_FM6_CONTROL_RUNTIME: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) "
    r"failures=(?P<failures>\d+) queue_capacity=(?P<queue>\d+) voices=(?P<voices>\d+)"
)
SEQUENCE_RE = re.compile(
    r"FM6_CONTROL_RUNTIME_SEQUENCE: (?P<status>PASS|FAIL) blocks=(?P<blocks>\d+) passes=(?P<passes>\d+) "
    r"commands=(?P<commands>\d+) programs=(?P<programs>\d+) steals=(?P<steals>\d+) "
    r"retriggers=(?P<retriggers>\d+) unmatched=(?P<unmatched>\d+) active_program=(?P<active_program>\d+) "
    r"notes_down=(?P<notes_down>\d+) algorithm=(?P<algorithm>\d+) "
    r"checksum=(?P<checksum>0x[0-9a-fA-F]+) energy=(?P<energy>\d+)"
)
DUAL_TEST_RE = re.compile(
    r"FM6_CONTROL_RUNTIME_DUALCORE_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
DUAL_RE = re.compile(
    r"FM6_CONTROL_RUNTIME_DUALCORE: (?P<status>PASS|FAIL) commands=(?P<commands>\d+) "
    r"producer_core=(?P<producer>\d+) consumer_core=(?P<consumer>\d+) "
    r"accepted=(?P<accepted>\d+) applied=(?P<applied>\d+) rejected=(?P<rejected>\d+) "
    r"programs=(?P<programs>\d+) active_program=(?P<active_program>\d+) blocks=(?P<blocks>\d+) "
    r"notes_down=(?P<notes_down>\d+) algorithm=(?P<algorithm>\d+) "
    r"checksum=(?P<checksum>0x[0-9a-fA-F]+) energy=(?P<energy>\d+)"
)
DUAL_SUMMARY_RE = re.compile(
    r"QEMU_FM6_CONTROL_RUNTIME_DUALCORE: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) failures=(?P<failures>\d+)"
)

EXPECTED_TESTS = {
    "preprogram_note_ignored",
    "program_only_starts_silent",
    "program_then_note_same_boundary",
    "note_then_program_resets_old_state",
    "program_note_program_note_total_order",
    "multiple_programs_latest_wins",
    "thirteenth_note_steals_oldest",
    "queue_backpressure",
    "counter_and_sequence_wrap",
    "deterministic_mixed_control_sequence",
}


def fail(message: str) -> int:
    print(f"FM6_CONTROL_RUNTIME_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_control_runtime_log.py <qemu.log>", file=sys.stderr)
        return 2

    text = pathlib.Path(sys.argv[1]).read_text(errors="replace")
    tests = {m.group("name"): m.group("status") for m in TEST_RE.finditer(text)}
    if set(tests) != EXPECTED_TESTS:
        return fail(
            f"directed coverage mismatch missing={sorted(EXPECTED_TESTS - set(tests))} "
            f"unexpected={sorted(set(tests) - EXPECTED_TESTS)}"
        )
    failed = sorted(name for name, status in tests.items() if status != "PASS")
    if failed:
        return fail(f"directed tests failed: {failed}")

    summary = SUMMARY_RE.search(text)
    if not summary or summary.group("status") != "PASS":
        return fail("missing passing QEMU_FM6_CONTROL_RUNTIME summary")
    if int(summary.group("tests")) != len(EXPECTED_TESTS) or int(summary.group("failures")) != 0:
        return fail("unexpected directed summary counts")
    if int(summary.group("queue")) != 16 or int(summary.group("voices")) != 12:
        return fail("unexpected unified queue/voice capacity")
    if "QEMU_FM6_CONTROL_RUNTIME_GATE: PASS failures=0" not in text:
        return fail("missing top-level unified control runtime gate")

    sequence = SEQUENCE_RE.search(text)
    if not sequence or sequence.group("status") != "PASS":
        return fail("missing passing deterministic mixed-control sequence")
    if int(sequence.group("blocks")) != 256 or int(sequence.group("passes")) != 2:
        return fail("unexpected deterministic sequence dimensions")
    if int(sequence.group("programs")) != 4 or int(sequence.group("commands")) == 0:
        return fail("deterministic sequence did not exercise expected program/control traffic")
    if int(sequence.group("checksum"), 16) == 0 or int(sequence.group("energy")) == 0:
        return fail("deterministic sequence produced no observable audio")

    dual_tests = {m.group("name"): m.group("status") for m in DUAL_TEST_RE.finditer(text)}
    if dual_tests != {"dualcore_mixed_control_stream": "PASS"}:
        return fail(f"unexpected dual-core directed result: {dual_tests}")

    dual = DUAL_RE.search(text)
    if not dual or dual.group("status") != "PASS":
        return fail("missing passing dual-core control result")
    expected_exact = {
        "commands": 256,
        "producer": 1,
        "consumer": 0,
        "accepted": 256,
        "applied": 256,
        "programs": 8,
        "active_program": 225,
        "algorithm": 21,
    }
    for key, expected in expected_exact.items():
        if int(dual.group(key)) != expected:
            return fail(f"dual-core {key}={dual.group(key)} expected={expected}")
    if int(dual.group("rejected")) < 1:
        return fail("dual-core run did not exercise queue backpressure")
    if int(dual.group("blocks")) == 0:
        return fail("dual-core consumer rendered no blocks")
    if int(dual.group("checksum"), 16) == 0 or int(dual.group("energy")) == 0:
        return fail("dual-core run produced no observable audio")

    dual_summary = DUAL_SUMMARY_RE.search(text)
    if not dual_summary or dual_summary.group("status") != "PASS":
        return fail("missing passing dual-core summary")
    if int(dual_summary.group("tests")) != 1 or int(dual_summary.group("failures")) != 0:
        return fail("unexpected dual-core summary counts")
    if "QEMU_FM6_CONTROL_RUNTIME_DUALCORE_GATE: PASS failures=0" not in text:
        return fail("missing top-level dual-core control gate")

    print("FM6_CONTROL_RUNTIME_LOG_CHECK: PASS")
    print("  ordering   : PROGRAM/NOTE commands share one FIFO and apply strictly at 32-frame boundaries")
    print("  reset      : later PROGRAM commands erase earlier-patch note state before the block renders")
    print("  allocator  : 12 voices with deterministic oldest-held-note stealing")
    print("  wrap       : queue counters and command sequence cross UINT32_MAX safely")
    print("  dual-core  : Core1 producer -> Core0 consumer, 256 commands, forced backpressure, no loss")
    print(
        f"  sequence   : blocks={sequence.group('blocks')} commands={sequence.group('commands')} "
        f"checksum={sequence.group('checksum')}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
