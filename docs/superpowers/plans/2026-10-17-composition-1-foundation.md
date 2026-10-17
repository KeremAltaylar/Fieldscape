# Composition step 1 — foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A setter can give each of a route's three voices a style (Free, Arpeggio, Pedal, Strum, Euclidean, Swells), a
role (on/off), a register, rate, length, accents and variation, voice 1's bass a mode (hold / pedal / off), and override
any of it per chord step, per section and per zone - in a composing dock under the map - and hear it at once; the
website, Android and iOS play the same notes.

**Architecture:** A plain-data `comp` block in the route patch, parsed by a new core module `core/composer.{hpp,cpp}`
into fixed-size structs (the patch is copied on the audio thread: no heap). The piece (`core/piece.cpp`) resolves the
settings in force once a bar (route ← section ← chord ← zone) and, for any voice not on Free, plays it through a style
generator on the sixteenth grid; Free runs today's code untouched. A new core call tells the piece which zones the walker
is in. On the page, a pure model (`web/compose-model.js`, node-tested) and a dock UI (`web/compose.js` + CSS) edit the
block through the existing patch path.

**Tech Stack:** C++17 core (em++ → wasm for the web, CMake/NDK for Android, Xcode for iOS), vanilla JS + MapLibre on the
page, node:test, headless Chrome over CDP (`tools/cdp.mjs`) for browser tests.

**Spec:** `docs/superpowers/specs/2026-10-17-composition-design.md` (mockups in `docs/superpowers/specs/2026-10-17-composition-mocks/`)

## Global Constraints

- Composing is desk only: the Compose button shows only when `innerWidth >= 1000`; phones get a one-line summary.
- Existing routes keep their sound: a route with no `comp`, or with today's defaults written out, plays note for note what it plays now.
- Nothing random unless allowed: every random choice is seeded by the route's `seed`, the bar and the voice (never the piece's `rnd`).
- Precedence: zone > chord > section > route; a style change lands on the bar line; continuous settings glide over `blend` bars.
- Zone overrides live on the route, keyed by the point's id; `when` is `"inside"` or `"enter"` (one phrase = 2 bars).
- No heap allocation on the audio thread; no audio block over budget (worst block < 1.33 ms at 128 frames, 48 kHz, on the PC).
- Rulebook: C-8 one screen (no page scroll), C-9 fixed grid, C-5 five states, C-1 tokens only (`Design System/Tokens.md`), A-2 nothing clicks, C-13 mock before building.
- Work on branch `compose-1` from `main`; merge only after Kerem's test.

## Not in this step (they arrive with later steps of the spec)

- "Show layers" per voice and the relation links between lanes (step 3, with relations); the motif roll and chain (step 2).
- Walking bass in voice 1's bass row (step 4, with Walking bass); step 1's bass row is hold / pedal / off.
- Roles melody / counter / accompaniment / bass act as "on" until step 2 gives the melody role the motif; only *off* changes the sound now.
- Selecting a zone by clicking it on the map (step 1 selects zones by their band in the dock; the map lights the selection).
- A "save failed" state: saving is the page's local patch save (as every panel edit), which has no failure path to show; publishing keeps its own.

## Review Focus

- A setter edits a route that is **not** the one being walked: the dock must still save into that route's patch, and the walker's own route must not change its sound.
- A chord step override on chord 16 of a 12-chord progression (progression shorter than 16): ignored, never read out of bounds.
- A zone override whose point has been deleted, or a section override past the place's section count: ignored, the rest still applies.
- Switching a voice from Swells back to another style: the voice's own note envelope (8b) comes back - the swell envelope must not stick.
- A route opened in Compose with no progression (`prog` empty): the dock shows "add chords first" and writes nothing.

