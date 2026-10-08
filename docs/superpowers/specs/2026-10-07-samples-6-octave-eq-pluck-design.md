# Sample harmony 6: octave, a per-voice EQ, Plucked at level (lab)

Kerem, 2026-10-07, after testing 3d-5: "overall system works well ... small things: I want octave control for every
sampler synth; a graph EQ I can adjust for each voice; Bell/Plucked of resonator sampler is on very low volume".
Choices: EQ is a draggable curve; every voice gets one (digital or sampler). Lab mode only (D1: core.wasm untouched).

## 1. Plucked at level (done first)
Measured (core test "6 resonator levels", first 300 ms of a note on wind, vs String Bowed): Bell Plucked -47.6 / -54.8 dB,
String/Tube Plucked -7 to -11 dB. Cause: a Bell's modes have unity gain only at their peaks, so a 25 ms broadband burst
barely excites them; Plucked had no level follower (Bowed has its AGC).
- Bell Plucked: at note start the burst is run through the note's modes (150 ms) and the note made up to the burst's
  level (x1 to x1000; a silent burst is not boosted).
- String/Tube Plucked: a fixed +8 dB.
- Check: every body x excite within 6 dB of String Bowed at Focus 0.5/0.9, 110/196/440 Hz (now worst -4.3).

## 2. Octave, every sampler synth
- `sampler.octave`: integer -2..+2, default 0. The note's frequency x 2^octave after the fold (the fold picks the octave
  the recording sounds in; Octave then moves the note from there), clamped as every note is.
- UI: an "octave" stepped slider on every sampler role, beside tune. Saved in the role's sampler object.
- Changing it re-voices held notes (the 3c.1 settings re-voice).
- Check (core): Retune and Resonator notes at octave +1 / -1 sound at 2x / 0.5x the octave-0 pitch.

## 3. A per-voice EQ (every role: First, Second, Third voice)
- Five bands: low shelf (default 80 Hz), three bells (250 Hz, 1 kHz, 4 kHz, Q 1), high shelf (10 kHz). Gain -12..+12 dB,
  frequency 30 Hz - 16 kHz. Flat by default (all gains 0): an EQ at 0 dB everywhere is bypassed, bit-exact.
- Engine: in each role's chain after its layer (drive, warp, low-pass, level) and before its effects - the voice's own
  sound, not its delay/room. RBJ cookbook biquads; coefficients move per 32-sample block toward their targets (~30 ms), so
  a drag never clicks.
- Patch: `<role>.eq = [[f, gain_db, q] x5]` in the lab roles row, overlaid onto the patch for the engine in lab mode for a
  digital role as well as a sampler one. The live patch is never written.
- UI: in the patch panel, under each role's instrument, a graph 0-12 dB grid over 30 Hz-16 kHz (log), the summed
  response curve, five dots. Drag: left/right frequency, up/down gain; double-click resets a dot; keyboard: focus a dot,
  arrows move it. Autosaves like the other lab controls. Tokens only (no new colours).
- Checks: core - a +6 dB bell at 1 kHz reads +6 +-0.3 dB at 1 kHz and within 0.5 dB at 100 Hz/10 kHz; flat EQ output is
  identical to no EQ; a gain jump does not step (largest sample-to-sample difference stays under the dry signal's).
  Site - the graph shows on every role in lab mode, a drag saves `eq` and reaches the engine's patch.
