# Composition Step 4 — Deeper Styles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Four more styles — **Ostinato**, **Chorale**, **Tintinnabuli**, **Pointillist** — a **walking** mode for voice 1's bass, and **ornaments** and **glides** that stack on any style (and on the melody), all edited in the dock and heard at once.

**Architecture:** Plain data in `comp` as before. The styles join `play_style` in `core/piece.cpp`; the walking bass joins `bass_bar`; ornaments and glides are applied at the one place notes go out (`Piece::note()` for composed voices) so they work on every style, the melody and relation-made notes alike. `Voice.mask` grows from 32 to 64 bits to hold the new fields (one bit per key, as since step 1). Each new style is level-matched through `comp_gains` and gets rows in `core/levels.hpp` (regenerated with `tools/levels.mjs`). The page model mirrors the fields; the dock gets each style's editor in the inspector, behind a mock Kerem approves. Generate form is left unchanged in this step (its seeded output stays identical).

**Tech Stack:** as steps 2–3 — C++17 core → wasm (em++ `-O1`), Android NDK, iOS on the Mac mini, plain JS page, node tests, headless-Chrome browser tests.

**Spec:** `docs/superpowers/specs/2026-10-17-composition-design.md`, section **A. Styles** (Ostinato, Chorale, Walking bass, Tintinnabuli, Pointillist; ornaments and glides), build-order step 4. Earlier steps' ledgers: `scratch/compose1-ledger.md`, `scratch/compose2-ledger.md`, `scratch/compose3-ledger.md`.

## Global Constraints

- Composing is desk only; phones show the summary only (it learns the new style names).
- Total control: every random choice has an amount (0 = exactly as set), seeded per route (`comp.seed`) through `comp::rand01` — Pointillist's randomness is the style itself, still seeded.
- Existing sound kept: routes without the new styles/fields play note for note as main `41f9faa` (`test_kept`; the step-2 repeat hash `7233712993534382426`; `core/test.cpp` row 0; existing relation tests).
- Nothing on the audio thread allocates, locks, sorts or simulates; the patch read stays ≤ 1 ms warm.
- Audio budget: composer cost tests p99.9 < 1.0 ms and no block > 1.5 ms after warm-up (≤ 2 OS spikes), including a Chorale of 6 voices with glides and a busy ornamented melody.
- Nothing clicks (A-2): glides are continuous pitch ramps; ornament notes use the same envelopes; a style change lands on the bar line.
- Every style's level within ±2 dB of Free on every synth (as steps 1–2).
- Rulebook: C-8, C-5, C-1, nothing under `--t-xs`; a full mock approved by Kerem before the dock is built (C-13).
- Supabase egress: never run `core/tests/web_sound.mjs` in a loop.
- Branch `compose-4` from main `41f9faa`; merge and push only with Kerem's OK. Opus subagents are limited until 2026-10-23 20:00 Istanbul — sonnet until then.

## Review Focus

1. A Chorale asked for more voices than the chord has tones doubles tones in other octaves inside the register, never stacks unisons or leaves the register — pinned in Task 2.
2. A walking bass on a chord that never changes (listening from one spot) still walks (around the root), never stops or repeats one note forever — pinned in Task 4.
3. A glide between two notes on a sampler voice (which has no continuous pitch) degrades to a plain note change, never a click or a stuck pitch — pinned in Task 5.
4. Ornaments on a fast style (16n) never push a note past the next step or stack more than one ornament per note — pinned in Task 5.
5. Microtonal offsets apply to the composed voice only and survive canon/heterophony replays (the follower plays the offset pitch) — pinned in Task 5.

---

## File structure

