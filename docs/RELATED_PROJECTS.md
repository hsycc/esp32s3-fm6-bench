# Related embedded 6-operator FM projects

This note records projects that are technically useful when judging whether a 12-voice 6-operator FM engine is realistic on ESP32-S3. They are references, not benchmark-equivalent proof.

## 1. rt-rtos/S3-Amysynth

Repository: https://github.com/rt-rtos/S3-Amysynth

Why it matters:

- ESP32-S3-N16R8 hardware.
- ESP-IDF application, not a desktop-only port.
- AMY includes DX7-style 6-operator FM and supports the 32 DX7 algorithms as starting topologies.
- Real-time synthesis runs on-chip.
- Audio DSP is pinned to Core 1 while UI, USB and BLE live on Core 0.
- The project documents fixed-point DSP, LUTs, IRAM placement, LTO, render-overrun counters and block-time diagnostics.
- Audio runs at 48 kHz with 256-frame blocks in that product architecture.

This is the closest public ESP32-S3 reference found for the overall product architecture, although it does not use the exact Felucca FM6 core or our 32-frame deadline.

## 2. copych/ESP32-S3_FM_Drum_Synth

Repository: https://github.com/copych/ESP32-S3_FM_Drum_Synth

Why it matters:

- ESP32-S3 target.
- 44.1 kHz I2S audio.
- Six FM operators per voice.
- 17 custom routing algorithms.
- Default `MAX_VOICES` is 10 in the public configuration.
- Uses LUT-oriented DSP optimizations and a modular voice allocator.

This is especially useful as evidence that 44.1 kHz multi-voice six-operator synthesis is practical on ESP32-S3, but it is not DX7-compatible in the same way as Felucca/MSFA and does not provide our 32-algorithm / 12-voice benchmark.

## 3. hugelton/Felucca

Repository: https://github.com/hugelton/Felucca

Why it matters:

- This is the direct upstream source for the fixed-point FM6 core used by this benchmark.
- The engine is derived from the MSFA/Dexed DX7 synthesis core.
- Six operators, 32 algorithms, DX7-style envelopes, keyboard scaling, feedback, pitch EG and LFO.
- It proves the engine is viable on embedded product hardware, but the reference firmware intentionally uses a lower polyphony target than this project.

This is the strongest reference for synthesis correctness, but not for ESP32-S3 performance.

## 4. zvenson/dxsloop and related FM-1 firmware

Repositories:

- https://github.com/zvenson/dxsloop
- https://github.com/keremimo/melodee

These projects extend the same FM-1/Felucca family with a complete 6-operator DX7 voice, 32 algorithms, on-device editing and SysEx workflows. They are useful for validating the feature surface expected from a practical DX7-compatible embedded engine.

## 5. shorepine/amy

Repository: https://github.com/shorepine/amy

AMY is a fixed-point-oriented synthesizer library for microcontrollers and includes a DX7-style operator/algorithm FM engine. S3-Amysynth is a concrete ESP32-S3 product using it.

## What was not found

A source search across `S3-Amysynth`, `ESP32-S3_FM_Drum_Synth` and `Felucca` did not find a QEMU-based ESP32-S3 FM validation workflow comparable to this repository's current CI path.

That means the useful public references split into two groups:

1. embedded projects proving that six-operator FM is practical on constrained hardware, and
2. ESP32-S3 projects proving that substantial real-time synthesis can run on the S3.

This repository still needs its own real-silicon timing measurement because no public project found has the same combination of:

- Felucca fixed-point FM6 core,
- ESP32-S3 @ 240 MHz,
- 44.1 kHz,
- 32-frame render block,
- all 32 algorithms,
- 12 simultaneous voices / 72 active operators,
- percentile and deadline-miss measurement.
