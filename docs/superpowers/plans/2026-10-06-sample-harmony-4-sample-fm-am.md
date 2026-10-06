# Sample harmony 4 — Sample FM / AM — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Two new sampler synths, Sample FM and Sample AM/ring, built on the pitch sampler, in the core and both panels.

**Architecture:** `sampler::Resonator` gains synths `SFM` (6) and `SAM` (7). Their start runs `start_retune` at Position 0
with brightness open; a 256-point modulator cycle (cut from the recording at its clearest pitched frame, else a sine) is
built once per recording. Per sample: FM offsets Retune's read by depth · 2 periods · m; AM scales Retune's output by
(1 − depth) + depth · m. The piece lists `s-fm`, `s-am` (NSYNTH 19) with trims measured against fm.

**Tech Stack:** C++ core (wasm), `index.html`, `web/lab-roles.js`, `web/lab.js`, Node/CDP tests.

**Spec:** `docs/superpowers/specs/2026-10-06-samples-4-sample-fm-am-design.md`

## Global Constraints

- D1: only `web/core-lab.wasm` is rebuilt. Ratio 0.25 · 16^focus; depth = colour; FM deviation 2 periods · depth.
- Budget: 24 voices within the 2c budget at -O1.

## Review Focus

1. A recording with no clear pitch frame — the modulator is a sine; no division by zero.
2. FM deviation reading outside the recording at Position 0 — reads stay inside (the read helper's bounds).
3. A Retune role switched to Sample FM keeps its sample and Model.
4. Depth morphing — next notes follow; held notes re-voice on a patch change.
5. Level: Sample AM at ring (depth 1) loses level (~3 dB) — acceptable, measured and stated.

---

### Task 1: Core
- [ ] Failing tests (spec "Tests", core rows). - [ ] Implement. - [ ] 19/19, rebuild `core-lab.wasm`, `web_lab` green. - [ ] Commit.

### Task 2: Panels
- [ ] Failing checks (8 samplers; FM/AM rows; sound). - [ ] Implement in `web/lab-roles.js` (SAMPLER list, colour names),
      `index.html` (`labFields`), `web/lab.js` (`buildRoleCtl`, `colourName`). - [ ] All suites green. - [ ] Commit.