(Each line's test is added to its owning task: Task 3 step 9, Task 2 step 6, Task 2 step 7, Task 4 step 13, Task 9 step 6.)

---

## File structure

| File | Responsibility |
|---|---|
| `core/composer.hpp` | plain-data types (`comp::Voice`, `Bass`, `Over`, `Cfg`, `Resolved`), the parse and resolve declarations |
| `core/composer.cpp` | `comp_of` (JSON → Cfg), `resolve`, `glide`, `id_hash`, the seeded `rand01` |
| `core/piece.cpp` | holds `comp::Cfg` in `Patch`; runs `compose_step` on the transport; style generators; bass modes; `fs_piece_zones` |
| `core/fieldscape.h` | declares `fs_piece_zones` |
| `core/engine.cpp` | tells the piece which zones the walker is in (web) |
| `android-app/app/src/main/cpp/jni.cpp`, `ios/RouteSound.swift` | the same for the apps |
| `core/tests/composer_test.cpp` | the step's core tests (own `main`), run by `run.py` |
| `web/compose-model.js` | pure model: defaults, templates, overrides, effective values with sources, bands along the route, history, summary |
| `tests/compose-model.test.mjs` | node tests for the model |
| `web/compose.js`, `web/compose.css` | the dock: strip, bands, lanes, inspector, editors, keyboard, Listen here |
| `index.html` | loads the two scripts and CSS; Compose button; the context the dock needs (map, route, chords, sections, zones, save) |
| `core/tests/web_compose.mjs` | browser test of the dock |
| build lists: `core/tests/run.py`, `web/build.sh`, `web/build-lab.sh`, `android-app/app/src/main/cpp/CMakeLists.txt` | add `core/composer.cpp` |

---

### Task 1: The composer's data and parse

**Files:**
- Create: `core/composer.hpp`, `core/composer.cpp`, `core/tests/composer_test.cpp`
- Modify: `core/tests/run.py` (targets + a row), `web/build.sh`, `web/build-lab.sh`, `android-app/app/src/main/cpp/CMakeLists.txt`

**Interfaces:**
- Produces: `namespace comp { enum Style { FREE, ARPEGGIO, PEDAL, STRUM, EUCLID, SWELLS, NSTYLE }; enum Role { MELODY, COUNTER, ACCOMP, BASS, OFF }; enum BassMode { HOLD, BASS_PEDAL, BASS_OFF }; enum When { INSIDE, ENTER }; struct Voice; struct Bass; struct Over; struct Cfg; struct Resolved; uint64_t id_hash(const char *); void comp_of(const Json *, Cfg &); double rand01(uint32_t seed, long long bar, int voice, int n); }`

- [ ] **Step 1: Write `core/composer.hpp`**

```cpp
#pragma once
/* Composition (docs/superpowers/specs/2026-10-17-composition-design.md): how a route's three voices play - a style per
   voice for the route, overridden per section, chord step and zone (zone > chord > section > route). Step 1: five styles,
   role on/off, voice 1's bass row. Plain data only, no heap: the route's Patch, which holds it, is copied on the audio thread. */
#include "json.hpp"
#include <cstdint>

namespace comp {
enum Style { FREE, ARPEGGIO, PEDAL, STRUM, EUCLID, SWELLS, NSTYLE };
enum Role { MELODY, COUNTER, ACCOMP, BASS, OFF };        /* step 1: OFF silences the voice; the others play their style */
enum BassMode { HOLD, BASS_PEDAL, BASS_OFF };
enum When { INSIDE, ENTER };
enum Field : uint32_t { F_STYLE = 1u, F_ROLE = 2u, F_RATE = 4u, F_LO = 8u, F_HI = 16u, F_LEN = 32u, F_VAR = 64u, F_ACC = 128u,
    F_GRID = 256u, F_SPAN = 512u, F_TONES = 1024u, F_PEDAL = 2048u, F_DIR = 4096u, F_SPREAD = 8192u, F_EUCLID = 16384u,
    F_SWELL = 32768u, F_ALL = 0xFFFFu };
static const int MAX_COLS = 16, NCHORD = 16, NSECTION = 16, NZONE = 12, PHRASE_BARS = 2, NVOICE = 3;

struct Voice {
    uint32_t mask = 0;                                /* which fields this layer sets (the route sets all) */
    uint8_t style = FREE, role = ACCOMP, rate = 2;    /* rate: a step, in sixteenths (1 2 3 4 6 8 12 16) */
    uint8_t lo = 48, hi = 84;                         /* register, MIDI */
    float len = 0.9f, variation = 0;                  /* share of the step a note fills; 0 = exactly as set */
    uint8_t acc[MAX_COLS];                            /* accent per column, 0-100 */
    int8_t grid[MAX_COLS];                            /* chord degree per column (0 root, 1 3rd, 2 5th, 3 7th, 4 9th), -1 rest */
    uint8_t span = 1, tones = 0x07;                   /* arpeggio: octaves; strum, swells: chord degrees, a bit each */
    uint8_t pedal = 0, pedal_note = 50, follow = 1;   /* pedal: 0 root, 1 fifth, 2 a fixed note; follow the chord */
    uint8_t dir = 0; float spread_ms = 40;            /* strum: 0 up, 1 down, 2 alternating; the roll */
    uint8_t pulses = 5, steps = 8, rot = 0;           /* euclidean */
    float attack = 0.3f, release = 0.4f, peak = 0.8f; /* swells: attack and release as shares of the note; level */
    Voice() { for (int i = 0; i < MAX_COLS; i++) { acc[i] = 70; grid[i] = (int8_t)(i % 3); } }
};
struct Bass { uint32_t mask = 0; uint8_t mode = HOLD, pedal = 0, note = 38; };   /* pedal: 0 key root, 1 key fifth, 2 note */
struct Over { bool used = false; Voice v[NVOICE]; Bass bass; uint8_t blend = 0, when = INSIDE; uint64_t zone = 0; };
struct Cfg { bool on = false; uint32_t seed = 1; Voice v[NVOICE]; Bass bass;
             Over chord[NCHORD], section[NSECTION], zone[NZONE]; int nzone = 0; };
struct Resolved { Voice v[NVOICE]; Bass bass; int blend = 0; };

uint64_t id_hash(const char *s);                      /* FNV-1a 64: a zone's point id */
void comp_of(const Json *j, Cfg &c);                  /* the route patch's "comp"; absent: c.on = false */
double rand01(uint32_t seed, long long bar, int voice, int n);   /* the composition's own seeded draw, [0, 1) */
}
```

- [ ] **Step 2: Write `core/composer.cpp` (parse)**

```cpp
/* The composition's data: the route patch's "comp" read into fixed-size structs (composer.hpp), and its seeded draw. */
#include "composer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace comp {

uint64_t id_hash(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (; s && *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; }
    return h;
}
double rand01(uint32_t seed, long long bar, int voice, int n) {   /* splitmix64 of (seed, bar, voice, n) */
    uint64_t z = (uint64_t)seed * 0x9E3779B97F4A7C15ull ^ (uint64_t)bar * 0xBF58476D1CE4E5B9ull ^ (uint64_t)(voice * 977 + n) * 0x94D049BB133111EBull;
    z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ull; z ^= z >> 27; z *= 0x94D049BB133111EBull; z ^= z >> 31;
    return (z >> 11) * (1.0 / 9007199254740992.0);
}

static int pick(const std::string &s, const char *const *names, int n, int def) {
    for (int i = 0; i < n; i++) if (s == names[i]) return i;
    return def;
}
static int stride(const std::string &s) {
    static const struct { const char *s; int n; } T[] = { { "16n", 1 }, { "8n", 2 }, { "8n.", 3 }, { "4n", 4 }, { "4n.", 6 }, { "2n", 8 }, { "2n.", 12 }, { "1n", 16 } };
    for (auto &t : T) if (s == t.s) return t.n;
    return 2;
}
static uint8_t clamp8(double x, int lo, int hi) { return (uint8_t)std::max(lo, std::min(hi, (int)std::lround(x))); }
static float clampf(double x, double lo, double hi) { return (float)std::max(lo, std::min(hi, std::isfinite(x) ? x : lo)); }

static const char *const STYLES[NSTYLE] = { "free", "arpeggio", "pedal", "strum", "euclid", "swells" };
static const char *const ROLES[5] = { "melody", "counter", "accomp", "bass", "off" };

/* one voice's settings: every key present sets its bit in mask; `all` (the route's own) sets every bit */
static void voice_of(const Json *j, Voice &v, bool all) {
    if (all) v.mask = F_ALL;
    if (!j || j->kind != Json::OBJ) return;
    auto has = [&](const char *k) { return j->get(k) != nullptr; };
    if (has("style")) { v.style = (uint8_t)pick(j->s("style", "free"), STYLES, NSTYLE, FREE); v.mask |= F_STYLE; }
    if (has("role")) { v.role = (uint8_t)pick(j->s("role", "accomp"), ROLES, 5, ACCOMP); v.mask |= F_ROLE; }
    if (has("rate")) { v.rate = (uint8_t)stride(j->s("rate", "8n")); v.mask |= F_RATE; }
    if (has("lo")) { v.lo = clamp8(j->n("lo", 48), 12, 115); v.mask |= F_LO; }
    if (has("hi")) { v.hi = clamp8(j->n("hi", 84), 12, 115); v.mask |= F_HI; }
    if (has("len")) { v.len = clampf(j->n("len", 0.9), 0.05, 1); v.mask |= F_LEN; }
    if (has("variation")) { v.variation = clampf(j->n("variation", 0), 0, 1); v.mask |= F_VAR; }
    if (const Json *a = j->get("acc")) if (a->kind == Json::ARR) {
        for (int i = 0; i < MAX_COLS; i++) { const Json *x = a->at(i); v.acc[i] = x && x->kind == Json::NUM ? clamp8(x->num, 0, 100) : v.acc[i]; }
        v.mask |= F_ACC; }
    if (const Json *g = j->get("grid")) if (g->kind == Json::ARR) {
        for (int i = 0; i < MAX_COLS; i++) { const Json *x = g->at(i); v.grid[i] = x && x->kind == Json::NUM ? (int8_t)std::max(-1, std::min(4, (int)std::lround(x->num))) : (int8_t)-1; }
        v.mask |= F_GRID; }
    if (has("span")) { v.span = clamp8(j->n("span", 1), 1, 3); v.mask |= F_SPAN; }
    if (const Json *t = j->get("tones")) if (t->kind == Json::ARR) {
        uint8_t b = 0; for (size_t i = 0; i < t->size(); i++) { const Json *x = t->at(i); if (x && x->kind == Json::NUM) { int d = (int)std::lround(x->num); if (d >= 0 && d <= 4) b |= (uint8_t)(1u << d); } }
        v.tones = b ? b : 0x01; v.mask |= F_TONES; }
    if (has("pedal") || has("pedal_note") || has("follow")) {
        static const char *const P[3] = { "root", "fifth", "fixed" };
        v.pedal = (uint8_t)pick(j->s("pedal", "root"), P, 3, 0); v.pedal_note = clamp8(j->n("pedal_note", 50), 12, 115);
        v.follow = j->flag("follow", true) ? 1 : 0; v.mask |= F_PEDAL; }
    if (has("dir")) { static const char *const D[3] = { "up", "down", "alt" }; v.dir = (uint8_t)pick(j->s("dir", "up"), D, 3, 0); v.mask |= F_DIR; }
    if (has("spread")) { v.spread_ms = clampf(j->n("spread", 40), 0, 400); v.mask |= F_SPREAD; }
    if (has("pulses") || has("steps") || has("rot")) {
        v.steps = clamp8(j->n("steps", 8), 1, 16); v.pulses = clamp8(j->n("pulses", 5), 0, v.steps); v.rot = clamp8(j->n("rot", 0), 0, 15);
        v.mask |= F_EUCLID; }
    if (has("attack") || has("release") || has("peak")) {
        v.attack = clampf(j->n("attack", 0.3), 0.01, 0.95); v.release = clampf(j->n("release", 0.4), 0.01, 0.95); v.peak = clampf(j->n("peak", 0.8), 0, 1);
        v.mask |= F_SWELL; }
}
static void bass_of(const Json *j, Bass &b, bool all) {
    if (all) b.mask = 1;
    if (!j || j->kind != Json::OBJ) return;
    static const char *const M[3] = { "hold", "pedal", "off" }, *const P[3] = { "root", "fifth", "fixed" };
    if (j->get("mode")) { b.mode = (uint8_t)pick(j->s("mode", "hold"), M, 3, HOLD); b.mask = 1; }
    if (j->get("pedal")) b.pedal = (uint8_t)pick(j->s("pedal", "root"), P, 3, 0);
    if (j->get("note")) b.note = clamp8(j->n("note", 38), 12, 72);
}
static const char *const VKEYS[NVOICE] = { "voice", "sect", "v3" };
static void over_of(const Json *j, Over &o) {
    if (!j || j->kind != Json::OBJ) return;
    o.used = true;
    for (int q = 0; q < NVOICE; q++) voice_of(j->get(VKEYS[q]), o.v[q], false);
    bass_of(j->get("bass"), o.bass, false);
    o.blend = clamp8(j->n("blend", 0), 0, 16);
    o.when = j->s("when", "inside") == std::string("enter") ? ENTER : INSIDE;
}

void comp_of(const Json *j, Cfg &c) {
    c = Cfg();
    if (!j || j->kind != Json::OBJ) return;
    c.on = true;
    c.seed = (uint32_t)std::max(0.0, j->n("seed", 1));
    for (int q = 0; q < NVOICE; q++) voice_of(j->get(VKEYS[q]), c.v[q], true);
    bass_of(j->get("bass"), c.bass, true);
    const Json *ov = j->get("over");
    if (!ov || ov->kind != Json::OBJ) return;
    if (const Json *ch = ov->get("chord")) if (ch->kind == Json::OBJ)
        for (auto &kv : ch->obj) { int i = std::atoi(kv.first.c_str()); if (i >= 0 && i < NCHORD) over_of(&kv.second, c.chord[i]); }
    if (const Json *se = ov->get("section")) if (se->kind == Json::OBJ)
        for (auto &kv : se->obj) { int i = std::atoi(kv.first.c_str()); if (i >= 0 && i < NSECTION) over_of(&kv.second, c.section[i]); }
    if (const Json *zo = ov->get("zone")) if (zo->kind == Json::OBJ)
        for (auto &kv : zo->obj) { if (c.nzone >= NZONE) break; Over &o = c.zone[c.nzone]; over_of(&kv.second, o); if (o.used) { o.zone = id_hash(kv.first.c_str()); c.nzone++; } }
}
}
```

- [ ] **Step 3: Write the failing parse tests in `core/tests/composer_test.cpp`**

```cpp
/* Composition step 1 (docs/superpowers/plans/2026-10-17-composition-1-foundation.md): the composer's core checks.
     python core/tests/run.py      (row C) */
#include "../composer.hpp"
#include "../fieldscape.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static void test_parse() {
    comp::Cfg c;
    comp::comp_of(nullptr, c);
    assert(!c.on);
    Json j = Json::parse(R"({"seed":7,"voice":{"style":"arpeggio","rate":"16n","lo":60,"hi":84,"grid":[0,1,2,-1]},
        "sect":{"style":"pedal","pedal":"fifth"},"bass":{"mode":"off"},
        "over":{"chord":{"3":{"voice":{"style":"strum"}},"40":{"voice":{"style":"swells"}}},
                "section":{"2":{"v3":{"role":"off"}}},
                "zone":{"p-1":{"when":"enter","blend":2,"sect":{"variation":0.5}}}}})");
    comp::comp_of(&j, c);
    assert(c.on && c.seed == 7);
    assert(c.v[0].style == comp::ARPEGGIO && c.v[0].rate == 1 && c.v[0].lo == 60 && c.v[0].grid[3] == -1 && c.v[0].grid[4] == -1);
    assert(c.v[0].mask == comp::F_ALL && c.v[2].style == comp::FREE);
    assert(c.v[1].style == comp::PEDAL && c.v[1].pedal == 1 && c.bass.mode == comp::BASS_OFF);
    assert(c.chord[3].used && c.chord[3].v[0].mask == comp::F_STYLE && c.chord[3].v[0].style == comp::STRUM);
    assert(c.section[2].used && c.section[2].v[2].role == comp::OFF);
    assert(c.nzone == 1 && c.zone[0].zone == comp::id_hash("p-1") && c.zone[0].when == comp::ENTER && c.zone[0].blend == 2);
    for (int i = 0; i < comp::NCHORD; i++) if (i != 3) assert(!c.chord[i].used);   /* "40": out of range, ignored */
    std::printf("C parse: route, chord, section and zone layers read; out-of-range chord 40 ignored\\n");
}

int main() {
    test_parse();
    std::printf("composer ok\\n");
    return 0;
}
```

- [ ] **Step 4: Add the target and row to `core/tests/run.py`**

In `build()`'s `targets`, after `"core_test": [...]`, add:

```python
        "composer_test": ["core/core.cpp", "core/mix.cpp", "core/place.cpp", "core/webm.cpp", "core/sections.cpp", "core/resample.cpp", "core/analysis.cpp", "core/bench.cpp", "core/composer.cpp", "core/piece.cpp", "core/engine.cpp", *devices, "core/tests/composer_test.cpp"],
```

and add `"core/composer.cpp",` before `"core/piece.cpp"` in `core_test` and in `stretch_test`. After the row for check 0 (`row(0, "core self-check ...")`), add:

```python
    out = run(exe["composer_test"])
    row("C", "composition (core/tests/composer_test.cpp)", out.strip().splitlines()[-1], "exits 0", "composer ok" in out)
```

- [ ] **Step 5: Add `core/composer.cpp` to the other builds**

- `web/build.sh` and `web/build-lab.sh`: in the em++ source list, before `core/piece.cpp`, add `core/composer.cpp`.
- `android-app/app/src/main/cpp/CMakeLists.txt` line 7: add `${CORE}/composer.cpp` before `${CORE}/piece.cpp`.
- iOS needs nothing: `ios/project.yml` compiles the whole `../core` folder.

- [ ] **Step 6: Run and see it pass**

Run: `python core/tests/run.py`
Expected: the table has `C  composition (core/tests/composer_test.cpp)  composer ok  exits 0  PASS`, every other row unchanged, last line `20/20 passed`.

- [ ] **Step 7: Commit**

```bash
git add core/composer.hpp core/composer.cpp core/tests/composer_test.cpp core/tests/run.py web/build.sh web/build-lab.sh android-app/app/src/main/cpp/CMakeLists.txt
git commit -m "composition 1: the comp block read into plain data (styles, roles, bass, chord/section/zone overrides)"
```

---

### Task 2: Resolve and glide

**Files:**
- Modify: `core/composer.hpp`, `core/composer.cpp`, `core/tests/composer_test.cpp`

**Interfaces:**
- Consumes: Task 1's types.
- Produces: `void comp::resolve(const Cfg &c, int chord, int nchord, int section, int nsection, const uint64_t *inside, int ninside, const uint64_t *entered, int nentered, Resolved &out);` and `void comp::glide(Voice &cur, const Voice &target, int blend);`

- [ ] **Step 1: Declare them in `composer.hpp`** (after `rand01`)

```cpp
/* the settings in force this bar: route <- section <- chord <- zones (inside, or entered within PHRASE_BARS: `entered`);
   a chord past the progression's length or a section past the place's count is ignored */
void resolve(const Cfg &c, int chord, int nchord, int section, int nsection, const uint64_t *inside, int ninside,
             const uint64_t *entered, int nentered, Resolved &out);
/* once a bar: the discrete settings taken at once, the continuous ones (len, variation, spread, swell) a 1/blend step closer */
void glide(Voice &cur, const Voice &target, int blend);
```

- [ ] **Step 2: Write the failing precedence test** (in `composer_test.cpp`, before `main`, and call it from `main`)

```cpp
static void test_resolve() {
    Json j = Json::parse(R"({"voice":{"style":"arpeggio","rate":"8n"},
        "over":{"section":{"1":{"voice":{"style":"pedal","lo":40}}},
                "chord":{"5":{"voice":{"style":"strum"}}},
                "zone":{"z":{"voice":{"style":"swells"}},"e":{"when":"enter","voice":{"rate":"4n"}}}}})");
    comp::Cfg c; comp::comp_of(&j, c);
    comp::Resolved r;
    const uint64_t z = comp::id_hash("z"), e = comp::id_hash("e");
    comp::resolve(c, 0, 16, -1, 7, nullptr, 0, nullptr, 0, r);  assert(r.v[0].style == comp::ARPEGGIO && r.v[0].lo == 48);
    comp::resolve(c, 0, 16, 1, 7, nullptr, 0, nullptr, 0, r);   assert(r.v[0].style == comp::PEDAL && r.v[0].lo == 40);
    comp::resolve(c, 5, 16, 1, 7, nullptr, 0, nullptr, 0, r);   assert(r.v[0].style == comp::STRUM && r.v[0].lo == 40);   /* chord over section; section's lo kept */
    comp::resolve(c, 5, 16, 1, 7, &z, 1, nullptr, 0, r);        assert(r.v[0].style == comp::SWELLS);                     /* zone over chord */
    comp::resolve(c, 0, 16, -1, 7, &e, 1, nullptr, 0, r);       assert(r.v[0].rate == 2);                                 /* an "enter" zone does nothing while merely inside */
    comp::resolve(c, 0, 16, -1, 7, nullptr, 0, &e, 1, r);       assert(r.v[0].rate == 4);                                 /* ... it acts for the phrase after entering */
    std::printf("C resolve: zone > chord > section > route, field by field; an enter-zone acts only after entering\\n");
}
```

- [ ] **Step 3: Run it to see it fail**

Run: `python core/tests/run.py`
Expected: build of composer_test fails with `no member named 'resolve' in namespace 'comp'`.

- [ ] **Step 4: Implement in `composer.cpp`** (inside `namespace comp`, after `comp_of`)

```cpp
static void merge(Voice &d, const Voice &s) {
    const uint32_t m = s.mask;
    if (m & F_STYLE) d.style = s.style;      if (m & F_ROLE) d.role = s.role;     if (m & F_RATE) d.rate = s.rate;
    if (m & F_LO) d.lo = s.lo;               if (m & F_HI) d.hi = s.hi;           if (m & F_LEN) d.len = s.len;
    if (m & F_VAR) d.variation = s.variation;
    if (m & F_ACC) std::memcpy(d.acc, s.acc, sizeof d.acc);
    if (m & F_GRID) std::memcpy(d.grid, s.grid, sizeof d.grid);
    if (m & F_SPAN) d.span = s.span;         if (m & F_TONES) d.tones = s.tones;
    if (m & F_PEDAL) { d.pedal = s.pedal; d.pedal_note = s.pedal_note; d.follow = s.follow; }
    if (m & F_DIR) d.dir = s.dir;            if (m & F_SPREAD) d.spread_ms = s.spread_ms;
    if (m & F_EUCLID) { d.pulses = s.pulses; d.steps = s.steps; d.rot = s.rot; }
    if (m & F_SWELL) { d.attack = s.attack; d.release = s.release; d.peak = s.peak; }
}
static void apply(Resolved &r, const Over &o) {
    for (int q = 0; q < NVOICE; q++) merge(r.v[q], o.v[q]);
    if (o.bass.mask) { r.bass.mode = o.bass.mode; r.bass.pedal = o.bass.pedal; r.bass.note = o.bass.note; }
    r.blend = std::max(r.blend, (int)o.blend);
}
void resolve(const Cfg &c, int chord, int nchord, int section, int nsection, const uint64_t *inside, int ninside,
             const uint64_t *entered, int nentered, Resolved &out) {
    out = Resolved();
    for (int q = 0; q < NVOICE; q++) out.v[q] = c.v[q];
    out.bass = c.bass;
    if (section >= 0 && section < NSECTION && section < nsection && c.section[section].used) apply(out, c.section[section]);
    if (chord >= 0 && chord < NCHORD && chord < nchord && c.chord[chord].used) apply(out, c.chord[chord]);
    for (int k = 0; k < c.nzone; k++) {
        const Over &o = c.zone[k];
        const uint64_t *list = o.when == ENTER ? entered : inside; const int n = o.when == ENTER ? nentered : ninside;
        for (int i = 0; i < n; i++) if (list[i] == o.zone) { apply(out, o); break; }
    }
}
void glide(Voice &cur, const Voice &t, int blend) {
    const float a = blend > 0 ? 1.0f / (float)blend : 1.0f;
    auto toward = [&](float &x, float y) { x += (y - x) * a; if (std::fabs(y - x) < 1e-4f) x = y; };
    const float len = cur.len, var = cur.variation, sp = cur.spread_ms, at = cur.attack, re = cur.release, pk = cur.peak;
    cur = t;                                            /* the discrete settings: at once, on this bar line */
    cur.len = len; cur.variation = var; cur.spread_ms = sp; cur.attack = at; cur.release = re; cur.peak = pk;
    toward(cur.len, t.len); toward(cur.variation, t.variation); toward(cur.spread_ms, t.spread_ms);
    toward(cur.attack, t.attack); toward(cur.release, t.release); toward(cur.peak, t.peak);
}
```

- [ ] **Step 5: Run and see it pass**

Run: `python core/tests/run.py` → row C passes with both lines printed by `core/tests/composer_test.js` (check with `node build/stretch-tests/composer_test.js`).

- [ ] **Step 6: Review Focus — a short progression ignores chord 16's override** (add to `test_resolve`, before its printf)

```cpp
    { Json k = Json::parse(R"({"voice":{"style":"arpeggio"},"over":{"chord":{"15":{"voice":{"style":"pedal"}}}}})");
      comp::Cfg d; comp::comp_of(&k, d);
      comp::resolve(d, 15, 12, -1, 7, nullptr, 0, nullptr, 0, r); assert(r.v[0].style == comp::ARPEGGIO); }   /* 12 chords: no chord 16 */
```

- [ ] **Step 7: Review Focus — a section past the count is ignored, the rest applies; glide** (add to `test_resolve`)

```cpp
    { Json k = Json::parse(R"({"voice":{"style":"arpeggio","len":0.2},"over":{"section":{"9":{"voice":{"style":"pedal"}}},"chord":{"0":{"blend":4,"voice":{"len":1.0}}}}})");
      comp::Cfg d; comp::comp_of(&k, d);
      comp::resolve(d, 0, 16, 9, 7, nullptr, 0, nullptr, 0, r); assert(r.v[0].style == comp::ARPEGGIO && r.blend == 4);   /* 7 sections: no section 10 */
      comp::Voice cur = d.v[0];
      comp::glide(cur, r.v[0], r.blend); assert(std::fabs(cur.len - 0.4f) < 1e-4f);     /* a quarter of the way per bar */
      for (int b = 0; b < 40; b++) comp::glide(cur, r.v[0], r.blend);
      assert(cur.len == 1.0f); }
```

Run `python core/tests/run.py` → row C PASS.

- [ ] **Step 8: Commit**

```bash
git add core/composer.hpp core/composer.cpp core/tests/composer_test.cpp
git commit -m "composition 1: resolve (zone > chord > section > route) and the per-bar glide"
```

---

### Task 3: The piece holds and plays the composition (Free and Off; no style yet)

**Files:**
- Modify: `core/piece.cpp`, `core/fieldscape.h`, `core/tests/composer_test.cpp`

**Interfaces:**
- Consumes: `comp::Cfg`, `comp::resolve`, `comp::glide`, `comp::id_hash`.
- Produces: `Patch::comp` (a `comp::Cfg`); `void fs_piece_zones(fs_device *d, const char *inside_csv, const char *entered);` in `fieldscape.h`; in `Piece`: `comp::Voice ccur[3]; comp::Bass cbass; bool styled(int q) const; void comp_bar(long long bar, double time); void compose_step(double time); void play_style(int q, const comp::Voice &v, long long k, long long bar, double time);` (the last a stub returning at once in this task).

- [ ] **Step 1: The patch carries the composition** — in `core/piece.cpp` add `#include "composer.hpp"` after `#include "eq.hpp"`; add to `struct Patch` (after `env_on`):

```cpp
    comp::Cfg comp;    /* composition step 1: the voices' styles and overrides ("comp"; absent: off, today's voices) */
```

and at the end of `patch_of` (the last statement inside the function), add:

```cpp
    comp::comp_of(p ? p->get("comp") : nullptr, P.comp);
```

- [ ] **Step 2: Move every stack `Patch` to the heap** (the patch grows by ~20 KB; the web build's stack is 64 KB). Replace:

`core/piece.cpp:765` `Patch d; patch_of(nullptr, d);` → `std::unique_ptr<Patch> dp(new Patch); Patch &d = *dp; patch_of(nullptr, d);`
`core/piece.cpp:1741` `Patch pt; patch_of(&j, pt);` → `std::unique_ptr<Patch> ptp(new Patch); Patch &pt = *ptp; patch_of(&j, pt);`
`core/piece.cpp:1912` the same, keeping its comment.
`core/piece.cpp:2122` `Patch P; patch_of(&j, P);` → `std::unique_ptr<Patch> Pp(new Patch); Patch &P = *Pp; patch_of(&j, P);`

(These run on the calling thread; `<memory>` is already included.)

- [ ] **Step 3: Write the failing "today's sound kept" test** (in `composer_test.cpp`; call from `main`)

```cpp
struct Log { std::vector<double> f, t, d; std::vector<int> role; };
static void lg(void *p, int role, double f, double dur, double t, double) { Log *l = (Log *)p; l->f.push_back(f); l->t.push_back(t); l->d.push_back(dur); l->role.push_back(role); }
static double draw(void *p) { unsigned *s = (unsigned *)p; *s = *s * 1664525u + 1013904223u; return (*s >> 8) / 16777216.0; }
static const char *ROUTE = R"({"version":17,"tempo":96,"key":50,"prog":[{"r":0,"q":"m7"},{"r":5,"q":"maj7"},{"r":7,"q":"7"},{"r":0,"q":"m9"}],
    "voice":{"on":true,"synth":"fm"},"sect":{"on":true,"synth":"fm"},"v3":{"on":true,"synth":"am"},"zones":{"on":false},"bed":{"on":false}%s})";
/* the notes a route plays in `secs`, walked from 0 to 1 along it */
static Log render(const std::string &comp, double secs, const char *zones_in = nullptr, const char *zone_entered = nullptr) {
    char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, comp.c_str());
    unsigned seed = 11; Log l;
    fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, lg, &l);
    const int r = fs_piece_add_route(pc, buf);
    const int blocks = (int)(secs * 48000 / 128);
    for (int b = 0; b < blocks; b++) {
        fs_piece_walk(pc, r, (double)b / blocks, 0);
        if (zones_in && b == blocks / 2) fs_piece_zones(pc, zones_in, zone_entered ? zone_entered : "");
        fs_process(pc, 128);
    }
    fs_destroy(pc); return l;
}
static bool same(const Log &a, const Log &b) { return a.f == b.f && a.t == b.t && a.d == b.d && a.role == b.role; }
static const char *DEFAULTS = R"(,"comp":{"seed":1,"voice":{"style":"free","role":"accomp"},"sect":{"style":"free","role":"accomp"},"v3":{"style":"free","role":"accomp"},"bass":{"mode":"hold"},"over":{"chord":{},"section":{},"zone":{}}})";
static void test_kept() {
    const Log none = render("", 40), defaults = render(DEFAULTS, 40);
    std::printf("C kept: %zu notes without comp, %zu with today's defaults written out; identical %d\\n", none.f.size(), defaults.f.size(), (int)same(none, defaults));
    assert(none.f.size() > 50 && same(none, defaults));
}
```

- [ ] **Step 4: Run it to see it fail** — `python core/tests/run.py` → link error `undefined reference to 'fs_piece_zones'`.

- [ ] **Step 5: Declare `fs_piece_zones`** in `core/fieldscape.h` after `fs_piece_zone`'s declaration:

```c
/* Composition: the zones the walker is inside now (point ids, comma-separated, "" none) and the one just entered ("" none) */
void fs_piece_zones(fs_device *d, const char *inside_csv, const char *entered);
```

- [ ] **Step 6: The inbox, the state and the transport** — in `core/piece.cpp`:

In `struct Inbox` add:
```cpp
    bool zones_set = false; uint64_t zin[16] = {}; int nzin = 0; uint64_t zent = 0;   /* composition: zones inside, one just entered */
```

In `struct Piece` (beside `int sector = -1;`) add:
```cpp
    /* composition (step 1): the settings in force, gliding a bar at a time; zones; per-voice state */
    comp::Voice ccur[comp::NVOICE]; comp::Bass cbass; bool comp_live = false;
    uint64_t zin[16] = {}; int nzin = 0; struct Ent { uint64_t id; long long bar; } zent[8]; int nzent = 0;
    int cpulse[comp::NVOICE] = {}, calt[comp::NVOICE] = {}, cped_root = -1; bool swell_shaped[comp::NVOICE] = {};
    bool styled(int q) const { return patch.comp.on && comp_live && (ccur[q].style != comp::FREE || ccur[q].role == comp::OFF); }
```

Where the inbox is taken (the block with `if (in.sector) { in.sector = false; sector = in.sector_v; }`), add after it:
```cpp
        if (in.zones_set) {
            in.zones_set = false; nzin = in.nzin; std::memcpy(zin, in.zin, sizeof zin);
            if (in.zent) { if (nzent == 8) { std::memmove(zent, zent + 1, sizeof(Ent) * 7); nzent = 7; } zent[nzent++] = Ent{ in.zent, ticks / 16 }; in.zent = 0; }
        }
```

In `take(double now)`, after `sector = -1;` add `comp_live = false; nzent = 0; cped_root = -1;`.

In `transport_start()` change `for (int id = 0; id < 5; id++)` to `for (int id = 0; id < 6; id++)`; in `interval()` id 5 is 1 (the existing `id == 0 ? 2 : id == 1 ? 16 : 1` already returns 1); in `tick()`'s switch add `case 5: compose_step(time); break;` before `default:` and change `default: third_step(time);` to `case 4: third_step(time); break;`. At the start of `tick()`, after `long long k = ticks++;`, add:
```cpp
        if (k % 16 == 0 && patch.comp.on) comp_bar(k / 16, time);   /* the bar's settings before any voice plays on it */
```

- [ ] **Step 7: `comp_bar`, `compose_step` and the Free voices stepping aside** — add in `struct Piece` after `third_step`:

```cpp
    /* ---- composition (step 1) ---- */
    void comp_bar(long long bar, double time) {
        uint64_t ent[8]; int ne = 0;
        for (int i = 0; i < nzent; i++) if (bar - zent[i].bar < comp::PHRASE_BARS) ent[ne++] = zent[i].id;
        comp::Resolved r;
        comp::resolve(patch.comp, chord_index(t_along), patch.nprog, sector, patch.sect.n, zin, nzin, ent, ne, r);
        for (int q = 0; q < comp::NVOICE; q++) {
            if (!comp_live) ccur[q] = r.v[q]; else comp::glide(ccur[q], r.v[q], r.blend);
            if (swell_shaped[q] && ccur[q].style != comp::SWELLS) {      /* the voice's own envelope back (8b) */
                Synth *sy = q == 0 ? top.sy() : q == 1 ? sectr.sy() : v3r.sy();
                if (sy) sy->shape(patch.env_on[q] ? &patch.env[q] : nullptr);
                swell_shaped[q] = false;
            }
        }
        cbass = r.bass; comp_live = true;
        bass_bar(time);
    }
    void bass_bar(double time) { (void)time; }        /* Task 4: pedal and off */
    void compose_step(double time) {
        if (!patch.comp.on || !comp_live || H.ntones <= 0) return;
        const long long k = ticks - 1, bar = k / 16;
        for (int q = 0; q < comp::NVOICE; q++) {
            const comp::Voice &v = ccur[q];
            if (v.style == comp::FREE || v.role == comp::OFF || v.rate == 0 || k % v.rate) continue;
            play_style(q, v, k, bar, time);
        }
    }
    void play_style(int, const comp::Voice &, long long, long long, double) {}   /* Task 4: the styles */
```

Then make the Free step functions step aside for a styled voice (their timbre/drive/cutoff updates stay):
- `voice_step`: immediately before `if (rnd() > density) return;` insert `if (styled(0)) return;`
- `sector_step`: immediately after `C.tick++;` insert `if (styled(1)) return;`
- `third_step`: immediately after `T3.tick++;` insert `if (styled(2)) return;`

- [ ] **Step 8: `fs_piece_zones`** — after `fs_piece_zone` in `core/piece.cpp`:

```cpp
void fs_piece_zones(fs_device *d, const char *inside_csv, const char *entered) {
    Piece *p = P(d); if (!p) return;
    uint64_t ids[16]; int n = 0;
    std::string s = inside_csv ? inside_csv : "";
    for (size_t a = 0; a < s.size() && n < 16;) { size_t b = s.find(',', a); if (b == std::string::npos) b = s.size(); if (b > a) ids[n++] = comp::id_hash(s.substr(a, b - a).c_str()); a = b + 1; }
    std::lock_guard<std::mutex> g(p->mu); sweep(p);
    p->in.zones_set = true; p->in.nzin = n; std::memcpy(p->in.zin, ids, sizeof(uint64_t) * n);
    if (entered && *entered) p->in.zent = comp::id_hash(entered);
}
```

- [ ] **Step 9: Run** — `python core/tests/run.py`: row 0 (the core self-check with its pinned note hashes) PASS and row C prints `identical 1`. If row 0's hashes moved, the transport's extra id changed an order: stop and fix before going on.

- [ ] **Step 10: Off silences a voice; the Review Focus "other route" case** (add to `composer_test.cpp`, call from `main`)

```cpp
static void test_off() {
    const Log off = render(R"(,"comp":{"sect":{"role":"off"}})", 40);
    int sect = 0, others = 0; for (int r : off.role) { if (r == 2) sect++; else others++; }
    std::printf("C off: Second voice off - %d notes from it, %d from the others\\n", sect, others);
    assert(sect == 0 && others > 20);
    /* two routes, the second composed: walking the first, its notes are the plain route's (Review Focus 1) */
    char a[8192], b[8192]; std::snprintf(a, sizeof a, ROUTE, ""); std::snprintf(b, sizeof b, ROUTE, R"(,"comp":{"voice":{"role":"off"}})");
    unsigned s1 = 11, s2 = 11; Log l1, l2;
    fs_device *p1 = fs_create("piece"); fs_prepare(p1, 48000, 128); fs_piece_test_hooks(p1, draw, &s1, lg, &l1); int r1 = fs_piece_add_route(p1, a);
    fs_device *p2 = fs_create("piece"); fs_prepare(p2, 48000, 128); fs_piece_test_hooks(p2, draw, &s2, lg, &l2); int r2 = fs_piece_add_route(p2, a); fs_piece_add_route(p2, b);
    for (int k = 0; k < (int)(30 * 48000 / 128); k++) { fs_piece_walk(p1, r1, k / 11250.0, 0); fs_piece_walk(p2, r2, k / 11250.0, 0); fs_process(p1, 128); fs_process(p2, 128); }
    fs_destroy(p1); fs_destroy(p2);
    assert(same(l1, l2));
}
```

Run `python core/tests/run.py` → row C PASS.

- [ ] **Step 11: Commit**

```bash
git add core/piece.cpp core/fieldscape.h core/tests/composer_test.cpp
git commit -m "composition 1: the piece holds the composition, resolves it each bar, steps Free voices aside; fs_piece_zones"
```

---

### Task 4: The five styles and voice 1's bass

**Files:**
- Modify: `core/piece.cpp` (`play_style`, `bass_bar`, `harmony_bar`), `core/tests/composer_test.cpp`

**Interfaces:**
- Consumes: Task 3's `ccur`, `cbass`, `compose_step`, `play_style` stub, `note(role, synth, f, dur, t, vel)`, `H.tones/H.ntones/H.bass/H.held`, `euclid_at(k, n, rot, at)`, `note_hz(midi)`.
- Produces: the styles' notes (roles 1 top, 2 sect, 3 v3; bass role 0).

- [ ] **Step 1: Write the failing Arpeggio test** (`composer_test.cpp`; call from `main`)

```cpp
static double midi(double f) { return 69 + 12 * std::log2(f / 440); }
/* the notes of `role` within one bar starting at bar `b` (seconds per bar at 96 bpm = 2.5) */
static std::vector<int> bar_notes(const Log &l, int role, int b) {
    std::vector<int> o; for (size_t i = 0; i < l.f.size(); i++) if (l.role[i] == role && l.t[i] >= b * 2.5 - 1e-6 && l.t[i] < (b + 1) * 2.5 - 1e-6) o.push_back((int)std::lround(midi(l.f[i])));
    return o;
}
static void test_arpeggio() {
    /* D minor 7 (D F A C, key 50): up 1-3-5-7 in sixteenths, from D3 (50) */
    const Log l = render(R"(,"comp":{"voice":{"style":"arpeggio","rate":"16n","lo":50,"hi":74,"grid":[0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3]}})", 6);
    const std::vector<int> n = bar_notes(l, 1, 1);
    std::printf("C arpeggio: bar 2 top notes"); for (int m : n) std::printf(" %d", m); std::printf("\\n");
    assert(n.size() == 16 && n[0] == 50 && n[1] == 53 && n[2] == 57 && n[3] == 60 && n[4] == 50);
}
```

(The progression's first chord is `{"r":0,"q":"m7"}` in key 50: D F A C. At 96 bpm a bar is 2.5 s and the walk spreads 4 chords over the 6 s, so bar 2 is still chord 1 or 2 — check the chord with the printf if it fails, and walk slower by raising `secs` rather than changing the assertion.)

- [ ] **Step 2: Run to see it fail** — row C fails: `n.size() == 16` (no notes yet).

- [ ] **Step 3: Implement `play_style`** (replace the stub)

```cpp
    /* a chord degree (0 root .. 4 ninth) as a MIDI note, folded into [lo, hi]; degrees past the chord's tones go up an octave */
    int deg_midi(int d, int lo, int hi, int oct) const {
        const int nt = std::max(1, H.ntones);
        int m = H.tones[d % nt] + 12 * (d / nt) + 12 * oct;
        while (m < lo) m += 12;
        while (m > hi && m - 12 >= lo) m -= 12;
        return m;
    }
    void play_style(int q, const comp::Voice &v, long long k, long long bar, double time) {
        Synth *sy = q == 0 ? top.sy() : q == 1 ? sectr.sy() : v3r.sy();
        const int role = q + 1, col = (int)((k % 16) / v.rate), ncols = std::max(1, (16 + v.rate - 1) / v.rate);
        const double step_s = 60.0 / patch.tempo / 4 * v.rate, dur = step_s * v.len;
        auto vel = [&](int c) { return 0.22 + 0.4 * v.acc[c % comp::MAX_COLS] / 100.0; };
        /* variation: a seeded chance to take another chord tone, and a seeded wobble of the level (0 = exactly as set) */
        auto vary = [&](int d, double &vl, int n) {
            if (v.variation <= 0) return d;
            const double h1 = comp::rand01(patch.comp.seed, bar, q, n * 3), h2 = comp::rand01(patch.comp.seed, bar, q, n * 3 + 1), h3 = comp::rand01(patch.comp.seed, bar, q, n * 3 + 2);
            vl *= 1 + (h3 - 0.5) * 0.6 * v.variation;
            return h1 < 0.35 * v.variation ? (d + 1 + (int)(h2 * 3)) % std::max(3, H.ntones) : d;
        };
        switch (v.style) {
        case comp::ARPEGGIO: {
            int d = v.grid[col % ncols]; if (d < 0) return;
            double vl = vel(col); d = vary(d, vl, col);
            note(role, sy, note_hz(deg_midi(d, v.lo, v.hi, v.span > 1 ? (int)(bar % v.span) : 0)), dur, time, vl);
            break;
        }
        case comp::PEDAL: {
            if (cped_root < 0 || v.follow) cped_root = H.tones[0];
            int m = v.pedal == 2 ? v.pedal_note : cped_root + (v.pedal == 1 ? 7 : 0);
            while (m < v.lo) m += 12; while (m > v.hi && m - 12 >= v.lo) m -= 12;
            double vl = vel(col); vary(0, vl, col);
            note(role, sy, note_hz(m), dur, time, vl);
            break;
        }
        case comp::STRUM: {
            int ds[5], nd = 0; for (int d = 0; d < 5; d++) if (v.tones & (1u << d)) ds[nd++] = d;
            const bool down = v.dir == 1 || (v.dir == 2 && (calt[q]++ & 1));
            for (int i = 0; i < nd; i++) {
                const int d = ds[down ? nd - 1 - i : i]; const double off = i * v.spread_ms / 1000.0;
                double vl = vel(col) + (i == 0 ? 0.12 : 0); vary(d, vl, col * 5 + i);
                note(role, sy, note_hz(deg_midi(d, v.lo, v.hi, 0)), std::max(0.03, dur - off), time + off, vl);
            }
            break;
        }
        case comp::EUCLID: {
            const int E = std::max(1, (int)v.steps), at = (int)((k / v.rate) % E);
            if (!euclid_at(std::min((int)v.pulses, E), E, v.rot % E, at)) return;
            int seq[comp::MAX_COLS], ns = 0; for (int i = 0; i < comp::MAX_COLS; i++) if (v.grid[i] >= 0) seq[ns++] = v.grid[i];
            if (!ns) return;
            int d = seq[cpulse[q]++ % ns]; double vl = vel(at); d = vary(d, vl, at);
            note(role, sy, note_hz(deg_midi(d, v.lo, v.hi, 0)), dur, time, vl);
            break;
        }
        case comp::SWELLS: {
            if (sy) { EnvSpec e{ std::max(0.005, v.attack * dur), 0.005, 1.0, std::max(0.02, v.release * dur), false, true }; sy->shape(&e); swell_shaped[q] = true; }
            for (int d = 0; d < 5; d++) if (v.tones & (1u << d)) { double vl = 0.15 + 0.45 * v.peak; vary(d, vl, d); note(role, sy, note_hz(deg_midi(d, v.lo, v.hi, 0)), dur, time, vl); }
            break;
        }
        default: break;
        }
    }
```

- [ ] **Step 4: Run** — row C: `C arpeggio: bar 2 top notes 50 53 57 60 50 ...` and PASS.

- [ ] **Step 5: Write the failing Pedal, Strum, Euclidean and Swells tests** (one function, call from `main`)

```cpp
static void test_styles() {
    /* Pedal: the fifth of the first chord, every quarter, held across chord changes when follow is off */
    const Log p = render(R"(,"comp":{"sect":{"style":"pedal","rate":"4n","pedal":"fifth","follow":false,"lo":55,"hi":80}})", 24);
    int bad = 0, n = 0; for (size_t i = 0; i < p.f.size(); i++) if (p.role[i] == 2) { n++; if ((int)std::lround(midi(p.f[i])) != 57) bad++; }
    std::printf("C pedal: %d notes, %d not A3 (57)\\n", n, bad);
    assert(n >= 30 && bad == 0);
    /* Strum: root, 3rd, 5th rolled up 40 ms apart, every half note */
    const Log s = render(R"(,"comp":{"v3":{"style":"strum","rate":"2n","tones":[0,1,2],"spread":40,"lo":50,"hi":74}})", 6);
    std::vector<double> ts; std::vector<int> ms; for (size_t i = 0; i < s.f.size(); i++) if (s.role[i] == 3) { ts.push_back(s.t[i]); ms.push_back((int)std::lround(midi(s.f[i]))); }
    assert(ts.size() >= 6 && ms[0] == 50 && ms[1] == 53 && ms[2] == 57 && std::fabs((ts[1] - ts[0]) - 0.04) < 1e-6 && std::fabs((ts[2] - ts[1]) - 0.04) < 1e-6);
    std::printf("C strum: D F A rolled 40 ms apart\\n");
    /* Euclidean: E(3, 8) in eighths, 3 notes a bar */
    const Log e = render(R"(,"comp":{"voice":{"style":"euclid","rate":"8n","pulses":3,"steps":8,"grid":[0,2,1]}})", 6);
    const std::vector<int> en = bar_notes(e, 1, 1);
    std::printf("C euclid: %zu top notes in bar 2\\n", en.size());
    assert(en.size() == 3);
    /* Swells: one long chord a bar, each note as long as the bar x len */
    const Log w = render(R"(,"comp":{"sect":{"style":"swells","rate":"1n","len":1,"tones":[0,1,2,3]}})", 6);
    int sw = 0; double dmin = 1e9; for (size_t i = 0; i < w.f.size(); i++) if (w.role[i] == 2) { sw++; dmin = std::fmin(dmin, w.d[i]); }
    std::printf("C swells: %d notes, shortest %.2f s\\n", sw, dmin);
    assert(sw >= 8 && std::fabs(dmin - 2.5) < 1e-6);
}
```

- [ ] **Step 6: Run** — they pass with Step 3's code (the styles are all in `play_style`); if one fails, fix `play_style`, not the test.

- [ ] **Step 7: Write the failing bass test** (call from `main`)

```cpp
static void test_bass() {
    const Log off = render(R"(,"comp":{"bass":{"mode":"off"}})", 20), ped = render(R"(,"comp":{"bass":{"mode":"pedal","pedal":"root"}})", 20);
    int b0 = 0; for (int r : off.role) b0 += r == 0;
    std::vector<int> pm; for (size_t i = 0; i < ped.f.size(); i++) if (ped.role[i] == 0) pm.push_back((int)std::lround(midi(ped.f[i])));
    std::printf("C bass: off %d notes; pedal %zu notes, first %d\\n", b0, pm.size(), pm.empty() ? -1 : pm[0]);
    assert(b0 == 0 && pm.size() == 1 && pm[0] % 12 == 50 % 12);   /* one held D, never re-struck across the 4 chords */
}
```

- [ ] **Step 8: Run to see it fail** — off still plays the held bass.

- [ ] **Step 9: Implement the bass modes** — replace `bass_bar`:

```cpp
    void bass_bar(double time) {
        if (cbass.mode == comp::BASS_OFF) { if (H.held && bass.sy()) bass.sy()->release(time); H.held = false; return; }
        if (cbass.mode != comp::BASS_PEDAL) return;
        int m = cbass.pedal == 2 ? cbass.note : patch.key - 24 + (cbass.pedal == 1 ? 7 : 0);
        if (!(H.held && H.have_bass && H.bass == m)) { note(0, bass.sy(), note_hz(m), -1, time, 0.5); H.bass = m; H.have_bass = true; H.held = true; }
    }
```

and in `harmony_bar`, guard the existing bass note: change `if (changed || !H.held || PERCUSSIVE[patch.voice.synth]) {` to
`if ((!patch.comp.on || cbass.mode == comp::HOLD) && (changed || !H.held || PERCUSSIVE[patch.voice.synth])) {`

- [ ] **Step 10: Run** — row C PASS, row 0 PASS (Free routes unchanged).

- [ ] **Step 11: Determinism and variation** (add to `test_styles`)

```cpp
    const std::string vr = R"(,"comp":{"seed":5,"voice":{"style":"arpeggio","rate":"16n","variation":0.8}})", vr2 = R"(,"comp":{"seed":6,"voice":{"style":"arpeggio","rate":"16n","variation":0.8}})";
    assert(same(render(vr, 6), render(vr, 6)));
    assert(!same(render(vr, 6), render(vr2, 6)));
    std::printf("C variation: the same seed repeats exactly; another seed differs\\n");
```

- [ ] **Step 12: Cost** (add to `composer_test.cpp`, call from `main`)

```cpp
#include <chrono>
static void test_cost() {
    char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, R"(,"comp":{"voice":{"style":"arpeggio","rate":"16n"},"sect":{"style":"strum","rate":"8n","tones":[0,1,2,3,4],"spread":20},"v3":{"style":"swells","rate":"2n","tones":[0,1,2,3,4]}})");
    unsigned seed = 3; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &seed, nullptr, nullptr);
    const int r = fs_piece_add_route(pc, buf); double worst = 0;
    for (int b = 0; b < (int)(30 * 48000 / 128); b++) { fs_piece_walk(pc, r, b / 11250.0, 0);
        auto t0 = std::chrono::steady_clock::now(); fs_process(pc, 128); if (b > 375) worst = std::fmax(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); }
    fs_destroy(pc);
    std::printf("C cost: worst block %.3f ms with every voice busy\\n", worst);
    assert(worst < 1.33);
}
```

- [ ] **Step 13: Review Focus — Swells gives the envelope back** (add to `test_styles`)

```cpp
    /* Swells on chord 1 only, then Arpeggio: the arpeggio's notes play with the voice's own envelope again. Checked through the
       piece's own hook: a held swell envelope would make its note-ons start at a 0.5 s+ attack; we read the voice's level 30 ms in */
    { char buf[8192]; std::snprintf(buf, sizeof buf, ROUTE, R"(,"comp":{"sect":{"style":"arpeggio","rate":"8n"},"over":{"chord":{"0":{"sect":{"style":"swells","rate":"1n","len":1}}}}})");
      unsigned sd = 11; fs_device *pc = fs_create("piece"); fs_prepare(pc, 48000, 128); fs_piece_test_hooks(pc, draw, &sd, nullptr, nullptr);
      const int r = fs_piece_add_route(pc, buf);
      float rl[2 + 6 * 3]; double late = -1;
      for (int b = 0; b < (int)(20 * 48000 / 128); b++) { fs_piece_walk(pc, r, b / 3750.0 > 1 ? 1 : b / 3750.0, 0); fs_process(pc, 128);
          if (b == (int)(16 * 48000 / 128)) { fs_piece_roles(pc, rl, 6); late = rl[8]; } }   /* the Second voice's level at 16 s (chord 4) */
      fs_destroy(pc);
      std::printf("C swells release: the Second voice at chord 4 %.1f dB (its own envelope)\\n", late);
      assert(late > -40); }
```

(`fs_piece_roles` returns 8 floats a role: level dB, note count, notes; the Second voice is at index 8.)

- [ ] **Step 14: Run** — `python core/tests/run.py` → 20/20; print the C lines with `node build/stretch-tests/composer_test.js` and keep them for the ledger.

- [ ] **Step 15: Commit**

```bash
git add core/piece.cpp core/tests/composer_test.cpp
git commit -m "composition 1: Arpeggio, Pedal, Strum, Euclidean, Swells, seeded variation; voice 1's bass hold/pedal/off"
```

---

### Task 5: Zones reach the composition (web engine, Android, iOS)

**Files:**
- Modify: `core/engine.cpp`, `android-app/app/src/main/cpp/jni.cpp`, `ios/RouteSound.swift`, `core/tests/composer_test.cpp`

**Interfaces:**
- Consumes: `fs_piece_zones(d, inside_csv, entered)`.
- Produces: each platform calls it whenever the set of zones the walker is inside changes, or one is entered.

- [ ] **Step 1: Write the failing engine test** (`composer_test.cpp`; call from `main`)

```cpp
static void test_engine_zone() {
    /* a route with a zone override on point "z1": inside it, voice 1 is off */
    char route[8192]; std::snprintf(route, sizeof route, R"({"type":"Feature","geometry":{"type":"LineString","coordinates":[[29.0,41.0],[29.004,41.0]]},
        "properties":{"id":"r1","kind":"route","name":"R","patch":%s}})",
        R"({"version":17,"tempo":96,"key":50,"prog":[{"r":0,"q":"m7"}],"bed":{"on":false},"zones":{"on":false},"voice":{"on":true,"synth":"fm"},"sect":{"on":false},"v3":{"on":false},"comp":{"voice":{"style":"arpeggio","rate":"16n"},"over":{"zone":{"z1":{"voice":{"role":"off"}}}}}})");
    const char *pt = R"({"type":"Feature","geometry":{"type":"Point","coordinates":[29.003,41.0]},"properties":{"id":"z1","kind":"point","name":"Z","sound":{"zoneR":40}}})";
    auto level = [&](double lon) {
        fs_engine *e = fs_engine_create(48000, 128); fs_engine_upsert(e, route); fs_engine_upsert(e, pt);
        double ms = 0; long long n = 0;
        for (int b = 0; b < (int)(12 * 48000 / 128); b++) { if (b % 40 == 0) fs_engine_step(e, lon, 41.0); fs_engine_process(e, 128);
            if (b > 4 * 48000 / 128) { const float *l = fs_engine_out(e, 0); for (int i = 0; i < 128; i++) { ms += l[i] * l[i]; n++; } } }
        fs_engine_destroy(e); return 10 * std::log10(ms / n + 1e-30); };
    const double out = level(29.001), in = level(29.003);
    std::printf("C zone: outside the zone %.1f dB, inside (voice 1 off) %.1f dB\\n", out, in);
    assert(out > -60 && in < out - 20);
}
```

- [ ] **Step 2: Run to see it fail** — inside and outside the same.

- [ ] **Step 3: The web engine passes its zones** — in `core/engine.cpp`, in `fs_engine_step`'s zone loop (`for (auto &sp : e->spots)`), collect and send. Replace the loop:

```cpp
    std::string inside, entered;
    for (auto &sp : e->spots) {
        double dist = fs_geo_distance(lon, lat, sp.lon, sp.lat);
        if (sp.audio) { cd.push_back(dist); cr.push_back(sp.radius); cc.push_back(sp.centroid); co.push_back(sp.onsets); }
        if (dist > sp.radius * FS_ZONE_MARGIN) { sp.zone = fs_zone_state{}; continue; }
        const int ev = fs_zone_step(&sp.zone, dist, sp.zoneR, now * 1000, FS_ZONE_MARGIN, FS_ZONE_COOLDOWN_MS);
        if (ev == 1 && sp.plain) fs_piece_zone(e->piece, sp.icon.c_str());
        if (ev == 1) entered = sp.id;
        if (sp.zone.inside) inside += (inside.empty() ? "" : ",") + sp.id;   /* composition: the zones the walker is in */
    }
    if (inside != e->zones_sent || !entered.empty()) { fs_piece_zones(e->piece, inside.c_str(), entered.c_str()); e->zones_sent = inside; }
```

and add `std::string zones_sent;` to `struct fs_engine`.

- [ ] **Step 4: Android** — in `android-app/app/src/main/cpp/jni.cpp`: add `std::string id;` to `struct Spot` (line 324) and fill it where spots are built (line ~402: the feature's `p->s("id", "")`); replace the zone line (~456) with the same collect-and-send as Step 3 (keeping `fs_piece_zone`), using a `static std::string zones_sent;` beside it.

- [ ] **Step 5: iOS** — in `ios/RouteSound.swift`: add `let id: String` to `struct Spot` (line 9) and pass `id: id` where spots are built (line ~136, the point's `p["id"] as? String ?? ""`); in the loop at line ~186 build `inside` (ids of spots whose `zone.inside != 0`) and `entered` (the id whose `fs_zone_step` returned 1), then after the loop:

```swift
        let ins = inside.joined(separator: ",")
        if ins != zonesSent || !entered.isEmpty { fs_piece_zones(piece, ins, entered); zonesSent = ins }
```

with `private var zonesSent = ""` on the class.

- [ ] **Step 6: Run** — `python core/tests/run.py` → row C prints the zone line, PASS. Build Android: `cd android-app && ./gradlew assembleDebug` → BUILD SUCCESSFUL. Build iOS on the Mac: `tar -cf - core ios places.geojson | ssh -i ~/.ssh/fieldscape_mac appdevcla@192.168.1.105 "cd ~/fs && tar -xf - && sh ios/build.sh"` → exit 0.

- [ ] **Step 7: Commit**

```bash
git add core/engine.cpp android-app/app/src/main/cpp/jni.cpp ios/RouteSound.swift core/tests/composer_test.cpp
git commit -m "composition 1: the zones the walker is in reach the composition (web engine, Android, iOS)"
```

---

### Task 6: The web engine build

**Files:**
- Modify: `web/core.wasm`, `web/core-lab.wasm` (built), `sw.js`, `tests/sw.test.mjs`

- [ ] **Step 1: Build** — `export EM_CONFIG=C:/Users/kerem/tools/emsdk/.emscripten PATH="$PATH:/c/Users/kerem/tools/emsdk/upstream/emscripten"; sh web/build.sh && sh web/build-lab.sh && cmp web/core.wasm web/core-lab.wasm` → no output (identical).
- [ ] **Step 2: Bump the shell** — in `sw.js` add `/* v16: composition step 1 - the voices' styles, per chord, section and zone. */` and `var SHELL = "fieldarc-shell-v16";`; in `tests/sw.test.mjs` change both `v15` to `v16`.
- [ ] **Step 3: Run** — `npm test` → all pass; `node core/tests/web_core.mjs` (server: `node tools/serve.mjs 8765`) → `errors: none`.
- [ ] **Step 4: Commit**

```bash
git add web/core.wasm web/core-lab.wasm sw.js tests/sw.test.mjs
git commit -m "composition 1: the web engine with the composer"
```

---

### Task 7: The page's composition model

**Files:**
- Create: `web/compose-model.js`, `tests/compose-model.test.mjs`

**Interfaces:**
- Produces (as `globalThis.FsCompose = { ... }` and `module.exports` for node):
  - `STYLES` (array of `{ id, label }`), `ROLES`, `RATES`, `FIELDS` (`{ common: [...], arpeggio: [...], ... }`, each field `{ k, label, kind: "menu"|"knob"|"grid"|"tones"|"toggle", min, max, step, options, def }`)
  - `defaults()` → a full `comp` object (today's sound)
  - `TEMPLATES` → `[{ id, label, comp(): comp }]`
  - `scopeObj(comp, scope)` → the object a scope writes into (`scope = { kind: "route" } | { kind: "chord", i } | { kind: "section", i } | { kind: "zone", id }`)
  - `setValue(comp, scope, voice, k, v)`, `clearValue(comp, scope, voice, k)`, `clearScope(comp, scope)`
  - `effective(comp, scope, voice)` → `{ values: {k: v}, source: {k: "route"|"here"} }`
  - `cell(comp, i, bandsAt)` → `[{ style, source: "route"|"chord"|"section"|"zone" } x3]` for the lanes
  - `bands(coords, sections, zones)` → `[{ kind: "section"|"zone", id, t0, t1 }]`, sampled along the route
  - `history(limit)` → `{ push(comp), undo(), redo(), canUndo(), canRedo() }`
  - `summary(comp)` → `"Arpeggio · Strum · Pedal"`

- [ ] **Step 1: Write the failing tests** — `tests/compose-model.test.mjs`

```js
import test from "node:test";
import assert from "node:assert/strict";
import M from "../web/compose-model.js";

test("defaults are today's sound, written out", () => {
  const c = M.defaults();
  assert.equal(c.voice.style, "free"); assert.equal(c.sect.style, "free"); assert.equal(c.v3.style, "free");
  assert.equal(c.bass.mode, "hold"); assert.deepEqual(c.over, { chord: {}, section: {}, zone: {} });
});
test("a chord override holds only what it changes; clearing it removes the key and the empty scope", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "chord", i: 4 }, "voice", "style", "strum");
  assert.deepEqual(c.over.chord["4"], { voice: { style: "strum" } });
  const e = M.effective(c, { kind: "chord", i: 4 }, "voice");
  assert.equal(e.values.style, "strum"); assert.equal(e.source.style, "here"); assert.equal(e.source.rate, "route");
  M.clearValue(c, { kind: "chord", i: 4 }, "voice", "style");
  assert.equal(c.over.chord["4"], undefined);
});
test("a zone override carries when and blend", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "zone", id: "p1" }, "sect", "role", "off");
  assert.equal(c.over.zone.p1.when, "inside"); assert.equal(c.over.zone.p1.blend, 1);
});
test("lane cells: zone > chord > section > route", () => {
  const c = M.defaults();
  M.setValue(c, { kind: "route" }, "voice", "style", "arpeggio");
  M.setValue(c, { kind: "section", i: 1 }, "voice", "style", "pedal");
  M.setValue(c, { kind: "chord", i: 2 }, "voice", "style", "strum");
  M.setValue(c, { kind: "zone", id: "z" }, "voice", "style", "swells");
  assert.equal(M.cell(c, 0, [])[0].style, "arpeggio");
  assert.deepEqual(M.cell(c, 0, [{ kind: "section", id: 1 }])[0], { style: "pedal", source: "section" });
  assert.deepEqual(M.cell(c, 2, [{ kind: "section", id: 1 }])[0], { style: "strum", source: "chord" });
  assert.deepEqual(M.cell(c, 2, [{ kind: "zone", id: "z" }])[0], { style: "swells", source: "zone" });
});
test("bands: where a zone and a section cross the route, as shares of its length", () => {
  const coords = [[29.0, 41.0], [29.01, 41.0]];
  const zones = [{ id: "z", lon: 29.005, lat: 41.0, r: 100 }];
  const sections = [{ id: 0, ring: [[28.999, 40.999], [29.003, 40.999], [29.003, 41.001], [28.999, 41.001], [28.999, 40.999]] }];
  const b = M.bands(coords, sections, zones);
  const z = b.find((x) => x.kind === "zone"), s = b.find((x) => x.kind === "section");
  assert.ok(Math.abs(z.t0 - 0.38) < 0.03 && Math.abs(z.t1 - 0.62) < 0.03, JSON.stringify(z));
  assert.ok(s.t0 === 0 && Math.abs(s.t1 - 0.3) < 0.03, JSON.stringify(s));
});
test("history: undo and redo walk the snapshots", () => {
  const h = M.history(50), c = M.defaults();
  h.push(c); const d = JSON.parse(JSON.stringify(c)); d.voice.style = "pedal"; h.push(d);
  assert.equal(h.undo().voice.style, "free"); assert.equal(h.redo().voice.style, "pedal"); assert.equal(h.canRedo(), false);
});
test("templates and the summary", () => {
  assert.ok(M.TEMPLATES.length >= 3);
  const t = M.TEMPLATES.find((x) => x.id === "arp-strum-pedal").comp();
  assert.equal(M.summary(t), "Arpeggio · Strum · Pedal");
  assert.equal(M.summary(M.defaults()), "Free · Free · Free");
});
```

- [ ] **Step 2: Run to see them fail** — `node --test tests/compose-model.test.mjs` → `Cannot find module '../web/compose-model.js'`.

- [ ] **Step 3: Write `web/compose-model.js`**

```js
/* Composition step 1 (docs/superpowers/specs/2026-10-17-composition-design.md): the page's model of a route's "comp" - its
   defaults (today's sound), templates, overrides per chord / section / zone, what a scope inherits, the lanes' cells, where
   zones and sections cross the route, undo history and a one-line summary. No DOM: node-tested (tests/compose-model.test.mjs). */
(function (root) {
  var VOICES = ["voice", "sect", "v3"];
  var STYLES = [{ id: "free", label: "Free" }, { id: "arpeggio", label: "Arpeggio" }, { id: "pedal", label: "Pedal" },
                { id: "strum", label: "Strum" }, { id: "euclid", label: "Euclidean" }, { id: "swells", label: "Swells" }];
  var ROLES = [{ id: "accomp", label: "on" }, { id: "off", label: "off" }];
  var RATES = ["16n", "8n", "8n.", "4n", "4n.", "2n", "2n.", "1n"];
  var COMMON = [
    { k: "style", label: "style", kind: "menu", options: STYLES, def: "free" },
    { k: "role", label: "voice", kind: "menu", options: ROLES, def: "accomp" },
    { k: "rate", label: "rate", kind: "menu", options: RATES.map(function (r) { return { id: r, label: r }; }), def: "8n" },
    { k: "lo", label: "lowest", kind: "knob", min: 24, max: 96, step: 1, def: 48, fmt: "note" },
    { k: "hi", label: "highest", kind: "knob", min: 36, max: 108, step: 1, def: 84, fmt: "note" },
    { k: "len", label: "length", kind: "knob", min: 0.05, max: 1, step: 0.05, def: 0.9, fmt: "pct" },
    { k: "variation", label: "variation", kind: "knob", min: 0, max: 1, step: 0.05, def: 0, fmt: "pct" }
  ];
  var FIELDS = {
    common: COMMON,
    free: [],
    arpeggio: [{ k: "grid", label: "pattern", kind: "grid", def: [0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3] },
               { k: "span", label: "octaves", kind: "knob", min: 1, max: 3, step: 1, def: 1 },
               { k: "acc", label: "accents", kind: "accents", def: [70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70] }],
    pedal: [{ k: "pedal", label: "note", kind: "menu", options: [{ id: "root", label: "root" }, { id: "fifth", label: "fifth" }, { id: "fixed", label: "fixed" }], def: "root" },
            { k: "pedal_note", label: "fixed note", kind: "knob", min: 24, max: 96, step: 1, def: 50, fmt: "note" },
            { k: "follow", label: "follows chords", kind: "toggle", def: true }],
    strum: [{ k: "tones", label: "tones", kind: "tones", def: [0, 1, 2] },
            { k: "dir", label: "direction", kind: "menu", options: [{ id: "up", label: "up" }, { id: "down", label: "down" }, { id: "alt", label: "alternating" }], def: "up" },
            { k: "spread", label: "spread", kind: "knob", min: 0, max: 200, step: 5, def: 40, fmt: "ms" }],
    euclid: [{ k: "pulses", label: "pulses", kind: "knob", min: 0, max: 16, step: 1, def: 5 },
             { k: "steps", label: "steps", kind: "knob", min: 1, max: 16, step: 1, def: 8 },
             { k: "rot", label: "rotation", kind: "knob", min: 0, max: 15, step: 1, def: 0 },
             { k: "grid", label: "pitches", kind: "grid", def: [0, 2, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1] }],
    swells: [{ k: "tones", label: "tones", kind: "tones", def: [0, 1, 2, 3] },
             { k: "attack", label: "attack", kind: "knob", min: 0.01, max: 0.95, step: 0.01, def: 0.3, fmt: "pct" },
             { k: "release", label: "release", kind: "knob", min: 0.01, max: 0.95, step: 0.01, def: 0.4, fmt: "pct" },
             { k: "peak", label: "level", kind: "knob", min: 0, max: 1, step: 0.05, def: 0.8, fmt: "pct" }]
  };
  var BASS = [{ k: "mode", label: "bass", kind: "menu", options: [{ id: "hold", label: "holds the root" }, { id: "pedal", label: "pedal" }, { id: "off", label: "off" }], def: "hold" },
              { k: "pedal", label: "pedal note", kind: "menu", options: [{ id: "root", label: "key root" }, { id: "fifth", label: "key fifth" }, { id: "fixed", label: "fixed" }], def: "root" }];
  function clone(x) { return JSON.parse(JSON.stringify(x)); }
  function voiceDefaults() { return { style: "free", role: "accomp", rate: "8n", lo: 48, hi: 84, len: 0.9, variation: 0 }; }
  function defaults() {
    return { seed: 1, voice: voiceDefaults(), sect: voiceDefaults(), v3: voiceDefaults(), bass: { mode: "hold" }, over: { chord: {}, section: {}, zone: {} } };
  }
  function tmpl(mod) { return function () { var c = defaults(); mod(c); return c; }; }
  var TEMPLATES = [
    { id: "today", label: "Today's sound", comp: tmpl(function () {}) },
    { id: "arp-strum-pedal", label: "Arpeggio · Strum · Pedal", comp: tmpl(function (c) {
        c.voice = Object.assign(c.voice, { style: "arpeggio", rate: "16n", lo: 55, hi: 84, grid: FIELDS.arpeggio[0].def });
        c.sect = Object.assign(c.sect, { style: "strum", rate: "2n", tones: [0, 1, 2, 3], spread: 50, lo: 50, hi: 74 });
        c.v3 = Object.assign(c.v3, { style: "pedal", rate: "1n", pedal: "fifth", lo: 43, hi: 62 }); }) },
    { id: "pulse", label: "Euclidean pulse", comp: tmpl(function (c) {
        c.voice = Object.assign(c.voice, { style: "euclid", rate: "8n", pulses: 5, steps: 8, grid: [0, 2, 1, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1] });
        c.sect = Object.assign(c.sect, { style: "euclid", rate: "16n", pulses: 3, steps: 8, rot: 2, grid: [2, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1], lo: 62, hi: 86 }); }) },
    { id: "ambient", label: "Swells (ambient)", comp: tmpl(function (c) {
        c.voice = Object.assign(c.voice, { style: "swells", rate: "1n", len: 1, tones: [0, 2, 4] });
        c.sect = Object.assign(c.sect, { style: "swells", rate: "2n.", len: 1, tones: [1, 3], lo: 60, hi: 84, attack: 0.6, release: 0.5 });
        c.v3.role = "off"; c.bass.mode = "pedal"; }) }
  ];
  function key(scope) { return scope.kind === "zone" ? scope.id : String(scope.i); }
  function scopeObj(comp, scope, create) {
    if (scope.kind === "route") { return comp; }
    var m = comp.over[scope.kind], k = key(scope);
    if (!m[k] && create) { m[k] = scope.kind === "zone" ? { when: "inside", blend: 1 } : {}; }
    return m[k] || null;
  }
  function setValue(comp, scope, voice, k, v) {
    var o = scopeObj(comp, scope, true);
    o[voice] = o[voice] || {};
    o[voice][k] = v;
  }
  function emptyOver(o) { return Object.keys(o).every(function (k) { return k === "when" || k === "blend"; }); }
  function clearValue(comp, scope, voice, k) {
    if (scope.kind === "route") { comp[voice][k] = (voice === "bass" ? { mode: "hold" } : voiceDefaults())[k]; if (comp[voice][k] === undefined) { delete comp[voice][k]; } return; }
    var o = scopeObj(comp, scope, false); if (!o || !o[voice]) { return; }
    delete o[voice][k];
    if (!Object.keys(o[voice]).length) { delete o[voice]; }
    if (emptyOver(o)) { delete comp.over[scope.kind][key(scope)]; }
  }
  function clearScope(comp, scope) { if (scope.kind !== "route") { delete comp.over[scope.kind][key(scope)]; } }
  function fieldDef(k) {
    var all = COMMON.concat(BASS); Object.keys(FIELDS).forEach(function (s) { all = all.concat(FIELDS[s]); });
    for (var i = 0; i < all.length; i++) { if (all[i].k === k) { return all[i].def; } }
    return undefined;
  }
  /* what a scope shows for a voice: its own values ("here") over the route's ("route"); every field of the voice's style */
  function effective(comp, scope, voice) {
    var base = comp[voice] || {}, own = scope.kind === "route" ? {} : ((scopeObj(comp, scope, false) || {})[voice] || {});
    var values = {}, source = {};
    var style = own.style || base.style || "free";
    var keys = voice === "bass" ? BASS.map(function (f) { return f.k; }) : COMMON.concat(FIELDS[style] || []).map(function (f) { return f.k; });
    keys.forEach(function (k) {
      if (own[k] !== undefined) { values[k] = own[k]; source[k] = "here"; }
      else { values[k] = base[k] !== undefined ? base[k] : fieldDef(k); source[k] = "route"; }
    });
    return { values: values, source: source, style: style };
  }
  /* a lane cell: chord i, with the bands over it; zone > chord > section > route */
  function cell(comp, i, bandsAt) {
    return VOICES.map(function (v) {
      var out = { style: comp[v].style, source: "route" };
      (bandsAt || []).filter(function (b) { return b.kind === "section"; }).forEach(function (b) {
        var o = comp.over.section[String(b.id)]; if (o && o[v] && o[v].style) { out = { style: o[v].style, source: "section" }; } });
      var ch = comp.over.chord[String(i)]; if (ch && ch[v] && ch[v].style) { out = { style: ch[v].style, source: "chord" }; }
      (bandsAt || []).filter(function (b) { return b.kind === "zone"; }).forEach(function (b) {
        var o = comp.over.zone[b.id]; if (o && o[v] && (o[v].style || o[v].role)) { out = { style: o[v].role === "off" ? "off" : (o[v].style || comp[v].style), source: "zone" }; } });
      if (out.source === "route" && comp[v].role === "off") { out.style = "off"; }
      return out;
    });
  }
  function dist(a, b) { var R = 6371008.8, la = (a[1] + b[1]) / 2 * Math.PI / 180;
    var dx = (b[0] - a[0]) * Math.PI / 180 * Math.cos(la) * R, dy = (b[1] - a[1]) * Math.PI / 180 * R; return Math.sqrt(dx * dx + dy * dy); }
  function inRing(p, ring) { var c = false;
    for (var i = 0, j = ring.length - 1; i < ring.length; j = i++) {
      if ((ring[i][1] > p[1]) !== (ring[j][1] > p[1]) && p[0] < (ring[j][0] - ring[i][0]) * (p[1] - ring[i][1]) / (ring[j][1] - ring[i][1]) + ring[i][0]) { c = !c; } }
    return c; }
  /* where each section and zone crosses the route, as [t0, t1] shares of its length (sampled every ~2 m) */
  function bands(coords, sections, zones) {
    var cum = [0]; for (var i = 1; i < coords.length; i++) { cum.push(cum[i - 1] + dist(coords[i - 1], coords[i])); }
    var total = cum[cum.length - 1] || 1, n = Math.max(20, Math.min(2000, Math.round(total / 2))), pts = [];
    for (var s = 0; s <= n; s++) { var d = s / n * total, k = 1; while (k < cum.length - 1 && cum[k] < d) { k++; }
      var f = (d - cum[k - 1]) / Math.max(1e-9, cum[k] - cum[k - 1]);
      pts.push([coords[k - 1][0] + (coords[k][0] - coords[k - 1][0]) * f, coords[k - 1][1] + (coords[k][1] - coords[k - 1][1]) * f]); }
    var out = [];
    function scan(kind, id, inside) { var start = -1;
      for (var s = 0; s <= n; s++) { var on = inside(pts[s]);
        if (on && start < 0) { start = s; }
        if ((!on || s === n) && start >= 0) { out.push({ kind: kind, id: id, t0: start / n, t1: (on ? s : s - 1) / n }); start = -1; } } }
    (sections || []).forEach(function (sc) { scan("section", sc.id, function (p) { return inRing(p, sc.ring); }); });
    (zones || []).forEach(function (z) { scan("zone", z.id, function (p) { return dist(p, [z.lon, z.lat]) <= z.r; }); });
    return out;
  }
  function history(limit) {
    var past = [], future = [];
    return {
      push: function (c) { past.push(JSON.stringify(c)); if (past.length > limit) { past.shift(); } future = []; },
      undo: function () { if (past.length < 2) { return null; } future.push(past.pop()); return JSON.parse(past[past.length - 1]); },
      redo: function () { if (!future.length) { return null; } var s = future.pop(); past.push(s); return JSON.parse(s); },
      canUndo: function () { return past.length > 1; }, canRedo: function () { return future.length > 0; }
    };
  }
  function label(id) { for (var i = 0; i < STYLES.length; i++) { if (STYLES[i].id === id) { return STYLES[i].label; } } return id; }
  function summary(comp) { return VOICES.map(function (v) { return comp[v].role === "off" ? "Off" : label(comp[v].style); }).join(" · "); }
  var api = { VOICES: VOICES, STYLES: STYLES, ROLES: ROLES, RATES: RATES, FIELDS: FIELDS, BASS: BASS, defaults: defaults, TEMPLATES: TEMPLATES,
    scopeObj: scopeObj, setValue: setValue, clearValue: clearValue, clearScope: clearScope, effective: effective, cell: cell,
    bands: bands, history: history, summary: summary, clone: clone, label: label };
  if (typeof module === "object" && module.exports) { module.exports = api; } else { root.FsCompose = api; }
})(typeof globalThis !== "undefined" ? globalThis : this);
```

- [ ] **Step 4: Run** — `node --test tests/compose-model.test.mjs` → 7 pass. The repo's `package.json` says `"type": "module"`, so node may load `web/compose-model.js` as ESM and find no default export. If it does, replace the test's import line with `import { createRequire } from "node:module"; const M = createRequire(import.meta.url)("../web/compose-model.js");` - the file itself stays a classic script (the page loads it with `<script>`).
- [ ] **Step 5: Run the whole suite** — `npm test` → all pass.
- [ ] **Step 6: Commit**

```bash
git add web/compose-model.js tests/compose-model.test.mjs
git commit -m "composition 1: the page's model - defaults, templates, overrides, inheritance, lanes, bands, history"
```

---

### Task 8: The full mock, for Kerem's approval (C-13)

**Files:**
- Create: `design/compose/dock-mock.html` (self-contained: tokens copied from `Design System/Tokens.md` as CSS variables, real Gazhane-like content: 16 chords, two sections, two zones, the three lanes, voice 1's bass row, the inspector on chord 5 / voice 1 with Arpeggio's grid, the zone inspector state in a second frame)

- [ ] **Step 1:** Read `C:/Users/kerem/OneDrive/Desktop/Claude/Design System/Tokens.md` and `C:/Users/kerem/.claude/skills/studio-rulebook/surfaces/web.md`; build the mock at 1440×900 with the map's top half as a static satellite-coloured block, the dock below at the real sizes, every value from the tokens.
- [ ] **Step 2:** Show it in the visual companion (`.superpowers/brainstorm/<session>/content/dock-mock.html`) and ask Kerem: does this look right, what would you change?
- [ ] **Step 3:** Iterate until Kerem approves. **Do not start Task 9 before he says yes.** Record his changes in the ledger (`scratch/compose1-ledger.md`).
- [ ] **Step 4: Commit** — `git add design/compose/dock-mock.html && git commit -m "composition 1: the dock's mock, approved by Kerem"`

---

### Task 9: The composing dock

**Files:**
- Create: `web/compose.js`, `web/compose.css`, `core/tests/web_compose.mjs`
- Modify: `index.html` (load the files; Compose button in `renderPatchPanel`; the context; the map's chord highlight)

**Interfaces:**
- Consumes: `FsCompose` (Task 7). From `index.html`: `patchOf(f)`, `commitPatch(f, p)`, `labChanged()`, `rootColour(pc)`, `chordRoot(patch, i)`, `chordLabel(patch, i)`, `routeMetrics(f)`, `pointAlong(m, t)`, `segmentCoords(m, t0, t1)`, `worldMove(pos)`, `voronoiCell(i)`, `sect.seeds`, `visible()`, `soundOf(f)`, `map`.
- Produces: `window.FsComposeDock = { open(ctx), close(), isOpen() }` where `ctx = { route: feature, comp(), save(comp), chords: [{ colour, label }], coords, sections(): [{ id, ring, colour }], zones(): [{ id, name, lon, lat, r }], light({ chord?: i, zone?: id }), listenAt(t) }`.

- [ ] **Step 1: Write the failing browser test** — `core/tests/web_compose.mjs` (headless Chrome, as `core/tests/web_t15.mjs` does: same launch, `ev`, `shot`, `fits`; signed in through `fsListen._gate(true)`; a local draft route made with `__fa.patchEdit`):

```js
/* Composition step 1 in the browser: the dock opens from a route's patch panel, its cells and bands select scopes, the
   inspector writes overrides into the route's comp and the engine gets them, undo works, the screen fits. Local edits only.
     node tools/serve.mjs 8765 ; node core/tests/web_compose.mjs      FS_SHOTS=<dir> */
import { spawn } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";
const URL = process.env.FS_URL || "http://localhost:8765/", SHOTS = process.env.FS_SHOTS || "";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe", ["--headless=new", "--remote-debugging-port=9297", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-cmp-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0; const check = (n, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + n + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9297))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready; await s.send("Runtime.enable"); await s.send("Page.enable");
  const errors = []; s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 300)); });
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const shot = async (n) => { if (!SHOTS) return; const r = await s.send("Page.captureScreenshot", { format: "png" }); writeFileSync(join(SHOTS, n + ".png"), Buffer.from(r.data, "base64")); };
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
  await sleep(2000); await ev("fsListen._gate(true), 0"); await sleep(600);
  const rid = await ev("(__fa.features().filter(function (f) { return f.properties.kind === 'route' && (f.properties.patch || {}).prog; })[0] || {properties:{}}).properties.id");
  await ev(`fsListen.select(${JSON.stringify(rid)}), 0`); await sleep(800);
  await ev("__fa.openPatchPanel && __fa.openPatchPanel(), 0"); await sleep(800);
  check("the patch panel has Compose", await ev("!!document.getElementById('pp-compose')"), null);
  await ev("document.getElementById('pp-compose').click(), 0"); await sleep(800);
  check("the dock opens: 16 chord cells, 3 lanes + the bass row", await ev("document.querySelectorAll('#cd-strip .cd-chord').length === __fa.routePatch(" + JSON.stringify(rid) + ").prog.length && document.querySelectorAll('#cd-lanes .cd-lane').length === 4"), null);
  const fit = await ev("JSON.stringify({ doc: document.documentElement.scrollHeight - innerHeight, dock: (function(){ var d = document.getElementById('compose-dock'); return d.scrollHeight - d.clientHeight; })() })");
  check("one screen: nothing scrolls", JSON.parse(fit).doc <= 0 && JSON.parse(fit).dock <= 1, fit);
  await shot("1-open");
  /* chord 5, voice 1: the inspector shows route values dimmed; style -> strum writes an override */
  await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-cell[data-i=\"4\"]').click(), 0"); await sleep(300);
  check("the inspector names the scope", /chord 5/.test(await ev("document.getElementById('cd-insp-title').textContent")), await ev("document.getElementById('cd-insp-title').textContent"));
  check("inherited settings are dimmed with their source", await ev("document.querySelector('#cd-insp .cd-row[data-k=\"rate\"]').classList.contains('inh') && /route/.test(document.querySelector('#cd-insp .cd-row[data-k=\"rate\"] .cd-src').textContent)"), null);
  await ev("(function(){ var m = document.querySelector('#cd-insp .cd-row[data-k=\"style\"] select'); m.value = 'strum'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(500);
  const comp = JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${JSON.stringify(rid)}).comp)`));
  check("chord 5's override is saved in the route's comp", comp && comp.over.chord["4"] && comp.over.chord["4"].voice.style === "strum", comp && comp.over);
  check("the cell shows it, outlined (a chord's)", await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-cell[data-i=\"4\"]').classList.contains('src-chord')"), null);
  check("the row is now this chord's own, with a reset", await ev("!document.querySelector('#cd-insp .cd-row[data-k=\"style\"]').classList.contains('inh') && !!document.querySelector('#cd-insp .cd-row[data-k=\"style\"] .cd-reset')"), null);
  /* undo */
  await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(400);
  check("Ctrl+Z takes it back", !JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${JSON.stringify(rid)}).comp.over.chord)`))["4"], null);
  /* a template */
  await ev("(function(){ var m = document.getElementById('cd-template'); m.value = 'arp-strum-pedal'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(500);
  check("a template writes the route's styles", await ev(`__fa.routePatch(${JSON.stringify(rid)}).comp.voice.style === 'arpeggio'`), null);
  await shot("2-template");
  /* the engine has it */
  await ev("fsListen.sound()"); for (let i = 0; i < 30 && !(await ev("!!__fa.coreSentFeature")); i++) await sleep(300);
  await sleep(800);
  check("the engine got the comp", await ev(`(function(){ var s = __fa.coreSentFeature(${JSON.stringify(rid)}); return !!s && JSON.parse(s).properties.patch.comp.voice.style === 'arpeggio'; })()`), null);
  check("no page errors", errors.length === 0, errors.slice(0, 3));
} finally { ch.kill(); }
console.log(failed ? failed + " FAILED" : "all passed"); process.exit(failed ? 1 : 0);
```

- [ ] **Step 2: Run to see it fail** — `node core/tests/web_compose.mjs` → `FAIL the patch panel has Compose`.

- [ ] **Step 3: The page's side** — in `index.html`:

(a) load the files after `web/lab-roles.js`:
```html
<script src="web/compose-model.js"></script>
<script src="web/compose.js"></script>
<link rel="stylesheet" href="web/compose.css">
```
(the `<link>` goes in `<head>` beside the other stylesheets; add `"./web/compose-model.js", "./web/compose.js", "./web/compose.css"` to `SHELL_FILES` in `sw.js`).

(b) test hooks next to the other `window.__fa` hooks: 
```js
  window.__fa.routePatch = function (id) { var f = feature(id); return f ? patchOf(f) : null; };
  window.__fa.openPatchPanel = function () { if (typeof openPatch === "function") { openPatch(); } else { renderPatchPanel(); $("#patchpanel").hidden = false; } };
```
(use whatever function the route card's Patch button calls; find it with `grep -n "patchpanel\").hidden = false" index.html`.)

(c) the Compose button at the top of `renderPatchPanel()`'s body, desk only:
```js
    if (innerWidth >= 1000 && f && f.properties.kind === "route") {
      var cb = document.createElement("button"); cb.type = "button"; cb.id = "pp-compose"; cb.className = "ghost";
      cb.textContent = "Compose"; cb.title = "How the three voices play, per chord, section and zone";
      cb.addEventListener("click", function () { openCompose(f); });
      body.insertBefore(cb, body.firstChild);
    }
```

(d) `openCompose(f)` — the context the dock needs:
```js
  /* Composition step 1: the dock under the map, editing this route's patch.comp (heard at once: the patch path) */
  function openCompose(f) {
    var patch = patchOf(f);
    if (!patch.comp) { patch.comp = FsCompose.defaults(); commitPatch(f, patch); }
    var m = routeMetrics(f);
    FsComposeDock.open({
      route: f,
      comp: function () { return patchOf(f).comp; },
      save: function (c) { var p = patchOf(f); p.comp = c; commitPatch(f, p); labChanged(); },
      chords: (patch.prog || []).map(function (c, i) { return { colour: rootColour(chordRoot(patch, i)), label: chordLabel(patch, i) }; }),
      coords: f.geometry.coordinates,
      sections: function () {
        if (!pacer || !sect.seeds || !pacer.patch || pacer.f !== f && pacer.routeId !== f.properties.id) { return null; }   /* null: walk onto the route to see them */
        return sect.seeds.map(function (s, i) { var ring = voronoiCell(i); ring.push(ring[0]);
          return { id: i, ring: ring, colour: rootColour(pacer.patch.key + pacer.patch.sectors[i % pacer.patch.sectors.length].r) }; });
      },
      zones: function () {
        return visible().filter(function (g) { return g.geometry.type === "Point"; }).map(function (g) {
          return { id: g.properties.id, name: label(g), lon: g.geometry.coordinates[0], lat: g.geometry.coordinates[1], r: soundOf(g).zoneR }; });
      },
      light: function (sel) { composeLight(f, m, sel); },
      listenAt: function (t) { var p = pointAlong(m, t); worldMove(p); }
    });
  }
  /* the selected chord's stretch on the map (a lamp line under the chord colours), or the selected zone */
  function composeLight(f, m, sel) {
    if (!map.getSource("compsel")) {
      map.addSource("compsel", { type: "geojson", data: { type: "FeatureCollection", features: [] } });
      map.addLayer({ id: "compsel-line", type: "line", source: "compsel", filter: ["==", ["geometry-type"], "LineString"],
        paint: { "line-color": lamp, "line-width": 18, "line-opacity": 0.55 }, layout: { "line-cap": "round" } }, "prog-active-ring");
      map.addLayer({ id: "compsel-zone", type: "line", source: "compsel", filter: ["==", ["geometry-type"], "Polygon"],
        paint: { "line-color": lamp, "line-width": 3.5 } });
    }
    var feats = [], n = (patchOf(f).prog || []).length || 16;
    if (sel && sel.chord != null) { feats.push({ type: "Feature", properties: {}, geometry: { type: "LineString", coordinates: segmentCoords(m, sel.chord / n, (sel.chord + 1) / n) } }); }
    if (sel && sel.zone) { var g = feature(sel.zone); if (g) { feats.push(circlePoly(g.geometry.coordinates[0], g.geometry.coordinates[1], soundOf(g).zoneR, 48)); } }
    map.getSource("compsel").setData({ type: "FeatureCollection", features: feats });
  }
```

(e) the summary on the route card (phones and desk): where the route card is built, after its name, add
```js
      if (patch.comp && typeof FsCompose !== "undefined") { var cs = document.createElement("p"); cs.className = "rc-comp"; cs.textContent = FsCompose.summary(patch.comp); card.appendChild(cs); }
```
(find the route card builder with `grep -n "function renderRouteCard\|routeCard" index.html`; use its own variable names.)

- [ ] **Step 4: The dock — `web/compose.js`**

```js
/* Composition step 1: the composing dock (docs/superpowers/specs/2026-10-17-composition-design.md, layout C, approved mock
   design/compose/dock-mock.html). The map keeps the top half; the dock below holds the chord strip, the section and zone
   bands, the three voice lanes and voice 1's bass row; the inspector (B) at its right shows every setting of the selection,
   inherited ones dimmed with their source. Edits go through ctx.save (the route's patch): heard at once. */
(function () {
  var M = window.FsCompose, ctx = null, comp = null, hist = null, sel = { kind: "route" }, voice = "voice", grown = false;
  var NAME = { voice: "voice 1", sect: "voice 2", v3: "voice 3", bass: "voice 1 bass" };
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) { e.className = cls; } if (text != null) { e.textContent = text; } return e; }
  function noteName(m) { return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1); }
  function fmt(f, v) { return f.fmt === "note" ? noteName(v) : f.fmt === "pct" ? Math.round(v * 100) + " %" : f.fmt === "ms" ? Math.round(v) + " ms" : String(v); }
  function scopeName(s) { return s.kind === "route" ? "the route" : s.kind === "chord" ? "chord " + (s.i + 1) : s.kind === "section" ? "section " + (s.i + 1) : "zone " + s.name; }
  function commit() { ctx.save(M.clone(comp)); hist.push(comp); render(); }

  function open(c) {
    ctx = c; comp = M.clone(ctx.comp()); hist = M.history(100); hist.push(comp); sel = { kind: "route" }; voice = "voice";
    var d = document.getElementById("compose-dock");
    if (!d) { d = el("section", "cd"); d.id = "compose-dock"; d.setAttribute("aria-label", "Compose"); document.body.appendChild(d); }
    document.body.classList.add("composing"); d.hidden = false;
    if (window.__fa && __fa.map) { setTimeout(function () { __fa.map.resize(); }, 0); }
    render();
  }
  function close() {
    var d = document.getElementById("compose-dock"); if (d) { d.hidden = true; }
    document.body.classList.remove("composing", "composing-grown"); grown = false; if (ctx) { ctx.light(null); } ctx = null;
    if (window.__fa && __fa.map) { setTimeout(function () { __fa.map.resize(); }, 0); }
  }

  function render() {
    var d = document.getElementById("compose-dock"); d.textContent = "";
    var n = ctx.chords.length;
    if (!n) {   /* C-5 empty: a route with no progression */
      var e = el("div", "cd-empty"); e.appendChild(el("p", null, "This route has no chords yet. Add a progression in its patch, then compose."));
      var b = el("button", "ghost", "Close"); b.addEventListener("click", close); e.appendChild(b); d.appendChild(e); return;
    }
    var main = el("div", "cd-main"), insp = el("aside", "cd-insp"); insp.id = "cd-insp";
    main.appendChild(header()); main.appendChild(strip(n));
    var secs = ctx.sections(), zs = ctx.zones(), bs = M.bands(ctx.coords, secs || [], zs);
    main.appendChild(bandRows(bs, secs === null, zs)); main.appendChild(lanes(n, bs));
    d.appendChild(main); inspector(insp); d.appendChild(insp);
  }
  function header() {
    var h = el("header", "cd-head");
    h.appendChild(el("h2", "cd-title", "Compose · " + (ctx.route.properties.name || "route")));
    var t = el("select", "cd-template"); t.id = "cd-template"; t.setAttribute("aria-label", "Start from a template");
    t.appendChild(new Option("Start from a template…", ""));
    M.TEMPLATES.forEach(function (x) { t.appendChild(new Option(x.label, x.id)); });
    t.addEventListener("change", function () { var x = M.TEMPLATES.filter(function (y) { return y.id === t.value; })[0]; if (x) { var nc = x.comp(); nc.over = comp.over; nc.seed = comp.seed; comp = nc; commit(); } });
    h.appendChild(t);
    var sd = el("label", "cd-seed"); sd.appendChild(el("span", null, "seed"));
    var si = el("input"); si.type = "number"; si.min = 0; si.max = 99999; si.value = comp.seed; si.addEventListener("change", function () { comp.seed = Math.max(0, +si.value | 0); commit(); });
    sd.appendChild(si); h.appendChild(sd);
    [["↶", "Undo (Ctrl+Z)", undo, hist.canUndo()], ["↷", "Redo (Ctrl+Shift+Z)", redo, hist.canRedo()],
     [grown ? "▾" : "▴", grown ? "Show the map" : "Grow the dock", function () { grown = !grown; document.body.classList.toggle("composing-grown", grown); if (__fa.map) { setTimeout(function () { __fa.map.resize(); }, 0); } render(); }, true],
     ["✕", "Close", close, true]].forEach(function (x) {
      var b = el("button", "cd-icon", x[0]); b.type = "button"; b.title = x[1]; b.setAttribute("aria-label", x[1]); b.disabled = !x[3]; b.addEventListener("click", x[2]); h.appendChild(b); });
    return h;
  }
  function strip(n) {
    var r = el("div", "cd-row"); r.appendChild(el("span", "cd-name", "chords"));
    var s = el("div", "cd-strip"); s.id = "cd-strip"; s.style.gridTemplateColumns = "repeat(" + n + ", 1fr)";
    ctx.chords.forEach(function (c, i) { var b = el("button", "cd-chord"); b.type = "button"; b.style.background = c.colour; b.title = (i + 1) + " · " + c.label;
      b.setAttribute("aria-label", "chord " + (i + 1) + ", " + c.label); if (sel.kind === "chord" && sel.i === i) { b.classList.add("on"); }
      b.addEventListener("click", function () { pick({ kind: "chord", i: i }); }); s.appendChild(b); });
    r.appendChild(s); return r;
  }
  function bandRows(bs, noSections, zs) {
    var w = el("div", "cd-bands");
    [["section", "sections"], ["zone", "zones"]].forEach(function (k) {
      var r = el("div", "cd-row"); r.appendChild(el("span", "cd-name", k[1]));
      var t = el("div", "cd-track");
      if (k[0] === "section" && noSections) { t.appendChild(el("span", "cd-hint", "walk onto the route to see its sections")); }
      bs.filter(function (b) { return b.kind === k[0]; }).forEach(function (b) {
        var name = k[0] === "zone" ? ((zs.filter(function (z) { return z.id === b.id; })[0] || {}).name || "zone") : "section " + (b.id + 1);
        var x = el("button", "cd-band " + k[0]); x.type = "button"; x.style.left = (b.t0 * 100) + "%"; x.style.width = Math.max(1.5, (b.t1 - b.t0) * 100) + "%";
        x.textContent = name; x.title = name;
        var mine = comp.over[k[0]][String(b.id)]; if (mine) { x.classList.add("has"); }
        if (sel.kind === k[0] && String(sel.i != null ? sel.i : sel.id) === String(b.id)) { x.classList.add("on"); }
        x.addEventListener("click", function () { pick(k[0] === "zone" ? { kind: "zone", id: b.id, name: name } : { kind: "section", i: b.id }); });
        t.appendChild(x); });
      r.appendChild(t); w.appendChild(r); });
    return w;
  }
  function lanes(n, bs) {
    var w = el("div", "cd-lanes"); w.id = "cd-lanes";
    M.VOICES.concat(["bass"]).forEach(function (v, vi) {
      var r = el("div", "cd-lane"); r.dataset.voice = v; r.appendChild(el("span", "cd-name", NAME[v]));
      var g = el("div", "cd-cells"); g.style.gridTemplateColumns = "repeat(" + n + ", 1fr)";
      for (var i = 0; i < n; i++) {
        var at = bs.filter(function (b) { return b.t0 < (i + 1) / n && b.t1 > i / n; });
        var c = v === "bass" ? { style: bassAt(i), source: comp.over.chord[String(i)] && comp.over.chord[String(i)].bass ? "chord" : "route" } : M.cell(comp, i, at)[vi];
        var b = el("button", "cd-cell src-" + c.source, v === "bass" ? c.style : (c.style === "off" ? "—" : M.label(c.style)));
        b.type = "button"; b.dataset.i = i; b.setAttribute("aria-label", NAME[v] + ", chord " + (i + 1) + ": " + b.textContent + (c.source !== "route" ? " (" + c.source + ")" : ""));
        if (sel.kind === "chord" && sel.i === i && voice === v) { b.classList.add("on"); }
        (function (i, v) { b.addEventListener("click", function () { voice = v; pick({ kind: "chord", i: i }); });
          b.addEventListener("dblclick", function () { ctx.listenAt((i + 0.5) / n); }); })(i, v);
        g.appendChild(b);
      }
      r.appendChild(g); w.appendChild(r); });
    return w;
  }
  function bassAt(i) { var o = comp.over.chord[String(i)]; return (o && o.bass && o.bass.mode) || comp.bass.mode; }
  function pick(s) { sel = s; ctx.light(s.kind === "chord" ? { chord: s.i } : s.kind === "zone" ? { zone: s.id } : null); render(); }

  function inspector(w) {
    var title = el("h3", "cd-insp-title", NAME[voice] + " · " + scopeName(sel)); title.id = "cd-insp-title"; w.appendChild(title);
    var tabs = el("div", "cd-vtabs");
    M.VOICES.concat(["bass"]).forEach(function (v) { var b = el("button", "cd-vtab" + (v === voice ? " on" : ""), NAME[v]); b.type = "button";
      b.addEventListener("click", function () { voice = v; render(); }); tabs.appendChild(b); });
    w.appendChild(tabs);
    if (sel.kind === "zone") { w.appendChild(zoneLines()); }
    var e = M.effective(comp, sel, voice);
    var fields = voice === "bass" ? M.BASS : M.FIELDS.common.concat(M.FIELDS[e.style] || []);
    fields.forEach(function (f) { w.appendChild(row(f, e.values[f.k], e.source[f.k])); });
    var tools = el("div", "cd-tools");
    if (sel.kind !== "route") { var cl = el("button", "ghost", "Clear " + scopeName(sel)); cl.type = "button"; cl.addEventListener("click", function () { M.clearScope(comp, sel); commit(); }); tools.appendChild(cl); }
    var li = el("button", "ghost", "Listen here"); li.type = "button"; li.title = "Put the walker on this stretch of the route";
    li.addEventListener("click", function () { var n = ctx.chords.length; ctx.listenAt(sel.kind === "chord" ? (sel.i + 0.5) / n : 0.02); });
    tools.appendChild(li); w.appendChild(tools);
  }
  function zoneLines() {
    var o = M.scopeObj(comp, sel, false) || { when: "inside", blend: 1 }, box = el("div", "cd-zone");
    [["when", [["inside", "while inside"], ["enter", "on entering (one phrase)"]]], ["blend", [["0", "at once"], ["1", "at the next bar"], ["2", "over 2 bars"], ["4", "over 4 bars"]]]].forEach(function (k) {
      var r = el("label", "cd-row2"); r.appendChild(el("span", null, k[0])); var s = el("select");
      k[1].forEach(function (x) { s.appendChild(new Option(x[1], x[0])); }); s.value = String(o[k[0]]);
      s.addEventListener("change", function () { var z = M.scopeObj(comp, sel, true); z[k[0]] = k[0] === "blend" ? +s.value : s.value; commit(); });
      r.appendChild(s); box.appendChild(r); });
    return box;
  }
  function row(f, v, src) {
    var r = el("div", "cd-row2" + (src === "route" && sel.kind !== "route" ? " inh" : "")); r.dataset.k = f.k;
    r.appendChild(el("span", "cd-k", f.label));
    var ctrl;
    var set = function (val) { M.setValue(comp, sel, voice, f.k, val); commit(); };
    if (f.kind === "menu") { ctrl = el("select"); f.options.forEach(function (o) { ctrl.appendChild(new Option(o.label, o.id)); }); ctrl.value = v; ctrl.addEventListener("change", function () { set(ctrl.value); }); }
    else if (f.kind === "toggle") { ctrl = el("button", "ghost", v ? "on" : "off"); ctrl.type = "button"; ctrl.setAttribute("aria-pressed", String(!!v)); ctrl.addEventListener("click", function () { set(!v); }); }
    else if (f.kind === "knob") { ctrl = el("input"); ctrl.type = "range"; ctrl.min = f.min; ctrl.max = f.max; ctrl.step = f.step; ctrl.value = v; ctrl.dataset.def = f.def;
      var out = el("i", null, fmt(f, v)); ctrl.addEventListener("input", function () { out.textContent = fmt(f, +ctrl.value); }); ctrl.addEventListener("change", function () { set(+ctrl.value); });
      r.appendChild(ctrl); r.appendChild(out); ctrl = null; }
    else if (f.kind === "tones") { ctrl = el("span", "cd-tones"); ["1", "3", "5", "7", "9"].forEach(function (lab, d) { var b = el("button", "cd-tone" + ((v || []).indexOf(d) >= 0 ? " on" : ""), lab); b.type = "button";
      b.addEventListener("click", function () { var t = (v || []).slice(); var k = t.indexOf(d); if (k >= 0) { t.splice(k, 1); } else { t.push(d); } t.sort(); set(t.length ? t : [0]); }); ctrl.appendChild(b); }); }
    else if (f.kind === "grid") { ctrl = gridEditor(v, set); }
    else if (f.kind === "accents") { ctrl = accentEditor(v, set); }
    if (ctrl) { r.appendChild(ctrl); }
    var s = el("span", "cd-src", src === "here" ? "here" : "route");
    if (src === "here" && sel.kind !== "route") { var x = el("button", "cd-reset", "×"); x.type = "button"; x.title = "Back to the route's"; x.setAttribute("aria-label", "Reset " + f.label);
      x.addEventListener("click", function () { M.clearValue(comp, sel, voice, f.k); commit(); }); s.appendChild(x); }
    r.appendChild(s);
    return r;
  }
  /* the pattern grid: rows chord degrees (9 7 5 3 root), columns steps; one degree per column, click again to rest */
  function gridEditor(v, set) {
    var g = el("div", "cd-grid"), cur = (v || []).slice(); while (cur.length < 16) { cur.push(-1); }
    ["9", "7", "5", "3", "root"].forEach(function (lab, ri) { var d = 4 - ri; g.appendChild(el("span", "cd-gk", lab));
      for (var c = 0; c < 16; c++) { (function (c) { var b = el("button", "cd-gc" + (cur[c] === d ? " on" : "") + (c % 4 === 0 ? " beat" : "")); b.type = "button";
        b.setAttribute("aria-label", "step " + (c + 1) + ", " + lab); b.setAttribute("aria-pressed", String(cur[c] === d));
        b.addEventListener("click", function () { cur[c] = cur[c] === d ? -1 : d; set(cur.slice()); }); g.appendChild(b); })(c); } });
    return g;
  }
  function accentEditor(v, set) {
    var g = el("div", "cd-acc"), cur = (v || []).slice(); while (cur.length < 16) { cur.push(70); }
    cur.forEach(function (a, c) { var b = el("button", "cd-ab"); b.type = "button"; b.style.setProperty("--h", a + "%"); b.setAttribute("aria-label", "accent, step " + (c + 1) + ", " + a);
      b.addEventListener("click", function (e) { var r = b.getBoundingClientRect(); cur[c] = Math.round(Math.max(0, Math.min(1, 1 - (e.clientY - r.top) / r.height)) * 100); set(cur.slice()); }); g.appendChild(b); });
    return g;
  }
  function undo() { var c = hist.undo(); if (c) { comp = c; ctx.save(M.clone(comp)); render(); } }
  function redo() { var c = hist.redo(); if (c) { comp = c; ctx.save(M.clone(comp)); render(); } }
  /* the keyboard: arrows move between cells, Enter opens, Delete clears, Ctrl+Z / Ctrl+Shift+Z */
  document.addEventListener("keydown", function (e) {
    if (!ctx || /INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) { return; }
    var n = ctx.chords.length;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") { e.preventDefault(); if (e.shiftKey) { redo(); } else { undo(); } return; }
    if (e.key === " ") { var pb = document.getElementById("patch-play"); if (pb) { pb.click(); } e.preventDefault(); return; }   /* Space: the sound on / off */
    if (e.key === "Escape") { close(); return; }
    if (sel.kind !== "chord" && /^Arrow/.test(e.key)) { sel = { kind: "chord", i: 0 }; render(); e.preventDefault(); return; }
    if (sel.kind === "chord") {
      var vs = M.VOICES.concat(["bass"]), vi = vs.indexOf(voice);
      if (e.key === "ArrowRight") { pick({ kind: "chord", i: Math.min(n - 1, sel.i + 1) }); e.preventDefault(); }
      else if (e.key === "ArrowLeft") { pick({ kind: "chord", i: Math.max(0, sel.i - 1) }); e.preventDefault(); }
      else if (e.key === "ArrowDown") { voice = vs[Math.min(vs.length - 1, vi + 1)]; render(); e.preventDefault(); }
      else if (e.key === "ArrowUp") { voice = vs[Math.max(0, vi - 1)]; render(); e.preventDefault(); }
      else if (e.key === "Delete" || e.key === "Backspace") { M.clearScope(comp, sel); commit(); e.preventDefault(); }
      else if (e.key === "Enter") { var f = document.querySelector("#cd-insp select, #cd-insp input, #cd-insp button"); if (f) { f.focus(); } e.preventDefault(); }
    }
  }, true);
  window.FsComposeDock = { open: open, close: close, isOpen: function () { return !!ctx; } };
})();
```

- [ ] **Step 5: The styles — `web/compose.css`** (every value from `Design System/Tokens.md` through the page's existing CSS variables; the sizes from the approved mock)

```css
/* Composition step 1: the dock (layout C). The map keeps the top half; the dock the bottom; grown, the map is a strip. */
body.composing #map { height: 50vh !important; }
body.composing.composing-grown #map { height: 96px !important; }
.cd { position: fixed; left: 0; right: 0; bottom: 0; height: 50vh; z-index: 30; display: grid; grid-template-columns: 1fr 340px;
      gap: var(--s-2); padding: var(--s-2); box-sizing: border-box; background: var(--ground); border-top: 1px solid var(--hairline); }
body.composing-grown .cd { height: calc(100vh - 96px); }
.cd[hidden] { display: none; }
.cd-main { display: flex; flex-direction: column; gap: var(--s-1); min-width: 0; min-height: 0; overflow: hidden; }
.cd-head { display: flex; align-items: center; gap: var(--s-2); }
.cd-title { flex: 1; margin: 0; font: 600 var(--t-md) var(--font-display); color: var(--ink); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.cd-head select, .cd-head input { font: var(--t-xs) var(--font-mono); background: var(--sunk); color: var(--ink); border: 1px solid var(--hairline); border-radius: 4px; padding: 2px var(--s-1); }
.cd-seed { display: flex; align-items: center; gap: 4px; font: var(--t-xs) var(--font-mono); color: var(--faint); text-transform: uppercase; }
.cd-seed input { width: 5em; }
.cd-icon { width: 32px; height: 32px; border-radius: 50%; border: 1px solid var(--hairline); background: var(--panel); color: var(--ink); cursor: pointer; }
.cd-icon:disabled { opacity: .35; cursor: default; }
.cd-icon:hover:not(:disabled), .cd-icon:focus-visible { border-color: var(--accent); outline: none; }
.cd-row, .cd-lane { display: grid; grid-template-columns: 96px 1fr; align-items: center; gap: var(--s-2); }
.cd-name { font: var(--t-xs) var(--font-mono); color: var(--faint); text-transform: uppercase; letter-spacing: .04em; white-space: nowrap; }
.cd-strip, .cd-cells { display: grid; gap: 2px; }
.cd-chord { height: 14px; border: 0; border-radius: 3px; cursor: pointer; opacity: .9; }
.cd-chord.on, .cd-chord:focus-visible { outline: 2px solid var(--ink); outline-offset: 1px; opacity: 1; }
.cd-track { position: relative; height: 18px; background: var(--sunk); border-radius: 3px; }
.cd-band { position: absolute; top: 1px; bottom: 1px; border-radius: 3px; font: var(--t-xs) var(--font-mono); color: var(--ink); white-space: nowrap; overflow: hidden;
           text-overflow: ellipsis; padding: 0 4px; cursor: pointer; text-align: left; }
.cd-band.section { background: color-mix(in oklab, var(--lamp) 10%, transparent); border: 1px dashed color-mix(in oklab, var(--lamp) 55%, transparent); }
.cd-band.zone { background: color-mix(in oklab, var(--ink) 8%, transparent); border: 1.5px solid color-mix(in oklab, var(--ink) 65%, transparent); }
.cd-band.has { font-weight: 600; }
.cd-band.on, .cd-band:focus-visible { outline: 2px solid var(--lamp); outline-offset: 1px; }
.cd-hint { position: absolute; left: var(--s-1); top: 2px; font: var(--t-xs) var(--font-body); color: var(--faint); }
.cd-lanes { display: flex; flex-direction: column; gap: 3px; min-height: 0; }
.cd-cell { height: 26px; border-radius: 3px; border: 1px solid transparent; background: var(--raised); color: var(--dim); cursor: pointer;
           font: var(--t-xs) var(--font-mono); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; padding: 0 2px; }
.cd-cell.src-section { background: color-mix(in oklab, var(--lamp) 14%, var(--raised)); color: var(--ink); }
.cd-cell.src-zone { background: color-mix(in oklab, var(--ink) 16%, var(--raised)); color: var(--ink); }
.cd-cell.src-chord { border-color: var(--ink); color: var(--ink); }
.cd-cell:hover { border-color: var(--accent); }
.cd-cell.on, .cd-cell:focus-visible { outline: 2px solid var(--lamp); outline-offset: 1px; color: var(--ink); }
.cd-insp { background: var(--panel); border: 1px solid var(--hairline); border-radius: 6px; padding: var(--s-2); overflow: hidden; display: flex; flex-direction: column; gap: 4px; }
.cd-insp-title { margin: 0; font: 600 var(--t-sm) var(--font-display); color: var(--ink); }
.cd-vtabs { display: flex; gap: 2px; }
.cd-vtab { flex: 1; font: var(--t-xs) var(--font-mono); padding: 3px 0; border: 1px solid var(--hairline); background: var(--sunk); color: var(--dim); border-radius: 4px; cursor: pointer; }
.cd-vtab.on { background: var(--raised); color: var(--ink); }
.cd-row2 { display: grid; grid-template-columns: 84px 1fr 52px; align-items: center; gap: var(--s-1); font: var(--t-xs) var(--font-mono); color: var(--dim); }
.cd-row2 > i { display: none; }
.cd-row2.inh > :not(.cd-src) { opacity: .45; }
.cd-row2 .cd-k { color: var(--faint); text-transform: uppercase; letter-spacing: .04em; }
.cd-row2 select, .cd-row2 input[type=range] { width: 100%; min-width: 0; }
.cd-src { text-align: right; font-size: 9px; color: var(--faint); white-space: nowrap; }
.cd-row2:not(.inh) .cd-src { color: var(--lamp); }
.cd-reset { margin-left: 4px; border: 0; background: none; color: var(--ink); cursor: pointer; font: var(--t-xs) var(--font-mono); }
.cd-tones { display: flex; gap: 2px; }
.cd-tone { flex: 1; font: var(--t-xs) var(--font-mono); border: 1px solid var(--hairline); background: var(--sunk); color: var(--dim); border-radius: 3px; cursor: pointer; }
.cd-tone.on { background: var(--accent); color: var(--sunk); }
.cd-grid { display: grid; grid-template-columns: 28px repeat(16, 1fr); gap: 1px; grid-column: 2 / 4; }
.cd-gk { font-size: 9px; color: var(--faint); text-align: right; padding-right: 3px; }
.cd-gc { height: 11px; border: 0; background: var(--sunk); border-radius: 1px; cursor: pointer; }
.cd-gc.beat { background: color-mix(in oklab, var(--ink) 6%, var(--sunk)); }
.cd-gc.on { background: var(--accent); }
.cd-acc { display: grid; grid-template-columns: repeat(16, 1fr); gap: 1px; height: 22px; grid-column: 2 / 4; }
.cd-ab { border: 0; background: linear-gradient(to top, var(--accent) var(--h), var(--sunk) var(--h)); cursor: pointer; }
.cd-zone { border-bottom: 1px solid var(--hairline); padding-bottom: 4px; display: flex; flex-direction: column; gap: 4px; }
.cd-tools { margin-top: auto; display: flex; gap: var(--s-1); }
.cd-empty { grid-column: 1 / -1; display: flex; align-items: center; justify-content: center; gap: var(--s-3); color: var(--dim); font: var(--t-sm) var(--font-body); }
.rc-comp { margin: 2px 0 0; font: var(--t-xs) var(--font-mono); color: var(--faint); }
```

(If a variable named above does not exist in the page — check with `grep -n "\-\-lamp\|\-\-raised\|\-\-t-md\|\-\-font-display" index.html` — add it to `Design System/Tokens.md` first and then to the page's `:root`, as C-1 says; never inline a value.)

- [ ] **Step 6: Review Focus — no progression** — add to `web_compose.mjs` before the final "no page errors" check:

```js
  /* a route with no chords: the dock says so and writes nothing */
  const empty = await ev(`(function(){ var f = __fa.features().filter(function (g) { return g.properties.kind === 'route'; })[0]; var c = JSON.parse(JSON.stringify(f)); c.properties.patch = Object.assign({}, c.properties.patch, { prog: [] });
    FsComposeDock.open({ route: c, comp: function () { return FsCompose.defaults(); }, save: function () { window.__saved = true; }, chords: [], coords: c.geometry.coordinates,
      sections: function () { return null; }, zones: function () { return []; }, light: function () {}, listenAt: function () {} });
    return document.querySelector('#compose-dock .cd-empty') ? document.querySelector('#compose-dock .cd-empty').textContent : null; })()`);
  check("no chords: the dock asks for them and saves nothing", /no chords yet/.test(empty || "") && !(await ev("!!window.__saved")), empty);
  await ev("FsComposeDock.close(), 0");
```

- [ ] **Step 7: Run** — `node tools/serve.mjs 8765` in one shell; `FS_SHOTS=scratch/compose1 node core/tests/web_compose.mjs` → `all passed`; **look at the screenshots** (`scratch/compose1/1-open.png`, `2-template.png`) against the approved mock; a numeric pass with a wrong picture is a fail (rulebook V-2).
- [ ] **Step 8: The other browser suites still pass** — `node core/tests/web_patch.mjs`, `node core/tests/web_sound.mjs`, `node core/tests/web_t15.mjs` → all passed.
- [ ] **Step 9: Commit**

```bash
git add web/compose.js web/compose.css index.html sw.js core/tests/web_compose.mjs
git commit -m "composition 1: the composing dock - strip, bands, lanes, inspector (inherited dimmed), grid, templates, undo, keyboard"
```

---

### Task 10: The apps show the summary; release builds

**Files:**
- Modify: `android-app/app/src/main/java/net/keremaltaylar/fieldscape/Cards.kt`, `android-app/app/src/main/java/net/keremaltaylar/fieldscape/Walk.kt` (RouteInfo gets `comp: String`), `ios/Cards.swift`, `ios/Walk.swift` (the same), `android-app/.../Chrome.kt` (`TEST_BUILD = 17`)

- [ ] **Step 1:** In both apps' route info, read the route patch's `comp` and produce the same one-line summary as `FsCompose.summary` (`"Arpeggio · Strum · Pedal"`, `"Off"` for a voice whose role is off, labels `Free Arpeggio Pedal Strum Euclidean Swells`); show it under the route card's title in the dim note style. A route without `comp` shows nothing.
- [ ] **Step 2:** Build Android (`./gradlew assembleDebug`), copy to `Desktop/Fieldscape-test-17.apk`; build iOS on the Mac (`sh ios/build.sh`).
- [ ] **Step 3:** On the emulator, at Koşuyolu with a composed probe route made in the browser test's way (or Kerem's own), check the card shows the summary and the log has 0 xruns.
- [ ] **Step 4: Commit** — `git commit -m "composition 1: the apps' route card shows the composition's summary (Test 17)"`.

---

### Task 11: Verify, review, release

- [ ] **Step 1:** `python core/tests/run.py` → 20/20; `npm test` → all pass; all browser suites pass.
- [ ] **Step 2:** A final whole-branch review (fresh reviewer on the most capable model) against the spec; fix what it finds with RED→GREEN tests.
- [ ] **Step 3:** Write the ledger `scratch/compose1-ledger.md` (what was built, every measured number from the C rows, what stays unverified: phone CPU, hearing on a phone, Kerem's ear).
- [ ] **Step 4:** Ask Kerem to test in the browser on the live site after the merge, and on the phones with Test 17. Merge `compose-1` into `main` and push only with his OK; then verify the live assets (`web/core.wasm` byte-identical, `index.html` with `FsComposeDock`, `node core/tests/web_compose.mjs` against `FS_URL=https://keremaltaylar.github.io/Fieldscape/`).
