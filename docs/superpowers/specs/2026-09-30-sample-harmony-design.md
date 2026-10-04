# Sample harmony — design

Date: 2026-09-30 · Branch: `sample-harmony` · Status: **for Kerem's review**

## Goal

Fieldscape becomes fully sample-based and key-aware. Every engine that plays a recording (stretch,
grains, rhythm, and new route samplers that replace the digital synths) is pitch-analysed and tuned
to the route's harmony, so that everything sounds together in "super consonance". No digital
oscillators remain once it is approved.

Kerem, 2026-09-29/30: *"I want my stretch and grains engines in harmony with a key"*, *"routes sample
players are playing in harmony and share true fundamentals"*, *"we are talking about moving to full
sample based music with new route and zone polysamplers in super consonance with everything. No
digital synths"*, *"the main challenge will be the sample synths for route synths"*, *"dont forget the
idea of super consonance between all engines"*.

## Decisions (made in the 2026-09-30 design session)

| # | Question | Decision |
|---|---|---|
| D1 | Where is it tested? | A separate **lab page** (`lab.html`), same engine and data, not linked. The live site and the apps do not change until Kerem approves; then it moves over. |
| D2 | What does "in harmony" snap to? | **Chord notes by default; scale notes (key + section mode) as a per-sound choice.** Glide on chord change; hysteresis on every snap (rulebook A-14). |
| D3 | Unpitched material? | **Moment by moment by pitch confidence:** confident → retuned exactly; not confident → coloured by resonances at the chord notes; crossfaded between. A **Tune** amount per sound, 0 = untouched. |
| D4 | Where route sounds come from | A **library of sampler instruments** built by setters (Kerem), chosen per route role like a synth today. |
| D5 | Which synths | **Four:** Poly pitch sampler, Poly non-pitch sampler (models), **Sample FM**, **Sample AM/ring**. |
| D6 | FM/AM modulator source | A **one-period cycle cut automatically from the instrument's own recordings**; another moment may be chosen. |
| D7 | Architecture | **New instrument types inside the existing route engine** (`core/piece.cpp`), behind the existing `tone::Synth` interface. |
| D8 | Tuning | A **harmony core** shared by every engine; **per-route tuning: just intonation (default) or equal temperament.** |
| D9 | Held notes on chord change | **Glide** for Voice and Third voice; **restart** for Sections (a melodic line). |
| D10 | Morphs | Kept; they drive each synth's two timbre controls. |
| D11 | Lab UI | Research tooling only, plain; the real UI is designed (and mocked, C-13) when it moves to the site. |
| D12 | Judging mechanisms | An **audition bench** in the lab from sub-project 1 on, and a **listening session** closing every sub-project: Kerem drops in his own recordings and judges each mechanism by ear; his verdict keeps, reworks or drops it. *"I will upload a non-pitched synth for example and it will play the ratios with that sample and mechanisms so I will consider the mechanisms quality to derive pitch from them."* |
| D13 | Order | The **non-pitch sampler comes right after sub-project 1**, before the pitch sampler, so the first listening session is a non-pitched recording playing the chord ratios through each mechanism. |

## Architecture

All of it lives in the shared C++ core, so the website (wasm) and both apps sound the same.

```
recording ──► analysis (once, at upload) ──► saved with the instrument / point
                                                   │
route patch ─► route engine picks notes ─► HARMONY CORE ─► exact Hz ─► instrument / point engine
 (tempo, prog, sections, roles, fx, morphs)   (chord, scale, tuning,
                                               hysteresis, glide)
```

### Units

1. **Analysis** (`core/analysis.cpp`, new) — pure function over PCM: pitch track, confidence,
   brightness, loop points, modulator cycle, verdict. No engine state.
2. **Harmony core** (`core/harmony.cpp`, new) — the current chord/scale/tuning in, exact target
   frequencies out; owns hysteresis and glide state per caller. Fed by the route engine every block.
3. **Instrument store** — instrument records (JSON) and their recordings, loaded into the engine the
   way point recordings are today (`need` / `source` messages; `fs_engine_source`).
4. **Four synths** (`core/samplers.hpp`, new) — `tone::Synth` implementations.
5. **Point integration** — grains, stretch, rhythm read their analysis and ask the harmony core.
6. **Lab page** (`lab.html` + `web/lab.js`) — the site with the new path on, plus research tools,
   including the audition bench (below).

### Audition bench (D12)

In the lab, setters only. It grows as each synth is built.

- **Source:** drop any recording (or pick a point's); its analysis is shown (pitch track, confidence,
  verdict, loop, cycle).
- **Synth and model:** any built so far — resonator, harmonic filter, formant, pulsar, freeze, pitch
  sampler, Sample FM, Sample AM/ring.
