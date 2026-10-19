# Composition Step 2 — Sentences Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A route can carry a motif (3–8 notes on a piano roll, in chord or scale degrees) that a *melody* voice plays and
develops along the route through a chain of operations (repeat, sequence, invert, retrograde, augment, diminish, fragment,
cadence). The step also clears the debts step 1 left behind: Show layers, routes past 16 chords, the level gaps on four synths,
the cost of reading a patch on the audio thread, and voice 3's thin root.

**Architecture:** Plain data in `comp` (`motif`, `chain`, role `melody`) parsed by `core/composer.cpp`. A small pure function
`comp::motif_step` gives the developed motif for a chord step. `core/piece.cpp` plays a melody voice from that function, once a
bar, fitted to the chord playing (chord degrees) or the key (scale degrees). The page's model (`web/compose-model.js`)
mirrors the same operations for the dock's lanes, the summary and Generate form. The dock gets the motif roll and the chain
editor in its grown state, and Show layers in the lanes. Build order and gates as in step 1: core first, then the model, then a
mock Kerem approves, then the dock, then the apps.

**Tech Stack:** C++17 core → wasm (em++ via emsdk at `C:/Users/kerem/tools/emsdk`, `-O1`), Android NDK (jni.cpp), iOS
(Swift, built on the Mac mini M1 `appdevcla@192.168.1.105`, `~/fs`), plain JS page (`index.html`, `web/*.js`), node tests,
headless-Chrome browser tests (`core/tests/web_*.mjs` with `tools/cdp.mjs`).

**Spec:** `docs/superpowers/specs/2026-10-17-composition-design.md`, section **B. Sentences**, build-order step 2. Mock
direction: `docs/superpowers/specs/2026-10-17-composition-mocks/motif.html` (Kerem chose **A, chord degrees**, with a switch per
motif to scale degrees). Step 1's ledger and deferrals: `scratch/compose1-ledger.md`.

## Global Constraints

- Composing is desk only; phones show the summary only ("I will compose on computer always").
- Total control: nothing random unless allowed — every random choice has a *variation* amount (0 = exactly as set), seeded per route (`comp.seed`), so a walk repeats.
- Existing routes keep their sound: a route without `comp`, or without `motif`/`chain`/a melody role, plays note for note as on main `3be1ef1` (`test_kept`, `core/test.cpp` row 0 pinned hashes).
- Precedence zone > chord > section > route; an override holds only what it changes.
- Nothing on the audio thread allocates, locks, simulates or sorts. Everything that costs is done when the patch is read, on the calling thread — and on the web that "calling thread" is the worklet's message handler, so reading a patch must cost ≤ 1 ms (Task 4).
- The rulebook (`studio-rulebook`): C-8 one screen (no page scroll), C-9 fixed grid, C-5 the five states, C-1 tokens only, A-2 nothing clicks. Text never below `--t-xs`. A full mock is approved by Kerem before the dock is built (C-13).
- Audio budget: 128 frames @ 48 kHz = 2.67 ms; the composer's cost tests assert p99.9 < 1.0 ms and no block > 1.5 ms after warm-up (except ≤ 2 OS spikes) with morphs on and onsets moving.
- Supabase egress: never run `core/tests/web_sound.mjs` in a loop; the browser tests use local drafts.
- Branch `compose-2` from main `3be1ef1`; merge and push only with Kerem's OK.

## Review Focus

1. A motif longer than the bar it is given (notes past `bars × 16` sixteenths, or `len` running past the end) must clip, not wrap or crash — pinned in Task 1's parse test and Task 2's play test.
2. A chain step whose `from`/`to` falls outside the progression, overlaps another step, or is empty must be ignored step by step, never shift other steps — pinned in Task 1.
3. A melody voice on a chord whose tones are fewer than the motif's highest degree (a triad under a motif that uses the 9th) plays the degree an octave up from the chord's tones, never silence — pinned in Task 2.
4. Changing the motif while the voice is mid-phrase must take effect at the next bar line with no hanging note — pinned in Task 2.
5. A 64-chord route with chord overrides on chords 17–64 must play them (after Task 3) and still read in ≤ 1 ms (Task 4) — pinned in Tasks 3 and 4.

---

## File structure

| File | Responsibility | Tasks |
|---|---|---|
| `core/composer.hpp`, `core/composer.cpp` | `Motif`, `ChainStep`, `Cfg.motif/chain`, role `melody`, `motif_step()`, `NCHORD` 64 | 1, 2, 3 |
| `core/piece.cpp` | melody playback (`play_melody`), cadence effect on other voices, cgain size, memoised `comp_gains`, voice 3 root | 2, 3, 4, 6 |
| `core/levels.hpp` (new, generated) + `tools/levels.mjs` (new) | measured per-synth × style correction table | 5 |
| `core/tests/composer_test.cpp` | all core tests | 1–6 |
| `web/compose-model.js`, `tests/compose-model.test.mjs` | motif/chain in the model, labels, summary, Generate form with a motif | 7 |
| `design/compose/sentence-mock.html` (new) | the mock for Kerem | 8 |
| `web/compose.js`, `web/compose.css`, `core/tests/web_compose.mjs`, `index.html` | motif roll, chain editor, melody role, Show layers | 9 |
| Android `Cards.kt`/`CompSummary.kt` + test, iOS `Walk.swift`/`Cards.swift`, `Chrome.kt`/`App.swift` | summary "Motif", TEST_BUILD 18 | 10 |

