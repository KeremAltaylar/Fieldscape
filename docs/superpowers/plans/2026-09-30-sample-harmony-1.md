# Sample harmony, sub-project 1: harmony core, analysis, lab bench — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A shared harmony core (just intonation / equal temperament, nearest-note with hysteresis, glide) that the route engine's notes go through; a recording analyser (pitch track, confidence, brightness, verdict, loop, modulator cycle); and a standalone `lab.html` audition bench where Kerem drops a recording and hears it play notes, the scale, a chord in just vs equal, and a route's progression.

**Architecture:** Two new pieces in the shared C++ core — `core/harmony.hpp` (header-only maths, used by `core/piece.cpp`) and `core/analysis.cpp` (a pure function over PCM). The route engine routes its five `mtof` note sites through the harmony core, reading a per-route `patch.tuning`; with no tuning set it uses a host default that stays **equal temperament** for the live site and the apps, so they sound bit-identical. The lab gets its **own** wasm file (`web/core-lab.wasm`, built at -O1), so the live site's `web/core.wasm` is not touched. In this sub-project the bench plays a plain retuned recording through WebAudio (`playbackRate` = (target ÷ measured f0)^Tune); sub-project 2 moves playback into the core's synths, and with them the bench's "how clearly the pitch was created" number (a plain retune creates no pitch to measure). Walking the routes in the new tuning comes when routes pick instruments (sub-project 3); here the lab is the bench alone.

**Tech Stack:** C++17 (core), Emscripten 6 → standalone wasm, plain browser JS (as `web/listener.js`), WebAudio, Node test runner + Chrome DevTools Protocol (existing `tools/cdp.mjs`).

**Spec:** `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (decisions D1–D13).

## Global Constraints

- Live site and apps unchanged (D1): `web/core.wasm`, `index.html` behaviour, `sw.js` shell list untouched. With `patch.tuning` absent the route engine's notes are bit-identical to today.
- Snap targets: chord notes by default, scale notes per sound (D2). Glide on chord change; hysteresis margin **30% of the gap** (rulebook A-14).
- Just intonation ratios of the chord root: 1, 16/15, 9/8, 6/5, 5/4, 4/3, 45/32, 3/2, 8/5, 5/3, **7/4 (dominant) or 9/5 (otherwise)**, 15/8; root equal-tempered from the key (D8).
- Analysis: YIN over 40 ms windows every 20 ms; confidence = 1 − aperiodicity; recording pitch = median over frames with confidence ≥ 0.8; pitched if ≥ 50% of frames confident; loop crossfade 10–50 ms; cycle = one period at the most confident steady frame, zero-crossing cut.
- Bench audio: every gain change ramps (A-2); attack ≥ 5 ms, release ≥ 30 ms, exponential to silence (A-3).
- Lab UI is research tooling, plain (D11), setters and Kerem only, not linked from the site.
- Windows App Control blocks `wasm-opt.exe`: every wasm build in this plan links at **-O1** (no binaryen pass).
- Commits end with the session's attribution lines.

## Review Focus

- **A recording shorter than one analysis window (< 40 ms) or pure silence** → `fs_analyse` returns `"verdict":"unpitched"`, `f0` 0, no NaN anywhere in the JSON, no crash. (Task 4 test)
- **A 44.1 kHz stereo file** → analysed as a mono mix: in the core at the rate it is given (Task 4 test); on the bench at the page's decoded rate, with loop points in the frames of the decoded buffer the bench plays (Task 6 browser test); f0 correct either way.
- **An old route patch with no `tuning` key, or a pre-version-4 patch** → equal temperament, notes identical to today. (Task 2 fingerprint test)
- **A long recording (10 min) dropped on the bench** → only the first 30 s are analysed, the page stays responsive, and the panel says so. (Task 6 test)
- **An output buffer too small for the JSON** → `fs_analyse` / `fs_harmony_progression` return the size needed and write nothing past the buffer. (Task 3 and Task 4 tests)

---

### Task 0: Engine tests run on Windows again

**Files:**
- Modify: `core/tests/run.py:38` (em++ flags), `core/tests/run.py:41` (core_test sources)

**Interfaces:**
- Produces: `python core/tests/run.py` builds and runs on Windows; `core/analysis.cpp` is in the core_test build.

- [ ] **Step 1: Link at -O1 and add the analysis source**

In `core/tests/run.py`, change the em++ flags line to:

```python
        # -O1: Windows App Control blocks binaryen's wasm-opt.exe (2026-09-30), which -O2 links run;
        # -O1 skips it and all 19 checks pass unchanged.
        flags, ext = ["-std=c++17", "-O1", "-Wall", "-Wextra", "-sNODERAWFS=1", "-sALLOW_MEMORY_GROWTH=1"], ".js"
```

and add `"core/analysis.cpp"` to the `core_test` list after `"core/resample.cpp"`.

Create an empty placeholder so the build links (Task 4 fills it):

```cpp
/* core/analysis.cpp — recording analysis (sample harmony, sub-project 1). Filled in by Task 4. */
#include "fieldscape.h"
```

- [ ] **Step 2: Run**

Run: `python core/tests/run.py`
Expected: `19/19 passed (runtime: em++ / node wasm)`

- [ ] **Step 3: Commit**

```bash
git add core/tests/run.py core/analysis.cpp
git commit -m "Engine tests link at -O1: Windows now blocks wasm-opt"
```

---

### Task 1: The harmony core

**Files:**
- Create: `core/harmony.hpp`
- Test: `core/test.cpp` (new block before `std::printf("core ok\n");`)

**Interfaces:**
- Produces (namespace `harmony`, header-only):
  - `enum Tuning { EQUAL = 0, JUST = 1 };`
  - `double mtof(double m)` — identical formula to `tone::mtof`.
  - `double just_ratio(int interval, bool dominant)`
  - `double hz(int tuning, int midi, int root_midi, bool dominant)`
  - `struct Follower { double margin = 0.3; double glide_s = 0.5; int idx = -1; double at = 0; double to = 0; int choose(double f0, const double *targets, int n); double step(double dt); }` — `targets` sorted ascending Hz; `choose` returns the index held (hysteresis) and sets `to`; `step` glides `at` toward `to` (log-frequency, time constant `glide_s`) and returns `at`.

- [ ] **Step 1: Write the failing tests**

Add `#include "harmony.hpp"` under the other includes of `core/test.cpp`, and before `std::printf("core ok\n");`:

```cpp
    {   /* the harmony core (sample harmony, spec D2/D8) */
        using namespace harmony;
        /* equal temperament is today's mtof, bit for bit */
        for (int m = 20; m < 110; m++) assert(hz(EQUAL, m, 50, false) == tone::mtof(m));
        /* just intonation: exact ratios of the root, the root itself equal-tempered, any octave */
        const int R = 50;                                     /* D3 */
        const double RR[12] = { 1, 16.0/15, 9.0/8, 6.0/5, 5.0/4, 4.0/3, 45.0/32, 3.0/2, 8.0/5, 5.0/3, 9.0/5, 15.0/8 };
        for (int iv = 0; iv < 12; iv++) {
            assert(std::fabs(hz(JUST, R + iv, R, false) / tone::mtof(R) - RR[iv]) < 1e-12);
            assert(std::fabs(hz(JUST, R + iv + 12, R, false) / tone::mtof(R) - 2 * RR[iv]) < 1e-12);
            assert(std::fabs(hz(JUST, R + iv - 24, R, false) / tone::mtof(R) - RR[iv] / 4) < 1e-12);
        }
        assert(std::fabs(hz(JUST, R + 10, R, true) / tone::mtof(R) - 7.0 / 4) < 1e-12);    /* dominant 7th */
        /* hysteresis: a pitch hovering across the midpoint of two targets never flips */
        double T[3] = { 220, 246.94, 277.18 };
        Follower f;
        assert(f.choose(221, T, 3) == 0);
        double mid = std::sqrt(220 * 246.94);
        int flips = 0, last = 0;
        for (int i = 0; i < 600; i++) {
            double wob = mid * std::pow(2.0, ((i * 7919 % 97) / 97.0 - 0.5) * 20 / 1200.0);   /* +-10 cents */
            int k = f.choose(wob, T, 3); if (k != last) flips++; last = k;
        }
        assert(flips == 0);
        /* ... while a sweep still reaches every target */
        Follower g; int seen = 0;
        for (double c = 0; c <= 1200; c += 5) { int k = g.choose(215 * std::pow(2.0, c / 1200), T, 3); seen |= 1 << k; }
        assert(seen == 7);
        /* glide: ends on the target exactly */
        Follower h; h.glide_s = 0.2; h.choose(220, T, 3); h.at = 200;
        double v = 0; for (int i = 0; i < 400; i++) v = h.step(0.01);
        assert(v == 220);
        std::printf("harmony: just ratios exact, 0 flips at a boundary, sweep reaches 3/3, glide lands\n");
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `python core/tests/run.py`
Expected: build of core_test failed: `'harmony.hpp' file not found`

- [ ] **Step 3: Implement `core/harmony.hpp`**

```cpp
/* The harmony core (sample harmony, docs/superpowers/specs/2026-09-30-sample-harmony-design.md):
   one place that turns "this note of this chord" into an exact frequency, for every engine.
   Equal temperament is today's mtof; just intonation takes the chord's root equal-tempered from the
   key and every other note as a pure ratio of it, so overtones line up and held notes do not beat. */
