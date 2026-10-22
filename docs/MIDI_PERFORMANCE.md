# MIDI performance controls (QEMU-first)

Target remains a single FM6 instrument, 12 voices, 44.1 kHz, 32-frame DSP blocks. No SF2 co-running engine is introduced.

## Implemented contract

- Pitch Bend: MIDI E0..EF, LSB first, 14-bit value 0..16383, center 8192. The receiver range is fixed at +/-2 semitones. The upper endpoint is one wheel step below +2 semitones. Convert to a Q24 log2-frequency offset using signed 64-bit intermediate arithmetic; do not quantize to the upstream 1/16-semitone note API.
- Bending changes the pitch of existing and new ratio-mode operators without note-on, EG, phase, allocator or LFO restarts. Fixed-frequency operators retain the upstream fixed-frequency behavior.
- CC7 Channel Volume and CC11 Expression: each 0..127, initial values 127/127 for bit-exact compatibility with the previous unattenuated engine. Project gain policy is linear amplitude multiplication: volume * expression / (127 * 127), represented in Q16. This is not a claim of GM2 volume-law conformance.
- Gain changes settle over 128 rendered samples (about 2.90 ms). A new target starts from the current gain; repeated identical targets do not restart the ramp. Unity gain preserves exact old PCM. Zero gain still renders FM state, so unmuting does not restart or freeze the note.
- CC121 value 0: center bend, restore expression to 127, release the sustain pedal and pedal-held notes. Keep physically-held notes, program, CC7 volume and voice capacity. It is not CC120 or CC123.
- Program Change retains receiver controllers and the gain ramp while clearing old-patch voices. CC120 hard-kills voices but retains receiver controls and the patch.
- Commands remain in one bounded SPSC FIFO and are applied only at audio-block entry. Invalid controller values and full-queue submissions reject without changing the running state or the caller's sequence output.

## Ownership and scope

There is one producer, one audio consumer and one global FM6 port. Runtime fields/accessors are audio-owner state, not a synchronized UI snapshot. The current router has a selected channel or a legacy Omni merge mode; this is not a multitimbral 16-channel synth and Omni does not provide independent per-channel controllers. New MIDI byte-stream tests do not imply tested physical UART/USB/BLE transports.

Pitch bend is updated at block rate; there is no extra pitch ramp yet. RPN bend-range control, CC1 modulation, aftertouch, pan, MPE and click-free voice stealing/patch transitions remain separate work. The output is still raw int32 mono; PCM16 conversion, limiter, I2S and device audio output are not part of this change.

The raw fm6_port checksum/energy are BEFORE output gain. Volume and full-chain regression tests deliberately accumulate the PCM returned by fm6_control_runtime_render_block instead. Their whole-stream hashes survive Program Change, which resets raw port counters.

## QEMU gates

`main/fm6_performance_tests.c` adds 23 checks including endpoint and fragmented MIDI decoding, actual sine zero-crossing frequency, fixed-mode invariance, unity bit equivalence, post-gain energy ratios, bounded ramps, mute/resume phase continuity, reset/program order, rejection, and twelve-voice allocator stability. A 512-block controller/note/program sequence runs twice, compares whole-stream PCM signatures and state accounting, then hard-stops to silence. The strict checker rejects missing or duplicated tests and summaries.

All previous DX7 import, program queue, dual-core, runtime, MIDI parser, 32-algorithm and stress gates remain enabled. QEMU validates function and reproducibility, not physical LX7 cycles, I2S deadlines or radio contention. Passing status must be taken from the actual Actions run, not from this design document.

## Protocol references

- MIDI Association, Summary of MIDI 1.0 Messages: https://midi.org/summary-of-midi-1-0-messages
- MIDI Association, Control Change Messages: https://midi.org/midi-1-0-control-change-messages
- MIDI Association, Response to Reset All Controllers (RP-015), listed in https://midi.org/midi-1-0-addenda
