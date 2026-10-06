# Sample harmony 4 — Sample FM and Sample AM/ring

Parent: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` ("The four synths": Sample FM — ratio (0.25–4),
depth (0 = plain recording), carrier = the recording retuned, modulator = its cycle at f × ratio bending the carrier's read
position; Sample AM/ring — ratio, depth (0 plain · ½ AM · 1 ring), carrier × modulator cycle at f × ratio). Lab only (D1).
Decisions under Kerem's go-ahead of 2026-10-06; each the recommended option.

## Decisions

| # | Question | Decision |
|---|---|---|
| F1 | Synths | Two sampler synths, `s-fm` (Sample FM) and `s-am` (Sample AM), appended after Freeze (NSYNTH 19). |
| F2 | Carrier | The pitch sampler (Retune) itself: its Model (One-shot, Looped, Granular) applies; Position 0; brightness open. |
| F3 | Modulator | One cycle of the recording: from the first rising zero crossing after its clearest pitched frame, 1/f0 long, resampled to 256 points, DC removed, peak 1. An unpitched recording: a sine. |
| F4 | Ratio | Focus: 0.25 · 16^focus (0.25 … 4; 0.5 → 1). |
| F5 | Depth | Colour, 0 … 1. FM: the read position bent by depth · 2 periods of the recording · m. AM: carrier · ((1 − depth) + depth · m) — ½ is AM, 1 is ring. |

## Tests

| Kind | Check |
|---|---|
| Core | FM depth 0 = Retune at Position 0 (sample for sample) |
| Core | FM on a 220 Hz sine, ratio 1, depth 0.5: a 440 Hz sideband ≥ −25 dB of 220 (depth 0: under −60) |
| Core | AM depth ½: 440 Hz at −13 … 0 dB of 220 (exactly −12.04: a quarter of the carrier); ring (depth 1): 220 Hz ≥ 30 dB under depth 0's |
| Core | an unpitched recording: finite output; 24 voices within the budget |
| Core | levels against fm on a route's Voice (the 3a level check gains s-fm, s-am) |
| Browser | both synths in the Sampler group (now 8); their panel rows: model, ratio, depth, tune, sample; each sounds on the Voice |
| Regression | every suite |