- **What it plays:** a single note · the scale · the current chord in **just vs equal** (the ratios
  audible) · a route's full 16-chord progression at its tempo.
- **Live controls:** the two timbre controls, Tune, attack/release, while it plays.
- **Compare:** A/B two models or two settings on the same recording, back to back, level-matched.
- **Keep:** save a setting with Kerem's note ("wind + resonator, focus 0.7: good"); kept settings
  become the synth's defaults.
- **Numbers beside the ear:** for each run, how clearly the pitch was created — the spectral peak at
  each chord note against the noise floor around it (dB) — and the output level.

### Harmony core

- **State:** chord root + notes (from the progression, as `fs_piece_chord_notes` reads today), scale
  (key + current section mode), tuning (`just` | `equal`), glide time.
- **`target(f0, mode, key)`**: nearest target to a measured fundamental, any octave. `mode` = chord
  | scale; `key` identifies the caller so its hysteresis state is its own.
- **Hysteresis:** a caller moves to another target only when its pitch passes the midpoint between
  targets by a margin (start 30% of the gap, as the rulebook's measured case).
- **Just intonation:** chord notes as ratios of the chord root — unison 1, m2 16/15, M2 9/8, m3 6/5,
  M3 5/4, P4 4/3, tritone 45/32, P5 3/2, m6 8/5, M6 5/3, m7 7/4 (dominant) or 9/5 (minor 7th in minor
  chords), M7 15/8; extensions by the same table an octave up. The root itself is equal-tempered
  from the key, so a progression does not drift.
- **Shared fundamentals:** two callers on the same chord note get the identical frequency.
- **Glide:** targets change on chord change; every caller glides over the same time (patch
  setting, default 1 beat), so the piece moves as one.
- **Equal temperament:** `mtof` as today; the per-route switch picks one.
- Every current `mtof(...)` call in the route engine (`core/piece.cpp` ~799, 829, 920, 961, 987)
  goes through the harmony core, so the old synths also follow the route's tuning.

### Instruments (data)

```json
{ "kind": "instrument", "name": "Cello drone", "synth": "pitch",
  "model": "looped",
  "recordings": [
    { "storage_path": "<id>/r0.wav", "f0": 146.9, "confidence": 0.93, "centroid_hz": 1840,
      "range": [ 45, 55 ], "loop": [ 18234, 90112 ], "cycle": [ 40211, 40538 ],
      "track": "<pitch/confidence/centroid every 20 ms, compact>" } ],
  "sound": { "attack": 0.02, "release": 0.6, "t1": 0.3, "t2": 0.5, "tune": 1, "snap": "chord" } }
```

- Stored as a feature row (`kind: "instrument"`, no place, no geometry), published like points, so
  Storage, RLS, publish, backup and the app download path are reused.
- **Cap:** up to 6 recordings, 20 s of audio in total per instrument (phone memory).
- Every analysed value is editable by the setter.

### Analysis

- **Pitch:** YIN over 40 ms windows every 20 ms, with a median/Viterbi smoothing across frames to
  avoid octave errors; confidence = 1 − YIN's aperiodicity. Recording pitch = median over frames
  with confidence ≥ 0.8.
- **Brightness:** spectral centroid per frame.
- **Range:** 50 Hz – 2 kHz (on a ~16 kHz copy) and 1 – 8 kHz (on the recording, 10 ms windows), the more confident kept per frame — birds sing at 2–8 kHz (a goldfinch read unpitched with the low range alone, 2026-09-30).
- **Verdict:** pitched if ≥ 50% of the *sounding* frames are confident (frames 40 dB under the loudest are silence and do not vote), else unpitched (a suggestion only).
- **Loop points** (looped model): within the steadiest confident stretch, the lag of maximal
  autocorrelation, both ends snapped to zero crossings; crossfade 10–50 ms.
- **Modulator cycle** (FM/AM): one period (1/f0) at the most confident, steadiest frame, cut at zero
  crossings.
- Runs once at upload in the shared core (web: wasm; the apps never re-analyse). Existing points are
  analysed once, from the lab.

### The four synths

| Synth | Timbre 1 (`harm` slot) | Timbre 2 (`index` slot) | Models |
|---|---|---|---|
| **Pitch sampler** | position (start / loop placement) | brightness (a filter following the note) | one-shot · looped sustain · granular pad |
| **Non-pitch sampler** | focus (resonator / filter width) | colour: damping · overtones · vowel · grain length · frozen moment | resonator · harmonic filter · formant · pulsar grains · spectral freeze |
| **Sample FM** | ratio (0.25–4) | depth (0 = plain recording) | carrier = the recording retuned; modulator = its cycle at f × ratio, bending the carrier's read position |
| **Sample AM / ring** | ratio | depth (0 plain · ½ AM · 1 ring) | carrier × modulator cycle at f × ratio |

- **Pitch sampler:** chooses the recording whose range covers the note; speed = target ÷ f0; within
  ±6 semitones of a recording, else the nearest octave of the note.
- **Voices:** fixed polyphony per role (6); stealing takes the quietest voice with a ≥ 5 ms fade
  (A-5); every note has attack and an exponential release to silence (A-3, floors 5 ms / 30 ms).
- **Level:** a measured trim per synth type (like `SYNTH_TRIM`) so switching instrument stays within
  ±1 dB.
- **Low-confidence moments** inside a pitched recording are coloured, not retuned (D3).
- A role's `synth` becomes either an old synth name or `inst:<instrument id>`; `Role` keeps
  sampler instances keyed by instrument alongside its `inst[NSYNTH]`.

### Points

- **Grains:** each grain reads the pitch track at its read position; confident → speed set to the
  harmony core's target (per-grain, so click-free); otherwise a light resonance at the chord notes.
- **Stretch:** a **pitch-follow** transpose (reading the track at the stretched read position) onto the
  target, gliding; the existing Tune filter and Layers then build on the corrected fundamental.
- **Rhythm:** each hit analysed; pitched hits retuned per hit; unpitched hits unchanged.
- **Off-route:** the last chord heard; before any, the park's key centre.
- Every point gets **Tune** (0 = today's sound, bit-identical) and **chord/scale**.

## Sub-projects (each: spec detail → plan → build → listening session on the audition bench)

1. **Analysis + harmony core + lab page + audition bench** ← first
2. **Non-pitch sampler** — resonator first, then harmonic filter, formant, pulsar, freeze (D13);
   listening session per model
3. **Pitch sampler** + instruments (store, upload, route role picker, per-route tuning)
4. **Sample FM** and **Sample AM/ring**
5. **Points:** tuned grains, pitch-follow stretch, tuned rhythm hits
6. **Move to the site and apps** (on Kerem's approval): real UI designed and mocked, old synths
   retired after by-ear sign-off
7. *(Research, later)* overtone consonance: partial alignment scored by a roughness measure

### Sub-project 1 in detail

- `core/analysis.cpp` + `fs_analyse(pcm, frames, channels, rate, out_json)` in `fieldscape.h`.
- `core/harmony.cpp` + the route engine's `mtof` calls routed through it; per-route
  `patch.tuning` (`"just"` default, `"equal"`); the existing sound unchanged when `tuning` is
  `"equal"` (bit-identical fingerprint against today).
- `lab.html`: the audition bench, standalone, on its own engine file (`web/core-lab.wasm`, so the live
  site's engine never changes): a source panel (drop a file → pitch track, confidence, verdict, loop,
  cycle drawn) and players (note, scale, chord just/equal, progression). Until sub-project 2 adds real
  synths, the bench plays a plain retuned recording, enough to hear the harmony core's ratios. Walking
  the routes in the new tuning joins the lab when routes pick instruments (sub-project 3).
- Nothing in `index.html`'s live behaviour changes.

## How we know it works

| What | Check |
|---|---|
| Pitch analysis | synthetic sine, saw, bell-like tones within ±5 cents; noise → unpitched; no octave errors on saw/bell |
| Harmony core | JI targets exact ratios of the root; 0 note flips while a pitch hovers at a boundary; glides end on the exact target |
| Pitch sampler | a note on a recorded tone within ±3 cents of target, both tunings |
| Non-pitch / FM / AM | noise through each model has a spectral peak at the note frequency; Tune 0 / Depth 0 bit-identical to the plain recording |
| Consonance | two instruments on one chord note → identical Hz; a JI fifth has a flat envelope (no beating), ET shows the expected slow beat |
| Clicks | shortest notes, stealing, loop points, glides: no sample step above threshold; exponential tails (A-2, A-3) |
| Level | per-synth trims measured; instrument swaps within ±1 dB |
| Performance | 4 roles × 6 voices + points on the iPhone 8 build: render well under the audio block; memory within the 20 s cap |
| No regressions | everything new off → existing routes and points fingerprint-identical to today; all current suites pass on the live site and apps |

## Not in scope

- Replacing the route engine's note choice, rhythm, sections, effects or morphs.
- Any change to the live site or apps before Kerem's approval in the lab.
- Listener-facing A/B or sharing the lab.
- Overtone-alignment consonance (sub-project 7, research).

## Risks

- **Field recordings rarely have a clear pitch** → D3 (colour instead of retune) and the non-pitch
  sampler make unpitched material a feature, not a failure.
- **Large retunes sound artificial** → ±6 semitone zones, octave fallback, more recordings per
  instrument.
- **Phone memory** → 20 s per instrument cap; recordings downloaded once and kept.
- **Just intonation "bends" held notes on chord changes** → glide, and the per-route switch to equal
  temperament.
- **Windows App Control blocks `wasm-opt.exe`**, which web engine builds need → Kerem allows it, or
  wasm builds move to the Mac.

## Listening sessions

### Session 1 — 2026-09-30, the audition bench (sub-project 1), Kerem on Chrome

Material: *29 American Goldfinch Song, Call.mp3* (a bird, 3–6 kHz calls with silence between).

What the session found, and what changed because of it:
- Birds read "unpitched": the pitch search stopped at 2 kHz and silence outvoted the calls → a 1–8 kHz
  search beside the low one; silence does not vote.
- No sound: chords were placed in their own octave (a 4 kHz bird played 28× slower) → the chord or scale
  moves by octaves as one block, centred on the recording.
- Scale too quiet, steps unclear, progression changes inaudible → notes start at the recording's clearest
  moment; scales climb; chords keep their shape.
- "I can't see which envelope plays which" → a playing view: each voice's envelope at its pitch, chord
  names, a playhead, and the notes sounding now with their cents and speed.
- An Octave control (−3…+3) was asked for; Octave and Tune act on what is already sounding.

**Verdict (Kerem):** *"this works well — pitch is nicely adjusted for a pitched sound file; this version
and test works great."* The harmony core's retuning of pitched material is accepted as the base for the
synths. Next: sub-project 2, the non-pitch sampler (resonator first), heard on the same bench.

### Session 2a — 2026-10-01, the Resonator (lab2 on a private claude.ai page), Kerem on a phone

Material: Kerem's own recordings, away from home; the Resonator beside Retune on the same bench.

What the session found, and what changed because of it:
- The page could not load its engine or routes inside claude.ai's sandbox ("Failed to fetch") → lab2 carries
  the engine embedded and plays the default progression when routes are unreachable.
- Kerem asked whether the pitches come only from the recording → yes: the recording is the only input; the
  loop or the Bell's filters keep the frequencies of it that fit the note (a pitch the recording lacks
  comes out weak). Accepted as the principle.
- "In progression the chord changes are abrupt ... I want smooth cloudy transitions when release is longer
  than the note" → six voices cut a releasing chord in 5 ms (an 11 dB drop in 20 ms): 24 voices, a 50 ms
  steal fade, an S-curve attack in both synths, Attack to 4 s and Release to 10 s (a change now dips 0.2-0.9 dB).
- "Increase the master output twice, it is too low" → x2 (+6 dB) into a soft 0.98 ceiling.

**Verdict (Kerem):** *"this works"*, *"it's working good"*. The Resonator is accepted as the first
non-pitch mechanism. Next: 2b, the Harmonic filter and the Formant, heard on the same bench.

### Session 2b — 2026-10-02/03, the Harmonic filter and the Formant (lab2), Kerem

Material: Kerem's recordings; Harmonic filter and Formant × Bank / Spectral / Comb × Dry / Ringing beside the Resonator.

What the session found, and what changed because of it:
- "Clicks and clips ... chord and progression with release full and overtones full" (Spectral worst, Comb a few,
  the Formant too; Ringing good; Resonator perfect) → the engine kept time (no late blocks, measured) but the lab's
  ×2 master, an oversampled curve, rang past full scale (output peaks 1.14) and hard-clipped dense Dry noise into
  crackle → a 5 ms look-ahead limiter to −1 dBFS (every synth now peaks at 0.891; quiet playing still ×2).
- *"Ringing mode sounds really good"*; *"Resonator work still perfect"*.

**Verdict (Kerem):** *"it worked very well"*. The Harmonic filter and the Formant, all three methods, are accepted
beside the Resonator. Next: 2c, Pulsar grains and Spectral freeze, on the same bench.

### Session 2c — 2026-10-04, Pulsar and Freeze (lab2), Kerem

Material: Kerem's recordings; Pulsar and Freeze beside the four earlier synths.

What the session found, and what changed because of it:
- "General volume can be ×2.5 since it's too low" → the master went ×2 → ×2.5 (+8 dB), the −1 dBFS limiter kept.
- "Freeze can be more since the outcome sound is low in volume" → Freeze plays 3 dB over its moment; Pulsar's start now
  counts the in-between read's loss (it crept up over 3 s, longer than a lab chord). Measured: on chords every synth
  already reaches the ceiling, so chord loudness follows each synth's crest factor (Resonator 8.9 dB … Pulsar 14.6 dB);
  a loudness control was offered and left — Kerem: "it works great".

**Verdict (Kerem):** *"it works great"*. Pulsar and Freeze are accepted; sub-project 2 (the non-pitch sampler: Resonator,
Harmonic filter, Formant, Pulsar, Freeze) is complete. Next: sub-project 3, the pitch sampler and instruments.