---

### Task 1: Motif and chain in the data (parse)

**Files:**
- Modify: `core/composer.hpp`, `core/composer.cpp`
- Test: `core/tests/composer_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace comp {
  static const int MOTIF_MAX = 8, CHAIN_MAX = 16;
  enum Basis { CHORD_DEG, SCALE_DEG };
  enum Op { OP_REPEAT, OP_SEQ, OP_INVERT, OP_RETRO, OP_AUGMENT, OP_DIMINISH, OP_FRAGMENT, OP_CADENCE, NOP };
  enum Cadence { CAD_FULL, CAD_HALF, CAD_DECEPTIVE };
  enum Others { OTH_HOLD, OTH_THIN, OTH_STOP };            /* what the other voices do on a cadence's last chord */
  struct MNote { int8_t deg = 0, oct = 0; uint8_t start = 0, len = 4; uint8_t acc = 70; };   /* start/len in sixteenths */
  struct Motif { uint8_t basis = CHORD_DEG, bars = 1, n = 0; MNote note[MOTIF_MAX]; };
  struct ChainStep { uint8_t op = OP_REPEAT; int8_t arg = 0; uint8_t from = 0, to = 0;      /* chord steps, inclusive */
                     uint8_t cad = CAD_FULL, others = OTH_THIN; float strength = 1; };
  /* Cfg gains: Motif motif; ChainStep chain[CHAIN_MAX]; int nchain = 0; */
  }
  ```
  Role `MELODY` (already in the enum) is now parsed from `"role":"melody"`. One motif and one chain per route: the spec's "one chain per melody voice" is narrowed to a shared chain (two melody voices play the same developed motif, each in its own register) — a per-voice chain waits until Kerem asks for it. JSON:
  `comp.motif = { basis: "chord"|"scale", bars: 1|2, notes: [{ deg, oct, start, len, acc }] }`,
  `comp.chain = [{ op: "repeat"|"seq"|"invert"|"retro"|"augment"|"diminish"|"fragment"|"cadence", arg, from, to, cad: "full"|"half"|"deceptive", others: "hold"|"thin"|"stop", strength }]`.

- [ ] **Step 1: Write the failing parse test** (in `composer_test.cpp`, called from `main` after `test_resolve`)

```cpp
static void test_motif_parse() {
    const char *J = R"({"comp":{"voice":{"role":"melody"},
      "motif":{"basis":"chord","bars":1,"notes":[{"deg":0,"start":0,"len":4},{"deg":2,"start":4,"len":4},{"deg":1,"oct":1,"start":8,"len":2,"acc":100},{"deg":4,"start":12,"len":8}]},
      "chain":[{"op":"repeat","from":0,"to":1},{"op":"seq","arg":2,"from":2,"to":3},{"op":"cadence","cad":"half","others":"stop","from":3,"to":3},
               {"op":"bogus","from":4,"to":5},{"op":"repeat","from":9,"to":2},{"op":"invert","from":70,"to":80}]}})";
    Json *j = Json::parse(J); comp::Cfg c; comp::comp_of(j->get("comp"), c);
    assert(c.on && c.v[0].role == comp::MELODY && (c.v[0].mask & comp::F_ROLE));
    assert(c.motif.n == 4 && c.motif.basis == comp::CHORD_DEG && c.motif.bars == 1);
    assert(c.motif.note[2].deg == 1 && c.motif.note[2].oct == 1 && c.motif.note[2].acc == 100 && c.motif.note[2].len == 2);
    assert(c.motif.note[3].start == 12 && c.motif.note[3].len == 4);       /* clipped to the bar: 12 + 4 = 16 */
    assert(c.nchain == 3);                                                   /* bogus op, from > to, out of range: dropped */
    assert(c.chain[1].op == comp::OP_SEQ && c.chain[1].arg == 2 && c.chain[2].cad == comp::CAD_HALF && c.chain[2].others == comp::OTH_STOP);
    /* more than 8 notes: the first 8; a note starting past the motif: dropped */
    Json *k = Json::parse(R"({"comp":{"motif":{"bars":1,"notes":[{"start":0},{"start":1},{"start":2},{"start":3},{"start":4},{"start":5},{"start":6},{"start":16},{"start":7},{"start":8}]}}})");
    comp::Cfg d; comp::comp_of(k->get("comp"), d);
    assert(d.motif.n == 8 && d.motif.note[7].start == 8);
    delete j; delete k;
    std::printf("C motif parse: 4 notes (one clipped to the bar), 3 of 6 chain steps kept, melody role\n");
}
```

