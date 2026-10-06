# Sample harmony B — consonance fitted to each recording's own overtones: researched, not built

Parent: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (sub-project 7, "overtone consonance: partial
alignment scored by a roughness measure"); Kerem 2026-10-04: *"we can tune the consonance by the analysis on the
recordings itself as well"*. Lab only (D1). Under Kerem's go-ahead of 2026-10-06 ("finish everything").

## What was proposed

A Retune note fine-tuned by up to ±30 cents to where its recording's measured overtones beat least with the chord
(Sethares' roughness: for every pair of partials a1·a2·(e^(−3.5 s Δ) − e^(−5.75 s Δ)), s = 0.24 / (0.0207 f_min + 18.96);
the chord's notes with four harmonics; the recording's six strongest spectral peaks). Retune only: the other samplers
impose harmonic overtones, already in tune with the chord's just ratios.

## What was measured (2026-10-06)

The search modelled on a just major triad (200, 250, 300 Hz) for recordings with stretched (1, 2.08, 3.3), bell-like
(1, 2.4, 4.2) and weak-fundamental (0.3 · 1, 2.08, 3.3) overtones, notes on and between chord tones, 150–400 Hz:

- The best detune within ±30 cents lowered roughness by **0–5 %** — inaudible. A note that is a chord tone lands its
  fundamental on the chord's own, and any detune adds more beating there than it removes among the overtones; stretched
  overtones pull in different directions, so no single detune aligns them.
- Choosing the octave by roughness instead varies it 5–10× — but by register (a higher note sits above the chord's
  overtones), the same for a harmonic recording: it would push every sampler up, not fit it to its overtones.

## Decision

**Not built.** Consonance with the recordings is already carried by what exists: just intonation with shared
fundamentals (every engine on a chord note gets the identical frequency — sub-project 1), and the overtone fold (3c.1 F2:
a sampler note goes to the nearest octave whose overtones meet the recording's own energy). A ±30-cent fit adds code with
no audible effect. Revisit if a listening session finds a recording that clashes with the chord in a way the fold does not
fix — the measurement here is the starting point.
