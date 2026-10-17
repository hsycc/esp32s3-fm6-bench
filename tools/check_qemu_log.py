#!/usr/bin/env python3
import pathlib
import re
import sys

ALG_RE = re.compile(
    r"alg=\s*(?P<alg>\d+) mean=\s*(?P<mean>\d+) cycles .*? sounded=(?P<sounded>\d+) checksum=(?P<checksum>0x[0-9a-fA-F]+)"
)
DET_RE = re.compile(
    r"det alg=\s*(?P<alg>\d+) expected=(?P<expected>0x[0-9a-fA-F]+) "
    r"actual=(?P<actual>0x[0-9a-fA-F]+) sounded=(?P<sounded>\d+)"
)
VOICE_RE = re.compile(
    r"FM6 voices=(?P<voices>\d+) alg=(?P<alg>\d+) \| cycles .*? mean=(?P<mean>\d+) .*? p95=(?P<p95>\d+) .*? p99=(?P<p99>\d+) max=(?P<max>\d+) misses=(?P<misses>\d+)"
)
RESULT_RE = re.compile(
    r"QEMU_RESULT voices=(?P<voices>\d+) alg=(?P<alg>\d+) mean_cycles=(?P<mean>\d+) "
    r"p95_cycles=(?P<p95>\d+) p99_cycles=(?P<p99>\d+) max_cycles=(?P<max>\d+) "
    r"deadline_misses=(?P<misses>\d+) checksum=(?P<checksum>0x[0-9a-fA-F]+)"
)
FEATURE_RE = re.compile(r"FEATURE name=(?P<name>[a-z0-9_]+) status=(?P<status>PASS|FAIL)\b")
SOAK_RE = re.compile(
    r"QEMU_SOAK: (?P<status>PASS|FAIL) cases=(?P<cases>\d+) passes=(?P<passes>\d+) "
    r"checksum=(?P<checksum>0x[0-9a-fA-F]+) failures=(?P<failures>\d+)"
)
FEATURE_SUMMARY_RE = re.compile(
    r"QEMU_FEATURE_MATRIX: (?P<status>PASS|FAIL) tests=(?P<tests>\d+) failures=(?P<failures>\d+)"
)

EXPECTED_ALGORITHMS = set(range(1, 33))
EXPECTED_VOICES = {6, 8, 10, 12}
EXPECTED_FEATURES = {
    "baseline_determinism",
    "pitch_low_high",
    "velocity_sensitivity",
    "feedback_0_7",
    "ratio_fixed_frequency",
    "pitch_envelope",
    "lfo_pitch_mod",
    "lfo_amplitude_mod",
    "keyboard_level_scaling",
    "envelope_rate_scaling",
    "note_off_release",
}


