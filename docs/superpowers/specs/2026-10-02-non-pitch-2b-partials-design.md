# Sample harmony 2b — the Harmonic filter and the Formant, three ways each — design

Date: 2026-10-02 · Branch: `partials-2b` (from `resonator-2a`) · Status: **for Kerem's review**
Parent spec: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (D1–D13; sessions 1 and 2a passed).
Previous step: `docs/superpowers/specs/2026-09-30-non-pitch-2a-resonator-design.md` (R1–R5).

## Goal

Two more ways to draw the route's chords out of an unpitched recording — wind, water, gravel, breath —
judged by ear on the lab bench beside the Resonator:

- **Harmonic filter** — the recording heard only at each note's partials (f, 2f, 3f …): the recording
  itself singing the chord, its gusts and splashes kept in rhythm.
- **Formant** — the same partials with an emphasis peak placed on one partial number: overtone singing,
  a bright partial moving over a held fundamental.

As in 2a, the recording is the only sound that goes in; the mechanism only chooses which of its
frequencies come out, tuned to the chord system's notes (Just or Equal per route).

## Decisions (2026-10-02 session)

| # | Question | Decision |
|---|---|---|
| P1 | The Harmonic filter in time | **A switch, Dry / Ringing:** Dry follows the recording exactly (no ring); Ringing lets each partial sustain for the Focus time. Applies to both synths. |
| P2 | The Formant's colour | **Harmonic partials, not vowels** (Kerem: *"not vowels but harmonic partials over the fundamental"*): the emphasis is placed on a partial number, so it moves with the note and every note of a chord gets the same character. |
| P3 | The Formant's shape | **One peak** for now: Colour places it (partial 1→16), Focus sets how many neighbouring partials it covers. |
| P4 | How it is built | **All three methods, as a switch:** Bank (a band-pass filter per partial), Spectral (FFT: keep the bins near each partial), Comb (the recording plus itself one period later). |
| P5 | Which synths get the methods | **Only the 2b synths.** The Resonator stays exactly as accepted in 2a — rebuilding String/Tube by filters or FFT would mostly duplicate the Harmonic filter and lose the loop's physical character. If one method wins in listening, it can be offered to other synths later. |

## The sound

### Colour

- **Harmonic filter — overtone balance.** Partial n has weight `n^(−k)`, with k from 2 at Colour 0
  (mostly the fundamental) through 1 at Colour 0.5 to 0 at Colour 1 (all partials equal, brightest).
- **Formant — the peak's place.** Colour 0→1 moves the peak smoothly from partial 1 to partial 16
  (fractional positions blend the two neighbours). Partial weights: a raised-cosine bump centred on
  the peak, its half-width set by Focus (see below), over a floor of −24 dB so the fundamental is always
  faintly present and the note keeps its pitch.

### Focus

- **Dry:** how narrow each partial is — but never narrower than a band that dies within **30 ms**
  (a two-pole band falls 60 dB in 2.2 / bandwidth s, so bandwidth ≥ **73 Hz**): the sound follows the
  recording. The cost is physical, not a bug: a fast filter cannot be narrow, so on low notes (partials
  < 73 Hz apart) Dry's neighbouring partials blur and its pitch is weaker; Ringing is the narrow one.
- **Ringing:** how long each partial sustains, **T60 0.2–10 s** (`0.2·50^Focus`, as R5).
- **Formant, additionally:** the peak's half-width, from 0.5 partial (Focus 1: one partial sings out —
  a whistle) to 4 partials (Focus 0: a bright region).

### The three methods

- **Bank.** One two-pole band-pass per partial, peak gain 1, at n·f for n = 1 … min(24, ⌊0.45·sr / f⌋);
  partials at or above 0.45·sr are skipped, never folded. Bandwidth from Focus (Dry: ≥ 73 Hz; Ringing:
  from T60). Each partial's output is scaled by its Colour weight.
- **Spectral.** STFT, 2048-point Hann frames, hop 512, overlap-add. The recording is a file, so each
  voice reads it **ahead** by one frame: no added delay. Per frame, the bins within the partial's width of
  each n·f are kept, scaled by the Colour weight; every other bin is zeroed. *Ringing:* each kept bin's
  magnitude is `max(now, previous × decay)` with decay from the Focus T60 — a spectral sustain; the
  phase follows the bin's own advance.
