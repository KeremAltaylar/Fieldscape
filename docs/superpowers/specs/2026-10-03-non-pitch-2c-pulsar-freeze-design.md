# Sample harmony 2c — Pulsar grains and Spectral freeze — design

Date: 2026-10-03 · Branch: `freeze-2c` (from `partials-2b`) · Status: **for Kerem's review**
Parent spec: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (D1–D13; sessions 1, 2a, 2b passed).
Previous steps: `…-2a-resonator-design.md` (R1–R5), `…-2b-partials-design.md` (P1–P5).

## Goal

The last two non-pitch mechanisms (D13), judged by ear on the lab bench beside the Resonator, the Harmonic filter
and the Formant:

- **Pulsar grains** — tiny slices of the recording fired exactly f times a second: the repetition is the pitch, the
  slices' content the colour. Buzzy, vocal, rhythmic low down.
- **Spectral freeze** — one moment of the recording held for ever, its colour placed on the note's partials: the
  chord sings that moment's timbre, unchanging.

As before, the recording is the only sound that goes in, and the notes come from the chord system (Just or Equal).

## Decisions (2026-10-03 session)

| # | Question | Decision |
|---|---|---|
| Q1 | What fills Pulsar's grains over time | **Focus moves between fresh and frozen:** 0 = every grain a fresh slice (the recording keeps moving inside the buzz), 1 = one slice repeated (a still buzz-tone); between, the slice is refreshed less and less often. |
| Q2 | Pulsar's Colour (grain length) | **Relative to the note:** the grain is a fraction of one period, so every chord note shares one character (as P2). |
| Q3 | How Freeze's moment becomes the note | **The moment's colour placed on the partials:** each partial n f gets the frozen spectrum's level at n f. |
| Q4 | Freeze's Focus | **Purity:** 0 = a wide band of the moment around each partial (breathy), 1 = a single clean line (glassy). |

## Pulsar

- **The train.** Grain k starts at `t_k = t_0 + k·P`, P = sr / f samples — fractional: each grain's window is
  evaluated at the continuous offset `τ = n − t_k`, so the repetition rate (the pitch) is exact.
- **The grain.** Length `D = max(4, d·P)` samples, d = `0.05 + 0.95·Colour` (5 % … 100 % of a period); a Hann window
  over D; between grains, silence (d < 1). Label: **Grain N %**.
- **The content.** Grain k reads the recording from its slice start `r_k` onward. The slice start is refreshed to the
  voice's current place in the recording every `R = 0.001·10^(4·Focus)` s (1 ms … 10 s), never at Focus 1; otherwise
  `r_k = r_{k−1}`. At Focus 0 each grain is fresh; at Focus 1 every grain is the first.
- **Level.** A Hann grain of duty d passes `0.375·d` of white power; the slow level matching (3 s, as 2b) starts there.

## Freeze

- **The moment.** At a note's start, the power spectra of 4 frames (2048-point Hann, hop 512) at the moment are
  averaged: magnitude `M_k = sqrt(mean power)`. The moment is `Colour × (the recording's length − 1 frame)`. Label:
  **Moment m:ss**.
- **The note.** Each partial n (n f < 0.45·sr, up to 24) keeps the bins within `h = 1 + 3·(1 − Focus)` bins of n f, a
  raised-cosine weight times `M_k`. Resynthesised by the 2b Spectral engine (real FFT, hop 512, Hann synthesis, voices
  staggered on the global clock). Each kept bin's phase advances at its partial's own frequency n f per hop — the
  partial is in tune — plus, for Focus < 1, a random turn of up to `(1 − Focus)·π` per hop, so a wide band breathes
  as noise around the line; at Focus 1 it is one clean line.
- **Level.** Fixed at the moment's own: the output is scaled once, at capture, so its RMS equals the moment's RMS
  (no running level matching — the recording moving on underneath must not move a frozen note).

## Shared (one voice system)

24 voices, the bench's note queue, steals (never a voice holding or about to start a note), the 50 ms steal fade,
the S-curve attack (≥ 8 ms), releases to 10 s, Tune blending in the unprocessed recording, Synth / Focus / Colour per
note. The Resonator, the Harmonic filter and the Formant render exactly as in 2b (a locked hash).

## The bench and the lab

- **Bench:** the Synth parameter gains 3 Pulsar, 4 Freeze.
- **Lab:** the Synth menu reads *Retune · Resonator · Harmonic filter · Formant · Pulsar · Freeze*; for Pulsar and
  Freeze only Focus and Colour show (no Method / Mode), Colour labelled **Grain N %** / **Moment m:ss**. Kept verdicts
  record the synth, Focus and Colour. Each step reaches lab2 when it passes; the live site changes only when merged.

## Safety and speed

- No clicks (A-2, A-3): Hann grains, ramped gains, exponential releases; Freeze's random phase never jumps a bin's
  level. No runaway: neither has feedback. Silence on no recording; extreme octaves clamped (6 Hz – 0.45·sr).
- Budget: 24 voices of each inside one 128-sample block (1.33 ms native, ×1.5 for the −O1 test build), with notes
  starting together and at lined-up times; a Freeze start (4 analysis frames) costs < 1 ms for an 8-note chord.

## How we know it works (measured at the output)

| Area | Check |
|---|---|
| Pitch | Pulsar and Freeze within **±1 cent**, 55 Hz – 3.5 kHz (Freeze at Focus 1) |
| Pulsar colour | Colour 0 (5 % grains) lifts partials 4–8 against the fundamental by **≥ 6 dB** over Colour 1 |
| Pulsar content | Focus 1: consecutive grains identical (after the first); Focus 0: consecutive grains differ |
| Freeze moment | two moments of different colour give partial balances that differ by **≥ 6 dB** somewhere in 1–8 |
| Freeze holds | the recording falling silent under a held Freeze note leaves its level within **0.5 dB** |
| Freeze purity | Focus 0 spreads a partial over ≥ 3× the width it has at Focus 1 |
| Level | Pulsar within **1 dB** of the recording, no fade-up, no swell; Freeze at the moment's level within 1 dB |
| Safety | no clicks, stability sweep, budget (together and lined-up), the 2b hash unchanged |
| Browser | the menus, labels, pitch created ≥ 10 dB for a chord on noise, and all earlier lab checks |

## Not in scope

Methods (Bank / Spectral / Comb) for Pulsar and Freeze; a breathing control separate from Focus (Q4 option C);
moments chosen automatically; the pitch sampler and routes using these synths; the live site (until merged).
