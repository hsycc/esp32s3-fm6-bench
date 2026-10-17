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
STRESS_RESULT_RE = re.compile(
    r"HARDWARE_STRESS_RESULT voices=(?P<voices>\d+) alg=(?P<alg>\d+) "
    r"mean_cycles=(?P<mean>\d+) p95_cycles=(?P<p95>\d+) "
    r"p99_cycles=(?P<p99>\d+) max_cycles=(?P<max>\d+) "
    r"deadline_misses=(?P<misses>\d+) checksum=(?P<checksum>0x[0-9a-fA-F]+)"
)
ACCEPT_RE = re.compile(r"HARDWARE_ACCEPTANCE: (?P<status>PASS|FAIL) failures=(?P<failures>\d+)")
STRESS_ACCEPT_RE = re.compile(
    r"HARDWARE_STRESS_ACCEPTANCE: (?P<status>PASS|FAIL) failures=(?P<failures>\d+) "
    r"stress_sink=(?P<sink>0x[0-9a-fA-F]+)"
)
CPU_RE = re.compile(r"CPU=(?P<mhz>\d+) MHz")


def print_result(title: str, data: dict[str, str], mhz: int) -> None:
    def us(name: str) -> float:
        return int(data[name]) / mhz

    print(title)
    print(f"  voices     : {data['voices']}")
    print(f"  algorithm  : {data['alg']}")
    print(f"  mean       : {us('mean'):.2f} us")
    print(f"  p95        : {us('p95'):.2f} us")
    print(f"  p99        : {us('p99'):.2f} us")
    print(f"  max        : {us('max'):.2f} us")
    print(f"  misses     : {data['misses']}")
    print(f"  checksum   : {data['checksum']}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: parse_hardware_log.py <serial-log.txt>", file=sys.stderr)
        return 2

    text = pathlib.Path(sys.argv[1]).read_text(errors="replace")
    result = RESULT_RE.search(text)
    stress_result = STRESS_RESULT_RE.search(text)
    acceptance = ACCEPT_RE.search(text)
    stress_acceptance = STRESS_ACCEPT_RE.search(text)
    cpu = CPU_RE.search(text)

    if not result or not stress_result or not acceptance or not stress_acceptance or not cpu:
        print(
            "incomplete benchmark log: missing hardware/stress result or acceptance marker",
            file=sys.stderr,
        )
        return 2

    mhz = int(cpu.group("mhz"))
    raw = result.groupdict()
    stressed = stress_result.groupdict()

    print("ESP32-S3 FM6 hardware benchmark")
    print(f"raw acceptance    : {acceptance.group('status')} (failures={acceptance.group('failures')})")
    print(f"stress acceptance : {stress_acceptance.group('status')} (failures={stress_acceptance.group('failures')})")
    print(f"stress sink       : {stress_acceptance.group('sink')}")
    print()
    print_result("Raw Core1 FM6", raw, mhz)
    print()
    print_result("Core1 FM6 + synthetic Core0 load", stressed, mhz)

    raw_p99 = int(raw["p99"]) / mhz
    stress_p99 = int(stressed["p99"]) / mhz
    raw_mean = int(raw["mean"]) / mhz
    stress_mean = int(stressed["mean"]) / mhz
    print()
    print(f"mean delta        : {stress_mean - raw_mean:+.2f} us")
    print(f"p99 delta         : {stress_p99 - raw_p99:+.2f} us")

    passed = (
        acceptance.group("status") == "PASS"
        and stress_acceptance.group("status") == "PASS"
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
