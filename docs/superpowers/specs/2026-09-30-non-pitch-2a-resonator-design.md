# Sample harmony 2a — the Resonator and the bench on the engine — design

Date: 2026-09-30 · Branch: `resonator-2a` · Status: **for Kerem's review**
Parent spec: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (D1–D13; listening session 1 passed).

## Goal

The first pitch-making mechanism for unpitched recordings: a **Resonator** that makes wind, water,
gravel or breath play the route's chords from its own material. It is built once, as an engine synth
that routes will use later, and it is judged on the lab's audition bench, which from now on plays
through the engine itself.

Kerem, 2026-09-30: *"if there is no pitch in the sound, we also have a sample synth that pitchifies it
... a poly non-pitch sampler which has attributes that establish pitches with formants or any other
mechanism"*; *"I will consider the mechanisms' quality to derive pitch from them."*

## Decisions (2026-09-30 session)

| # | Question | Decision |
|---|---|---|
| R1 | Pluck or bow? | **Both, selectable:** Plucked (a short burst strikes, the note rings and decays) · Bowed (the recording feeds continuously, the note sustains). |
| R2 | The resonating body | **A Body choice: String, Tube, Bell** — all three in this build. |
| R3 | Scope | **Three steps, a listening session after each:** 2a plumbing + Resonator; 2b Harmonic filter + Formant; 2c Pulsar + Spectral freeze. This spec is 2a. |
| R4 | Architecture | **A bench device in the engine:** the Resonator is an engine synth (`tone::Synth`); a small bench device holds one synth, its recording and settings, and plays notes the bench sends at exact audio-clock times, in an AudioWorklet from `web/core-lab.wasm`. |
| R5 | Focus | **How long a note rings:** ~0.2 s (close to the raw recording, a hint of pitch) to ~10 s (singing). |

## The Resonator

Per note, a tuned resonator fed by the recording.

- **Excitation.** *Bowed:* the recording streams into the resonator for as long as the note is held,
  from the synth's read position onward, looping over the recording. *Plucked:* at note-on a 10–40 ms
  burst of the recording (raised-cosine window) strikes it once.
- **Bodies.**
  - *String* — a delay line of one period (sr ÷ f) with feedback: all harmonics.
  - *Tube* — a delay of half a period with **inverted** feedback: odd harmonics only (1×, 3×, 5×…).
  - *Bell* — four two-pole resonators at 1, 2.76, 5.40, 8.93 × f (a bell/bar's inharmonic modes), each
    with its own decay, the higher modes shorter.
- **Tuning.** The loop length is fractional: a first-order all-pass tunes the remainder, and the loop
  filter's own delay is subtracted, so the fundamental lands within **±3 cents** from 50 Hz to 4 kHz.
- **Controls.**
  - *Focus* (timbre 1, the `harm` slot the morphs drive): decay time T60 from 0.2 s to 10 s, set as the
    loop gain `g = 10^(-3 / (T60 × f))` per period (per mode for Bell).
  - *Colour* (timbre 2, the `index` slot): a one-pole low-pass in the loop (String/Tube) or the mode
    balance (Bell) — dark and woody to bright and glassy.
  - *Tune*: 0 = the raw recording (bit-identical), 1 = fully resonated, between = a blend.
  - *Body*, *Plucked/Bowed*, *Attack*, *Release* (floors 5 ms / 30 ms, exponential release, A-3),
    *Octave* (the bench's, ±3).
- **Register.** An unpitched recording has no pitch to centre on: notes play at the chord's written
  register (the route's key region, D3–D5 for the current routes), moved by Octave.
- **Voices.** 6 per synth; a 7th steals the quietest with a ≥ 5 ms fade (A-5).
- **Safety.** Loop gain strictly < 1; a DC blocker on the output; no denormals (flush tiny values);
  every gain change ramped (A-2).
- **Level.** A measured trim so the Resonator plays within ±1 dB of the bench's Retune path.

## The bench device

- New engine device `"bench"` (`core/bench.cpp`), registered with the others (`core/core.cpp`
  REGISTRY): one synth (the Resonator in 2a), its recording (`fs_set_source_i16`), its parameters
  (`fs_set_param`: synth, body, mode, focus, colour, tune, attack, release), and a note queue.
- C calls (`core/fieldscape.h`): `fs_bench_note(d, hz, at_s, dur_s, vel)` — a note at device time
  `at_s`; `fs_bench_stop(d)` — every note released with a short fade; `fs_bench_time(d)` — device
  seconds; `fs_bench_created(d, hz)` — how far the output at `hz` stands above the spectrum around it
  (dB, the "pitch created" number).
- The Resonator lives in `core/samplers.hpp` as a `tone::Synth` (attack / release / timbre / render)
  plus `set_source`, so sub-project 3 drops it into route roles unchanged.
- `web/core-worklet.js`'s generic `FieldscapeCore` processor gains `note` / `stop` / `time` messages;
  `web/build-lab.sh` exports the new calls. The live `web/core.wasm` and site are untouched (D1).

## The lab page

- A **Synth** choice: *Retune* (today's WebAudio path, pitched recordings) · *Resonator* (the engine).
  Resonator shows Body, Plucked/Bowed, Focus, Colour.
- With Resonator, an unpitched recording is playable (the buttons are enabled; the why-line explains
  the register).
- The playing view, "Now" line, meter, Octave, Keep work for both; Keep records the synth settings.
- "**pitch created: +N dB**" beside the meter while the Resonator plays.

## How we know it works

| What | Check |
|---|---|
| Pitch | white noise, each body × mode: the output's peak at each note within ±3 cents, 50 Hz – 4 kHz, just and equal |
| Tube | odd harmonics ≥ 15 dB above the even ones |
| Bell | modes at 2.76 / 5.40 / 8.93 × f within ±1% |
| Focus | measured T60 ≈ 0.2 s at 0 and ≈ 10 s at 1 (±20%) |
| Tune 0 | output bit-identical to the recording (dry path) |
| Clicks | note-on, note-off, stealing, Plucked bursts: no sample step above threshold; exponential tails |
| Stability | Focus 1 for 60 s on loud noise: bounded, no NaN / inf / denormal slowdown |
| Level | Resonator vs Retune within ±1 dB (measured trim) |
| Performance | 6 voices Bowed String at 48 kHz: well under the 128-sample block budget |
| Browser | a noise "wind" file, Resonator, a chord: at the output, each chord note stands ≥ 10 dB above the floor; the unpitched file plays; Retune still passes its 33 checks |
| No regressions | live site and apps unchanged; all current suites pass |

## Not in scope (later steps)

Harmonic filter, Formant (2b); Pulsar grains, Spectral freeze (2c); the pitch sampler and routes
choosing instruments (sub-project 3); route effects on the bench.