(Check `Json::parse`/`delete` against `core/json.hpp`'s actual API and the existing `test_parse`; use the same calls it uses.)

- [ ] **Step 2: Run to see it fail** — `python core/tests/run.py` → row C FAIL (compile error: no member `motif`).

- [ ] **Step 3: Implement** in `composer.hpp` the structs above (add to `Cfg`: `Motif motif; ChainStep chain[CHAIN_MAX]; int nchain = 0;`); in `composer.cpp`:
  - role table gains `"melody"` → `MELODY` (it already exists in `enum Role`; check `voice_of`'s role parse and add the string).
  - `motif_of(const Json *m, Motif &o)`: basis `"scale"` → `SCALE_DEG` else `CHORD_DEG`; `bars` clamped 1..2; notes in array order, a note with `start >= bars*16` skipped, at most `MOTIF_MAX` kept; `deg` clamped −7..13 (chord basis uses 0..4 and folds higher, scale basis 0..13), `oct` −2..2, `len` clamped so `start + len <= bars*16` and ≥ 1, `acc` 0..100 (default 70).
  - `chain_of(const Json *a, ChainStep *o, int &n)`: each element with a known `op`, `0 <= from <= to < NCHORD`, appended while `n < CHAIN_MAX`; `arg` clamped −7..7 (`seq`) / 1..8 (`fragment`); `strength` 0..1; overlapping steps keep array order (the later one wins on its chords — Task 2's lookup scans from the end).
  - `comp_of` calls both when `"motif"` / `"chain"` exist.

- [ ] **Step 4: Run** — `python core/tests/run.py` → 20/20, row C prints "C motif parse: …"; `test_kept` still "identical 1".

- [ ] **Step 5: Commit** — `git add core/composer.hpp core/composer.cpp core/tests/composer_test.cpp && git commit -m "composition 2: motif and chain in the data"`

---

### Task 2: The melody voice plays the motif along the chain

**Files:**
- Modify: `core/composer.hpp`, `core/composer.cpp` (`motif_step`), `core/piece.cpp` (`compose_step`, `play_melody`, cadence on other voices)
- Test: `core/tests/composer_test.cpp`

**Interfaces:**
- Consumes: Task 1's `Motif`, `ChainStep`, `Cfg.motif/chain/nchain`, `MELODY`.
- Produces:
  ```cpp
  namespace comp {
  struct Phrase { int n = 0; MNote note[MOTIF_MAX * 2]; uint8_t len16 = 16; bool cadence = false; uint8_t cad = CAD_FULL, others = OTH_THIN; float strength = 1; bool last = false; };
  /* the motif as developed for chord step `chord` (the chain step covering it, else repeat); `last`: chord is that step's last */
  void motif_step(const Cfg &c, int chord, Phrase &out);
  }
  ```
  `Piece::play_melody(int q, const comp::Voice &v, long long k, long long bar, double time)`.

The operations (on the motif's notes, `deg` in the motif's basis, times in sixteenths, `L = bars*16`):
- `repeat`: unchanged. `seq arg`: every `deg += arg`. `invert`: `deg = 2*d0 - deg` around the first note's degree `d0`.
- `retro`: notes in reverse order, `start' = L - (start + len)`. `augment`: `start *= 2, len *= 2`, `len16 = 2L` (the phrase spans twice as long; notes past 32 sixteenths clip). `diminish`: `start /= 2, len = max(1, len/2)`, and the shortened motif plays twice (second copy at `+L/2`).
- `fragment arg`: the first `arg` notes only. `cadence`: the motif repeats, and on the step's **last** chord its last note is replaced by the cadence target with `len` extended to the bar's end: `full` → the key's tonic, `half` → the key's 5th, `deceptive` → the key's 6th (scale degree 5, 0-based); for other voices `others` applies on that chord: `hold` nothing, `thin` = the voices with a rate faster than 4n play only their first note of the bar, `stop` = the other two voices are silent for that chord's bars. `strength` scales the cadence note's velocity (0.5 + 0.5·strength) and, for `thin`/`stop`, the chance they apply is `strength` through `rand01` (strength 1 = always).

Playback: a melody voice ignores its style. Once per bar (`k % 16 == 0` and the phrase cursor at the motif's start), it schedules the phrase's notes for that bar: the cursor is `bar % (len16 / 16)` bars into the phrase. Each note: chord basis → `deg_midi(deg, v.lo, v.hi, oct)` (the existing helper; degrees past the chord's tones go up an octave — Review Focus 3); scale basis → the key's scale (`patch.key`, ionian if the progression's first chord is major quality, aeolian if minor; reuse the piece's mode table), folded into `[lo, hi]`. Velocity `(0.22 + 0.4·acc/100)·g` like the styles, `g` = the voice's level gain (Task 5 extends the table to melody). The phrase changes only at bar lines (Review Focus 4); a note running past the bar line is cut at the next bar's change with the existing release (no hang).

- [ ] **Step 1: Write the failing tests**

```cpp
/* D minor 7 (key 50), the walk's first chord for >= 2 bars: a 4-note chord-degree motif root-5th-3rd-9th, each a quarter */
static const char *MOT = R"("motif":{"basis":"chord","notes":[{"deg":0,"start":0,"len":4},{"deg":2,"start":4,"len":4},{"deg":1,"start":8,"len":4},{"deg":4,"start":12,"len":4}]})";
static std::string melody(const char *chain, const char *extra = "") {
    char b[2048]; std::snprintf(b, sizeof b, R"(,"comp":{"voice":{"role":"melody","lo":50,"hi":81},"sect":{"role":"off"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":%s%s})", MOT, chain, extra);
    return b;
}
static void test_melody() {
    /* repeat: D (50) A (57) F (53) and the 9th of a 4-tone chord = degree 4 -> root an octave up (62) on the quarters of bar 0 */
    const Log l = render(melody("[]"), 6);
    std::vector<int> n = bar_notes(l, 1, 0);
    std::printf("C melody repeat bar 0:"); for (int m : n) std::printf(" %d", m); std::printf("\n");
    assert(n.size() == 4 && n[0] == 50 && n[1] == 57 && n[2] == 53 && n[3] == 62);
    /* sequence +1 degree on chord 0: 3rd 7th 5th, root+octave+1 -> F C A (53 60 57) and degree 5 -> 3rd up an octave (65) */
    const Log s = render(melody(R"([{"op":"seq","arg":1,"from":0,"to":3}])"), 6);
    n = bar_notes(s, 1, 0); assert(n.size() == 4 && n[0] == 53 && n[1] == 60 && n[2] == 57 && n[3] == 65);
    /* retrograde: 9th, 3rd, 5th, root in time order */
    const Log r = render(melody(R"([{"op":"retro","from":0,"to":3}])"), 6);
    n = bar_notes(r, 1, 0); assert(n.size() == 4 && n[0] == 62 && n[1] == 53 && n[2] == 57 && n[3] == 50);
    /* fragment 2: the first two notes only */
    const Log f = render(melody(R"([{"op":"fragment","arg":2,"from":0,"to":3}])"), 6);
    n = bar_notes(f, 1, 0); assert(n.size() == 2 && n[0] == 50 && n[1] == 57);
    /* diminish: 8 notes in the bar (the halved motif twice) */
    const Log d = render(melody(R"([{"op":"diminish","from":0,"to":3}])"), 6);
    assert(bar_notes(d, 1, 0).size() == 8);
    /* augment: 2 notes in bar 0, the next 2 in bar 1 */
    const Log a = render(melody(R"([{"op":"augment","from":0,"to":3}])"), 12);
    assert(bar_notes(a, 1, 0).size() == 2 && bar_notes(a, 1, 1).size() == 2);
    /* a triad never silences the 9th (Review Focus 3): every motif note sounds on every bar of a 40 s walk */
    const Log t = render(melody("[]"), 40);
    int bars = 0, short_bars = 0; for (int b = 0; b < 15; b++) { bars++; if (bar_notes(t, 1, b).size() != 4) short_bars++; }
    assert(short_bars == 0);
    std::printf("C melody: repeat / seq +1 / retro / fragment 2 / diminish / augment on the chord's degrees\n");
}
static void test_cadence() {
    /* a one-chord-step cadence on chord 0, full: the last note is the key's tonic D (50 or 62 in range), held to the bar's end */
    const Log c = render(melody(R"([{"op":"cadence","cad":"full","from":0,"to":0}])"), 6);
    std::vector<int> n = bar_notes(c, 1, 0); assert(!n.empty() && (n.back() % 12) == (50 % 12));
    const Log h = render(melody(R"([{"op":"cadence","cad":"half","from":0,"to":0}])"), 6);
    n = bar_notes(h, 1, 0); assert(!n.empty() && (n.back() % 12) == (57 % 12));          /* A, the key's 5th */
    /* others "stop": with voice 2 strumming, voice 2 is silent on the cadence chord's bars */
    char b[2048]; std::snprintf(b, sizeof b, R"(,"comp":{"voice":{"role":"melody","lo":50,"hi":81},"sect":{"style":"strum","rate":"4n"},"v3":{"role":"off"},"bass":{"mode":"off"},%s,"chain":[{"op":"cadence","cad":"full","others":"stop","from":0,"to":0}]})", MOT);
    const Log s = render(b, 6);
    assert(bar_notes(s, 2, 0).empty() && !bar_notes(s, 1, 0).empty());
    std::printf("C cadence: full ends on the tonic, half on the 5th, others stop\n");
}
static void test_melody_change_at_bar() {
    /* Review Focus 4: the motif replaced mid-bar via fs_piece_set_route takes effect at the next bar line, no note longer than its bar */
    Log l = render(melody("[]"), 6);
    for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1) assert(l.d[i] <= 2.5 + 1e-6);
    std::printf("C melody: no note outlasts its bar\n");
}
```

(`render`, `Log`, `bar_notes` exist in the file; the walk's first chord holds bar 0 by construction, as in step 1's `test_arpeggio`. If a bar index lands on another chord, pick the bar that matches the chord — never weaken an assertion.)

- [ ] **Step 2: Run to see them fail** — row C FAIL at `n.size() == 4` (a melody voice plays nothing yet).

- [ ] **Step 3: Implement** `comp::motif_step` in `composer.cpp` (pure, plain data): find the chain step covering `chord` scanning from the last to the first (a later step wins), default `OP_REPEAT`; copy the motif's notes into `out.note`, apply the op as listed above, `out.last = (chord == step.to)`, `out.cadence = (op == OP_CADENCE)`; clip every note to `out.len16`. In `piece.cpp`:
  - `compose_step`: a voice with `role == MELODY` calls `play_melody` (on `k % 16 == 0` only) instead of `play_style`.
  - `play_melody`: `comp::motif_step(patch.comp, H.chord, ph)`; cursor `cb = (int)(bar % (ph.len16 / 16))`; for each note with `start` in `[cb*16, cb*16+16)`: time `time + (start - cb*16) * s16`, dur `min(len, cb*16+16 - start) * s16 * v.len`, pitch as described, `note(role, sy, hz, dur, t, vel, true)`; cadence last note replacement when `ph.cadence && ph.last && note is the phrase's last`.
  - The cadence's `others`: a `cad_hold[q]` flag set per bar in `comp_bar` from the melody voice's phrase (`ph.cadence && ph.last`), read by `compose_step` and the Free paths' existing `styled()` gate (`thin`: only the first note of the bar; `stop`: skip). Free voices are untouched unless a cadence with `others != hold` is in force (no change for routes without a chain).
  - `comp_gains` (Task 4's memo applies later): a melody voice gets a gain like a style, from its phrase's hits (extend the hit list builder with a melody case that walks `motif_step` for chord 0).

- [ ] **Step 4: Run** — 20/20; row C prints the three new lines; `test_kept` identical; cost lines unchanged within noise.

- [ ] **Step 5: Commit** — `git commit -am "composition 2: the melody voice plays the motif along the chain"`

---

### Task 3: Routes past 16 chords

**Files:**
- Modify: `core/composer.hpp` (`NCHORD` 64), `core/piece.cpp` (`cgain` dims follow the constant), `web/compose-model.js` (`NCHORD` 64), `core/tests/composer_test.cpp`, `tests/compose-model.test.mjs`

**Interfaces:** `comp::NCHORD = 64`; `FsCompose.NCHORD = 64`; Generate form spans `min(n, 64)` chords.

- [ ] **Step 1: Failing tests** — core: a 20-chord route (`prog` of 20 alternating `m7`/`maj7`) with `over.chord["18"].voice.style = "pedal"` → the voice-1 notes inside chord 18's stretch are all one pitch (and not before); model: `generate(…, { nchords: 24 })` writes `over.chord["20"]` and the panel note "form over chords 1–16 of 24" is gone (assert `FsCompose.NCHORD === 64`).

```cpp
static void test_chord_past_16() {
    std::string prog = "["; for (int i = 0; i < 20; i++) prog += std::string(i ? "," : "") + (i % 2 ? R"({"r":5,"q":"maj7"})" : R"({"r":0,"q":"m7"})"); prog += "]";
    char route[4096]; std::snprintf(route, sizeof route, R"({"version":17,"tempo":96,"key":50,"prog":%s,"voice":{"on":true,"synth":"fm"},"sect":{"on":false},"v3":{"on":false},"zones":{"on":false},"bed":{"on":false},"comp":{"voice":{"style":"arpeggio","rate":"16n"},"bass":{"mode":"off"},"over":{"chord":{"18":{"voice":{"style":"pedal","rate":"4n","pedal":"root"}}}}}})", prog.c_str());
    unsigned seed = 11; Log l; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, lg, &l);
    const int r = fs_piece_add_route(pc, route); const double secs = 120; const int blocks = (int)(secs * 48000 / 128);
    for (int b = 0; b < blocks; b++) { fs_piece_walk(pc, r, (double)b / blocks, 0); fs_process(pc, 128); }
    fs_destroy(pc);
    const double c0 = secs * 18 / 20 + 3, c1 = secs * 19 / 20 - 1;     /* well inside chord 18's stretch */
    std::set<int> p; for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == 1 && l.t[i] > c0 && l.t[i] < c1) p.insert((int)std::lround(midi(l.f[i])));
    assert(p.size() == 1);
    std::printf("C chord 19 of 20 obeys its own override (NCHORD %d)\n", comp::NCHORD);
}
```

- [ ] **Step 2: Run** → FAIL (`p.size()` > 1: the override is ignored past 16).
- [ ] **Step 3: Implement** — `NCHORD = 64` in `composer.hpp`; every array sized by it already follows (check `Over chord[NCHORD]`, `cgain[1 + NCHORD + NSECTION + NZONE]`, `comp_gains` loops, `comp_bar` bounds, `composer.cpp` key bounds `< NCHORD`); model `NCHORD = 64`; remove the "form over chords 1–16 of n" note when `n <= 64` (keep it for n > 64). Measure `sizeof(comp::Cfg)` and print it in the test; it is copied on the audio thread in `take()` — assert the worst block in `test_cost` is unchanged within noise.
- [ ] **Step 4: Run** — 20/20, npm test; record `sizeof(Cfg)` before/after in the commit message.
- [ ] **Step 5: Commit** — `git commit -am "composition 2: chord overrides on all 64 chord steps"`

---

### Task 4: Reading a patch costs ≤ 1 ms

**Files:** Modify `core/piece.cpp` (`comp_gains`), `core/tests/composer_test.cpp`.

The web reads patches inside the AudioWorklet's message handler — the audio thread between blocks — and `comp_gains` costs 2–5 ms there per edit (measured: fm 2.3, formant 4.5, metal 1.9 ms on this PC; ×2 on the 2018 MacBook Air). Target ≤ 1 ms worst for a 64-chord route with every chord overridden, on this PC.

- [ ] **Step 1: Failing test**

```cpp
static void test_patch_read_cost() {
    /* a 64-chord route, every chord overridden (alternating styles), synths fm / formant / metal: fs_piece_set_route's worst time */
    double worst = 0;
    for (const char *sy : { "fm", "formant", "metal" }) {
        std::string prog = "[", over = "{"; for (int i = 0; i < 64; i++) { prog += std::string(i ? "," : "") + R"({"r":0,"q":"m7"})";
            over += std::string(i ? "," : "") + "\"" + std::to_string(i) + "\":{\"voice\":{\"style\":\"" + (i % 3 == 0 ? "strum" : i % 3 == 1 ? "swells" : "arpeggio") + "\"}}"; }
        prog += "]"; over += "}";
        std::string r = std::string(R"({"version":17,"tempo":96,"key":50,"prog":)") + prog + R"(,"voice":{"on":true,"synth":")" + sy + R"("},"sect":{"on":true,"synth":")" + sy + R"("},"v3":{"on":true,"synth":")" + sy + R"("},"zones":{"on":false},"bed":{"on":false},"comp":{"voice":{"style":"arpeggio"},"sect":{"style":"strum"},"v3":{"style":"swells"},"over":{"chord":)" + over + "}}}";
        fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); const int id = fs_piece_add_route(pc, r.c_str());
        for (int rep = 0; rep < 5; rep++) { auto t0 = std::chrono::steady_clock::now(); fs_piece_set_route(pc, id, r.c_str());
            worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); }
        fs_destroy(pc);
    }
    std::printf("C patch read: worst %.2f ms (64 chords overridden; fm, formant, metal)\n", worst);
    assert(worst < 1.0);
}
```

(Check `fs_piece_set_route`'s real signature in `core/fieldscape.h`; it exists since 6b.)

- [ ] **Step 2: Run** → FAIL (worst ≈ 2–5 ms, more with 64 chords).
- [ ] **Step 3: Implement** — memoise: a layer's gain depends only on (synth type + timbre settings of that role, the resolved `Voice` of that layer, tempo, density). Hash those bytes (FNV-1a over the struct bytes; `Voice` is POD) into a small fixed table in the `Patch` (or a per-Piece fixed cache of 128 entries, LRU by counter — plain arrays, no heap); compute each distinct (role, voice) once. Shorten the simulation to the least bars that keep the level within 0.3 dB of the current result (measure: 16 → 4 bars of Free and 6 → 2 of the style, verify against the current gains on all 11 synths × 5 styles). Keep the result identical within 0.3 dB (assert in the levels test).
- [ ] **Step 4: Run** — 20/20; the new line shows < 1 ms; the levels test still passes its bounds.
- [ ] **Step 5: Commit** — `git commit -am "composition 2: reading a patch under 1 ms - each distinct layer's gain once, shorter solve"`

---

### Task 5: Measured level corrections for metal, formant, mono, comb

**Files:**
- Create: `core/levels.hpp` (generated table, committed), `tools/levels.mjs` (regenerates it; documented in the file header)
- Modify: `core/piece.cpp` (`comp_gains` multiplies by the table), `core/tests/composer_test.cpp` (bounds to ±2 dB for every synth)

The envelope-only model misses each synth's own level movement (metal's highpass rides its envelope, formant's normalisation lags pitch by ~120 ms, mono's filter envelope reopens per strike, comb rings). Measured residual gaps today: comb −2.3 (high arpeggio), mono ±2.3, formant +2 to +4.1, metal +2.4 to +3.5 dB. Rendering the real synth at patch-read time is too slow (50–350 ms), so measure offline, once, and ship a table: `float level_fix[NSYNTH][NSTYLE][4 /*rate class: 16n, 8n, 4n, 2n+*/]` in dB, applied as `g *= pow(10, -fix/20)`.

- [ ] **Step 1: Failing test** — tighten `test_levels`' bounds to ±2 dB for all 11 synths (it fails on the four today).
- [ ] **Step 2: Run** → FAIL on metal/formant/mono/comb.
- [ ] **Step 3: Implement** — `tools/levels.mjs` runs a dedicated `composer_test levels-table` mode (add it: for each synth × style × rate class, render Free and the style through the real engine path dry, as `test_levels` does, and print the dB gap as a C initialiser) and writes `core/levels.hpp`; `comp_gains` applies it. Synths already within ±0.5 dB get 0. Document regeneration in the header: "run `node tools/levels.mjs` after changing a synth or the gain solve".
- [ ] **Step 4: Run** — 20/20 with ±2 dB everywhere; print the worst gap per synth.
- [ ] **Step 5: Commit** — `git add core/levels.hpp tools/levels.mjs && git commit -am "composition 2: every synth's styles within 2 dB of Free (measured corrections)"`

---

### Task 6: Voice 3's thin root — diagnose, then Kerem decides

**Files:** Read `core/piece.cpp` (voice 3's chain: its synth defaults, EQ, filters, `v3` patch defaults), test probe in `core/tests/composer_test.cpp` (a `v3-root` mode, not run by default).

Measured in step 1: on voice 3, D3 sits ~15 dB under A3 even played alone, so its chords sound thin at the root. Voice 3 is also today's held inner voice, so any change to its chain changes **existing routes' sound** — that is Kerem's call, not ours.

- [ ] **Step 1:** Measure voice 3's response: one note at a time, MIDI 38–74, through the engine path; print dB per note and per stage (synth out, after EQ, after filter, after level) to find the stage that tilts.
- [ ] **Step 2:** Write the finding to `scratch/compose2-ledger.md`: the stage, the curve, and two options — (a) fix it in the chain for everyone (existing routes' voice 3 gets fuller lows; pinned hashes change), (b) compensate only for composed chord notes (no change to existing routes).
- [ ] **Step 3: HUMAN GATE** — the controller asks Kerem (a) or (b). Do not change code before his answer.
- [ ] **Step 4:** Implement his choice with a test: root within 3 dB of the 5th on voice 3 for the composed path (and, if (a), for Free too, with the pinned hashes deliberately updated and the change recorded).
- [ ] **Step 5: Commit.**

---

### Task 7: The model knows motifs and chains

**Files:** Modify `web/compose-model.js`, `tests/compose-model.test.mjs`.

**Interfaces:**
- Produces: `FsCompose.OPS` (`[{ id: "repeat", label: "repeat" }, { id: "seq", label: "sequence", arg: { min: -7, max: 7, def: 1 } }, { id: "invert", label: "invert" }, { id: "retro", label: "retrograde" }, { id: "augment", label: "augment" }, { id: "diminish", label: "diminish" }, { id: "fragment", label: "fragment", arg: { min: 1, max: 8, def: 2 } }, { id: "cadence", label: "cadence" }]`); `ROLES` adds `{ id: "melody", label: "melody" }`; `motifDefault()` → `{ basis: "chord", bars: 1, notes: [{deg:0,start:0,len:4},{deg:2,start:4,len:4},{deg:1,start:8,len:4},{deg:0,oct:1,start:12,len:4}] }`; `motifStep(comp, chord)` → `{ op, notes }` (the same operations as `comp::motif_step`, for display); `setMotif(comp, motif)`, `setChain(comp, chain)`, `chainAt(comp, chord)`; `cell()` for a melody voice returns `{ style: "melody", label: "A↑2" | "A inv" | "A frag 2" | "cad." | "A" , source }`; `summary()` shows "Motif" for a melody voice; `generate()` (Generate form) gains a motif: one voice becomes `melody` (the mover), `comp.motif` is drawn from the seed (3–6 notes, chord degrees, stepwise-leaning), and `comp.chain` follows the form letters: A → repeat, A′ → seq ±1/±2, B → invert or fragment, C → retro, each letter's last chord → cadence (`half` inside the form, `full` at the end).

- [ ] **Step 1: Failing tests** (node): `motifStep` for each op on the default motif equals hand-computed notes (the same cases as Task 2's C++ test, as degree/start/len triples); `summary` with a melody voice = "Motif · Strum · Pedal"; `generate` with a seed writes a motif of 3–6 notes, a chain covering every chord of the form with the letters' ops, cadence `full` on the last chord; every op/basis/cad/others string is one `core/composer.cpp` parses (extend the existing core-vs-model check to the new tables).
- [ ] **Step 2: Run** `npm test` → FAIL.
- [ ] **Step 3: Implement** as above; keep `generate` pure and seeded (mulberry32).
- [ ] **Step 4: Run** `npm test` → all pass.
- [ ] **Step 5: Commit** — `git commit -am "composition 2: the model's motif, chain and melody voice; Generate form writes a motif"`

---

### Task 8: The mock — HUMAN GATE (C-13)

**Files:** Create `design/compose/sentence-mock.html` (self-contained, the tokens as in `design/compose/dock-mock.html`, at 1440×900).

- [ ] **Step 1:** Build two frames on the approved dock:
  1. **Grown dock, motif selected:** the map a 96 px strip; the dock full height; the **motif roll** (rows root/3/5/7/9 for chord basis, a `chord | scale` switch; 16 or 32 columns for 1–2 bars; notes as bars with their length, an accent strip below; the motif shown fitted under two example chords as note names) and the **chain** below it: a row of step chips (op, arg, span "chords 1–4"), drag handles, "+ step" menu with the 8 ops, each step's span drawn on the chord strip; the selected step's inspector (op, arg, from/to, cadence type, others, strength).
  2. **Normal dock, Show layers on voice 1:** the voice-1 lane expanded into four thin sub-rows (zone / chord / section / route) showing each layer's value per chord, the winning one lit; the melody lane's cells labelled "A", "A↑2", "A inv", "cad.".
  Plus the states: no motif yet ("Draw a motif or generate one"), a melody voice with an empty motif, the role menu with "melody".
- [ ] **Step 2:** Measure the mock (no spill, no truncation: `scratch/mockshot.mjs` adapted) and screenshot it.
- [ ] **Step 3:** Show it in the visual companion (restart the server: `bash scripts/start-server.sh --project-dir <repo> --open` from the brainstorming skill dir) and ask Kerem. **Do not start Task 9 before he says yes.** Record his changes in `scratch/compose2-ledger.md`.
- [ ] **Step 4: Commit** — `git add design/compose/sentence-mock.html && git commit -m "composition 2: the sentence mock, approved by Kerem"`

---

### Task 9: The dock — motif roll, chain, melody role, Show layers

**Files:** Modify `web/compose.js`, `web/compose.css`, `core/tests/web_compose.mjs` (and `index.html` only if a new hook is needed).

**Interfaces:** Consumes Task 7's model. DOM ids the test uses: `#cd-motif` (the roll; cells `.cd-mcell[data-row][data-col]`, notes `.cd-mnote[data-i]`), `#cd-basis` (chord/scale switch), `#cd-chain` (`.cd-step[data-i]`, `#cd-addstep` menu), `.cd-lane[data-voice] .cd-layers` toggle and `.cd-layer[data-k="zone|chord|section|route"]` sub-rows.

- [ ] **Step 1: Failing browser test** (append to `web_compose.mjs`, same harness): set voice 1's role to melody → the lane shows "A" cells; grow the dock → `#cd-motif` visible, the page doesn't scroll (C-8), nothing spills; click an empty roll cell → a note is added and `patch.comp.motif.notes` grows; drag a note's end → `len` changes; switch to scale → `basis === "scale"`; add a chain step "sequence +2" over chords 3–4 → `patch.comp.chain` holds it and those cells read "A↑2"; Show layers on voice 1 → four sub-rows, the chord row lit where a chord override exists; Ctrl+Z reverts each edit in one step; keyboard: arrows move in the roll, Enter adds/removes a note, Delete removes the selected note; screenshots `10-motif.png`, `11-layers.png`.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** to the approved mock (Task 8 is the visual authority, as step 1's dock mock was). Tokens only; nothing under `--t-xs`; focus restored after every render (as `focusAt`/`refocus` do); Space/Enter/Delete only on the dock's own cells (as step 1's keyboard rule).
- [ ] **Step 4: Run** `FS_SHOTS=scratch/compose2 node core/tests/web_compose.mjs` → all passed; `node core/tests/web_patch.mjs` → all passed; `npm test`; **look at the screenshots** against the mock (V-2).
- [ ] **Step 5: Commit** — `git commit -am "composition 2: the dock's motif roll, chain, melody voice and Show layers"`

---

### Task 10: The apps

**Files:** Android `CompSummary.kt` + `CompSummaryTest.kt` (a melody voice → "Motif"), `Chrome.kt` (`TEST_BUILD = 18`); iOS `Walk.swift` summary (same), `App.swift` (`TEST_BUILD 18`).

- [ ] **Step 1: Failing JUnit test** — `compSummary(mapOf("voice" to mapOf("role" to "melody"), "sect" to v("strum"), "v3" to v("pedal")))` == `"Motif \u00B7 Strum \u00B7 Pedal"` (use the `\u00B7` escape — step 1 shipped a Latin-1 byte once).
- [ ] **Step 2–3:** Implement in both apps; Swift mirrors Kotlin.
- [ ] **Step 4:** `./gradlew testDebugUnitTest assembleDebug` → copy to `C:/Users/kerem/OneDrive/Desktop/Fieldscape-test-18.apk`; tar-sync `core/` and `ios/` to the Mac and `sh ios/build.sh` → rc 0 (the Mac's `~/fs/core` must match — step 1 found it stale).
- [ ] **Step 5: Commit** — `git commit -am "composition 2: the apps' summary shows a melody voice as Motif (Test 18)"`

---

### Task 11: Verify, review, release

- [ ] **Step 1:** `python core/tests/run.py` → 20/20 (alone, machine idle); `npm test`; `web_compose`, `web_patch`, `web_core` → all passed (not `web_sound`).
- [ ] **Step 2:** Final whole-branch review on the most capable model against the spec section B and this plan; one fix dispatch; scoped re-review.
- [ ] **Step 3:** `scratch/compose2-ledger.md`: what was built, every measured number (melody notes, cadence targets, patch read ms, level gaps per synth, Cfg size, cost lines), what stays unverified (phones, Kerem's ear).
- [ ] **Step 4:** Test link for Kerem on the 2018 MacBook Air (`node tools/serve.mjs 8765` + `C:/Users/kerem/tools/cloudflared.exe tunnel --url http://localhost:8765`; he runs `fsListen._gate(true)`), then merge `compose-2` → main and push **only with his OK**; verify live (`web/core.wasm` byte-identical, `index.html` has the motif roll, `web_compose.mjs` against `FS_URL=https://keremaltaylar.github.io/Fieldscape/`).
