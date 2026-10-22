# Composition Step 3 — Relations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The three voices relate to each other: canon, call & response, hocket, heterophony, motion rules, unison and
shared density — each over a span of chords, optionally limited to a zone, all edited in the dock and heard at once.

**Architecture:** Relations are plain data in `comp.relations` parsed by `core/composer.cpp`. They act on the notes the voices
already produce, at one place: `Piece::note()` for composed voices. A small fixed ring per voice remembers its last notes
(time in ticks, MIDI, length, level, chord degree); **replay** relations (canon, heterophony) read the ring, **gating**
relations (hocket, call & response) decide whether a note sounds, **pitch** relations (motion rule, unison) move a note's
pitch before it sounds, and **shared density** caps a bar's notes by weight. Nothing allocates on the audio thread; every
random choice is seeded (`comp::rand01`). The page model mirrors the data; the dock draws relations as links between lanes
(the approved direction in `docs/superpowers/specs/2026-10-17-composition-mocks/editors.html`, relations card), edited in the
inspector, behind a mock Kerem approves.

**Tech Stack:** as step 2 — C++17 core → wasm (em++ `-O1`), Android NDK, iOS on the Mac mini, plain JS page, node tests,
headless-Chrome browser tests.

**Spec:** `docs/superpowers/specs/2026-10-17-composition-design.md`, section **C. Relations**, build-order step 3. Step 2's
ledger: `scratch/compose2-ledger.md`.

## Global Constraints

- Composing is desk only; phones show the summary only.
- Total control: nothing random unless allowed — every random choice has an amount (0 = exactly as set) and is seeded per route (`comp.seed`).
- Existing sound kept: a route without `comp.relations` plays note for note as main `b54fa16` (`test_kept`, the step-2 "repeat" hash `7233712993534382426` still matches with `"bars":"repeat"`, `core/test.cpp` row 0).
- Precedence zone > chord > section > route stays; a relation's span is chord steps (0..63); a relation with `zone` acts only while the walker is inside that zone.
- Nothing on the audio thread allocates, locks, simulates or sorts; the patch read stays ≤ 1 ms warm.
- Audio budget: the composer cost tests' p99.9 < 1.0 ms and no block > 1.5 ms after warm-up (≤ 2 OS spikes), with all relations on.
- Rulebook: C-8 one screen, C-5 states, C-1 tokens, nothing under `--t-xs`, A-2 nothing clicks; a full mock is approved by Kerem before the dock is built (C-13).
- Supabase egress: never run `core/tests/web_sound.mjs` in a loop.
- Branch `compose-3` from main `b54fa16`; merge and push only with Kerem's OK. Opus subagents are rate-limited until 2026-10-23 20:00 Istanbul — use sonnet until then.

## Review Focus

1. A relation whose leader or follower is off, or whose two voices are the same, does nothing (no crash, no silence of the other voice) — pinned in Task 1.
2. A canon follower must not feed back (follower → leader → follower): only notes a voice produced itself enter its ring — pinned in Task 2.
3. Hocket and call & response never silence the line entirely for a bar where the line has notes — pinned in Task 3.
4. Motion and unison moves stay inside the voice's register and on chord/scale tones — pinned in Task 4.
5. Shared density with every voice busy never drops below one note per voice that had notes, unless its weight is 0 — pinned in Task 5.

---

## File structure

| File | Responsibility | Tasks |
|---|---|---|
| `core/composer.hpp`, `core/composer.cpp` | `Relation` data, parse, `relations_in_force()` | 1 |
| `core/piece.cpp` | per-voice note ring, relation dispatch in `note()`/`compose_step`, each relation | 2–5 |
| `core/tests/composer_test.cpp` | core tests | 1–6 |
| `web/compose-model.js`, `tests/compose-model.test.mjs` | relations in the model, link geometry, labels, Generate's relations | 7 |
| `design/compose/relations-mock.html` | the mock for Kerem | 8 |
| `web/compose.js`, `web/compose.css`, `core/tests/web_compose.mjs` | links on the lanes, "+ relation", the relation inspector | 9 |
| apps (`Chrome.kt`, `App.swift`), wasm | TEST_BUILD 19, builds | 10 |

---

### Task 1: Relations in the data

**Files:** `core/composer.hpp`, `core/composer.cpp`, `core/tests/composer_test.cpp`

