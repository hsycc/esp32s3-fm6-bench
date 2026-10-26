# PCM16 output boundary (QEMU-first)

Target: one FM6 instrument, 12 voices, 44,100 Hz, 32 frames per render block. This adds an actual reusable output API, not a physical audio driver.

## Path and API

```
MIDI router -> SPSC control FIFO -> control_runtime block entry
  -> 12 FM6 voices -> int64 sum -> saturate to signed int32 Q24
  -> existing smoothed CC7 x CC11 gain
  -> fixed headroom -> round -> saturate -> signed PCM16 mono
  -> explicit little-endian byte sink
```

Call `fm6_pcm_output_init(&output, FM6_PCM_HEADROOM_DEFAULT)` once, then `fm6_pcm_output_render_block(&output, &runtime, pcm16, &applied)`. This wrapper renders exactly one block. Do not render the runtime separately before calling it. `fm6_pcm_output_convert_block` also accepts an already-rendered post-gain int32 block. `fm6_pcm16_encode_le` serializes for a file/test sink; it is not a UART, USB or I2S driver.

Objects belong to the audio task. No allocation, lock, file operation or second engine/voice pool is introduced. Buffers must not overlap. Invalid pointer/configuration/capacity calls do not consume queued commands or modify destination PCM/statistics. An underlying runtime apply failure can consume earlier commands; it is reported, not silently converted into a successful block. Discard destination PCM when a call fails. These accessors are not synchronized UI snapshots.

## Numeric policy

The pinned Felucca core labels its per-voice output Q24. Define one unit as 2^24. Q24 to PCM16/Q15 therefore divides by 2^9 before extra headroom.

The fixed default headroom is **4 bits**, so the actual divisor is **8192** (2^(9+4)). This is /16 amplitude, about 24.08 dB of headroom relative to the unattenuated Q24-to-Q15 conversion. It is an initial engineering policy, not an optimum loudness setting. Twelve coherent one-unit inputs produce 24576, but a six-carrier FM patch can exceed one unit per voice; clipping is still possible. No active-voice normalization or automatic gain pumping occurs. Headroom 0..15 is accepted at initialization; changing it during a note is not smoothed. Use the existing MIDI volume/expression controls for live gain changes.

Rounding is nearest with exact halves away from zero. Widen before taking magnitude, so INT32_MIN is valid. Shift unsigned magnitudes only. Clamp to [-32768,32767] before the int16 cast. Positive and negative clipping counts record rounded values outside this asymmetric representable range; reaching exactly -32768 is not clipping. Zero input gives exact zero. No dither/noise shaping is enabled.

The voice sum now uses 32 int64 accumulators (256 bytes of additional render stack), then narrows once. Saturating each addition would destroy later cancellation; unchecked signed int32 addition could wrap/overflow. Normal in-range sums remain exact. The upstream operator core itself is unchanged; this is not a claim that all its arithmetic has been sanitizer-audited.

## Two different clipping diagnostics

- `mix_saturated_frames`: raw int64 voice sum exceeded int32. It is measured BEFORE CC7/CC11. Large downstream attenuation cannot recover peaks already clipped here.
- `clipped_positive`, `clipped_negative`, `clipped_blocks`: the final PCM16 representation clipped after MIDI gain and headroom.

These must not be conflated. The QEMU tests deliberately make the first nonzero while the second stays zero. A future fully-wide runtime could avoid the intermediate int32 saturation; this change preserves the existing runtime ABI and makes the limitation explicit. This is hard saturation protection, **not a transparent limiter, compressor, speaker protector or click-free patch/voice-steal solution**.

Other counters are total frames/blocks and peak input/output magnitude. PCM output statistics survive Program Change and CC120; explicit `fm6_pcm_output_reset_stats` resets diagnostics only. `fm6_port_last_mix_saturated` is per block and resets on program load/kill. Standalone converter calls cannot infer pre-gain clipping; only the runtime wrapper aggregates it.

## Validation

26 QEMU directed checks cover known vectors, signed extreme inputs, rounding, monotonicity, headroom, exact silence, little-endian packing, rejection, wide accumulation/cancellation, runtime ownership, mute/hard stop and statistics. A loud 12-voice coherent patch must equal `clamp(12 * separately rendered single-voice PCM)` at every sample over 2048 frames, exercising BOTH clipping polarities. This is an independent numeric oracle rather than an arbitrary changed checksum.

A MIDI/CC/pedal/program sequence renders 1024 blocks plus one final hard-stop block (32800 samples), repeats twice, and also runs through the explicit render/gain/convert reference path. Hashes cover final serialized PCM16 bytes across all program changes. Commands must all apply, maximum held-note count must reach 12, and the final block must be zero. The log checker has mutation tests to reject missing, duplicated, failed or inconsistent evidence. All earlier QEMU gates remain required.

No physical I2S/DMA deadlines, analog levels, actual S3 throughput, listening quality or DX7 bit-identical emulation is established by this gate. Use the actual Actions run for pass/fail evidence, not this document.