#pragma once
#include <cmath>

namespace harmony {

enum Tuning { EQUAL = 0, JUST = 1 };

inline double mtof(double m) { return 440 * std::pow(2.0, (m - 69) / 12); }

/* The chord root's ratio for an interval in semitones (any octave folded away). The minor seventh is
   the harmonic seventh 7/4 in a dominant chord, 9/5 otherwise. */
inline double just_ratio(int iv, bool dominant) {
    static const double R[12] = { 1, 16.0 / 15, 9.0 / 8, 6.0 / 5, 5.0 / 4, 4.0 / 3, 45.0 / 32, 3.0 / 2, 8.0 / 5, 5.0 / 3, 9.0 / 5, 15.0 / 8 };
    iv = ((iv % 12) + 12) % 12;
    return iv == 10 && dominant ? 7.0 / 4 : R[iv];
}

inline double hz(int tuning, int m, int root_midi, bool dominant) {
    if (tuning != JUST) return mtof(m);
    int d = m - root_midi, oct = (int)std::floor(d / 12.0), iv = d - 12 * oct;
    return mtof(root_midi) * just_ratio(iv, dominant) * std::pow(2.0, oct);
}

/* One caller's snapping: the nearest target, held until the pitch is clearly past the midpoint
   (margin x the gap, rulebook A-14), and a glide toward it in log frequency. */
struct Follower {
    double margin = 0.3, glide_s = 0.5;
    int idx = -1;
    double at = 0, to = 0;
    static double cents(double a, double b) { return 1200 * std::log2(a / b); }
    int choose(double f0, const double *t, int n) {
        if (n <= 0 || !(f0 > 0)) return idx;
        int best = 0;
        for (int i = 1; i < n; i++) if (std::fabs(cents(f0, t[i])) < std::fabs(cents(f0, t[best]))) best = i;
        if (idx < 0 || idx >= n) idx = best;
        else if (best != idx) {
            double gap = std::fabs(cents(t[idx], t[best]));
            if (std::fabs(cents(f0, t[idx])) - std::fabs(cents(f0, t[best])) > margin * gap) idx = best;
        }
        to = t[idx];
        if (!(at > 0)) at = to;
        return idx;
    }
    double step(double dt) {
        if (!(to > 0)) return at;
        if (!(at > 0) || glide_s <= 0) { at = to; return at; }
        double c = cents(at, to) * std::exp(-dt / glide_s);
        at = std::fabs(c) < 0.01 ? to : to * std::pow(2.0, c / 1200);
        return at;
    }
};

}  // namespace harmony
```

- [ ] **Step 4: Run to verify it passes**

Run: `python core/tests/run.py`
Expected: the new line `harmony: just ratios exact, 0 flips at a boundary, sweep reaches 3/3, glide lands`, then `19/19 passed`.

- [ ] **Step 5: Commit**

```bash
git add core/harmony.hpp core/test.cpp
git commit -m "The harmony core: just and equal tuning, nearest note with hysteresis, glide"
```

---

### Task 2: The route engine's notes go through the harmony core

**Files:**
- Modify: `core/piece.cpp` — `struct Patch` (line ~159), `patch_of` (line ~176), `struct Piece` (line ~544), the five note sites (`harmony_bar` ~799, `voice_step` ~829, section step ~920, `third_step` ~961, `zone_fire` ~987), and the `extern "C"` block (~1385)
- Modify: `core/fieldscape.h` (declare `fs_piece_default_tuning`)
- Test: `core/test.cpp`

**Interfaces:**
- Consumes: `harmony::hz`, `harmony::EQUAL`, `harmony::JUST` (Task 1).
- Produces:
  - patch key `"tuning": "just" | "equal"` (absent → the host default).
  - `void fs_piece_default_tuning(fs_device *d, int tuning);` — 0 equal (default), 1 just. Only the lab calls it.
  - `Piece::note_hz(int midi)` — the frequency of a note under the current chord.

- [ ] **Step 1: Write the failing tests**

In `core/test.cpp`, add these helpers above `int main()`:

```cpp
/* Every note the route engine plays over a 60 s walk, hashed (frequencies to 1 mHz), with fixed draws. */
struct NoteLog { std::vector<double> f; std::vector<int> role; };
static double fixed_draw(void *p) { unsigned *s = (unsigned *)p; *s = *s * 1664525u + 1013904223u; return (*s >> 8) / 16777216.0; }
static void log_note(void *p, int role, double f, double, double, double) { ((NoteLog *)p)->f.push_back(f); ((NoteLog *)p)->role.push_back(role); }
static NoteLog walk_notes(const char *patch, int default_tuning) {
    NoteLog log; unsigned seed = 12345;
    fs_device *d = fs_create("piece");
    fs_prepare(d, 48000, 128);
    fs_piece_test_hooks(d, fixed_draw, &seed, log_note, &log);
    fs_piece_test_walk(d, 60);
    if (default_tuning >= 0) fs_piece_default_tuning(d, default_tuning);
    int r = fs_piece_add_route(d, patch);
    fs_piece_walk(d, r, 0, 0);
    for (int i = 0; i < 60 * 48000 / 128; i++) fs_process(d, 128);
    fs_destroy(d);
    return log;
}
static unsigned long long note_hash(const NoteLog &l) {
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < l.f.size(); i++) { h = (h ^ (unsigned long long)std::llround(l.f[i] * 1000)) * 1099511628211ULL; h = (h ^ (unsigned)l.role[i]) * 1099511628211ULL; }
    return h;
}
```

and before `std::printf("core ok\n");`:

```cpp
    {   /* the route engine through the harmony core: equal temperament is today, note for note */
        NoteLog today = walk_notes("{}", -1);
        std::printf("route notes, equal (default): %zu notes, hash %llu\n", today.f.size(), note_hash(today));
        assert(today.f.size() > 20);
        assert(note_hash(today) == ROUTE_NOTES_HASH);            /* captured from the engine before Task 2 */
        assert(note_hash(walk_notes("{\"tuning\":\"equal\"}", -1)) == ROUTE_NOTES_HASH);
        assert(note_hash(walk_notes("{}", 0)) == ROUTE_NOTES_HASH);
        /* just: the same notes (same draws, same drift), each moved from equal temperament by exactly one
           just ratio's correction - just_ratio(iv) / 2^(iv/12) - and the non-root notes really move */
        NoteLog just = walk_notes("{}", 1);
        assert(just.f.size() == today.f.size());
        double fix[13];
        for (int iv = 0; iv < 12; iv++) fix[iv] = harmony::just_ratio(iv, false) / std::pow(2.0, iv / 12.0);
        fix[12] = 7.0 / 4 / std::pow(2.0, 10 / 12.0);
        int moved = 0;
        for (size_t i = 0; i < just.f.size(); i++) {
            if (!(today.f[i] > 0)) continue;                      /* a noise zone has no pitch */
            double q = just.f[i] / today.f[i];
            bool one = false; for (double v : fix) if (std::fabs(q - v) < 1e-9) one = true;
            assert(one);
            if (std::fabs(q - 1) > 1e-9) moved++;
        }
        assert(moved > 0);
        assert(note_hash(walk_notes("{\"tuning\":\"just\"}", 0)) == note_hash(just));   /* the patch wins over the default */
        std::printf("route notes, just: every note one exact just correction off equal, %d moved; the patch overrides the default\n", moved);
    }
