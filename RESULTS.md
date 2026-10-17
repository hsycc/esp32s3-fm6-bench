# ESP32-S3 FM6 Benchmark Results

Target configuration:

- ESP32-S3 @ 240 MHz
- 44.1 kHz
- 32 frames / block
- hard block deadline: ~725.6 us
- Felucca FM6 core revision: `5738617968a22b5d4c0d3470cfcc95559052f446`

## Hardware

| Item | Value |
|---|---|
| Board | TBD |
| ESP-IDF | TBD |
| Flash mode/frequency | TBD |
| PSRAM | not used by FM6 realtime state |
| Compiler optimization | `-O2` |

## Worst algorithm scan

TBD after first device run.

## Full percentile results

| Voices | Algorithm | Mean us | P50 us | P95 us | P99 us | Max us | Deadline misses |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 6 | TBD | | | | | | |
| 8 | TBD | | | | | | |
| 10 | TBD | | | | | | |
| 12 | TBD | | | | | | |

## 12-voice acceptance target

- Mean < 400 us
- P95 < 450 us
- P99 < 500 us
- Max < 600 us
- Deadline misses = 0

These are project engineering targets, not measured claims.
