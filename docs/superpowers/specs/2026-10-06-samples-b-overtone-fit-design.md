# Sample harmony B — consonance fitted to each recording's own overtones

Parent: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (sub-project 7, "overtone consonance: partial
alignment scored by a roughness measure"); Kerem 2026-10-04: *"we can tune the consonance by the analysis on the
recordings itself as well"*. Lab only (D1). Decisions under Kerem's go-ahead of 2026-10-06; each the recommended option.

## Decisions

| # | Question | Decision |
|---|---|---|
| B1 | Which synths | **Retune** (the pitch sampler) only: it keeps the recording's own overtones. The other samplers impose harmonic overtones (n·f), already in tune with the chord's just ratios. |
| B2 | What is fitted | Each Retune note is fine-tuned by up to ±30 cents to where the recording's measured overtones (as they sound at that note) beat least against the current chord's notes and their first four harmonics. |
| B3 | The measure | Sethares' roughness (the Plomp–Levelt curve): for every pair of partials, a1·a2·(e^(−3.5 s Δ) − e^(−5.75 s Δ)), s = 0.24 / (0.0207 f_min + 18.96). Searched coarse (5 cents) then fine (1 cent); a fit under 2 % better than 0 cents stays at 0. |
| B4 | The recording's overtones | The six strongest peaks of its spectrum (`spectrum_of`, already measured when the recording arrives), each ≥ −30 dB of the strongest, at their parabolic-interpolated frequency, relative to its f0. An unpitched recording (no f0) is not fitted. |
| B5 | Control | A per-role **fit** switch (on by default) for Retune, saved in `sampler.fit`; off = the note as tuned. |

So a harmonic recording (a voice, a flute) lands at ~0 cents — unchanged; a bell, a metal bowl or a bird with stretched
overtones is moved to where it rings with the chord.

## Tests

| Kind | Check |
|---|---|
| Core | inharmonic recording (overtones 1, 2.08, 3.3) on the chord's fifth: fitted ≠ 0 cents, its roughness ≥ 10 % under 0 cents', and the output's pitch moved by the chosen cents |
| Core | harmonic recording (1, 2, 3): fitted within 2 cents of 0 |
| Core | fit off: the note exactly as tuned; budget: an 8-note Retune chord starting within the 2c budget |
| Core | the engine: a Retune role with an inharmonic recording sounds off the logged Hz with fit on, on it with fit off |
| Browser | the site panel and the lab show the **fit** switch for Retune; it saves |
| Regression | every suite |
