# Sample harmony B — overtone fit — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Retune notes fine-tuned (±30 cents) to where the recording's own overtones beat least with the chord.

**Architecture:** `sampler::peaks_of` finds a recording's six strongest spectral peaks when it arrives (piece.cpp
`fs_piece_role_source`, beside `spectrum_of`); they travel with the role recording (`RoleRec`) to the Resonator. The piece
hands the Resonator the current chord (Hz) before each sampler note. `start_retune` searches the detune that minimises
Sethares roughness and scales the note. `SamplerCfg.fit` (default on) switches it.

**Tech Stack:** C++ core (wasm), `index.html`, `web/lab.js`, Node/CDP tests.

**Spec:** `docs/superpowers/specs/2026-10-06-samples-b-overtone-fit-design.md`

## Global Constraints

- D1: only `web/core-lab.wasm` is rebuilt.
- ±30 cents; coarse 5 cents then 1 cent; < 2 % better than 0 → 0; chord tones with 4 harmonics (1/n); recording 6 peaks ≥ −30 dB.
- Budget: an 8-note Retune chord start within the 2c budget (< 2 ms at -O1).

## Review Focus

1. A chord with no notes yet (the piece's first beat) — no fit, no crash.
2. A recording whose f0 is 0 (unpitched) or whose spectrum is silent — no fit.
3. Re-voice and stealing — a restarted note fits again (the same answer for the same chord).
4. Fold and fit together — the folded note is the one fitted.
5. A saved role without `fit` — on (the default).

---

### Task 1: Core

**Files:** `core/samplers.hpp` (`peaks_of`, Resonator `fit`, chord, peaks, the search in `start_retune`), `core/piece.cpp`
(`SamplerCfg.fit`, `smp_of`, `RoleRec` peaks, `fs_piece_role_source`, `bind`, `note()` sets the chord and `fit`),
`core/test.cpp`.

- [ ] Failing tests (spec "Tests", core rows; Review Focus 1, 2).
- [ ] Implement; 19/19; rebuild `core-lab.wasm`; `web_lab` green.
- [ ] Commit.

### Task 2: Panels

**Files:** `index.html` (`labFields`: Retune gets a "fit" toggle on `st.fit`), `web/lab-roles.js` (state default
`fit: 1`, saved in `sampler`), `web/lab.js` (the toggle), tests.

- [ ] Failing checks: the toggle shows for Retune on the site and in the lab, and is saved.
- [ ] Implement; site, lab, save, patch suites, `npm test` green.
- [ ] Commit.