def fail(message: str) -> int:
    print(f"QEMU_LOG_CHECK: FAIL {message}")
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_qemu_log.py <qemu.log>", file=sys.stderr)
        return 2

    text = pathlib.Path(sys.argv[1]).read_text(errors="replace")

    if "QEMU_MODE=1" not in text:
        return fail("missing QEMU_MODE=1")
    if "QEMU_DETERMINISM: PASS algorithms=32 failures=0" not in text:
        return fail("determinism pass missing or failed")
    if "QEMU_VALIDATION: PASS failures=0" not in text:
        return fail("validation did not pass")
    if "=== benchmark complete ===" not in text:
        return fail("benchmark did not complete")

    bad_markers = [
        "FUNCTIONAL_FAIL",
        "FEATURE_SOAK_FAIL",
        "status=FAIL",
        "QEMU_SOAK: FAIL",
        "QEMU_FEATURE_MATRIX: FAIL",
        "Guru Meditation",
        "assert failed",
        "panic'ed",
        "QEMU_DETERMINISM: FAIL",
        "QEMU_VALIDATION: FAIL",
    ]
    for marker in bad_markers:
        if marker in text:
            return fail(f"found failure marker: {marker}")

    algs = {}
    for m in ALG_RE.finditer(text):
        alg = int(m.group("alg"))
        if alg in EXPECTED_ALGORITHMS:
            algs[alg] = m.groupdict()

    if set(algs) != EXPECTED_ALGORITHMS:
        missing = sorted(EXPECTED_ALGORITHMS - set(algs))
        return fail(f"algorithm coverage incomplete, missing={missing}")

    for alg, row in sorted(algs.items()):
        if int(row["sounded"]) != 12:
            return fail(f"algorithm {alg} sounded={row['sounded']} expected=12")
        if int(row["checksum"], 16) == 0:
            return fail(f"algorithm {alg} checksum is zero")

    det = {}
    for m in DET_RE.finditer(text):
        alg = int(m.group("alg"))
        if alg in EXPECTED_ALGORITHMS:
            det[alg] = m.groupdict()

    if set(det) != EXPECTED_ALGORITHMS:
        missing = sorted(EXPECTED_ALGORITHMS - set(det))
        return fail(f"determinism coverage incomplete, missing={missing}")

    for alg, row in sorted(det.items()):
        if int(row["sounded"]) != 12:
            return fail(f"determinism algorithm {alg} sounded={row['sounded']} expected=12")
        if row["expected"].lower() != row["actual"].lower():
            return fail(
                f"determinism algorithm {alg} mismatch expected={row['expected']} actual={row['actual']}"
            )
        if row["expected"].lower() != algs[alg]["checksum"].lower():
            return fail(f"algorithm {alg} first-pass signature does not match determinism baseline")

    features = {}
    for m in FEATURE_RE.finditer(text):
        features[m.group("name")] = m.group("status")

    if set(features) != EXPECTED_FEATURES:
        missing = sorted(EXPECTED_FEATURES - set(features))
        unexpected = sorted(set(features) - EXPECTED_FEATURES)
        return fail(f"feature matrix mismatch missing={missing} unexpected={unexpected}")

    failed_features = sorted(name for name, status in features.items() if status != "PASS")
    if failed_features:
        return fail(f"feature tests failed: {failed_features}")

    soak = SOAK_RE.search(text)
    if not soak:
        return fail("missing QEMU_SOAK result")
    if soak.group("status") != "PASS":
        return fail("QEMU soak failed")
    if int(soak.group("cases")) != 64 or int(soak.group("passes")) != 2:
        return fail(
            f"unexpected soak dimensions cases={soak.group('cases')} passes={soak.group('passes')}"
        )
    if int(soak.group("failures")) != 0:
        return fail(f"QEMU soak failures={soak.group('failures')}")
    if int(soak.group("checksum"), 16) == 0:
        return fail("QEMU soak checksum is zero")

    feature_summary = FEATURE_SUMMARY_RE.search(text)
    if not feature_summary:
        return fail("missing QEMU_FEATURE_MATRIX summary")
    if feature_summary.group("status") != "PASS":
        return fail("feature matrix summary failed")
    if int(feature_summary.group("tests")) != 12 or int(feature_summary.group("failures")) != 0:
        return fail(
            f"unexpected feature summary tests={feature_summary.group('tests')} failures={feature_summary.group('failures')}"
        )

    voices = {}
    for m in VOICE_RE.finditer(text):
        v = int(m.group("voices"))
        if v in EXPECTED_VOICES:
            voices[v] = m.groupdict()

    if set(voices) != EXPECTED_VOICES:
        return fail(f"voice sweep incomplete, got={sorted(voices)}")

    for v, row in sorted(voices.items()):
        if int(row["misses"]) != 0:
            return fail(f"voice sweep {v} has emulator deadline misses={row['misses']}")

    result = RESULT_RE.search(text)
    if not result:
        return fail("missing QEMU_RESULT")
    if int(result.group("voices")) != 12:
        return fail("QEMU_RESULT is not the 12-voice case")
    if int(result.group("misses")) != 0:
        return fail(f"QEMU_RESULT deadline misses={result.group('misses')}")
    if int(result.group("checksum"), 16) == 0:
        return fail("QEMU_RESULT checksum is zero")

    print("QEMU_LOG_CHECK: PASS")
    print("  algorithms : 32/32, each sounded=12, non-zero signature")
    print("  determinism: 32/32 signatures reproduced exactly")
    print("  features   : 11 directed DX7 checks + deterministic 64-case soak x2")
    print("  voice sweep: 6/8/10/12")
    print(
        "  regression : alg={alg} mean={mean} p95={p95} p99={p99} max={max} checksum={checksum}".format(
            **result.groupdict()
        )
    )
    print("  note       : timing is emulator-only and is a regression metric, not real S3 performance")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
