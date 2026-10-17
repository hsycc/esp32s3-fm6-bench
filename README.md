# ESP32-S3 FM6 QEMU Validation

Validate a 6-operator DX7-style FM engine for the **ESP32-S3 target** with **12 simultaneous voices**, using Espressif ESP32-S3 QEMU as the project's primary development and acceptance environment.

## Target configuration

- ESP32-S3 target, nominal 240 MHz configuration
- ESP-IDF 6.0.x
- 44.1 kHz synthesis rate
- 32-sample DSP block
- 6 operators / voice
- 32 DX7 algorithms
- 12 simultaneous voices = 72 active operator instances
- integer/fixed-point FM core imported from Felucca's MSFA/Dexed-derived engine

The nominal 44.1 kHz / 32-frame block period is approximately **725.6 us**. QEMU does not model physical ESP32-S3 execution time accurately, so emulator cycle counts and derived microseconds are used for **version-to-version regression only**, not as physical ESP32-S3 performance claims.

## Primary validation strategy: QEMU

GitHub Actions builds the actual `esp32s3` firmware image and runs it in Espressif's ESP32-S3 QEMU. QEMU is the required project gate. The hardware-image workflow is optional/manual and does not block normal development.

The CI currently validates:

- pinned Felucca revision and generated fixed-point tables;
- ESP-IDF compilation for the ESP32-S3 target;
- target boot and benchmark completion under QEMU;
- all **32/32 DX7 algorithms** with 12 requested voices;
- every algorithm reporting all **12 voices sounded** in the full-level topology scan;
- non-zero deterministic audio signatures for every algorithm;
- a second 32-algorithm pass reproducing the first-pass signatures exactly;
- directed DX7 feature coverage: pitch, velocity, feedback, ratio/fixed mode, pitch EG, LFO pitch/amp modulation, keyboard scaling, rate scaling and Note Off/Release;
- a deterministic **64-case patch/configuration soak**, executed twice;
- a deterministic **4096-block Note On / Note Off / active-voice retrigger stress**, executed twice and required to drain to silence;
- 6 / 8 / 10 / 12 voice render paths;
- no panic, assert, Guru Meditation or functional-failure marker;
- zero emulator deadline misses in the configured voice sweep;
- emulator timing statistics for trend/regression analysis.

A structured log checker (`tools/check_qemu_log.py`) makes the workflow fail if any required coverage, lifecycle, activity or determinism invariant is missing.

## Current QEMU baseline

The current accepted baseline completed all gates successfully:

| Check | Result |
|---|---:|
| DX7 algorithms | 32 / 32 PASS |
| Algorithm determinism replay | 32 / 32 exact signatures |
| Directed feature matrix | 12 / 12 PASS |
| Patch/configuration soak | 64 cases × 2 PASS |
| Event stress | 4096 blocks × 2 PASS |
| Event Note On | 875 |
| Event Note Off | 491 |
| Active-voice retriggers | 560 |
| Event-stress final release | 48 blocks to `final_sounded=0` |
| Directed Note Off/Release | 111 blocks to silence |
| QEMU functional failures | 0 |

The latest QEMU topology scan selected **Algorithm 21** as the highest-mean emulator regression case in that run. The voice sweep was:

| Voices | Mean cycles | P95 | P99 | Max | Emulator deadline misses |
|---:|---:|---:|---:|---:|---:|
| 6 | 986 | 1296 | 3444 | 3743 | 0 |
| 8 | 1315 | 1837 | 3771 | 4311 | 0 |
| 10 | 1644 | 2144 | 4063 | 4465 | 0 |
| 12 | 1973 | 2497 | 4348 | 4774 | 0 |

The accepted 12-voice regression marker is currently:

```text
QEMU_RESULT voices=12 alg=21 mean_cycles=1973 p95_cycles=2497 p99_cycles=4348 max_cycles=4774 deadline_misses=0 checksum=0xc29b6420
```

Again, those timing values are **QEMU regression metrics only**. They are intentionally not converted into a physical ESP32-S3 CPU-utilization claim.

## Validation stages

### Stage A — FM6 topology and polyphony — complete

- 32 algorithms
- 12 voices per algorithm
- 6 / 8 / 10 / 12 voice sweep
- checksum and energy activity
- crash/assert detection

### Stage B — deterministic audio regression — complete

