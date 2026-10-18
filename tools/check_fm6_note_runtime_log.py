#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(
    r"FM6_NOTE_RUNTIME_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
SUMMARY_RE = re.compile(
    r"QEMU_FM6_NOTE_RUNTIME: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) "
    r"failures=(?P<failures>\d+) queue_capacity=(?P<queue>\d+) voices=(?P<voices>\d+)"
)
SEQUENCE_RE = re.compile(
    r"FM6_NOTE_RUNTIME_SEQUENCE: (?P<status>PASS|FAIL) blocks=(?P<blocks>\d+) passes=(?P<passes>\d+) "
    r"events=(?P<events>\d+) steals=(?P<steals>\d+) retriggers=(?P<retriggers>\d+) "
    r"unmatched=(?P<unmatched>\d+) notes_down=(?P<notes_down>\d+) sounded=(?P<sounded>\d+) "
    r"checksum=(?P<checksum>0x[0-9a-fA-F]+) energy=(?P<energy>\d+)"
)

EXPECTED_TESTS = {
    "silent_program_load",
    "note_on_block_boundary",
    "velocity_zero_is_note_off",
    "same_note_retrigger_same_voice",
    "polyphony_steals_oldest_down_voice",
    "released_voice_reused_before_steal",
    "all_notes_off_release_drain",
    "program_reset_discards_stale_events",
    "queue_backpressure_and_flush",
    "deterministic_note_sequence",
}


def fail(message: str) -> int:
    print(f"FM6_NOTE_RUNTIME_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_note_runtime_log.py <qemu.log>", file=sys.stderr)
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

    summary = SUMMARY_RE.search(text)
    if not summary:
        return fail("missing QEMU_FM6_NOTE_RUNTIME summary")
    if summary.group("status") != "PASS" or int(summary.group("failures")) != 0:
        return fail("note runtime summary failed")
    if int(summary.group("tests")) != len(EXPECTED_TESTS):
        return fail(f"unexpected test count {summary.group('tests')}")
    if int(summary.group("queue")) != 32 or int(summary.group("voices")) != 12:
        return fail("unexpected note queue or voice capacity")
    if "QEMU_FM6_NOTE_RUNTIME_GATE: PASS failures=0" not in text:
        return fail("missing top-level note runtime gate pass")

    sequence = SEQUENCE_RE.search(text)
    if not sequence or sequence.group("status") != "PASS":
        return fail("missing passing deterministic note sequence")
    if int(sequence.group("blocks")) != 256 or int(sequence.group("passes")) != 2:
        return fail("unexpected deterministic sequence dimensions")
    if int(sequence.group("events")) == 0:
        return fail("deterministic sequence applied no note events")
    if int(sequence.group("checksum"), 16) == 0 or int(sequence.group("energy")) == 0:
        return fail("deterministic sequence produced no observable audio")

    print("FM6_NOTE_RUNTIME_LOG_CHECK: PASS")
    print("  program    : product program load starts with a silent 12-voice pool")
    print("  boundary   : queued note events apply only when a 32-frame render block begins")
    print("  allocator  : same-note retrigger, released-slot reuse, oldest-held-note stealing at 13th note")
    print("  reset      : program changes discard stale note events before the new patch renders")
    print("  pressure   : fixed-capacity 32-event SPSC queue rejects overflow deterministically")
    print(
        f"  sequence   : 256 blocks x2 deterministic, events={sequence.group('events')}, "
        f"steals={sequence.group('steals')}, checksum={sequence.group('checksum')}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