| File | Responsibility | Tasks |
|---|---|---|
| `core/composer.hpp`, `core/composer.cpp` | new styles, fields (64-bit mask), bass walk, ornament/glide fields, parse + merge + glide | 1 |
| `core/piece.cpp` | Ostinato, Chorale, Tintinnabuli, Pointillist in `play_style`; walk in `bass_bar`; ornaments/glides in `note()`; `comp_gains` cases | 2–6 |
| `core/levels.hpp`, `tools/levels.mjs` | regenerated table (NSTYLE grows) | 6 |
| `core/tests/composer_test.cpp` | core tests | 1–6 |
| `web/compose-model.js`, `tests/compose-model.test.mjs` | FIELDS for the new styles, BASS walk, ORNAMENTS/GLIDES, labels | 7 |
| `design/compose/styles-mock.html` | the mock | 8 |
| `web/compose.js`, `web/compose.css`, `core/tests/web_compose.mjs` | the editors | 9 |
| Android `CompSummary.kt`(+test), iOS `Walk.swift`, `Chrome.kt`, `App.swift` | labels, TEST_BUILD 20, builds | 10 |

---

### Task 1: The data

**Files:** `core/composer.hpp`, `core/composer.cpp`, `core/tests/composer_test.cpp`

**Interfaces:**
```cpp
namespace comp {
enum Style { FREE, ARPEGGIO, PEDAL, STRUM, EUCLID, SWELLS, OSTINATO, CHORALE, TINTINNABULI, POINTILLIST, NSTYLE };   /* appended: indexes 0-5 keep */
enum BassMode { HOLD, BASS_PEDAL, BASS_OFF, BASS_WALK };
enum Refit { RF_DEGREE, RF_TRANSPOSE };                 /* ostinato: refit by chord degree / fixed transposition from the first chord */
enum Voicing { VO_CLOSE, VO_OPEN, VO_DROP2 };
enum TMode { TT_ABOVE, TT_BELOW, TT_ALT };              /* tintinnabuli: nearest triad tone above / below / alternating */
enum Approach { AP_CHROMATIC, AP_SCALE, AP_FIFTH };
enum Orn { ORN_NONE, ORN_GRACE, ORN_TRILL, ORN_MORDENT, ORN_SLIDE };
/* Voice gains (mask becomes uint64_t; new bits F_REFIT .. F_MICRO, one per key):
   ostinato: uses grid/acc (as arpeggio) + uint8_t refit = RF_DEGREE;
   chorale:  uint8_t voicing = VO_CLOSE, nvoices = 4, restrike = 16 (sixteenths); float strict = 1;   (voice-leading strictness 0..1)
   tintinnabuli: uint8_t tvoice = 0 (which voice it answers, 0..2), tmode = TT_BELOW;
   pointillist: float sil_min = 0.25, sil_max = 1.5 (seconds of silence between notes), plen = 0.15 (seconds);
   ornaments: uint8_t orn = ORN_NONE, orn_every = 0 (0 = by chance), float orn_prob = 0.3, orn_speed = 1 (x: 1 = a 1/32 at 96 bpm);
   glides: float glide_ms = 0; uint8_t glide_iv = 0 (0 = every move, 1 = steps only (≤ 2 semitones), 2 = leaps only (≥ 5)); int8_t micro[5] = {0}; (cents per chord degree, −50..50) */
/* Bass gains: uint8_t approach = AP_CHROMATIC, before = 1 (beats before the bar's end that approach the next chord), wrate = 4 (sixteenths per walking note) */
}
```
JSON: style ids `"ostinato" | "chorale" | "tintinnabuli" | "pointillist"`; keys `refit: "degree"|"transpose"`, `voicing: "close"|"open"|"drop2"`, `voices` (2..6), `restrike` (rate string as `rate`), `strict` (0..1), `answers` (0..2), `tmode: "above"|"below"|"alt"`, `silence: [min, max]` (0..8 s), `plen` (0.03..2 s), `orn: "none"|"grace"|"trill"|"mordent"|"slide"`, `orn_every` (0..8), `orn_prob` (0..1), `orn_speed` (0.5..2), `glide` (0..400 ms), `glide_iv: "all"|"steps"|"leaps"`, `micro: [c0..c4]` (−50..50); bass `mode: "walk"`, `approach: "chromatic"|"scale"|"fifth"`, `before` (1..4), `wrate` (rate string).

