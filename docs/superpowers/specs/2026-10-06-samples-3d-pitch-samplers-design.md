# Sample harmony 3d — the pitch sampler's three models and Position

Parent: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` ("The four synths": Pitch sampler — Timbre 1
position, Timbre 2 brightness; models one-shot · looped sustain · granular pad). Lab only (D1). Decisions made under
Kerem's go-ahead of 2026-10-06 ("finish everything then I will test"); each is the recommended option.

## Decisions

| # | Question | Decision |
|---|---|---|
| R1 | Where the model lives | Retune's `method` (already saved per role in `sampler.method`): 0 One-shot, 1 Looped, 2 Granular. No data change. |
| R2 | Position | Retune's Focus slot (unused until now): where in the recording a note reads, 0 … 1 from the pitch track's first clear frame to the end. Morphs drive it as any timbre. Default 0.5. |
| R3 | Looped sustain | A loop at Position: about 0.5 s cut to a whole number of the recording's periods there (from its pitch track, else its f0), repeated with a 30 ms crossfade. A held note sustains. |
| R4 | Granular pad | Four overlapping 80 ms Hann grains (one every 20 ms) read around Position (±30 ms random), each at the note's speed. A steady, smeared pad at the note's pitch. |
| R5 | Brightness | Colour, as now. |

## The panel (site lab mode and lab.html)

Retune shows a **model** menu (One-shot, Looped, Granular), **position** (its readout the time in the recording, m:ss),
**brightness** and **tune**. The unpitched-sample warning stays.

## Tests

| Kind | Check |
|---|---|
| Core | One-shot unchanged at Position 0 (the 3a pitch and level checks pass) |
| Core | Position: a note at Position 0.75 reads the recording's second half (a recording whose halves differ in timbre) |
| Core | Looped: a 5 s note on a 1 s recording still sounds at 4-5 s (≥ its first second -6 dB); no join steps beyond the tone's own; on pitch within 5 cents |
| Core | Granular: a 5 s note sounds steadily (100 ms windows within 3 dB) on pitch within 10 cents |
| Browser | the site panel and the lab show the model menu and position for Retune; Looped on the Voice sounds |
| Regression | every suite |
