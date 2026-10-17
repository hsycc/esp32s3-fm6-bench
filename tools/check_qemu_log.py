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

EXPECTED_ALGORITHMS = set(range(1, 33))
EXPECTED_VOICES = {6, 8, 10, 12}


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