**Interfaces:**
```cpp
namespace comp {
static const int REL_MAX = 12;
enum RelType { R_CANON, R_CALL, R_HOCKET, R_HETERO, R_MOTION, R_UNISON, R_DENSITY, NREL };
enum Transform { T_NONE, T_INVERT, T_RETRO };
enum AnswerSrc { A_OWN, A_HALF, A_MIRROR };
enum Split { S_ALT, S_EVERY, S_MASK };
enum Motion { M_CONTRARY, M_PAR3, M_PAR6, M_FREE };
enum Meet { U_CADENCE, U_CHANGE, U_NEVER };
struct Relation {
    uint8_t type = R_CANON, a = 0, b = 1, c = 255;   /* voices: a leader/caller/first, b follower/answerer/second, c third (hocket, density: 255 = none) */
    uint8_t from = 0, to = NCHORD - 1; uint64_t zone = 0;   /* chord span; zone (id hash) or 0 = everywhere */
    uint8_t delay16 = 8; int8_t trans = 0; uint8_t transform = T_NONE;   /* canon: delay in sixteenths, transposition in chord degrees */
    uint8_t source = A_OWN, gap16 = 2, on_enter = 0;                     /* call & response */
    uint8_t split = S_ALT, every = 2; uint16_t mask = 0x5555;            /* hocket */
    float offset_ms = 30, ornament = 0;                                  /* heterophony */
    uint8_t motion = M_CONTRARY; float strength = 1;                      /* motion rule (strength: chance, 1 = strict) */
    uint8_t meet = U_CADENCE, octave = 0;                                /* unison: 0 unison, 1 octaves */
    uint8_t cap = 8; uint8_t weight[3] = { 1, 1, 1 };                     /* shared density: notes per bar, voice weights */
};
/* Cfg gains: Relation rel[REL_MAX]; int nrel = 0; */
/* the relations acting at chord `chord` with the walker inside `inside` zones: indices into c.rel, at most REL_MAX */
int relations_in_force(const Cfg &c, int chord, const uint64_t *inside, int ninside, int *out);
}
```
JSON: `comp.relations = [{ type: "canon"|"call"|"hocket"|"hetero"|"motion"|"unison"|"density", a: 0..2, b: 0..2, c?: 0..2, from, to, zone?: "<point id>", delay (sixteenths, 1..32), trans (−7..7), transform: "none"|"invert"|"retro", source: "own"|"half"|"mirror", gap (1..16), trigger: "phrase"|"enter", split: "alt"|"every"|"mask", every (2..4), mask (16-char "x."), offset (0..120 ms), ornament (0..1), motion: "contrary"|"par3"|"par6"|"free", strength (0..1), meet: "cadence"|"change"|"never", octave: "unison"|"octave", cap (1..32), weight: [0..4,0..4,0..4] }]`. Voices are 0 voice, 1 sect, 2 v3.

