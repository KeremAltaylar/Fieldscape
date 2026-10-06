# Sample harmony 3d — pitch sampler models and Position — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Retune gains Looped and Granular models and a Position control, in the core and both panels.

**Architecture:** `sampler::Resonator::retune()` branches on the voice's `method` (0 one-shot, 1 looped, 2 granular);
`start_retune()` places the read at Position (`focus`). The panels add a model menu and a position control for Retune.

**Tech Stack:** C++ core (wasm), `index.html`, `web/lab.js`, Node/CDP tests.

**Spec:** `docs/superpowers/specs/2026-10-06-samples-3d-pitch-samplers-design.md`

## Global Constraints

- D1: `web/core.wasm` is not rebuilt; only `web/core-lab.wasm`.
- Position 0 … 1 spans from the pitch track's first clear frame (`tclear`) to the recording's end; default 0.5.
- Loop ≈ 0.5 s, a whole number of periods; 30 ms crossfade. Grains 80 ms Hann, hop 20 ms, ±30 ms jitter, 4 at once.
- Budget: 24 voices of each model within the 2c budget (99.9 % of blocks < 1.33 ms × 1.5 at -O1).

## Review Focus

1. A loop or a grain reaching the recording's end or start — reads stay inside, no wrap-around clicks.
2. A very short recording (< 0.5 s) with Looped — the loop shrinks to what exists, still no clicks.
3. Position morphing while a note sounds — the next note follows; the sounding one keeps reading (re-voice applies on a patch change).
4. A Retune role saved before 3d (method 0, focus 0.5) — plays One-shot from the middle; the panel shows One-shot.
5. Granular level matching — its level near One-shot's on the same note (within 3 dB).

---

### Task 1: Core — the models and Position

**Files:** `core/samplers.hpp` (Voice fields, `start_retune`, `retune`), `core/test.cpp`.

- [ ] Write the failing tests (spec "Tests", core rows; plus Review Focus 1, 2, 5 and the budget).
- [ ] Run `python core/tests/run.py` — FAIL (Looped silent after the recording; Position ignored).
- [ ] Implement: Voice gets `rls, rle, rL, gpos[4], gage[4], gk`; `start_retune` sets the read start from `focus` and, for
      Looped, the loop `[rls, rle)` of whole periods; `retune()` crossfades the loop in its last 30 ms and wraps; Granular
      runs four Hann grains, a new one every 20 ms at Position ± 30 ms, each advancing at the note's speed.
- [ ] Run — 19/19 with the new checks; rebuild `core-lab.wasm`; `node core/tests/web_lab.mjs` green.
- [ ] Commit.

### Task 2: Panels — model menu and position

**Files:** `index.html` (`labFields`: for `s-retune` a "model" menu on `method` with One-shot/Looped/Granular and
"position" on `focus`, readout m:ss), `web/lab.js` (`buildRoleCtl`: the same two for Retune), `core/tests/web_site_lab.mjs`,
`core/tests/web_lab.mjs`.

- [ ] Write failing checks: the site panel and lab.html show model + position for Retune; Looped Retune on the Voice
      sounds (level above -45 dB with a pitched sample).
- [ ] Run — FAIL.
- [ ] Implement.
- [ ] Run the site, lab and save suites, `npm test` — green.
- [ ] Commit.