```

- [ ] **Step 2: Capture today's hash**

Temporarily define `#define ROUTE_NOTES_HASH 0ULL` above `int main()`, and temporarily add to `core/fieldscape.h` and `core/piece.cpp` (inside `extern "C"`) a no-op `void fs_piece_default_tuning(fs_device *, int) {}` so it builds. Run: `python core/tests/run.py 2>&1 | grep "route notes, equal"`. Expected: `route notes, equal (default): N notes, hash H` then an assertion failure. Replace `0ULL` with `HULL` (the printed H). Remove the no-op body from `piece.cpp` (Step 3 adds the real one; keep the declaration in `fieldscape.h`).

- [ ] **Step 3: Run to verify it fails**

Run: `python core/tests/run.py`
Expected: build fails, `undefined symbol: fs_piece_default_tuning`.

- [ ] **Step 4: Implement**

`core/fieldscape.h`, after `fs_piece_chord_notes`:

```c
/* The tuning a route plays in when its patch does not say (0 equal - the default - or 1 just).
   Only the lab sets it; the live site and the apps keep equal temperament (sample harmony, D1/D8). */
void fs_piece_default_tuning(fs_device *d, int tuning);
```

`core/piece.cpp`: add `#include "harmony.hpp"` beside the other includes. In `struct Patch` add `int tuning = -1;   /* "just" 1, "equal" 0, absent -1: the piece's default */`.

In `patch_of`, before the line `if (!use) p = nullptr;`:

```cpp
    std::string tu = p ? p->s("tuning", "") : "";           /* read before an old patch is set aside */
    P.tuning = tu == "just" ? harmony::JUST : tu == "equal" ? harmony::EQUAL : -1;
```

In `struct Piece`, after `double test_walk_s = 0;`:

```cpp
    int default_tuning = harmony::EQUAL;    /* fs_piece_default_tuning */
    /* A note under the chord playing (the harmony core): today's mtof in equal temperament. */
    double note_hz(int m) {
        int step = H.chord >= 0 ? H.chord : chord_index(t_along);
        int tu = patch.tuning >= 0 ? patch.tuning : default_tuning;
        return harmony::hz(tu, m, chord_root(step), dom_q(chord_quality(step)));
    }
```

Replace the five note-site calls:
- `harmony_bar`: `note(0, bass.sy(), mtof(b), -1, time, 0.5);` → `note(0, bass.sy(), note_hz(b), -1, time, 0.5);`
- `voice_step`: `double f = mtof(mm) * drift;` → `double f = note_hz(mm) * drift;`
- section step: `double f = mtof(bm) * drift;` → `double f = note_hz(bm) * drift;`
- `third_step`: `double f = mtof(bm) * (1 + (rnd() - 0.5) * 0.004);` → `double f = note_hz(bm) * (1 + (rnd() - 0.5) * 0.004);`
- `zone_fire`: `note(5, &o->s, mtof(midi), tim.dur, at, 0.4);` → `note(5, &o->s, note_hz(midi), tim.dur, at, 0.4);`

In the `extern "C"` block, after `fs_piece_test_walk`:

```cpp
void fs_piece_default_tuning(fs_device *d, int tuning) { Piece *p = P(d); if (p) p->default_tuning = tuning == harmony::JUST ? harmony::JUST : harmony::EQUAL; }
```

