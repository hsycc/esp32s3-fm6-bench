#!/usr/bin/env python3
import pathlib
import re
import sys

RESULT_RE = re.compile(
    r"HARDWARE_RESULT voices=(?P<voices>\d+) alg=(?P<alg>\d+) "
    r"mean_cycles=(?P<mean>\d+) p95_cycles=(?P<p95>\d+) "
    r"p99_cycles=(?P<p99>\d+) max_cycles=(?P<max>\d+) "
    r"deadline_misses=(?P<misses>\d+) checksum=(?P<checksum>0x[0-9a-fA-F]+)"
)
ACCEPT_RE = re.compile(r"HARDWARE_ACCEPTANCE: (?P<status>PASS|FAIL) failures=(?P<failures>\d+)")
CPU_RE = re.compile(r"CPU=(?P<mhz>\d+) MHz")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: parse_hardware_log.py <serial-log.txt>", file=sys.stderr)
        return 2

    text = pathlib.Path(sys.argv[1]).read_text(errors="replace")
    result = RESULT_RE.search(text)
    acceptance = ACCEPT_RE.search(text)
    cpu = CPU_RE.search(text)

    if not result or not acceptance or not cpu:
        print("incomplete benchmark log: missing HARDWARE_RESULT/HARDWARE_ACCEPTANCE/CPU marker", file=sys.stderr)
        return 2

    d = result.groupdict()
    mhz = int(cpu.group("mhz"))

    def us(name: str) -> float:
        return int(d[name]) / mhz

    print("ESP32-S3 FM6 hardware result")
    print(f"  acceptance : {acceptance.group('status')}")
    print(f"  failures   : {acceptance.group('failures')}")
    print(f"  voices     : {d['voices']}")
    print(f"  algorithm  : {d['alg']}")
    print(f"  mean       : {us('mean'):.2f} us")
    print(f"  p95        : {us('p95'):.2f} us")
    print(f"  p99        : {us('p99'):.2f} us")
    print(f"  max        : {us('max'):.2f} us")
    print(f"  misses     : {d['misses']}")
    print(f"  checksum   : {d['checksum']}")

    return 0 if acceptance.group("status") == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
