# Sample harmony 5 — points follow the chord — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** a lab-only "follow" per point moves pitched point recordings (stretch, grains, hits) onto the chord.

**Architecture:** `core/harmony.hpp` gains a pure `follow_rate`. The stretch device takes a pitch track
(`fs_stretch_track`) and a `follow` parameter (appended; read from `sound.shape.follow`), scaling its per-frame transpose.
The piece's rhythm points take a track per source (`fs_piece_rhythm_track`) and `rhythm.follow`, scaling grain and hit
rates. The engine routes a track message to the slot or rhythm handle (`fs_engine_source_track`); the worklet passes it.
The page analyses point recordings in lab mode and sends the track; the panels get the follow control, saved in
`lab_route_roles` under the point's id and overlaid on the point for the engine.

**Tech Stack:** C++ core (wasm), `web/core-worklet.js`, `index.html`, Node/CDP tests.

**Spec:** `docs/superpowers/specs/2026-10-06-samples-5-points-follow-design.md`

## Global Constraints

- D1: only `web/core-lab.wasm` is rebuilt; follow 0 = today's sound sample for sample.
- Confidence ≥ 0.8; nearest chord note in cents, any octave; analysis of the first 60 s of a point recording.

## Review Focus

1. A point recording longer than 60 s: beyond the analysed part follow does nothing (ratio 1), no stale pitch.
2. A slot reused for another point: its old track is cleared with the new recording.
3. Off every route: the last chord; before any, no follow.
4. follow > 0 with an unpitched recording: unchanged sound.
5. The page's analysis cost on a long recording: off the audio path, once per recording.

---

### Task 1: Core
- [ ] Failing tests (spec rows "Core"). - [ ] `follow_rate`, stretch track/param/frame, rhythm track + grain/hit rates,
      engine routing, worklet message, exports. - [ ] 19/19; rebuild `core-lab.wasm`; `web_lab` green. - [ ] Commit.

### Task 2: Page and panels
- [ ] Failing browser checks. - [ ] Analysis + track send in lab mode; overlay; follow controls; autosave in the lab
      table. - [ ] All suites green. - [ ] Commit.