(`note_hz` reads `H.chord` which `harmony_bar` sets before any note of the bar; the voice, section and third steps' drift multipliers stay as they are — A-11 — and their effect under just intonation is judged in the listening session.)

- [ ] **Step 5: Run to verify it passes**

Run: `python core/tests/run.py`
Expected: `route notes, equal (default): N notes, hash H` (same H), `route notes, just: every note one exact just correction off equal, … moved …`, `19/19 passed`.

- [ ] **Step 6: The live site's engine is untouched**

Run: `git diff --stat web/core.wasm index.html sw.js`
Expected: no output.

- [ ] **Step 7: Commit**

```bash
git add core/piece.cpp core/fieldscape.h core/test.cpp
git commit -m "Route notes go through the harmony core; per-route tuning, equal by default and unchanged"
```

---

### Task 3: A route's progression for the bench

**Files:**
- Modify: `core/piece.cpp` (`extern "C"` block), `core/fieldscape.h`
- Test: `core/test.cpp`

**Interfaces:**
- Consumes: `patch_of`, `root_of`, `quality_of`, `dom_q`, `CHORDS`, `MODES`, `harmony::hz` (Tasks 1–2).
- Produces:
  `int fs_harmony_progression(const char *patch_json, int tuning, int sector, char *out, int size);`
  → writes `{"tempo":72,"key":50,"tuning":"just","scale":[pcs],"chords":[{"label":"Dm9","root":50,"notes":[50,53,57,60,64],"hz":[...],"scale_hz":[...]} , ...]}` (NUL-terminated); returns the length needed (excluding NUL). If `size` is too small it writes nothing and returns the length needed. `sector` picks the section mode for `scale` (clamped).

- [ ] **Step 1: Write the failing test**

```cpp
    {   /* the bench's progression: 16 chords of the default patch, exact just ratios, a safe buffer */
        char small[8] = "unused";
        int need = fs_harmony_progression("{}", 1, 0, small, sizeof small);
        assert(need > 100 && std::strcmp(small, "unused") == 0);           /* too small: nothing written */
        std::vector<char> buf(need + 1);
        assert(fs_harmony_progression("{}", 1, 0, buf.data(), (int)buf.size()) == need);
        std::string j = buf.data();
        int n = 0; for (size_t k = 0; (k = j.find("\"label\"", k)) != std::string::npos; k++) n++;
        assert(n == 16);
        assert(j.find("\"tuning\":\"just\"") != std::string::npos && j.find("\"scale\":[") != std::string::npos);
        /* the first chord (D m9, root 50): its fifth is exactly 3/2 of its root */
        size_t hz = j.find("\"hz\":[");
        double r = std::atof(j.c_str() + hz + 6);
        size_t c3 = hz + 6; for (int i = 0; i < 2; i++) c3 = j.find(',', c3) + 1;
        double fifth = std::atof(j.c_str() + c3);
        assert(std::fabs(fifth / r - 1.5) < 1e-6);
        std::printf("bench progression: 16 chords, just fifth %.4f / root %.4f\n", fifth, r);
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `python core/tests/run.py`
Expected: build fails, `undefined symbol: fs_harmony_progression`.

- [ ] **Step 3: Implement**

`core/fieldscape.h`:

```c
/* A route's progression for the lab's audition bench: each chord's notes (MIDI) and their frequencies
   in the given tuning (0 equal, 1 just), plus the scale of one section's mode. JSON into out; returns
   the length needed, and writes nothing if size is too small. */
int fs_harmony_progression(const char *patch_json, int tuning, int sector, char *out, int size);
```

`core/piece.cpp`, inside `extern "C"`:

```cpp
int fs_harmony_progression(const char *patch_json, int tuning, int sector, char *out, int size) {
    Json j = Json::parse(patch_json);
    Patch P; patch_of(&j, P);
    std::string s = "{\"tempo\":" + std::to_string((int)std::lround(P.tempo)) + ",\"key\":" + std::to_string(P.key) +
                    ",\"tuning\":\"" + (tuning == harmony::JUST ? "just" : "equal") + "\",\"scale\":[";
    if (P.nsectors > 0) {
        const Sector &sc = P.sectors[std::max(0, std::min(P.nsectors - 1, sector))];
        int mode = sc.mode >= 0 ? sc.mode : MODE_DORIAN;
        for (int i = 0; i < MODES[mode].n; i++) s += (i ? "," : "") + std::to_string(((P.key + sc.r + MODES[mode].iv[i]) % 12 + 12) % 12);
    }
    s += "],\"chords\":[";
    char num[32];
    for (int step = 0; step < P.nprog; step++) {
        int q = quality_of(P, step); if (q < 0) q = Q_M7;
        int root = root_of(P, step);
        bool dom = dom_q(q);
        s += std::string(step ? "," : "") + "{\"label\":\"" + CHORDS[q].name + "\",\"root\":" + std::to_string(root) + ",\"notes\":[";
        for (int i = 0; i < CHORDS[q].n; i++) s += (i ? "," : "") + std::to_string(root + CHORDS[q].iv[i]);
        s += "],\"hz\":[";
        for (int i = 0; i < CHORDS[q].n; i++) { std::snprintf(num, sizeof num, "%.6f", harmony::hz(tuning, root + CHORDS[q].iv[i], root, dom)); s += (i ? "," : "") + std::string(num); }
        s += "],\"scale_hz\":[";                    /* the section scale from this chord's root, in the same tuning */
        if (P.nsectors > 0) {
            const Sector &sc = P.sectors[std::max(0, std::min(P.nsectors - 1, sector))];
            int mode = sc.mode >= 0 ? sc.mode : MODE_DORIAN;
            for (int i = 0; i < MODES[mode].n; i++) {
                int pcn = ((P.key + sc.r + MODES[mode].iv[i]) % 12 + 12) % 12, m = root + ((pcn - root) % 12 + 12) % 12;
                std::snprintf(num, sizeof num, "%.6f", harmony::hz(tuning, m, root, dom)); s += (i ? "," : "") + std::string(num);
            }
        }
        s += "]}";
    }
    s += "]}";
    int need = (int)s.size();
    if (out && size > need) std::memcpy(out, s.c_str(), need + 1);
    return need;
}
```

(The label is the chord quality; the bench shows it after the root's note name. `scale_hz` lets the bench play the scale in the route's tuning without a second copy of the ratio table.)

- [ ] **Step 4: Run to verify it passes**

Run: `python core/tests/run.py`
Expected: `bench progression: 16 chords, just fifth … / root …`, `19/19 passed`.

- [ ] **Step 5: Commit**

```bash
git add core/piece.cpp core/fieldscape.h core/test.cpp
git commit -m "fs_harmony_progression: a route's chords and scale for the audition bench"
```

---

### Task 4: The analyser

**Files:**
- Modify: `core/analysis.cpp` (fill in), `core/fieldscape.h`
- Test: `core/test.cpp`

**Interfaces:**
- Consumes: `core/devices/fft.hpp` (`FFT::reserve/plan/twiddles/passes/pass/butterflies`).
- Produces:
  `int fs_analyse(const float *mono, long long frames, double rate, char *out, int size);`
  → JSON `{"rate":48000,"frames":N,"f0":146.83,"confidence":0.93,"centroid_hz":1840,"verdict":"pitched"|"unpitched","range":[lo,hi]|null,"loop":[a,b]|null,"cycle":[a,b]|null,"hop_s":0.02,"track":[[f0,conf,centroid],...]}`; frame indices in the input's own frames; `track` values rounded (f0 0.01 Hz, conf 0.001, centroid 1 Hz); returns length needed, writes nothing if `size` too small. Inputs shorter than one window → `"verdict":"unpitched"`, `f0` 0, `track` [].

- [ ] **Step 1: Write the failing tests**

```cpp
    {   /* the analyser: known pitches within 5 cents, noise unpitched, loop and cycle on the waveform */
        auto analyse = [](const std::vector<float> &x, double sr) {
            int need = fs_analyse(x.data(), (long long)x.size(), sr, nullptr, 0);
            std::vector<char> b(need + 1);
            fs_analyse(x.data(), (long long)x.size(), sr, b.data(), (int)b.size());
            return std::string(b.data());
        };
        auto num = [](const std::string &j, const char *k) { size_t p = j.find(std::string("\"") + k + "\":"); return p == std::string::npos ? -1.0 : std::atof(j.c_str() + p + std::strlen(k) + 3); };
        auto cents = [](double a, double b) { return 1200 * std::log2(a / b); };
        const double SR = 48000, PI = 3.141592653589793;
        std::vector<float> sine(SR * 2), saw(SR * 2), bell(SR * 2), noise(SR * 2);
        uint32_t r = 99;
        for (size_t i = 0; i < sine.size(); i++) {
            double t = i / SR;
            sine[i] = (float)(0.5 * std::sin(2 * PI * 220 * t));
            double s = 0; for (int k = 1; k <= 30; k++) s += std::sin(2 * PI * 110 * k * t) / k; saw[i] = (float)(0.3 * s);
            bell[i] = (float)((0.5 * std::sin(2 * PI * 330 * t) + 0.3 * std::sin(2 * PI * 660 * t) + 0.2 * std::sin(2 * PI * 990 * t) + 0.1 * std::sin(2 * PI * 330 * 2.76 * t)) * std::exp(-t * 0.8));
            r = r * 1664525u + 1013904223u; noise[i] = (float)(((int)(r >> 8) - 8388608) / 16777216.0);
        }
        std::string js = analyse(sine, SR), jw = analyse(saw, SR), jb = analyse(bell, SR), jn = analyse(noise, SR);
        std::printf("analyse sine %.2f, saw %.2f, bell %.2f, noise %s\n", num(js, "f0"), num(jw, "f0"), num(jb, "f0"), jn.find("\"unpitched\"") != std::string::npos ? "unpitched" : "PITCHED");
        assert(std::fabs(cents(num(js, "f0"), 220)) < 5 && js.find("\"pitched\"") != std::string::npos);
        assert(std::fabs(cents(num(jw, "f0"), 110)) < 5);          /* no octave error on a bright tone */
        assert(std::fabs(cents(num(jb, "f0"), 330)) < 5);
        assert(jn.find("\"verdict\":\"unpitched\"") != std::string::npos);
        /* the loop on the sine: a whole number of periods, ends on zero crossings */
        size_t lp = js.find("\"loop\":["); assert(lp != std::string::npos);
        long a = std::atol(js.c_str() + lp + 8), b = std::atol(js.c_str() + js.find(',', lp + 8) + 1);
        double periods = (b - a) * 220 / SR;
        assert(b > a && std::fabs(periods - std::round(periods)) < 0.02 && std::fabs(sine[a]) < 0.02 && std::fabs(sine[b]) < 0.02);
        /* the cycle: one period long */
        size_t cp = js.find("\"cycle\":["); assert(cp != std::string::npos);
        long c0 = std::atol(js.c_str() + cp + 9), c1 = std::atol(js.c_str() + js.find(',', cp + 9) + 1);
        assert(std::fabs((c1 - c0) - SR / 220) <= 1.5);
        /* 44.1 kHz, too short, silence: sane answers and no NaN */
        std::vector<float> s44(44100); for (size_t i = 0; i < s44.size(); i++) s44[i] = (float)(0.5 * std::sin(2 * PI * 440 * i / 44100.0));
        assert(std::fabs(cents(num(analyse(s44, 44100), "f0"), 440)) < 5);
        std::vector<float> tiny(500, 0.1f), quiet(SR, 0.0f);
        for (auto &j : { analyse(tiny, SR), analyse(quiet, SR) }) {
            assert(j.find("\"verdict\":\"unpitched\"") != std::string::npos);
            assert(j.find("nan") == std::string::npos && j.find("inf") == std::string::npos);
        }
        char small[4] = "abc";
        assert(fs_analyse(sine.data(), (long long)sine.size(), SR, small, sizeof small) > 4 && std::strcmp(small, "abc") == 0);
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `python core/tests/run.py`
Expected: build fails, `undefined symbol: fs_analyse`.

- [ ] **Step 3: Implement**

`core/fieldscape.h`:

```c
/* Sample harmony: a recording's pitch over time (YIN, 40 ms windows every 20 ms), how sure it is,
   its brightness, a pitched/unpitched verdict, a loop for sustaining it and one period for FM/AM.
   Mono float at its own rate; JSON into out; returns the length needed (writes nothing if too small). */
int fs_analyse(const float *mono, long long frames, double rate, char *out, int size);
```

`core/analysis.cpp`:

```cpp
/* Recording analysis for sample harmony (docs/superpowers/specs/2026-09-30-sample-harmony-design.md):
   run once when a recording is attached; every engine then tunes from what it measured. */
#include "fieldscape.h"
#include "devices/fft.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const double PI = 3.141592653589793;
struct Frame { double f0, conf, centroid; };

/* YIN (de Cheveigne & Kawahara 2002): the cumulative-mean-normalised difference, the first dip under
   the threshold, refined by parabolic interpolation. conf = 1 - d'(tau). */
Frame yin(const float *x, int W, double sr, double fmin, double fmax, std::vector<double> &d) {
    int tmax = std::min(W / 2, (int)(sr / fmin)), tmin = std::max(2, (int)(sr / fmax));
    d.assign(tmax + 1, 0);
    double run = 0;
    for (int tau = 1; tau <= tmax; tau++) {
        double s = 0;
        for (int i = 0; i < W - tmax; i++) { double e = x[i] - x[i + tau]; s += e * e; }
        run += s; d[tau] = run > 0 ? s * tau / run : 1;
    }
    int best = -1;
    for (int tau = tmin; tau <= tmax; tau++) if (d[tau] < 0.15) { while (tau + 1 <= tmax && d[tau + 1] < d[tau]) tau++; best = tau; break; }
    if (best < 0) { best = tmin; for (int tau = tmin; tau <= tmax; tau++) if (d[tau] < d[best]) best = tau; }
    double t = best;
    if (best > 1 && best < tmax) { double a = d[best - 1], b = d[best], c = d[best + 1], den = a - 2 * b + c; if (den != 0) t = best + 0.5 * (a - c) / den; }
    double conf = std::max(0.0, std::min(1.0, 1 - d[best]));
    return { t > 0 ? sr / t : 0, conf, 0 };
}

double centroid(const float *x, int n, double sr, FFT &fft, std::vector<float> &buf) {
    const int N = fft.n;
    float *ar = buf.data(), *ai = ar + N, *br = ai + N, *bi = br + N;
    for (int i = 0; i < N; i++) { double w = 0.5 - 0.5 * std::cos(2 * PI * i / N); ar[i] = i < n ? (float)(x[i] * w) : 0; ai[i] = 0; }
    for (int p = 0; p < fft.passes; p++) {
        if (p % 2 == 0) fft.pass(p, ar, ai, br, bi, 0, fft.butterflies(p)); else fft.pass(p, br, bi, ar, ai, 0, fft.butterflies(p));
    }
    const float *re = fft.passes % 2 ? br : ar, *im = fft.passes % 2 ? bi : ai;
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; k++) { double m = std::sqrt((double)re[k] * re[k] + (double)im[k] * im[k]); num += m * k * sr / N; den += m; }
    return den > 1e-9 ? num / den : 0;
}

long zero_up(const float *x, long long n, long at, long reach) {   /* the nearest rising zero crossing */
    for (long d = 0; d <= reach; d++) for (long s : { at + d, at - d }) if (s > 0 && s < n && x[s - 1] <= 0 && x[s] > 0) return s;
    return -1;
}

void num(std::string &s, double v, const char *fmt) { char b[40]; std::snprintf(b, sizeof b, fmt, std::isfinite(v) ? v : 0.0); s += b; }

}  // namespace

extern "C" int fs_analyse(const float *x, long long n, double sr, char *out, int size) {
    /* YIN's cost grows with the square of the window, so the pitch search runs on a copy averaged down
       to ~16 kHz (a 30 s file in well under a second in the browser); brightness and the loop and cycle
       cuts use the recording itself. W and H are in the recording's own frames. */
    const int K = std::max(1, (int)(sr / 16000));
    const double sd = sr / K;
    const int Wd = (int)std::lround(sd * 0.04), Hd = (int)std::lround(sd * 0.02), W = Wd * K, H = Hd * K;
    std::vector<float> dx((size_t)(n / K));
    for (size_t i = 0; i < dx.size(); i++) { double acc = 0; for (int k = 0; k < K; k++) acc += x[i * K + k]; dx[i] = (float)(acc / K); }
    std::vector<Frame> tr;
    FFT fft; int N = 2048; fft.reserve(N); fft.plan(N); fft.twiddles(0, N);
    std::vector<float> buf(4 * N); std::vector<double> d;
    double peak = 0; for (long long i = 0; i < n; i++) peak = std::max(peak, (double)std::fabs(x[i]));
    for (long long at = 0; Wd > 0 && at + Wd <= (long long)dx.size() && peak > 1e-4; at += Hd) {
        Frame f = yin(dx.data() + at, Wd, sd, 50, 2000, d);
        double e = 0; for (int i = 0; i < Wd; i++) e += (double)dx[at + i] * dx[at + i];
        if (e / Wd < 1e-7) f = { 0, 0, 0 };                                 /* a silent frame is not a pitch */
        f.centroid = centroid(x + at * K, std::min(W, N), sr, fft, buf);
        tr.push_back(f);
    }
    /* octave errors: a confident frame an octave off the median of its confident neighbours is folded */
    std::vector<double> good; for (auto &f : tr) if (f.conf >= 0.8 && f.f0 > 0) good.push_back(f.f0);
    double f0 = 0;
    if (!good.empty()) { std::vector<double> g = good; std::nth_element(g.begin(), g.begin() + g.size() / 2, g.end()); f0 = g[g.size() / 2]; }
    for (auto &f : tr) if (f0 > 0 && f.f0 > 0) { double c = 1200 * std::log2(f.f0 / f0); if (std::fabs(c - 1200) < 60) f.f0 /= 2; else if (std::fabs(c + 1200) < 60) f.f0 *= 2; }
    bool pitched = !tr.empty() && good.size() * 2 >= tr.size();
    double conf = 0, cen = 0; for (auto &f : tr) { conf += f.conf; cen += f.centroid; } if (!tr.empty()) { conf /= tr.size(); cen /= tr.size(); }
    /* the steadiest confident run (within 20 cents of f0): the loop lives there; its most confident frame gives the cycle */
    long loop_a = -1, loop_b = -1, cyc_a = -1, cyc_b = -1;
    if (pitched && f0 > 0) {
        int bs = -1, bl = 0, s = -1;
        for (int i = 0; i <= (int)tr.size(); i++) {
            bool ok = i < (int)tr.size() && tr[i].conf >= 0.8 && std::fabs(1200 * std::log2(std::max(tr[i].f0, 1e-9) / f0)) < 20;
            if (ok && s < 0) s = i;
            if (!ok && s >= 0) { if (i - s > bl) { bl = i - s; bs = s; } s = -1; }
        }
        if (bs >= 0) {
            double period = sr / f0;
            long a = zero_up(x, n, (long)bs * H + W / 2, (long)period);
            long want = (long)((long long)(bl - 1) * H);
            long k = std::max(1L, (long)std::floor(want / period));
            long b = a >= 0 ? zero_up(x, n, a + (long)std::lround(k * period), (long)(period / 2)) : -1;
            if (a >= 0 && b > a) { loop_a = a; loop_b = b; }
            int bestf = bs; for (int i = bs; i < bs + bl; i++) if (tr[i].conf > tr[bestf].conf) bestf = i;
            long c0 = zero_up(x, n, (long)bestf * H + W / 2, (long)period);
            long c1 = c0 >= 0 ? zero_up(x, n, c0 + (long)std::lround(period), (long)(period / 4)) : -1;
            if (c0 >= 0 && c1 > c0) { cyc_a = c0; cyc_b = c1; }
        }
    }
    std::string s = "{\"rate\":"; num(s, sr, "%.0f"); s += ",\"frames\":" + std::to_string(n) + ",\"f0\":"; num(s, pitched ? f0 : 0, "%.2f");
    s += ",\"confidence\":"; num(s, conf, "%.3f"); s += ",\"centroid_hz\":"; num(s, cen, "%.0f");
    s += std::string(",\"verdict\":\"") + (pitched ? "pitched" : "unpitched") + "\",\"range\":";
    if (pitched && f0 > 0) { int m = (int)std::lround(69 + 12 * std::log2(f0 / 440)); s += "[" + std::to_string(m - 6) + "," + std::to_string(m + 6) + "]"; } else s += "null";
    s += ",\"loop\":" + (loop_a >= 0 ? "[" + std::to_string(loop_a) + "," + std::to_string(loop_b) + "]" : std::string("null"));
    s += ",\"cycle\":" + (cyc_a >= 0 ? "[" + std::to_string(cyc_a) + "," + std::to_string(cyc_b) + "]" : std::string("null"));
    s += ",\"hop_s\":0.02,\"track\":[";
    for (size_t i = 0; i < tr.size(); i++) { s += i ? ",[" : "["; num(s, tr[i].f0, "%.2f"); s += ","; num(s, tr[i].conf, "%.3f"); s += ","; num(s, tr[i].centroid, "%.0f"); s += "]"; }
    s += "]}";
    int need = (int)s.size();
    if (out && size > need) std::memcpy(out, s.c_str(), need + 1);
    return need;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `python core/tests/run.py`
Expected: `analyse sine 220.xx, saw 110.xx, bell 330.xx, noise unpitched`, `19/19 passed`. If a pitch is off by an octave, fix the octave fold or the YIN threshold — never loosen the ±5 cent assertion.

- [ ] **Step 5: Commit**

```bash
git add core/analysis.cpp core/fieldscape.h core/test.cpp
git commit -m "fs_analyse: a recording's pitch track, confidence, brightness, verdict, loop and cycle"
```

---

### Task 5: The lab's own engine file

**Files:**
- Create: `web/build-lab.sh`, `web/core-lab.wasm` (built), `tests/lab-wasm.test.mjs`

**Interfaces:**
- Consumes: `fs_analyse`, `fs_harmony_progression` (Tasks 3–4).
- Produces: `web/core-lab.wasm` exporting `fs_analyse`, `fs_harmony_progression`, `malloc`, `free`, `memory` (and everything `web/core.wasm` exports, for later sub-projects).

- [ ] **Step 1: Write the failing test**

`tests/lab-wasm.test.mjs`:

```js
/* The lab's engine file answers the bench's two questions (sample harmony, sub-project 1). */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const x = (await WebAssembly.instantiate(readFileSync("web/core-lab.wasm"), {
  env: new Proxy({}, { get: () => () => 0 }),
  wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
})).instance.exports;
if (x._initialize) x._initialize();
const str = (s) => { const b = new TextEncoder().encode(s + "\0"), p = x.malloc(b.length); new Uint8Array(x.memory.buffer, p, b.length).set(b); return p; };
const call = (fn, ...a) => { const need = fn(...a, 0, 0), out = x.malloc(need + 1); fn(...a, out, need + 1); const s = new TextDecoder().decode(new Uint8Array(x.memory.buffer, out, need)); x.free(out); return JSON.parse(s); };

test("the progression of the default patch, in just intonation", () => {
  const p = str("{}"), j = call(x.fs_harmony_progression, p, 1, 0);
  assert.equal(j.chords.length, 16);
  assert.equal(j.tuning, "just");
  assert.ok(Math.abs(j.chords[0].hz[2] / j.chords[0].hz[0] - 1.5) < 1e-6);
});

test("a 220 Hz sine analysed within 5 cents", () => {
  const n = 48000, p = x.malloc(n * 4), f = new Float32Array(x.memory.buffer, p, n);
  for (let i = 0; i < n; i++) f[i] = 0.5 * Math.sin(2 * Math.PI * 220 * i / 48000);
  const j = call(x.fs_analyse, p, BigInt(n), 48000);
  assert.equal(j.verdict, "pitched");
  assert.ok(Math.abs(1200 * Math.log2(j.f0 / 220)) < 5);
});

test("the live site's engine file is not the lab's", () => {
  assert.notDeepEqual(readFileSync("web/core.wasm"), readFileSync("web/core-lab.wasm"));
});
```

- [ ] **Step 2: Run to verify it fails**

Run: `node --test tests/lab-wasm.test.mjs`
Expected: FAIL, `ENOENT … web/core-lab.wasm`.

- [ ] **Step 3: Write `web/build-lab.sh` and build**

```sh
#!/bin/sh
# The lab's own engine (sample harmony): everything web/core.wasm has, plus the analyser and the bench's
# progression. A separate file so the live site's engine never changes while the lab is researched.
# Links at -O1: Windows App Control blocks binaryen's wasm-opt.exe, which -O2 runs. From the repo root:
#   sh web/build-lab.sh        (em++ on PATH; on Windows a wrapper that runs python em++.py)
set -e
em++ -std=c++17 -O1 --no-entry -sSTANDALONE_WASM -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=32MB \
  -sEXPORTED_FUNCTIONS=_fs_create,_fs_destroy,_fs_prepare,_fs_set_param,_fs_param_count,_fs_in,_fs_out,_fs_process,_fs_set_source,_fs_set_source_i16,_fs_stats,_fs_mix_create,_fs_mix_prepare,_fs_mix_add,_fs_mix_set_gain,_fs_mix_set_ramp,_fs_mix_set_lowpass,_fs_mix_process,_fs_mix_out,_fs_mix_stats,_fs_piece_add_route,_fs_piece_walk,_fs_piece_route,_fs_piece_sect_n,_fs_piece_bed_voices,_fs_piece_sector,_fs_piece_sector_now,_fs_piece_character,_fs_piece_zone,_fs_piece_rhythm_add,_fs_piece_rhythm_gain,_fs_piece_rhythm_source,_fs_piece_rhythm_remove,_fs_alloc_i16,_fs_engine_create,_fs_engine_features,_fs_engine_places,_fs_engine_step,_fs_engine_source,_fs_engine_process,_fs_engine_out,_fs_engine_state,_fs_engine_morphs,_fs_engine_chord,_fs_engine_route,_fs_engine_solo,_fs_engine_upsert,_fs_engine_remove,_fs_piece_default_tuning,_fs_analyse,_fs_harmony_progression,_malloc,_free \
  core/core.cpp core/mix.cpp core/place.cpp core/sections.cpp core/webm.cpp core/resample.cpp core/piece.cpp core/engine.cpp core/analysis.cpp core/devices/*.cpp -o web/core-lab.wasm
```

On Windows, with the wrapper directory `W` holding an executable `em++` that runs `python C:/Users/kerem/tools/emsdk/upstream/emscripten/em++.py "$@"`:

Run: `EM_CONFIG=C:/Users/kerem/tools/emsdk/.emscripten PATH="$W:$PATH" sh web/build-lab.sh`
Expected: `web/core-lab.wasm` written, no error.

- [ ] **Step 4: Run to verify it passes**

Run: `node --test tests/lab-wasm.test.mjs` then `npm test`
Expected: 3 pass; the full suite still passes.

- [ ] **Step 5: Commit**

```bash
git add web/build-lab.sh web/core-lab.wasm tests/lab-wasm.test.mjs
git commit -m "The lab's own engine file: the analyser and the bench's progression, built at -O1"
```

---

### Task 6: `lab.html` — the audition bench

**Files:**
- Create: `lab.html`, `web/lab.js`
- Test: `core/tests/web_lab.mjs`

**Interfaces:**
- Consumes: `web/core-lab.wasm` (`fs_analyse`, `fs_harmony_progression`); Supabase anon REST `public_features` (as `ios/MapView.swift` `Supa.published()`: URL `https://ujdygmcpqsbyeysggypc.supabase.co`, anon key copied from `index.html`'s `FA_CONFIG`).
- Produces: `window.fsLab` test bridge: `{ ready: Promise, load(file: File): Promise<analysis>, analysis, routes, setRoute(id), play(kind: "note"|"scale"|"chord"|"progression", opts?: { tuning?: "just"|"equal", step?: number }): {rates: number[], hz: number[]}, stop(), settings }`.

- [ ] **Step 1: Write the failing browser test**

`core/tests/web_lab.mjs` (same harness shape as `core/tests/web_parks.mjs`):

```js
/* The lab's audition bench (sample harmony, sub-project 1): a dropped recording is analysed, and the
   bench plays it retuned to exact chord ratios. Nothing is published.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_lab.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = (process.env.FS_URL || "http://localhost:8765/") + "lab.html";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9266", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-lab-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
const WAV = (hz, secs, sr) => `(function(){ var sr = ${sr}, n = Math.round(sr * ${secs}), b = new ArrayBuffer(44 + n * 4), v = new DataView(b);
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 4, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 4, true);
  for (var i = 0; i < n; i++) { var x = Math.round(12000 * Math.sin(2 * Math.PI * ${hz} * i / sr)); v.setInt16(44 + i * 4, x, true); v.setInt16(46 + i * 4, x, true); }
  return new File([b], "tone.wav", { type: "audio/wav" }); })()`;
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9266))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 300)); });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 60 && !(await ev("!!window.fsLab")); i++) await sleep(250);
  await ev("fsLab.ready");
  check("the bench loads its engine", await ev("!!fsLab.ready"), null);
  check("published routes are listed", (await ev("fsLab.routes.length")) >= 1, await ev("fsLab.routes.length"));

  const a = await ev(`fsLab.load(${WAV(146.83, 2, 44100)}).then(function (a) { return a; })`);
  check("a 44.1 kHz stereo D3 is analysed as pitched, within 5 cents", a && a.verdict === "pitched" && Math.abs(1200 * Math.log2(a.f0 / 146.83)) < 5, a && [a.verdict, a.f0]);
  check("its pitch track is drawn", await ev("document.querySelector('#lab-track') && document.querySelector('#lab-track').dataset.points > 0"), null);

  const just = await ev("fsLab.play('chord', { tuning: 'just', step: 0 })");
  await sleep(300); await ev("fsLab.stop()");
  const eq = await ev("fsLab.play('chord', { tuning: 'equal', step: 0 })");
  await sleep(300); await ev("fsLab.stop()");
  const ratio = just.hz[2] / just.hz[0];
  check("the chord in just intonation has an exact 3/2 fifth", Math.abs(ratio - 1.5) < 1e-6, ratio);
  check("equal temperament's fifth is 700 cents", Math.abs(1200 * Math.log2(eq.hz[2] / eq.hz[0]) - 700) < 0.01, eq.hz);
  check("each note's rate = its target / the measured f0", just.rates.every((r, i) => Math.abs(r - just.hz[i] / a.f0) < 1e-9), just.rates);

  const prog = await ev("fsLab.play('progression', { tuning: 'just' })");
  await sleep(500); await ev("fsLab.stop()");
  check("the progression plays 16 chords", prog && prog.chords === 16, prog);

  const long = await ev(`fsLab.load(${WAV(220, 120, 22050)}).then(function (a) { return { secs: a.frames / a.rate, note: document.querySelector('#lab-note').textContent }; })`);
  check("a long recording: only the first 30 s analysed, and the panel says so", Math.abs(long.secs - 30) < 0.01 && /first 30 s/.test(long.note), long);
  check("no page errors", errors.length === 0, errors);
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
```

- [ ] **Step 2: Run to verify it fails**

Run: `node tools/serve.mjs 8765` (another shell), then `node core/tests/web_lab.mjs`
Expected: FAIL (no `lab.html`; the page is the site's index via SPA fallback, `fsLab` never appears).

- [ ] **Step 3: Write `lab.html`**

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>Fieldscape lab</title>
<style>
  /* Research tooling (spec D11): plain, on the site's own dark ground. */
  :root { color-scheme: dark; --ground: #0d1310; --ink: #e3e7e4; --dim: #9ca49d; --lamp: #bae6b1; --line: #2a332d; }
  body { margin: 0; padding: 16px; background: var(--ground); color: var(--ink); font: 15px/1.45 system-ui, sans-serif; max-width: 880px; }
  h1 { font-size: 20px; margin: 0 0 12px; } h2 { font-size: 13px; letter-spacing: .08em; text-transform: uppercase; color: var(--dim); margin: 20px 0 8px; }
  section { border-top: 1px solid var(--line); padding-top: 4px; }
  button, select, input { font: inherit; color: var(--ink); background: #18201b; border: 1px solid var(--line); border-radius: 8px; min-height: 44px; padding: 0 14px; }
  button[aria-pressed="true"] { border-color: var(--lamp); color: var(--lamp); }
  .row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin: 8px 0; }
  #lab-drop { border: 1px dashed var(--dim); border-radius: 10px; padding: 18px; text-align: center; color: var(--dim); }
  canvas { width: 100%; height: 140px; background: #111814; border-radius: 8px; }
  dl { display: grid; grid-template-columns: max-content 1fr; gap: 4px 12px; margin: 8px 0; } dt { color: var(--dim); }
  label { display: flex; gap: 8px; align-items: center; } input[type=range] { min-height: 0; padding: 0; }
</style>
</head>
<body>
<h1>Fieldscape lab — audition bench</h1>
<section>
  <h2>Source</h2>
  <div id="lab-drop">Drop a recording here, or <label style="display:inline"><input type="file" id="lab-file" accept="audio/*"></label></div>
  <p id="lab-note"></p>
  <canvas id="lab-track" width="1600" height="280" aria-label="Pitch track"></canvas>
  <dl id="lab-facts"></dl>
</section>
<section>
  <h2>Harmony</h2>
  <div class="row"><label>Route <select id="lab-route"></select></label>
    <button id="lab-just" aria-pressed="true">Just</button><button id="lab-equal" aria-pressed="false">Equal</button></div>
  <div class="row"><label>Chord <select id="lab-step"></select></label></div>
</section>
<section>
  <h2>Play</h2>
  <div class="row"><button data-play="note">Note</button><button data-play="scale">Scale</button><button data-play="chord">Chord</button><button data-play="progression">Progression</button><button id="lab-stop">Stop</button></div>
  <div class="row"><label>Tune <input type="range" id="lab-tune" min="0" max="1" step="0.01" value="1"></label>
    <label>Attack <input type="range" id="lab-attack" min="0.005" max="1" step="0.005" value="0.02"></label>
    <label>Release <input type="range" id="lab-release" min="0.03" max="3" step="0.01" value="0.6"></label></div>
  <div class="row"><input type="text" id="lab-verdict" placeholder="Your note on this sound" style="flex:1"><button id="lab-keep">Keep</button></div>
  <ul id="lab-kept"></ul>
</section>
<script src="web/lab.js"></script>
</body>
</html>
```

- [ ] **Step 4: Write `web/lab.js`**

```js
/* The audition bench (sample harmony, sub-project 1; spec D12): drop a recording, see what the analyser
   measured, and hear it retuned to a route's chords in just intonation or equal temperament. In this
   sub-project the sound is the recording itself at playbackRate = target / measured f0 (sub-project 2
   moves playback into the core's synths). */
(function () {
  "use strict";
  var SUPA = "https://ujdygmcpqsbyeysggypc.supabase.co";
  var ANON = /* the site's public anon key (index.html FA_CONFIG.anonKey) */ "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVqZHlnbWNwcXNieWV5c2dneXBjIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODkwNDE4NDIsImV4cCI6MjEwNDYxNzg0Mn0.SJlrNKQftKxdM0G6f18e6PsRCvdhIH8fco5tW3CktwM";
  var MAX_S = 30;                                   /* analysed length; a longer file is cut (spec Review Focus) */
  var $ = function (s) { return document.querySelector(s); };
  var ctx = null, x = null, buffer = null, analysis = null, routes = [], route = null, prog = null, tuning = 1, playing = [];

  function audio() { if (!ctx) { ctx = new AudioContext(); } if (ctx.state === "suspended") { ctx.resume(); } return ctx; }
  function cstr(s) { var b = new TextEncoder().encode(s + "\0"), p = x.malloc(b.length); new Uint8Array(x.memory.buffer, p, b.length).set(b); return p; }
  function call(fn, args) {
    var need = fn.apply(null, args.concat([0, 0])), out = x.malloc(need + 1);
    fn.apply(null, args.concat([out, need + 1]));
    var s = new TextDecoder().decode(new Uint8Array(x.memory.buffer, out, need)); x.free(out);
    return JSON.parse(s);
  }

  var ready = fetch("web/core-lab.wasm").then(function (r) { return r.arrayBuffer(); }).then(function (b) {
    return WebAssembly.instantiate(b, { env: new Proxy({}, { get: function () { return function () { return 0; }; } }),
      wasi_snapshot_preview1: new Proxy({}, { get: function () { return function () { return 0; }; } }) });
  }).then(function (r) {
    x = r.instance.exports; if (x._initialize) { x._initialize(); }
    return fetch(SUPA + "/rest/v1/public_features?select=id,properties&kind=eq.route", { headers: { apikey: ANON, Authorization: "Bearer " + ANON } });
  }).then(function (r) { return r.json(); }).then(function (rows) {
    routes = rows.map(function (r) { return { id: r.id, name: r.properties.name || "Route", patch: r.properties.patch || {} }; });
    $("#lab-route").innerHTML = routes.map(function (r) { return "<option value='" + r.id + "'>" + r.name.replace(/</g, "&lt;") + "</option>"; }).join("");
    setRoute(routes.length ? routes[0].id : null);
  });

  function setRoute(id) {
    route = routes.filter(function (r) { return r.id === id; })[0] || null;
    var p = cstr(JSON.stringify(route ? route.patch : {}));
    prog = call(x.fs_harmony_progression, [p, tuning, 0]); x.free(p);
    $("#lab-step").innerHTML = prog.chords.map(function (c, i) { return "<option value='" + i + "'>" + (i + 1) + " · " + c.label + "</option>"; }).join("");
  }

  function load(file) {
    return file.arrayBuffer().then(function (ab) { return audio().decodeAudioData(ab); }).then(function (b) {
      var sr = b.sampleRate, n = Math.min(b.length, Math.round(MAX_S * sr)), mono = new Float32Array(n);
      for (var c = 0; c < b.numberOfChannels; c++) { var d = b.getChannelData(c); for (var i = 0; i < n; i++) { mono[i] += d[i] / b.numberOfChannels; } }
      buffer = b;
      var p = x.malloc(n * 4); new Float32Array(x.memory.buffer, p, n).set(mono);
      analysis = call(x.fs_analyse, [p, BigInt(n), sr]); x.free(p);
      $("#lab-note").textContent = file.name + (b.length > n ? " — only the first 30 s analysed" : "");
      show();
      return analysis;
    });
  }

  function show() {
    var a = analysis, cv = $("#lab-track"), g = cv.getContext("2d");
    g.clearRect(0, 0, cv.width, cv.height);
    var pts = 0;
    a.track.forEach(function (f, i) {
      if (!(f[0] > 0)) { return; }
      var X = i / Math.max(1, a.track.length - 1) * cv.width, Y = cv.height - (Math.log2(f[0] / 50) / Math.log2(40)) * cv.height;
      g.fillStyle = "rgba(186,230,177," + (0.15 + 0.85 * f[1]) + ")"; g.fillRect(X, Y - 2, 3, 4); pts++;
    });
    cv.dataset.points = pts;
    var note = a.f0 > 0 ? noteName(69 + 12 * Math.log2(a.f0 / 440)) : "—";
    $("#lab-facts").innerHTML = "<dt>Verdict</dt><dd>" + a.verdict + "</dd><dt>Pitch</dt><dd>" + (a.f0 > 0 ? a.f0.toFixed(2) + " Hz · " + note : "none") +
      "</dd><dt>Confidence</dt><dd>" + a.confidence.toFixed(2) + "</dd><dt>Brightness</dt><dd>" + a.centroid_hz + " Hz</dd>" +
      "<dt>Loop</dt><dd>" + (a.loop ? (a.loop[0] / a.rate).toFixed(3) + "–" + (a.loop[1] / a.rate).toFixed(3) + " s" : "none") +
      "</dd><dt>Cycle</dt><dd>" + (a.cycle ? (a.cycle[1] - a.cycle[0]) + " samples" : "none") + "</dd>";
  }
  function noteName(m) { var N = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"], r = Math.round(m), c = Math.round((m - r) * 100); return N[(r % 12 + 12) % 12] + (Math.floor(r / 12) - 1) + (c ? (c > 0 ? " +" : " ") + c + "¢" : ""); }

  /* one retuned voice: attack and release ramps (A-2, A-3), exponential to silence */
  function voice(rate, at, dur) {
    var c = audio(), src = c.createBufferSource(), g = c.createGain(), A = Math.max(0.005, +$("#lab-attack").value), R = Math.max(0.03, +$("#lab-release").value);
    src.buffer = buffer; src.playbackRate.value = rate;
    if (analysis.loop) { src.loop = true; src.loopStart = analysis.loop[0] / analysis.rate; src.loopEnd = analysis.loop[1] / analysis.rate; }
    g.gain.setValueAtTime(0.0001, at); g.gain.exponentialRampToValueAtTime(0.25, at + A);
    g.gain.setValueAtTime(0.25, at + A + dur); g.gain.exponentialRampToValueAtTime(0.0001, at + A + dur + R);
    src.connect(g).connect(c.destination); src.start(at); src.stop(at + A + dur + R + 0.05);
    playing.push({ src: src, g: g });
  }
  function rateFor(hz) { var tune = +$("#lab-tune").value; return Math.pow(hz / analysis.f0, tune); }

  function play(kind, opts) {
    opts = opts || {};
    if (!buffer || !analysis || !(analysis.f0 > 0)) { return null; }
    if (opts.tuning) { tuning = opts.tuning === "just" ? 1 : 0; setRoute(route ? route.id : null); }
    stop();
    var c = audio(), t = c.currentTime + 0.05, step = opts.step != null ? opts.step : +$("#lab-step").value || 0;
    var chord = prog.chords[step], beat = 60 / prog.tempo, hz = [], rates = [];
    if (kind === "note") { hz = [chord.hz[0]]; }
    else if (kind === "chord") { hz = chord.hz.slice(); }
    else if (kind === "scale") { hz = chord.scale_hz.slice(); }
    if (kind === "progression") {
      prog.chords.forEach(function (ch, i) { ch.hz.forEach(function (h) { voice(rateFor(h), t + i * beat * 4, beat * 4 - 0.1); }); });
      return { chords: prog.chords.length };
    }
    hz.forEach(function (h, i) { var r = rateFor(h); rates.push(r); voice(r, kind === "scale" ? t + i * beat : t, kind === "scale" ? beat * 0.9 : beat * 4); });
    return { rates: rates, hz: hz };
  }
  function stop() {
    var c = ctx; if (!c) { return; }
    playing.forEach(function (v) { v.g.gain.cancelScheduledValues(c.currentTime); v.g.gain.setValueAtTime(Math.max(0.0001, v.g.gain.value), c.currentTime); v.g.gain.exponentialRampToValueAtTime(0.0001, c.currentTime + 0.05); v.src.stop(c.currentTime + 0.06); });
    playing = [];
  }

  /* the listener's kept verdicts: this browser only (research notes; spec D12 "keep") */
  function kept() { try { return JSON.parse(localStorage.getItem("fs.lab.kept")) || []; } catch (e) { return []; } }
  function renderKept() { $("#lab-kept").innerHTML = kept().map(function (k) { return "<li>" + k.replace(/</g, "&lt;") + "</li>"; }).join(""); }
  $("#lab-keep").addEventListener("click", function () {
    var t = $("#lab-verdict").value.trim(); if (!t) { return; }
    var line = t + " — " + ($("#lab-note").textContent || "no file") + ", tune " + $("#lab-tune").value + ", " + (tuning ? "just" : "equal");
    try { localStorage.setItem("fs.lab.kept", JSON.stringify(kept().concat([line]))); } catch (e) { /* private window: not kept */ }
    $("#lab-verdict").value = ""; renderKept();
  });
  renderKept();

  $("#lab-file").addEventListener("change", function () { if (this.files[0]) { load(this.files[0]); } });
  var drop = $("#lab-drop");
  drop.addEventListener("dragover", function (e) { e.preventDefault(); });
  drop.addEventListener("drop", function (e) { e.preventDefault(); if (e.dataTransfer.files[0]) { load(e.dataTransfer.files[0]); } });
  $("#lab-route").addEventListener("change", function () { setRoute(this.value); });
  $("#lab-just").addEventListener("click", function () { tuning = 1; this.setAttribute("aria-pressed", "true"); $("#lab-equal").setAttribute("aria-pressed", "false"); setRoute(route && route.id); });
  $("#lab-equal").addEventListener("click", function () { tuning = 0; this.setAttribute("aria-pressed", "true"); $("#lab-just").setAttribute("aria-pressed", "false"); setRoute(route && route.id); });
  document.querySelectorAll("[data-play]").forEach(function (b) { b.addEventListener("click", function () { play(b.dataset.play); }); });
  $("#lab-stop").addEventListener("click", stop);

  window.fsLab = { ready: ready, load: load, get analysis() { return analysis; }, get routes() { return routes; }, setRoute: setRoute, play: play, stop: stop };
})();
```

(`ANON` is `index.html`'s `FA_CONFIG.anonKey`, the public anon key every visitor already receives; no secret.)

- [ ] **Step 5: Run to verify it passes**

Run: `node core/tests/web_lab.mjs`
Expected: all PASS lines, exit 0.

- [ ] **Step 6: The live site is unchanged**

Run: `git diff --stat main -- index.html web/core.wasm sw.js web/listener.js`, then the existing suites `node core/tests/web_listener.mjs` and `node core/tests/web_setter.mjs` against the local server.
Expected: no diff; both suites pass.

- [ ] **Step 7: Commit**

```bash
git add lab.html web/lab.js core/tests/web_lab.mjs
git commit -m "lab.html: the audition bench - analyse a recording, hear it on a route's chords in just or equal"
```

---

### Task 7: Hand the bench to Kerem (listening session 1)

**Files:** none new.

- [ ] **Step 1: Run everything**

Run: `python core/tests/run.py`, `npm test`, `node core/tests/web_lab.mjs`, `node core/tests/web_listener.mjs`, `node core/tests/web_setter.mjs`, `node core/tests/web_core.mjs 20`.
Expected: all pass; `web_core` levels as before (−16 to −24 dB on the Koşuyolu route).

- [ ] **Step 2: Ask Kerem before it goes online**

The lab reaches his phone only on GitHub Pages, i.e. by merging `sample-harmony` into `main`. It is unlinked and changes nothing of the site (Task 6 Step 6), but it is a push to the live repository: ask first. On his OK: merge, push, wait for Pages, then fetch `https://keremaltaylar.github.io/Fieldscape/lab.html` and `…/web/core-lab.wasm` and confirm they are the committed files (V-5), and run `FS_URL=https://keremaltaylar.github.io/Fieldscape/ node core/tests/web_lab.mjs`.

- [ ] **Step 3: The listening session**

Send Kerem the link and what to try: drop a pitched recording (voice, bell, cello) and an unpitched one (wind, water); play Chord in Just then Equal and listen for beating on the held chord; play the Progression; move Tune from 0 to 1. Record his verdicts (the bench's Keep list, and in `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` under a new "Listening sessions" heading) — they decide the defaults sub-project 2 starts from. State plainly anything not verified (the bench's sound on his iPhone is heard only by him).
