# Sample harmony 3a — routes play sampler synths — design

Date: 2026-10-04 · Branch: `instruments-3a` (from main `cfc378b`, where sub-project 2 is live) · Status: **for Kerem's review**
Parent spec: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (D1–D13; sessions 1, 2a, 2b, 2c passed).

## Goal

A route's roles can be played by **sampler synths** as well as the existing **digital synths**, all in superconsonance:
every note from either family takes its exact pitch from the one harmony core, in the route's tuning. Judged by ear in
the lab on Kerem's real routes, with his recordings.

Kerem, 2026-10-04: *"we normally have digital synths on routes, maybe we can have a section for digital synth (keep them)
and secondly sampler synth and we put these there. All of them will be in superconsonance with each other"*; *"we can tune
the consonance by the analysis on the recordings itself as well"*.

## Decisions (2026-10-04 session)

| # | Question | Decision |
|---|---|---|
| I1 | Where instruments live during research | **The real database, as unpublished drafts** (3b) — the data model final from day one. |
| I2 | How sub-project 3 is split | **3a** routes play sampler synths (this spec) · **3b** the recordings library (drafts in the database) · **3c** the two-section picker in the route editor + the pitch sampler's looped / granular models. A listening session closes each. |
| I3 | The role picker | **Two sections:** *Digital* (today's synths, kept unchanged) and *Sampler* (Retune, Resonator, Harmonic filter, Formant, Pulsar, Freeze). |
| I4 | Consonance from the recordings' analysis | **(A) in 3a:** every sampler note is corrected by its recording's own measured pitch, moment by moment. **(B)** bending the tuning to a recording's inharmonic overtones: its own step right after sub-project 3 (the parent spec's item 7, brought forward). |

## In the lab

- A **Route** panel: one of the published routes; for each role — **Voice** (bass and top), **Sections**, **Third voice** —
  a sound from **Digital** (the route's own synth, or another) or **Sampler** (the six), and for a sampler role a dropped
  recording (3b saves them). Sampler controls per role as on the bench: Body / Excite or Method / Mode, Focus, Colour, Tune.
- **Play** walks the route at a steady pace (a pace slider; the position shown) through the real route engine
  (`core/piece.cpp`, device `piece`, `fs_piece_add_route` / `fs_piece_walk`): its progression, sections, morphs and tuning.
  Any role can be switched while it plays.

## In the engine

- **Six sampler types** join the route engine's synth list after the digital ones: `s-retune`, `s-resonator`,
  `s-harmonic`, `s-formant`, `s-pulsar`, `s-freeze`. A role's patch keeps its `synth` string; a sampler role adds
  `"sampler": { "body", "excite", "method", "mode", "focus", "colour", "tune" }` (absent → the bench defaults).
- **The recording per role** comes from the host: `fs_piece_role_source(d, role, channels, frames, pcm)` and, for the pitch
  sampler, `fs_piece_role_analysis(d, role, analysis_json)` (the lab's `fs_analyse` result). A sampler role with no
  recording is silent, never a crash.
- **The non-pitch five** are the 2a–2c `sampler::Resonator` (24 voices there; a role uses 6, the parent spec's per-role
  polyphony), unchanged in sound. **Morphs** drive Focus and Colour through the two timbre slots (`timbre(0/1)`, D10).
- **The pitch sampler (`s-retune`)** becomes an engine instrument (`sampler::PitchSampler`, one-shot model; looped and
  granular in 3c): 6 voices per role, the shared envelopes (S-curve attack ≥ 8 ms, exponential release, 50 ms steal fade),
  reading the recording with the core's resampler kernel at `speed = target ÷ f0(t)`, where `f0(t)` is the analysis's
  pitch track at the read position — **(A)**: the note lands on the harmony core's pitch moment by moment, gliding over
  10 ms; frames under 0.8 confidence keep the last confident speed and are coloured, not retuned (D3); a recording with no
  confident frame plays at `target ÷ analysis.f0`. Timbre 1 = position (start offset), Timbre 2 = brightness (a filter
  following the note), as the parent spec.
- **Level:** a measured trim per sampler type (`SYNTH_TRIM` for the new entries) so swapping a role between a digital and
  a sampler synth stays within **±1 dB**.

## Superconsonance — what is guaranteed and measured

- **Same pitch:** a sampler note and a digital note on the same chord tone agree within **1 cent**, Just and Equal, for all
  six (the pitch sampler also on a recording whose pitch drifts ±30 cents).
- **No beating:** a Just fifth, digital root + sampler fifth, has a flat amplitude envelope (fluctuation < 1 dB at 1–20 Hz);
  in Equal the expected slow beat appears.
- **Limits stated, not hidden:** inharmonic material (bells, metal, the Resonator's Bell) still rubs above its fundamental
  — (B) addresses it; unpitched moments are coloured, not retuned; a fast slide inside a recording is followed with a
  10 ms glide.

## Safety, speed, and the live walk

- The live walk, setter and listener are **unchanged**: a route without sampler roles renders bit-identically (the
  existing route-engine fingerprints); the apps are untouched until Kerem approves (D1).
- No clicks, no runaway (the 2a–2c guarantees); 4 roles × 6 sampler voices inside one 128-sample block's budget, with
  chords starting together; a role's recording arrives off the audio thread.

## How we know it works

| Area | Check |
|---|---|
| Pitch | pitch sampler within ±1 c of the harmony core's Hz (steady and ±30 c drifting recordings, 55 Hz–2 kHz); non-pitch five as in 2a–2c |
| Consonance | digital and sampler on one chord tone: same Hz ±1 c; a Just fifth digital + sampler: envelope flat < 1 dB |
| Level | each sampler type's trim measured; a role swapped digital ↔ sampler within ±1 dB |
| Engine | routes without samplers bit-identical; a sampler role without a recording silent; switching a role mid-play click-free |
| Budget | 4 roles × 6 sampler voices + chord starts within the block budget |
| Browser | the Route panel: a route plays, each role switchable, Play / Stop, no page errors; all earlier lab checks |

## Not in scope

Saving recordings (3b); the route editor's picker and the looped / granular models (3c); tuning to inharmonic overtones (B,
after 3); FM / AM sample synths (sub-project 4); points (5); the live site and apps (6).