- [ ] **Step 1: Failing test** `test_relations_parse`: a list of 9 entries — one of each type with every field set, one with `a == b` (dropped: Review Focus 1), one unknown type (dropped), one with `from > to` (dropped), one with a zone id; assert `nrel == 7` in order, every field read and clamped, the zone hashed with `id_hash`; `relations_in_force` returns the span-and-zone-matching indices (a zone relation only when that zone is in `inside`); a 13th valid entry beyond `REL_MAX` dropped. (Write it in the style of `test_motif_parse`, using the file's Json API.)
- [ ] **Step 2: Run** `python core/tests/run.py` → FAIL (no member `rel`).
- [ ] **Step 3: Implement** the structs, a `rel_of` parser (round, then validate, as `chain_of` does), `relations_in_force` (fixed arrays).
- [ ] **Step 4: Run** → 20/20; `test_kept` identical.
- [ ] **Step 5: Commit** `composition 3: relations in the data`.

---

### Task 2: The note ring; canon and heterophony (replay)

**Files:** `core/piece.cpp`, `core/tests/composer_test.cpp`

Each composed voice keeps a ring of its last 64 notes: `{ long long tick; int midi; int deg; float len16; float vel; }`, written in `note()` only for notes the voice produced itself (not ones a relation made — Review Focus 2). In `compose_step`, a **canon** follower `b` (inside the relation's span) plays, instead of its own style, the leader's ring notes whose `tick == k - delay16`, transposed by `trans` chord degrees (re-fitted with `deg_midi` on the chord now playing), transformed (`invert` around the leader's first note of the bar, `retro` replays the leader's previous bar backwards), into `b`'s register, at `b`'s level gain. **Heterophony** `b` doubles `a`'s notes at `+offset_ms` with `b`'s own synth; `ornament` is the seeded chance of a grace note (the scale tone above, 1/32 long, just before).

- [ ] **Step 1: Failing tests** `test_canon` (leader melody with the default 4-note motif, follower v3 delay 8 = a half bar, trans +2: every follower note-on is a leader note-on + 8 sixteenths, its pitch two chord degrees up, inside v3's register; the follower's own style plays nothing inside the span; outside the span it plays its style again), `test_canon_no_feedback` (two canons a→b and b→a: no note appears more than twice), `test_hetero` (b's notes = a's at +30 ms ± 1 sample; ornament 0 → no grace notes; ornament 1 → one grace per a-note, seeded: same seed same graces).
- [ ] **Step 2–4:** RED, implement, GREEN; cost lines unchanged within noise.
- [ ] **Step 5: Commit** `composition 3: canon and heterophony replay a voice's own notes`.

---

### Task 3: Hocket and call & response (gating)

**Files:** `core/piece.cpp`, tests.

**Hocket** over voices `a, b (, c)`: the line is `a`'s notes; each line note sounds on exactly one member chosen by `split` (`alt`: in turn; `every N`: N notes on one, then the next; `mask`: bit i of the bar's sixteenth picks `a` (x) or `b` (.)); members other than `a` play nothing of their own inside the span. **Call & response**: `b` sounds only in `a`'s rests of at least `gap16`; its material: `own` = its own style gated to those rests, `half` = the motif's second half fitted to the chord, `mirror` = `a`'s last phrase inverted; `trigger: "enter"` = only during the phrase after entering the relation's zone.

- [ ] **Step 1: Failing tests** `test_hocket` (alt over a 16n arpeggio line: a and b alternate note by note, no note sounds on both, the union equals the line — and the line never goes silent for a bar that had notes: Review Focus 3), `test_hocket_mask`, `test_call` (b's notes never overlap a's sounding notes; with a 2-bar motif leaving bar 2 empty, b fills it; `source: "half"` plays the motif's second half; trigger enter: nothing before entering the zone, the answer in the 2 bars after).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `composition 3: hocket and call & response`.

---

### Task 4: Motion rules and unison (pitch)

**Files:** `core/piece.cpp`, tests.

**Motion rule** a/b: when `b` strikes, look at `a`'s last interval (its previous to its current pitch); with chance `strength` (seeded), move `b` to the nearest chord tone (or, for `par3`/`par6`, the scale tone a third/sixth below `a`) in `b`'s register that makes the motion contrary / parallel; `free` = no change. **Unison**: at `meet` moments (`cadence`: a cadence step's chord; `change`: the first note of each voice after a chord change), the voices' first notes take `a`'s pitch class (`octave`: the nearest octave of it in each register).

- [ ] **Step 1: Failing tests** `test_motion` (contrary, strength 1: over 2 bars every b move opposes a's move or is 0 when a's is 0; par3: b is a diatonic third below a on ≥ 90% of shared onsets; strength 0 = identical to no relation; every moved note inside b's register and a chord/scale tone: Review Focus 4), `test_unison` (meet change: the first note of each voice after every chord change shares a's pitch class; octave mode puts each in its own register).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `composition 3: motion rules and unison`.

---

### Task 5: Shared density (cap)

**Files:** `core/piece.cpp`, tests.

Per bar, the composed voices' notes are counted against `cap`; once over it, a voice's next note sounds only if its share so far (notes / weight) is the lowest among voices with weight > 0; a voice with weight 0 drops all notes past the cap; a voice that had notes in the bar keeps at least one unless its weight is 0 (Review Focus 5). Decided per note in `note()` from the counts so far (no look-ahead, no sorting).

- [ ] **Step 1: Failing test** `test_density` (three busy voices — 16n arpeggio, 8n strum of 4, 8n euclid — cap 8, weights 2/1/1: ≤ 8 notes a bar on average, the shares within ±1 of 4/2/2; weight 0 silences that voice past the cap; cap ≥ total = identical to no relation; every busy voice keeps ≥ 1 note a bar).
- [ ] **Step 2–5:** RED, implement, GREEN; run the cost lines with **all seven relations on** at once (one of each across the voices) and assert the step's budget. Commit `composition 3: shared density`.

---

### Task 6: Levels and the patch read with relations

**Files:** `core/piece.cpp` (`comp_gains`), tests.

A canon/heterophony follower plays the leader's notes, so its level solve must use the notes it actually plays inside the span (the leader's hits, transposed), or the follower is off by the density difference; hocket members share one line; density caps notes. Extend `comp_gains` so a voice whose material comes from a relation is solved from that material (its share for hocket/density). The memo key must include the relations that touch the voice.

- [ ] **Step 1: Failing test**: canon follower level within 2 dB of Free on fm/am/metal; hocket members within 2 dB; patch read with all relations warm ≤ 1 ms; the memo test extended with a relation edit (memo read == fresh solve).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `composition 3: relations are level-matched and the read stays under 1 ms`.

---

### Task 7: Relations in the model

**Files:** `web/compose-model.js`, `tests/compose-model.test.mjs`

`FsCompose.RELATIONS` (each type: label, fields with kinds/ranges/defaults matching Task 1's JSON), `addRelation(comp, type, a, b, span)`, `setRelation(comp, i, k, v)`, `removeRelation(comp, i)`, `relationsAt(comp, chord, zonesInside)`, `links(comp, n)` → `[{ i, type, a, b, c, from, to, label }]` for drawing, cell labels gain a relation mark (`"canon←v1"`, `"answers v1"`, `"hocket"`), `summary` unchanged. `generate()`: in a B letter at contrast ≥ 0.5, add one relation from the form — a canon of the melody by the support voice (delay a half bar, +2 degrees) or a call & response (support answers the melody, source half) — seeded; nothing else changes.

- [ ] Tests: every field/string/range matches what `core/composer.cpp` parses (extend the core-vs-model check); add/set/remove are one undo step each through `history`; `relationsAt` honours span and zone; Generate's relation appears only at contrast ≥ 0.5, deterministically. RED → GREEN → commit `composition 3: relations in the model`.

---

### Task 8: The mock — HUMAN GATE (C-13)

**Files:** `design/compose/relations-mock.html` (built on `design/compose/sentence-mock.html`, the tokens as before, 1440×900).

- [ ] **Step 1:** two frames on the normal dock:
  1. A canon from voice 1 (melody) to voice 3 over chords 9–12, drawn as a link between the two lanes over those chords (a bracket from the leader's lane to the follower's, labelled "canon · ½ bar · +2"); the follower's cells read "canon"; the inspector edits it (leader, follower, delay, transposition, transform, span, zone); "+ relation" menu open listing the seven types.
  2. A hocket (voices 1 + 2, alt) and a shared density (cap 8, weights) at once; a call & response limited to a zone (the band lit); the inspector on the density relation (cap, three weight knobs).
  Plus states: no relations ("Relations let the voices answer, follow and share a line — + relation"), a relation whose voice is off (dimmed, "voice 2 is off here").
- [ ] **Step 2:** measure (no spill/truncation: `scratch/mockshot.mjs` adapted), screenshot.
- [ ] **Step 3:** show Kerem (visual companion or a test link); **do not start Task 9 before he says yes**; record changes in `scratch/compose3-ledger.md`.
- [ ] **Step 4: Commit** `composition 3: the relations mock, approved by Kerem`.

---

### Task 9: The dock — links, "+ relation", the relation inspector

**Files:** `web/compose.js`, `web/compose.css`, `core/tests/web_compose.mjs`

To the approved mock. Ids: `#cd-rels` (the link layer over the lanes; `.cd-link[data-i]`), `#cd-addrel` (menu), the inspector's relation rows `.cd-row2[data-k]` as elsewhere.

- [ ] Browser test: add each of the seven types from the menu → `patch.comp.relations` holds it with defaults; its link draws between the right lanes over its span; selecting a link opens its inspector; editing delay/trans/split/cap writes the field; removing it is one undo step; a zone-limited relation lights its band; no scroll/spill at 1440×900 and 1024×700 with three links drawn; keyboard: Tab reaches links, Enter opens, Delete removes (only on links — never on buttons/inputs). Screenshots `14-relations.png`, `15-relations-1024.png` compared to the mock. RED → GREEN; `web_patch.mjs` and `npm test` pass; commit `composition 3: the dock's relations`.

---

### Task 10: Builds

- [ ] Rebuild the wasm (`sh web/build.sh && sh web/build-lab.sh && cmp`), `TEST_BUILD = 19` in `Chrome.kt` and `App.swift`, Android `assembleDebug` → `C:/Users/kerem/OneDrive/Desktop/Fieldscape-test-19.apk`, iOS: tar-sync `core/` + `ios/` to the Mac and `sh ios/build.sh` (rc 0). Commit `composition 3: the builds (Test 19)`.

---

### Task 11: Verify, review, release

- [ ] `python core/tests/run.py` 20/20 (alone, idle); `npm test`; `web_compose`, `web_patch`, `web_core` (not `web_sound`).
- [ ] Final whole-branch review (most capable model available); one fix dispatch; scoped re-review.
- [ ] `scratch/compose3-ledger.md`: built, measured (every relation's test numbers, cost with all on, levels), unverified.
- [ ] Test link for Kerem; merge `compose-3` → main and push **only with his OK**; verify live; tag and back up (`node --env-file=.env.local tools/backup.mjs <label>`).