- **Comb.** The recording plus a copy delayed by one period (fractional, the 2a all-pass tuning), through
  a one-pole low-pass in the delayed path set by Colour (dark ↔ bright). *Dry:* feed-forward only.
  *Ringing:* feedback, gain from the Focus T60, capped below 1 as in 2a. *Formant:* after the comb, one
  peak filter (constant 0 dB skirt, +18 dB at its centre) at the peak's partial × f, its width from Focus.
  The comb's overtone balance is a tilt, not per-partial weights — its character, stated in the lab.

### Shared with the Resonator (one voice system)

24 voices, the bench's note queue, a steal only of a voice not holding or about to start a note, the
50 ms steal fade, the S-curve attack (≥ 8 ms), releases up to 10 s, Tune blending in the unprocessed
recording (with its 10 ms glide), and the output matched to the recording's own level (the bowed
automatic gain, ceiling 1000, adapting only while the recording sounds). A change of Synth, Method,
Mode, Focus or Colour applies to the next note; sounding notes keep theirs.

## Safety and speed

- **No clicks** (A-2, A-3): every gain change ramped; releases exponential to silence.
- **Never unstable:** Bank filters have fixed peak gain; Comb Ringing caps its loop gain below 1 at every
  frequency (the 2a rule); Spectral holds magnitudes ≤ their own past. A stability sweep covers every
  synth × method × mode × Focus × Colour across 55 Hz – 3.5 kHz.
- **Budget:** 24 voices of each method inside one 128-sample block's budget (1.33 ms native; ×1.5 for
  the −O1 test build). If Bank or Spectral does not fit, the partial count is lowered for the lowest
  notes, and the measured figure is recorded.
- **Errors:** no recording or a silent one → silence, no crash; extreme octaves clamped (6 Hz – 0.45·sr).

## The bench device and the lab

- **Bench:** the device holds a Resonator *and* a Partials synth; a **Synth** parameter picks which
  plays (Resonator, Harmonic filter, Formant). New parameters: **Method** (Bank, Spectral, Comb) and
  **Mode** (Dry, Ringing). Body and Excite stay the Resonator's.
- **Lab:** the Synth menu reads *Retune · Resonator · Harmonic filter · Formant*. For the two new ones,
  Method and Mode menus appear beside Focus and Colour; Colour's label reads **Overtones** (Harmonic
  filter) or **Partial N** (Formant, with the number). "Pitch created" works for all; kept verdicts record
  synth, method and mode.
- **Delivery:** each step reaches **lab2** (the private claude.ai page) as soon as its tests pass; the
  live site changes only when Kerem merges.

## Order of work

1. The Partials synth on **Bank**, both synths, Dry and Ringing; the bench and the lab menus.
2. **Spectral.**
3. **Comb.**

Each step ends green and published to lab2; one listening session (2b) records the verdicts for all three.

## How we know it works (measured at the output)

| Area | Check |
|---|---|
| Pitch | every synth × method plays the note: Bank and Spectral within **±1 cent**, Comb within **±3 cents** (the 2a loop's figure), 55 Hz – 3.5 kHz |
| Dry | after the recording falls silent, the output is down **60 dB within 30 ms** (Bank, Comb) or **one frame, 43 ms** (Spectral: a frame cannot end sooner) |
| Ringing | the decay's T60 matches Focus within **20 %** |
| Harmonic filter | Colour 1 raises partials 4–8 relative to the fundamental by **≥ 12 dB** over Colour 0 |
| Formant | among partials 2–16, the loudest is the one Colour names (±1 at a fractional position) |
| Level | within **1 dB** of the recording, every synth × method × mode |
| Safety | no clicks (largest step within the envelope bound), the stability sweep, silence on no recording |
| Budget | 24 voices per method within the block budget |
| Browser | the lab's menus, pitch created ≥ 10 dB for a chord on a noise file, and all earlier lab checks |

## Not in scope

Pulsar grains and Spectral freeze (2c); methods for the Resonator (P5); two-peak or drawn partial shapes
(P3); the pitch sampler and routes using these synths (later sub-projects); the live site (until merged).
