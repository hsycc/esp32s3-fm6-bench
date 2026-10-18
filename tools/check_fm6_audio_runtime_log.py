#!/usr/bin/env python3
import pathlib
import re
import sys

TEST_RE = re.compile(
    r"FM6_AUDIO_RUNTIME_TEST name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)"
)
SUMMARY_RE = re.compile(
    r"QEMU_FM6_AUDIO_RUNTIME: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) "
    r"failures=(?P<failures>\d+) block_frames=(?P<block_frames>\d+)"
)
SEQUENCE_RE = re.compile(
    r"FM6_AUDIO_RUNTIME_SEQUENCE: (?P<status>PASS|FAIL) blocks=(?P<blocks>\d+) "
    r"commits=(?P<commits>\d+) passes=(?P<passes>\d+) final_generation=(?P<generation>\d+) "
    r"active_blocks=(?P<active>\d+) signature=(?P<signature>0x[0-9a-fA-F]+)"
)

EXPECTED_TESTS = {
    "invalid_init_rejected",
    "commit_before_render",
    "one_program_per_block",
    "empty_queue_render_keeps_program",
    "applied_voice_count_tracks_command",
    "deterministic_runtime_sequence",
}


def fail(message: str) -> int:
    print(f"FM6_AUDIO_RUNTIME_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_audio_runtime_log.py <qemu.log>", file=sys.stderr)
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
        return fail("missing QEMU_FM6_AUDIO_RUNTIME summary")
    if summary.group("status") != "PASS" or int(summary.group("failures")) != 0:
        return fail("FM6 audio runtime summary did not pass")
    if int(summary.group("tests")) != len(EXPECTED_TESTS):
        return fail(f"unexpected runtime test count {summary.group('tests')}")
    if int(summary.group("block_frames")) != 32:
        return fail(f"unexpected audio block size {summary.group('block_frames')}")

    if "QEMU_FM6_AUDIO_RUNTIME_GATE: PASS failures=0" not in text:
        return fail("missing top-level FM6 audio runtime gate pass")

    sequence = SEQUENCE_RE.search(text)
    if not sequence:
        return fail("missing deterministic audio runtime sequence")
    if sequence.group("status") != "PASS":
        return fail("deterministic audio runtime sequence failed")
    if int(sequence.group("blocks")) != 8 or int(sequence.group("commits")) != 4:
        return fail("unexpected block/commit dimensions")
    if int(sequence.group("passes")) != 2 or int(sequence.group("generation")) != 4:
        return fail("runtime sequence did not reproduce the same four program generations")
    active = int(sequence.group("active"))
    if active < 1 or active > 8:
        return fail(f"invalid active block count {active}")
    if int(sequence.group("signature"), 16) == 0:
        return fail("runtime sequence signature must be non-zero")

    print("FM6_AUDIO_RUNTIME_LOG_CHECK: PASS")
    print("  boundary   : each render call applies at most one pending program before its 32-frame block")
    print("  state      : voice count, algorithm and generation track the exact committed command")
    print("  empty      : empty-queue renders preserve the active program and still advance one block")
    print(f"  sequence   : 8 blocks / 4 commits reproduced x2, active_blocks={active}, signature={sequence.group('signature')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
