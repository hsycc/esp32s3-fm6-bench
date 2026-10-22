#!/usr/bin/env python3
"""Strict output-chain gate; no emulator timing is used for hardware acceptance."""
import pathlib
import re
import sys

EXPECTED = {
    "controller_defaults", "controller_block_boundary", "bend_fragmented_realtime",
    "bend_running_status_endpoints", "bend_q24_endpoints", "unity_pcm_equivalence",
    "bend_audio_frequency", "fixed_frequency_invariance", "volume_expression_gain",
    "mute_output_zero", "gain_ramp_128_frames", "gain_retarget_continuity",
    "gain_repeated_target_progress", "mute_advances_synth", "reset_preserves_held_notes",
    "program_preserves_controls", "reset_before_program", "all_sound_off_keeps_controls",
    "invalid_controller_ranges", "controller_backpressure", "controller_channel_filter",
    "twelve_voices_no_retrigger", "controls_sequence_determinism",
}
TEST = re.compile(r"^FM6_PERFORMANCE_TEST name=(\w+) status=(PASS|FAIL)$", re.M)
SUMMARY = re.compile(
    r"^QEMU_FM6_PERFORMANCE: PASS tests=(\d+) failures=0 voices=12 ramp_frames=128$", re.M)
SEQUENCE = re.compile(
    r"^FM6_PERFORMANCE_SEQUENCE: PASS blocks=512 passes=2 commands=(\d+) "
    r"events=(\d+) max_notes=12 rejected=0 checksum=(0x[0-9a-fA-F]+) "
    r"energy=(\d+) final_silent=1$", re.M)


def validate(text: str) -> None:
    text = text.replace("\r", "")
    rows = TEST.findall(text)
    if len(rows) != len(EXPECTED) or {name for name, _ in rows} != EXPECTED:
        raise ValueError("missing, duplicate or unexpected performance tests")
    if any(status != "PASS" for _, status in rows):
        raise ValueError("a performance test failed")
    summaries = SUMMARY.findall(text)
    if summaries != [str(len(EXPECTED))]:
        raise ValueError("missing or duplicated performance summary")
    if text.splitlines().count("QEMU_FM6_PERFORMANCE_GATE: PASS failures=0") != 1:
        raise ValueError("missing or duplicated top-level performance gate")
    sequences = SEQUENCE.findall(text)
    if len(sequences) != 1:
        raise ValueError("missing or duplicated deterministic sequence")
    commands, events, checksum, energy = sequences[0]
    if not (int(commands) > int(events) > 0 and int(checksum, 16) != 0 and int(energy) > 0):
        raise ValueError("sequence produced no meaningful events/audio")
    if re.search(r"(?:FM6_PERFORMANCE[^\n]*\bFAIL|Guru Meditation|assert failed|panic'ed)", text):
        raise ValueError("failure or panic in log")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_performance_log.py <qemu.log>", file=sys.stderr)
        return 2
    try:
        validate(pathlib.Path(sys.argv[1]).read_text(errors="replace"))
    except (OSError, ValueError) as exc:
        print(f"FM6_PERFORMANCE_LOG_CHECK: FAIL {exc}")
        return 1
    print("FM6_PERFORMANCE_LOG_CHECK: PASS")
    print("  coverage: 23 directed checks; post-gain PCM, not raw-engine counters")
    print("  pitch: 14-bit endpoints, stream fragmentation, actual frequency, fixed-mode invariance")
    print("  gain: CC7 x CC11, bounded 128-frame ramp, mute/resume phase continuity")
    print("  state: CC121 preserves held keys/program/CC7; full 12-voice controller updates")
    print("  stress: 512 blocks x2, whole-stream checksum, drains to hard silence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
