# Sample harmony 5 — points that follow the chord

Parent: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` ("Points": grains each read the pitch track at their
read position and go to the harmony's target; the stretch a pitch-follow transpose onto the target, gliding; rhythm hits
each analysed, pitched hits retuned; off-route the last chord; every point a Tune, 0 = today's sound). Lab only (D1).
Decisions under Kerem's go-ahead of 2026-10-06 ("finish everything"); each the recommended option.

## What points already have

The stretched bed's own shaping (2026-09-27): **Tune** — a comb that pulls the sound onto the chord's notes ("wind hums
the chord"), layers at the chord's intervals, glide. What it does not do: move a *pitched* recording's own pitch onto the
chord. That is this sub-project.

## Decisions

| # | Question | Decision |
|---|---|---|
| P1 | The control | One per point, **follow** (0 … 1; 0 = today's sound, sample for sample), lab mode only. How much of the way to the chord a pitched recording's pitch moves. |
| P2 | Stretch (bed) points | Each frame: the recording's pitch at the stretch's read position (its pitch track, confidence ≥ 0.8) → the nearest chord note (any octave) → the frame's transpose ratio moves there by follow, gliding with the point's own glide. Unclear pitch: the ratio glides back to 1. |
| P3 | Grains points | Each grain: the pitch at its read position → its rate scaled toward the nearest chord note by follow. Unclear: unchanged. |
| P4 | Hit points | Each pitched hit (its recording's f0) → its rate scaled toward the nearest chord note by follow. Unpitched: unchanged. |
| P5 | The chord | The route's chord now (the engine's harmony); off every route, the last one heard. |
| P6 | Pitch data | In lab mode the page analyses each point recording (first 60 s) when the engine loads it, and sends the pitch track to that slot or hit; the engine keeps it with the recording. |
| P7 | Where follow is kept | The lab setups table, keyed by the point (as a route's roles); it reaches the engine as a lab-only overlay on the point (`sound.shape.follow`, `rhythm.follow`), never saved into the point. |
| P8 | Scale mode | Not built (chord only). |

## The panel

Lab mode on: the point's sound panel (Pitch & harmony) gains **follow (lab)**; a rhythm point's panel gains the same.

## Tests

| Kind | Check |
|---|---|
| Core | `follow_rate`: a 330 Hz pitch on a D minor chord at follow 1 goes to F (349.2 Hz); at 0 unchanged; ½ halfway in cents |
| Core | the stretch: a 330 Hz recording with its track, chord D minor, follow 1 → its output's peak at 349 Hz; follow 0 → 330 |
| Core | hits (the engine's note log): a pitched hit's logged rate moves to the chord at follow 1; unchanged at 0 |
| Browser | the follow control in lab mode on a point; it saves; follow 0 leaves the engine's point JSON unchanged |
| Regression | every suite |