- first-pass signature for each of 32 algorithms
- identical second pass from a reset state
- structured QEMU log validation

### Stage C — DX7 feature matrix — complete

- low / high MIDI pitch response
- velocity sensitivity
- feedback 0 / 7
- ratio and fixed-frequency operator modes
- operator envelope rate scaling
- pitch envelope
- LFO pitch modulation
- LFO amplitude modulation
- keyboard level scaling
- Note Off / Release to silence
- deterministic baseline replay

### Stage D — patch and event stress — complete

- 64 deterministic pseudo-random patch/configuration cases, replayed twice
- 4096 audio blocks of repeated per-voice Note On / Note Off activity
- active-voice retrigger semantics matching Felucca's non-fresh retrigger path
- Release-tail drain to `final_sounded=0`
- exact second-pass reproducibility

### Stage E — DX7 patch / SysEx compatibility — next

The next layer validates the actual DX7 voice interchange path rather than only synthetic in-memory patches:

- 155-byte runtime voice ↔ 128-byte packed DX7 voice conversion;
- value sanitization and canonical round-trip behavior;
- single-voice SysEx import/export;
- 32-voice bank SysEx import;
- Yamaha checksum validation;
- deterministic rendering from imported packed voices;
- malformed/truncated input rejection without touching the realtime renderer.

### Stage F — emulator product-path regression

After patch import is stable, extend the QEMU harness around the FM core with product-level state transitions such as allocator/voice stealing, patch commits at block boundaries, MIDI event queues, mixer output and optional effects. QEMU remains the primary acceptance environment.

## Repository layout

```text
.
├── .github/workflows/
│   ├── qemu.yml              primary CI gate
│   └── hardware-image.yml    optional/manual only
├── main/
│   ├── fm6_bench.c           topology/polyphony regression
│   ├── fm6_feature_tests.c   directed DX7 feature matrix + patch soak
│   └── fm6_event_stress.c    Note On/Off/retrigger lifecycle stress
├── components/fm6_port/      thin adapter around imported Felucca FM6
├── tools/
│   ├── import_felucca.py     reproducible upstream import
│   └── check_qemu_log.py     structured QEMU acceptance gate
├── docs/RELATED_PROJECTS.md  comparable embedded 6-op FM projects
└── LICENSES/                 upstream attribution/license notes
```

## Upstream FM6

The project uses the fixed-point 6-operator FM core from Felucca, derived from the MSFA/Dexed synthesis core. The exact upstream revision is pinned by `tools/import_felucca.py`.

```bash
python3 tools/import_felucca.py
```

The importer stages the required source and generated fixed-point tables into `components/fm6_port/upstream/` for the build.

Only the synthesis-core material needed by this benchmark is imported. Felucca's surrounding product/engine code has different licensing, so new compatibility helpers in this repository should remain independently implemented rather than copying GPL-only engine code into the benchmark.

## Run locally in QEMU

With Espressif QEMU available in the ESP-IDF environment:

```bash
export FM6_QEMU_CI=1
python3 tools/import_felucca.py
idf.py set-target esp32s3
idf.py build
idf.py qemu
```

A successful run includes markers similar to:

```text
QEMU_DETERMINISM: PASS algorithms=32 failures=0
QEMU_SOAK: PASS cases=64 passes=2 ...
QEMU_FEATURE_MATRIX: PASS tests=12 failures=0
QEMU_EVENT_STRESS: PASS blocks=4096 passes=2 ... final_sounded=0 ... deterministic=1
QEMU_RESULT voices=12 ... deadline_misses=0 ...
QEMU_VALIDATION: PASS failures=0 algorithms=32 features=12 event_blocks=4096 voices=6/8/10/12
=== benchmark complete ===
```

The CI additionally runs:

```bash
python3 tools/check_qemu_log.py qemu.log
```

and requires:

```text
QEMU_LOG_CHECK: PASS
```

## Optional hardware image

A hardware-image workflow is retained for occasional manual experiments, but it is **not** part of the normal acceptance path and does not run on every push.

## Why this exists

Felucca's product firmware uses a lower FM6 polyphony target. This repository isolates a narrower engineering question and makes it continuously testable: **can the ESP32-S3-targeted implementation remain functionally correct and deterministic with a true 12-voice, 6-operator DX7-style workload as the engine evolves?**
