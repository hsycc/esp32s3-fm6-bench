# ESP32-S3 FM6 Benchmark Results

Target configuration:

- ESP32-S3 @ 240 MHz
- 44.1 kHz
- 32 frames / block
- hard block deadline: ~725.6 us
- Felucca FM6 core revision: `5738617968a22b5d4c0d3470cfcc95559052f446`

## QEMU functional validation

GitHub Actions Run #7:

- Run: <https://github.com/hsycc/esp32s3-fm6-bench/actions/runs/39068217925>
- Result: **PASS**
- Environment: `espressif/idf:v6.0.3`
- Target: `esp32s3`
- Emulated CPU frequency reported by ESP-IDF: 240 MHz
- Full ESP-IDF build: PASS
- QEMU boot: PASS
- 32/32 DX7 algorithms rendered at 12 simultaneous voices
- Every post-warmup algorithm scan reported `sounded=12`
- 6 / 8 / 10 / 12 voice percentile paths completed
- Final checksum: `0x88b22111`
- Functional failures: `0`
- Panic/assert/Guru Meditation: none
- Firmware marker: `QEMU_VALIDATION: PASS failures=0`

The generated application binary was `0x29d10` bytes in this run, leaving 84% of the default 1 MiB application partition free.

### QEMU-only timing snapshot

These numbers are retained only as an emulator regression baseline. **They are not physical ESP32-S3 CPU performance measurements and must not be used to claim real-time headroom.**

The QEMU scan selected algorithm 21 as the highest mean-cost algorithm in that particular emulated run.

| Voices | Algorithm | Mean us | P50 us | P95 us | P99 us | Max us | Emulated misses |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 6 | 21 | 4.01 | 3.70 | 3.95 | 15.94 | 17.15 | 0 |
| 8 | 21 | 6.05 | 5.00 | 8.05 | 18.99 | 24.56 | 0 |
| 10 | 21 | 6.67 | 6.15 | 6.31 | 19.25 | 19.32 | 0 |
| 12 | 21 | 7.95 | 7.42 | 7.56 | 19.25 | 19.48 | 0 |

Again, the table above validates deterministic regression behavior only. QEMU's virtual cycle counter and execution rate are not equivalent to real LX7 silicon timing.

## Hardware

| Item | Value |
|---|---|
| Board | TBD |
| ESP-IDF | TBD on hardware (`v6.0.3` in QEMU CI) |
| Flash mode/frequency | TBD |
| PSRAM | not used by FM6 realtime state |
| Compiler optimization | `-O2` |

## Hardware worst algorithm scan

TBD after first physical ESP32-S3 run.

## Hardware full percentile results

| Voices | Algorithm | Mean us | P50 us | P95 us | P99 us | Max us | Deadline misses |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 6 | TBD | | | | | | |
| 8 | TBD | | | | | | |
| 10 | TBD | | | | | | |
| 12 | TBD | | | | | | |

## 12-voice hardware acceptance target

- Mean < 400 us
- P95 < 450 us
- P99 < 500 us
- Max < 600 us
- Deadline misses = 0

These are project engineering targets, not measured hardware claims.
