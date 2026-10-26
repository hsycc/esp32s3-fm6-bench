#!/usr/bin/env python3
"""Validate the complete QEMU PCM16 gate, not merely a trailing PASS token."""
import pathlib
import re
import sys

EXPECTED_TESTS = {
    "init_reject_atomic", "invalid_buffers_no_mutation", "silence_exact",
    "q24_scale_known", "rounding_ties_away", "saturation_endpoints",
    "extreme_input_peaks", "headroom_bounds", "fixed_headroom_twelve_units",
    "monotonic_no_wrap", "statistics_reset", "little_endian_bytes", "pack_bounds_atomic",
    "wide_mix_extremes", "wide_mix_cancellation", "coherent_twelve_voice_reference",
    "raw_saturation_diagnostics_reset", "runtime_initial_silence", "runtime_invalid_no_consume",
    "runtime_one_block_boundary", "runtime_mute_after_ramp", "runtime_hard_silence",
    "output_stats_survive_program_change", "raw_clip_visible_when_pcm_not_clipped",
    "pcm_sequence_determinism", "pcm_reference_pipeline_equivalence",
}


def records(text: str, prefix: str) -> list[str]:
    return [line for line in text.splitlines() if line.startswith(prefix)]


def fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"([a-z_]+)=([^\s]+)", line))


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise ValueError(reason)


def validate(text: str) -> dict[str, str]:
    require(not any(token in text for token in (
        "Guru Meditation", "assert failed", "panic'ed", "FUNCTIONAL_FAIL",
    )), "firmware panic/assert/failure marker")
    lines = records(text, "FM6_PCM_TEST ")
    parsed = [re.fullmatch(r"FM6_PCM_TEST name=([a-z0-9_]+) status=(PASS|FAIL)", line)
              for line in lines]
    require(len(lines) == len(EXPECTED_TESTS) and all(parsed), "missing/duplicate/malformed tests")
    found = [match.group(1) for match in parsed]
    require(set(found) == EXPECTED_TESTS and len(set(found)) == len(found), "test coverage mismatch")
    require(all(match.group(2) == "PASS" for match in parsed), "directed test failed")

    summary = records(text, "QEMU_FM6_PCM: ")
    require(len(summary) == 1, "missing/duplicate PCM summary")
    require(summary[0] == f"QEMU_FM6_PCM: PASS tests={len(EXPECTED_TESTS)} failures=0 voices=12 block_frames=32 headroom=4",
            "PCM summary counts/configuration mismatch")
    require(records(text, "QEMU_FM6_PCM_GATE: ") == ["QEMU_FM6_PCM_GATE: PASS failures=0"],
            "missing/duplicate/failed top-level PCM gate")

    overload = records(text, "FM6_PCM_OVERLOAD ")
    require(len(overload) == 1, "missing/duplicate overload evidence")
    o = fields(overload[0])
    require(set(o) == {"positive", "negative", "observed", "frames"}, "overload fields mismatch")
    require(int(o["frames"]) == 2048 and int(o["positive"]) > 0 and int(o["negative"]) > 0,
            "both saturation polarities must be exercised")
    require(int(o["observed"]) == int(o["positive"]) + int(o["negative"]) <= 2048,
            "raw saturation accounting mismatch")

    sequence = records(text, "FM6_PCM_SEQUENCE: ")
    require(len(sequence) == 1 and sequence[0].startswith("FM6_PCM_SEQUENCE: PASS "),
            "missing/duplicate/failed PCM sequence")
    s = fields(sequence[0])
    require(set(s) == {"blocks", "passes", "reference", "frames", "commands", "routed",
                       "rejected", "max_notes", "checksum", "energy", "mix_clips", "pcm_clips", "final_silent"},
            "sequence fields mismatch")
    for key, expected in {"blocks": 1025, "passes": 2, "reference": 1, "frames": 32800,
                          "rejected": 0, "max_notes": 12, "final_silent": 1}.items():
        require(int(s[key]) == expected, f"sequence {key} mismatch")
    require(int(s["commands"]) == int(s["routed"]) > 100, "routed/applied command accounting mismatch")
    require(re.fullmatch(r"0x[0-9a-fA-F]{8}", s["checksum"]) is not None and
            int(s["checksum"], 16) != 0 and int(s["energy"]) > 0, "no observable final PCM")
    require(0 <= int(s["mix_clips"]) <= 32800 and 0 <= int(s["pcm_clips"]) <= 32800,
            "invalid clipping count")
    return s


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_fm6_pcm_log.py <qemu.log>", file=sys.stderr)
        return 2
    try:
        result = validate(pathlib.Path(sys.argv[1]).read_text(errors="replace"))
    except (OSError, ValueError, KeyError) as exc:
        print(f"FM6_PCM_LOG_CHECK: FAIL {exc}")
        return 1
    print("FM6_PCM_LOG_CHECK: PASS")
    print(f"  coverage: {len(EXPECTED_TESTS)} directed checks, signed endpoints, rounding and wide accumulation")
    print(f"  PCM16: frames={result['frames']} checksum={result['checksum']} energy={result['energy']}")
    print("  sequence: repeated twice and matches explicit render/gain/convert reference path")
    print("  scope: function/determinism only; no I2S/DMA or physical-performance claim")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
