#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(
    r"FM6_MIDI_ROUTER_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
SUMMARY_RE = re.compile(
    r"QEMU_FM6_MIDI_ROUTER: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) "
    r"failures=(?P<failures>\d+) bank_slots=(?P<bank>\d+) queue_capacity=(?P<queue>\d+)"
)
SEQUENCE_RE = re.compile(
    r"FM6_MIDI_SEQUENCE: (?P<status>PASS|FAIL) blocks=(?P<blocks>\d+) passes=(?P<passes>\d+) "
    r"messages=(?P<messages>\d+) routed=(?P<routed>\d+) commands=(?P<commands>\d+) "
    r"programs=(?P<programs>\d+) sustain=(?P<sustain>\d+) steals=(?P<steals>\d+) "
    r"notes_down=(?P<notes_down>\d+) sustained=(?P<sustained>\d+) algorithm=(?P<algorithm>\d+) "
    r"checksum=(?P<checksum>0x[0-9a-fA-F]+) energy=(?P<energy>\d+)"
)

EXPECTED_TESTS = {
    "program_bank_128_slots",
    "explicit_note_on_off",
    "velocity_zero_note_off",
    "running_status_realtime_interleave",
    "channel_filter",
    "omni_channel",
    "missing_program_rejected",
    "sustain_hold_release",
    "all_notes_off_respects_sustain",
    "all_sound_off_hard_silence",
    "program_change_preserves_sustain_order",
    "sysex_isolation_running_status",
    "unsupported_messages_ignored",
    "queue_backpressure_reported",
    "deterministic_midi_sequence",
}


def fail(message: str) -> int:
    print(f"FM6_MIDI_ROUTER_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_midi_router_log.py <qemu.log>", file=sys.stderr)
        return 2

    text = pathlib.Path(sys.argv[1]).read_text(errors="replace")
    tests = {m.group("name"): m.group("status") for m in TEST_RE.finditer(text)}
    if set(tests) != EXPECTED_TESTS:
        return fail(
            f"coverage mismatch missing={sorted(EXPECTED_TESTS - set(tests))} "
            f"unexpected={sorted(set(tests) - EXPECTED_TESTS)}"
        )
    failed = sorted(name for name, status in tests.items() if status != "PASS")
    if failed:
        return fail(f"tests failed: {failed}")

    summary = SUMMARY_RE.search(text)
    if not summary or summary.group("status") != "PASS":
        return fail("missing passing QEMU_FM6_MIDI_ROUTER summary")
    if int(summary.group("tests")) != len(EXPECTED_TESTS) or int(summary.group("failures")) != 0:
        return fail("unexpected MIDI test summary counts")
    if int(summary.group("bank")) != 128 or int(summary.group("queue")) != 16:
        return fail("unexpected program-bank or control-queue capacity")
    if "QEMU_FM6_MIDI_ROUTER_GATE: PASS failures=0" not in text:
        return fail("missing top-level MIDI router gate pass")

    sequence = SEQUENCE_RE.search(text)
    if not sequence or sequence.group("status") != "PASS":
        return fail("missing passing deterministic MIDI sequence")
    if int(sequence.group("blocks")) != 128 or int(sequence.group("passes")) != 2:
        return fail("unexpected deterministic MIDI sequence dimensions")
    if int(sequence.group("programs")) != 4:
        return fail(f"expected 4 program changes, got {sequence.group('programs')}")
    if int(sequence.group("messages")) == 0 or int(sequence.group("routed")) == 0:
        return fail("deterministic MIDI sequence routed no messages")
    if int(sequence.group("commands")) == 0:
        return fail("deterministic MIDI sequence applied no control commands")
    if int(sequence.group("checksum"), 16) == 0 or int(sequence.group("energy")) == 0:
        return fail("deterministic MIDI sequence produced no observable audio")

    print("FM6_MIDI_ROUTER_LOG_CHECK: PASS")
    print("  parser     : Note On/Off, velocity-zero, Running Status and interleaved realtime bytes")
    print("  routing    : channel filter/omni, 128-slot Program Change and missing-program rejection")
    print("  controls   : CC64 sustain, CC123 All Notes Off, CC120 hard All Sound Off")
    print("  isolation  : SysEx clears channel running status and remains outside the MIDI voice router")
    print("  pressure   : queue overflow is surfaced explicitly instead of silently dropping statistics")
    print(
        f"  sequence   : 128 blocks x2, messages={sequence.group('messages')}, "
        f"commands={sequence.group('commands')}, checksum={sequence.group('checksum')}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
