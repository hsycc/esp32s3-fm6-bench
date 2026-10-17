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

The nominal 44.1 kHz / 32-frame block period is approximately **725.6 us**. QEMU does not model physical ESP32-S3 execution time accurately, so that value is useful for architectural context only; emulator cycle counts are used for **version-to-version regression**, not as a real-silicon CPU claim.

## Primary validation strategy: QEMU

GitHub Actions builds the actual `esp32s3` firmware image and runs it in Espressif's ESP32-S3 QEMU. QEMU is the required project gate.

The CI currently validates:

- pinned Felucca revision and generated fixed-point tables;
- ESP-IDF compilation for the ESP32-S3 target;
- target boot and benchmark completion under QEMU;
- all **32/32 DX7 algorithms** with 12 requested voices;
- every algorithm reporting all **12 voices sounded**;
- non-zero deterministic audio signatures for every algorithm;
- a second 32-algorithm pass reproducing the first-pass signatures exactly;
- 6 / 8 / 10 / 12 voice render paths;
- non-zero final output checksum;
- no panic, assert, Guru Meditation or functional-failure marker;
- no emulator deadline-miss regression in the configured sweep;
- emulator timing statistics for trend/regression analysis.

A structured log checker (`tools/check_qemu_log.py`) makes the workflow fail if any required coverage or determinism invariant is missing.

## Validation stages

### Stage A — FM6 topology and polyphony

- 32 algorithms
- 12 voices per algorithm
- 6 / 8 / 10 / 12 voice sweep
- checksum activity
- crash/assert detection

### Stage B — deterministic audio regression

- first-pass signature for each of 32 algorithms
- identical second pass from a fully reset state
- structured QEMU log validation
- later: freeze revision-specific golden signatures after repeatability is established

### Stage C — DX7 feature matrix

The next functional layer expands beyond the current full-level stress patch and covers:

- low / middle / high MIDI notes
- low / high velocity
- feedback 0 / 7
- ratio and fixed-frequency operator modes
- operator envelopes and release
- pitch envelope
- LFO pitch/amplitude modulation
- keyboard scaling / rate scaling / velocity sensitivity
- note-on / note-off transitions

### Stage D — long-run and event stress

- repeated note allocation/release
- rapid algorithm/patch changes at block boundaries
- deterministic pseudo-random event streams
- long QEMU soak runs
- checksum/signature reproducibility

### Stage E — emulator performance regression

QEMU min / mean / P50 / P95 / P99 / max cycle values are stored and compared as regression indicators. Exact emulator numbers are not converted into claims about physical ESP32-S3 CPU utilization.

## Repository layout

```text
.
├── .github/workflows/
│   ├── qemu.yml              primary CI gate
│   └── hardware-image.yml    optional/manual only
├── main/                     ESP-IDF benchmark application
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
QEMU_RESULT voices=12 ...
QEMU_VALIDATION: PASS failures=0 algorithms=32 voices=6/8/10/12
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