- [ ] **Step 1: Failing test** `test_styles4_parse`: one Voice per new style with every new key, the bass walk keys, ornaments and glides on a voice; assert every field read and clamped; a layer setting only `orn` keeps all other fields inherited (mask bit per key — the 64-bit mask); an unknown style string → FREE; old routes (no new keys) parse bit-identical (compare the `Cfg` bytes of a step-3 route before and after).
- [ ] **Step 2–4:** RED; implement (mask `uint64_t`, every merge/glide site updated — `comp::merge`, `apply`, `glide`: continuous new fields `strict`, `sil_min/max`, `plen`, `orn_prob`, `glide_ms`, `micro` glide over blend bars, the rest are discrete); GREEN 20/20; `test_kept` identical.
- [ ] **Step 5: Commit** `composition 4: the new styles, the walking bass, ornaments and glides in the data`.

---

### Task 2: Ostinato and Chorale

**Files:** `core/piece.cpp`, tests.

**Ostinato:** the voice's grid (as arpeggio: column = step of the rate, cell = chord degree or rest, accents) repeats every bar; `refit: degree` fits the degrees to the chord playing (deg_midi), `transpose` keeps the cell's intervals from the first chord's root and moves the whole cell by the root movement (the cell's shape is exact on every chord). Bars develop (step 2) unless `"bars": "repeat"`: an ostinato does **not** develop — it is the one style that repeats on purpose (say so in a comment).
**Chorale:** every `restrike` sixteenths, `nvoices` chord tones sound together as a voicing: `close` (within an octave above the bass of the voicing), `open` (spread: every other tone up an octave), `drop2` (close with the second-highest dropped an octave), inside `lo..hi`; voice leading with `strict`: each voice of the new chord moves to the nearest tone of the next voicing (strict 1 = minimal total motion, the voicing's top note may change; 0 = the plain voicing every time). Chord tones through the pool voices (`chord_voice`), notes held to the next restrike.

- [ ] **Step 1: Failing tests** `test_ostinato` (a 4-note cell 0 2 1 3 over D minor 7 → D A F C in bar 0; on chord 2 (F maj7) degree refit gives F C A E, transpose gives the cell moved by the root step with its intervals exact; with bars develop the ostinato's bars stay identical), `test_chorale` (close/open/drop2 voicings of D minor 7 with 4 voices inside the register — exact pitches; 6 voices on a 4-tone chord double tones in other octaves, no unisons, all inside the register (Review Focus 1); strict 1: total motion between consecutive chords ≤ that of strict 0; notes held to the next restrike).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `composition 4: ostinato and chorale`.

---

### Task 3: Tintinnabuli and Pointillist

**Tintinnabuli** (Pärt): the voice answers voice `tvoice`: for each of that voice's notes (read from the per-voice ring of step 3 — or, when that voice is Free, from its notes as they are struck; if not available, the voice plays nothing and the dock says so), it plays the nearest **triad** tone (root, 3rd, 5th of the chord playing) `above` / `below` / alternating, in its own register, at the same time and length. **Pointillist:** single short notes (`plen`) separated by seeded silences between `sil_min` and `sil_max`, pitches drawn from the chord's tones across the register (seeded), accents seeded within ±20 %.

- [ ] **Step 1: Failing tests** `test_tintinnabuli` (against a melody of known pitches: every T-voice note is the nearest triad tone above (or below) the melody note, distinct from it, inside the register; alt alternates above/below), `test_pointillist` (every gap between onsets in [sil_min, sil_max] (± one sixteenth), every pitch a chord tone, same seed same notes, other seed differs; sil_min = sil_max gives a regular pulse).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `composition 4: tintinnabuli and pointillist`.

---

### Task 4: The walking bass

**Files:** `core/piece.cpp` (`bass_bar` and a walk step on the compose transport), tests.

Bass mode `walk`: a note every `wrate` sixteenths. Within a bar it walks from the chord's root through chord and scale tones (seeded order, step-wise); in the bar's last `before` beats it approaches the **next** chord's root (`next` = the chord the walker meets next along the route — the piece knows the progression and the walking direction): `chromatic` (half steps into it), `scale` (scale steps), `fifth` (the next root's fifth, then the root). Because chords change by position, not time, the approach targets the next chord every bar; on a chord that never changes it walks around the root (Review Focus 2). In the bass register (as `hold`: around `patch.key - 24`).

- [ ] **Step 1: Failing test** `test_walk` (wrate 4: four notes a bar; the last note of a bar before a chord change is a half step (chromatic) / a scale step (scale) / the fifth (fifth) from the next root, and the change's first note is the next root; listening from one spot for 8 bars: never the same pitch more than 2 notes running and never stops; every note inside the bass register; `hold` and `pedal` unchanged).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `composition 4: the walking bass`.

---

### Task 5: Ornaments and glides

**Files:** `core/piece.cpp` (`note()` for composed voices; synth pitch ramps), tests.

**Ornaments** on a note (every `orn_every`-th note, or by `orn_prob` seeded chance when `orn_every` is 0): `grace` (the scale tone above, 1/32 × 1/speed, just before — the note starts on time, the grace takes time before it only if the previous note has ended, else it is shortened), `trill` (alternate with the scale tone above at 1/32 × 1/speed for the note's length), `mordent` (note → tone above → note in the first 3/32), `slide` (a glide from a third below into the note over 1/16). At most one ornament per note; none that would run past the next step (Review Focus 4). **Glides**: when `glide_ms` > 0 and the move qualifies (`glide_iv`), a one-voice synth's pitch ramps from the previous note's pitch to the new one over `glide_ms` (continuous, no retrigger click — use the synth's `freq` Param ramp; envelope retrigger as today); on samplers and chord-voice pools it falls back to a plain note change (Review Focus 3). **Microtonal offsets**: `micro[d]` cents added to a note played on chord degree `d` (and in canon/heterophony replays of it — the ring keeps the offset, Review Focus 5).

- [ ] **Step 1: Failing tests** `test_ornaments` (grace: an extra note one scale step up just before every Nth note, short; trill: the note's length filled with alternating pitches at the set speed; mordent shape; slide: a pitch ramp into the note; orn_every 0 + prob 0.5 → about half, same seed same notes; 16n arpeggio with trill never crosses the next step), `test_glides` (fm voice, glide 120 ms: the output's pitch (zero-crossing or autocorrelation estimate) moves continuously between two notes over ~120 ms, no onset spike above a clean attack (the onset check of step 1); steps-only skips leaps; a sampler voice: plain change, no click), `test_micro` (+25 cents on the 3rd: notes on degree 1 are 25 cents sharp; a canon follower replaying them is too).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `composition 4: ornaments, glides and microtonal offsets`.

---

### Task 6: Levels, cost and the read

**Files:** `core/piece.cpp` (`comp_gains` cases for the four styles, walk, ornaments), `core/levels.hpp` (regenerate — NSTYLE 6 → 10), `tools/levels.mjs`, tests.

- [ ] **Step 1: Failing test:** `test_levels` extended to the four new styles on all 11 synths within ±2 dB of Free; a walking bass's level within 2 dB of hold; an ornamented melody within 2 dB of the plain one; patch read warm ≤ 1 ms with the new styles; the cost lines with a 6-voice Chorale + glides + a trilled 16n melody within budget.
- [ ] **Step 2–5:** RED; solve each new style's hits (Chorale's n voices with chord share; Pointillist's sparse notes; Tintinnabuli from its answered voice's material); regenerate `core/levels.hpp` with `node tools/levels.mjs` (two passes); GREEN; commit `composition 4: the new styles level-matched`.

---

### Task 7: The model

**Files:** `web/compose-model.js`, `tests/compose-model.test.mjs`

`STYLES` gains Ostinato, Chorale, Tintinnabuli, Pointillist (labels as named); `FIELDS` per style (ostinato: grid/acc/refit; chorale: voicing/voices/restrike/strict; tintinnabuli: answers/tmode; pointillist: silence min/max, plen); `FIELDS.common` (or an `ORNAMENT`/`GLIDE` group) gets orn, orn_every, orn_prob, orn_speed, glide, glide_iv, micro[5]; `BASS` gains `walk` with approach/before/wrate; `setValue(style)` writes the style's defaults (as Task 7 of step 2 made it, so what is shown is what plays); `label`/`summary` know the names; `generate` unchanged (pin: its output is byte-identical over the step-3 seeds — record the hash first).
- [ ] Tests: every key/string/range matches `core/composer.cpp` (extend the core-vs-model check); defaults equal the core's; `effective` shows the right defaults per style; generate's output hash unchanged. RED → GREEN → commit `composition 4: the new styles in the model`.

---

### Task 8: The mock — HUMAN GATE (C-13)

**Files:** `design/compose/styles-mock.html` (built on `design/compose/relations-mock.html`, 1440×900).

- [ ] Frames: (1) the inspector on an **Ostinato** voice (the pattern grid as arpeggio's + the "refit: degree | transpose" switch) and a **Chorale** voice side by side in two states of one frame, or two frames: Chorale's voicing picker (close / open / drop-2 drawn as little stacked note diagrams), voices 2–6, re-strike rate, strictness; (2) the inspector's **ornaments & glides** section on a melody voice (ornament type segmented, every N / chance, speed; glide time, which moves, the five micro-tuning knobs labelled root/3/5/7/9 in cents) and the **bass row** set to walk (approach segmented, beats before, rate); plus states: Tintinnabuli answering a Free voice ("voice 1 is Free — tintinnabuli answers the notes it plays"), Pointillist's silence range as a two-handle slider, a glide on a sampler voice ("samplers change pitch without gliding").
- [ ] Measure (no spill/truncation), screenshot, show Kerem, **wait for his yes**, record in `scratch/compose4-ledger.md`, commit `composition 4: the styles mock, approved by Kerem`.

---

### Task 9: The dock

**Files:** `web/compose.js`, `web/compose.css`, `core/tests/web_compose.mjs`

- [ ] To the approved mock: each new style's editor in the inspector; the ornaments & glides section (collapsible, closed by default, a dot when anything is set); the bass row's walk; lane glyphs for the four styles (an ostinato loop, a chorale stack, a tintinnabuli pair, scattered dots) and an ornament mark on cells with ornaments. Browser test: every control writes its key; choosing each style writes its defaults; the micro knobs write `micro`; one undo step per edit; no scroll/spill at 1440×900 and 1024×700 (normal, grown, layers, generate panel open) with the ornaments section open; keyboard rules kept. Screenshots `16-styles.png`, `17-ornaments.png`. RED → GREEN; `web_patch`, `npm test`; commit `composition 4: the dock's new styles, ornaments and glides`.

---

### Task 10: The apps and the builds

- [ ] Android `CompSummary.kt` + test and iOS `Walk.swift`: the four style labels (separator as `\u00B7`); `TEST_BUILD = 20` (`Chrome.kt`, `App.swift`); rebuild the wasm (`cmp`), Android `assembleDebug` → `C:/Users/kerem/OneDrive/Desktop/Fieldscape-test-20.apk`, iOS: tar-sync `core/` + `ios/` to the Mac and `sh ios/build.sh` (rc 0). Commit `composition 4: the apps know the new styles (Test 20)`.

---

### Task 11: Verify, review, release

- [ ] `python core/tests/run.py` 20/20 (alone, idle); `npm test`; `web_compose`, `web_patch`, `web_core` (not `web_sound`).
- [ ] Final whole-branch review (the most capable model available); one fix dispatch; scoped re-review.
- [ ] `scratch/compose4-ledger.md`: built, measured, unverified.
- [ ] Test link for Kerem; merge `compose-4` → main and push **only with his OK**; verify live; tag and back up.
