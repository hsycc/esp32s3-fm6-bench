# ESP32-S3 FM6 Benchmark

Benchmark a 6-operator DX7-style FM engine on **ESP32-S3 @ 240 MHz** with a target of **12 simultaneous voices**.

## Target

- ESP32-S3 dual-core LX7 @ 240 MHz
- ESP-IDF 5.x
- 44.1 kHz sample rate
- 32-sample DSP block
- 6 operators / voice
- 32 DX7 algorithms
- 12 simultaneous voices (72 operators)
- Integer/fixed-point FM core derived from Felucca's MSFA/Dexed port

At 44.1 kHz and 32 frames, one audio block has a hard deadline of approximately **725.6 us**. At 240 MHz that is approximately **174,149 CPU cycles** on one core.

The first milestone measures only DSP rendering cost. No display, BLE, Wi-Fi, filesystem, sequencer or I2S is included in the timed region.

## Benchmark stages

1. **Stage A — FM6 core only**
   - 6 / 8 / 10 / 12 voices
   - min / mean / P50 / P95 / P99 / max cycles
   - deadline miss count
   - worst-case active operators
2. **Stage B — FM6 + mixer + I2S**
3. **Stage C — FM6 + product load (MIDI/BLE/UI)**

## Initial pass criteria for 12 voices

| Metric | Target |
|---|---:|
| Mean | < 400 us |
| P95 | < 450 us |
| P99 | < 500 us |
| Max | < 600 us |
| Hard deadline | 725.6 us |
| Deadline misses | 0 |

These thresholds are engineering targets, not claims about measured performance. Real ESP32-S3 measurements are required.

## Repository layout

```text
.
├── main/                    ESP-IDF benchmark application
├── components/fm6_port/     thin adapter around the imported FM6 core
├── tools/                   reproducible upstream import helper
└── LICENSES/                upstream attribution and license notes
```

## Upstream FM6

This project targets the fixed-point 6-operator FM core from Felucca, derived from the MSFA/Dexed synthesis core. The upstream revision is pinned in `tools/import_felucca.py`.

Run the importer before the first build:

```bash
python3 tools/import_felucca.py
```

The importer clones the pinned upstream revision (including submodules), generates Felucca's fixed-point tables, and stages the required source into `components/fm6_port/upstream/`.

## Build

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The initial harness is intentionally focused on deterministic CPU timing. I2S output is added only after the core-only numbers are known.

## Why this exists

Felucca currently caps its FM6 engine at six voices. This repository answers a narrower question: **can ESP32-S3 sustain a true 12-voice, 6-operator, 44.1 kHz FM engine with enough real-time headroom for a product?**
