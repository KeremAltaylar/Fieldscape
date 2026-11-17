# Modulation FX Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One shared modulation-FX engine in the C++ core, heard as (1) a parallel master FX on a route's three voices replacing the patch screen's "Rhythm effects", (2) a sixth point device, **Loop**, with its own instance; plus (3) the morph LFOs retargeted and given a live playhead, and (4) one distinctive marker per device on the web, Android and iOS.

**Architecture:** `core/modfx.hpp` (namespace `mfx`, header-only): nine serial modules, each with its own mix, 4 LFOs + envelope follower + walk sources + an 8-slot matrix, all buffers allocated once in `init`. `Piece` owns one engine fed by per-voice sends and returned beside the untouched dry; `fs_mix` owns a pool of two for point slots, set through `fs_point_sound` (the seam all three platforms already call). The page mirrors the parameter table in `web/fx-model.js` and draws one editor component (`web/fx.js`) for both the route master and the Loop panel. No patch version bump: new keys only (`patch.mfx`, `sound.fx`), ignored by installed apps.

**Tech Stack:** C++17 core → wasm (em++ `-O1`, built on the Mac: `sh tools/mac-wasm.sh`), Android NDK, iOS on the Mac mini, plain JS page, node tests, headless-Chrome browser tests against `node tools/serve.mjs 8765`.

**Spec:** `docs/superpowers/specs/2026-11-17-modulation-fx-design.md` — read it first; parameter names, ranges and defaults are its module table and are not repeated here.

## Global Constraints

- **Kerem answered the open questions (2026-11-17); see the spec's "Kerem's decisions". They override the tasks below:**
  - Part 1 Task 6: drop the walk sources (`S_ALONG/S_PACE/S_NEAR`, `walk()`, `test_walk`, Live slots 9-11 become unused). Sources = lfo1-4, env, chord.
  - Part 1 Task 8: drop `fs_piece_pace` and `master.walk`. **The core stops applying `rfx`** (rhythm points run clean). `rfx` stays parsed only so old patches still load. Update the "`rfx` stays in the core" constraint accordingly.
  - Part 1 Tasks 10-11: no legacy line or Remove button; no phone summary frame; no `web/patch.js` phone-tab change. The Master FX section fills the column the rhythm effects left. Show each rate as division · seconds.
  - Part 2: drop `fs_mix_fx_walk`, Task 2's pace/near feeding and the apps' `fxWalk`. Task 2 keeps only `fs_point_fx_follow` (chord) and the live read. The cap is one constant `MFX_POINT_CAP` (start at 4) and the test is "exactly CAP loudest". The phone CPU gate at Test 24 sets the final value. Mock text: "other points' effects are sounding".
  - Part 3: the mock and the build show **every morph cell** (six on a typical route) with its wave, a moving playhead and the live target value.
  - Part 4: better distinctive icons per point kind/device. Keep the current marker structure (no badge/marker swap on the web).
- **(Superseded) Kerem's open questions come first.** The spec's ten open questions carry assumed defaults; the controller confirms them with Kerem (or records "do as you recommend") in `scratch/fx-ledger.md` before Task 1. A changed answer changes the tasks that name it.
- Dry by default (A-8): a route without `mfx`, or with `ret` 0, renders **bit-identical** to main `99f0459` (`test_kept`, the step-2 repeat hash `7233712993534382426`, `core/test.cpp` row 0). A point without `fx.on` is untouched.
- Patch version stays 17. `rfx` stays in the core. `fs_piece_morphs` keeps its 7-double layout. `DEST_KEYS` only grows at the end.
- Audio thread: nothing allocates, locks, sorts or simulates; the patch read stays ≤ 1 ms warm; the composer cost lines (p99.9 < 1.0 ms, no block > 1.5 ms after warm-up) still pass with the master FX all on.
- FX budgets (wasm `-O1`, Node, 128-frame blocks, 48 kHz): typical preset ≤ 3.0 % of realtime, all nine modules ≤ 8.0 %, p99.9 block ≤ 0.35 ms, idle ≤ 0.05 %, a module's first block after `mix` leaves 0 ≤ 1.5 ms.
- Nothing clicks (A-2). **The click metric**, used by every "no click" check: over the 50 ms after a change, `max |y[n] − 2y[n−1] + y[n−2]|` ≤ 4 × its maximum over the 500 ms before, on a 220 Hz sine at −12 dBFS and on pink noise.
- Levels: a timbre control does not move the level (A-18: ≤ 3 dB across the control while a spectral measure moves); level-dependent stages work at −30 dBFS as at −6 dBFS (A-19).
- Every rate is a division of the route's tempo, shown with its seconds (A-9); one writer per parameter.
- Rulebook: invoke `studio-rulebook` (core + audio + web; mobile for the apps) before any UI task. C-1 tokens only, C-5 five states, C-8 one screen (toggle, not scrollbar), C-9 fixed grid, C-13 mock approved by Kerem before a panel is built, nothing under `--t-xs`, V-2 scroll overflow 0 **and** a screenshot.
- Supabase egress: browser tests use recordings generated in the page (as `core/tests/web_plain.mjs` does); never tour live points; never loop `core/tests/web_sound.mjs`.
- Builds: wasm and the test programs link on the Mac (Smart App Control blocks `wasm-ld.exe` here). Every new C export is added to **both** `web/build.sh` and `web/build-lab.sh` and `cmp web/core.wasm web/core-lab.wasm` must pass.
- `python core/tests/run.py` (20 min, alone, PC idle) runs once per part, in that part's verify task — not per task.
- Branches `fx-1` … `fx-4` (one per part, each from main after the previous part is live). Merge and push only with Kerem's OK. Check `git status` after each subagent (no force-added ignored files, no leftover background runs).

## Review Focus

1. A point leaves range while its Echo/Space tail rings and the slot is given to another point: the new point never plays the old one's tail — Part 2 Task 1.
2. Three Loop points in range: exactly two get FX, the third is dry, and the hand-over when one leaves does not click — Part 2 Task 1.
3. Echo feedback 0.98 + Shift feedback 0.9 + Space decay 60 s into a loud recording for minutes: output stays finite and under the limiter; a NaN from anywhere resets the module, never the mix — Part 1 Task 5.
4. A walker standing still (pace 0, near and along frozen), LFO-free preset: the FX keeps sounding and nothing sticks at an extreme (A-15) — Part 1 Task 6.
5. A route whose stored `mfx`/`fx` holds junk (strings for numbers, values far out of range, 40 matrix slots, an unknown `dst`): clamped or ignored, never a crash, never louder than the limiter — Part 1 Task 1.

---

## File structure

| File | Responsibility | Part·Task |
|---|---|---|
| `core/modfx.hpp` | the engine: table, config, modules, modulators, live read | 1·1–6 |
| `core/tests/fx_test.cpp`, `tools/mac-fx.sh` | the engine's own fast test (native on the Mac, seconds) | 1·1–6 |
| `core/tests/fx_bench.cpp`, `tools/mac-tests.sh`, `core/tests/run.py` | cost in wasm `-O1`; a row in `run.py` | 1·7 |
| `core/piece.cpp`, `core/fieldscape.h`, `core/test.cpp` | `Patch.mfx`, sends/return, sources, `fs_piece_fx_live`; later the destinations and `fs_piece_mod` | 1·8, 3·1 |
| `core/mix.cpp`, `core/devices/stretch.cpp`, `core/engine.cpp` | the point pool, `fs_point_sound` reads `fx`, the engine feeds near/pace | 2·1–2 |
| `web/fx-model.js`, `tests/fx-model.test.mjs` | the table and defaults on the page, checked against the core | 1·9 |
| `design/fx/master-mock.html`, `loop-mock.html`, `lfo-mock.html`, `icons-mock.html` | the mocks (human gates) | 1·10, 2·3, 3·2, 4·1 |
| `web/fx.js`, `web/fx.css`, `index.html`, `web/patch.js`, `web/core-worklet.js`, `core/tests/web_fx.mjs` | the editor, the patch screen, the live reads | 1·11 |
| `index.html`, `web/listener.js`, `core/tests/web_loop.mjs` | the Loop device on the page | 2·4 |
| `Walk.kt`, `Cards.kt`, `Core.kt`, `jni.cpp`, `Walk.swift`, `Cards.swift`, `App.swift` | the apps: Loop, the walk feeds | 2·5 |
| `web/markers.js`, `Icons.kt`, `MainActivity.kt`, `MapView.swift`, `tests/markers.test.mjs` | the markers | 4·2–3 |

---

# Part 1 — the engine and the route's master FX (branch `fx-1`)

### Task 1: The engine's frame — table, config, smoothing, silence

**Files:** Create `core/modfx.hpp`, `core/tests/fx_test.cpp`, `tools/mac-fx.sh`.

**Interfaces (Produces):**
```cpp
namespace mfx {
enum Mod { SHIFT, SWARM, PHASE, COMB, VOWEL, CLOUD, TAPE, ECHO, SPACE, NMOD };
enum Src { S_LFO1, S_LFO2, S_LFO3, S_LFO4, S_ENV, S_ALONG, S_PACE, S_NEAR, S_CHORD, NSRC };
enum Taper { LIN, EXP, CUBIC };                      /* CUBIC: bipolar, x^3 (shift.hz) */
struct ParamDef { const char *key; Mod mod; float min, max, def; Taper taper; bool cont; };   /* key "phase.hz"; cont: a matrix/morph destination */
extern const ParamDef PARAMS[]; extern const int NPARAM;   /* ONE ROW PER LINE, the spec's table in its order: the page's test reads these lines */
int param_of(const char *key);                       /* -1 unknown */
double div_beats(const char *d);                     /* "32m".."32n", dotted, triplets; unknown -> 1 */
struct LfoCfg { uint8_t shape = 0; char rate[5] = "1n"; float phase = 0; };
struct Slot { int8_t src = -1; int16_t dst = -1; float amt = 0; };
struct Cfg {                                         /* POD, no pointers: copied whole between threads */
    float v[64];                                     /* every PARAMS row's value, by index (NPARAM <= 64, static_assert) */
    char rate[NMOD][5];                              /* the division rows (swarm.rate, phase.rate, echo.time): in PARAMS with cont false, their strings here */
    float ret = 0, send[3] = { 1, 1, 1 }, wet = 0; bool on = false;
    LfoCfg lfo[4]; float env_a = 10, env_r = 300, env_s = 1, smooth = 2; Slot mods[8];
};
void cfg_defaults(Cfg &c);
void cfg_of(const Json *j, Cfg &c);                  /* absent/junk -> default; every number clamped to its row; mods beyond 8 dropped; unknown dst dropped */
struct Live { float v[32]; };                        /* 0-3 lfo phase, 4-7 lfo value, 8 env, 9 along, 10 pace, 11 near, 12 chord, 13-21 module levels, 22 in, 23 out, 24-31 each slot's destination now (0-1) */
struct Engine {
    void init(double sr);                            /* every buffer; the only allocation */
    void apply(const Cfg &c);                        /* targets only */
    void clock(double bpm, double beats);            /* the transport: tempo and position in beats */
    void chord(const float *midi5, float root, int serial);   /* serial changes on a chord change */
    void walk(float along, float pace, float near);  /* raw; smoothed inside */
    void process(const float *inL, const float *inR, float *wetL, float *wetR, int n);   /* writes the wet only; n <= 128 */
    bool idle() const;                               /* nothing on, or the tail under -80 dB for 0.5 s */
    void reset();                                    /* all state cleared, config kept */
    void live(Live &out) const;
};
}
```
- [ ] **Step 1: Failing tests** (`fx_test.cpp`, plain `main` with a `check(name, ok)` helper as `composer_test.cpp`; prints `fx ok` last): `test_table` (NPARAM is 52 + nothing else; every spec row present with its min/max/def; `param_of("echo.fb")` round-trips); `test_div` (`4n` 1, `8n.` 0.75, `8t` 1/3, `2m` 8, `32m` 128, junk 1); `test_parse` (the spec's JSON example reads back field for field; `{}` → `cfg_defaults`; Review Focus 5: `"hz":"loud"`, `"fb": 40`, 40 mods, `"dst":"nope.x"` → defaults, clamps, 8 slots, slot dropped); `test_silent` (defaults: 2 s of pink noise in → wet exactly 0.0f every sample, `idle()` true).
- [ ] **Step 2: Run** `sh tools/mac-fx.sh` (new: `tar czf - core | ssh -i ~/.ssh/fieldscape_mac appdevcla@192.168.1.105 'cd ~/fsw && tar xzf - && c++ -std=c++17 -O2 -Wall core/tests/fx_test.cpp -o /tmp/fx_test && /tmp/fx_test'`) — FAIL (no header).
- [ ] **Step 3: Implement** the table, parse, `Engine` with every module a pass-through stub behind a per-module `mix` smoother (30 ms), the "all mixes 0 → output 0" gate, and the module-skip rule (skipped at mix 0; state cleared before it next runs).
- [ ] **Step 4:** `sh tools/mac-fx.sh` → `fx ok`. **Step 5: Commit** `fx 1: the engine's frame - table, config, silence`.

### Task 2: Shift, Phase, Vowel (filters and the Hilbert pair)

**Files:** `core/modfx.hpp`, `core/tests/fx_test.cpp`.

Hilbert pair: two chains of four second-order all-passes `y[n] = c·(x[n] + y[n−2]) − x[n−2]`, c = a² with a = {0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737} (chain 1, then one sample of delay) and {0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278} (chain 2). SSB: `I·cos φ − Q·sin φ`; ring: `I·cos φ`. Vowel formants (Hz, a e i o u): F1 800 400 300 450 325 · F2 1150 1700 2200 800 700 · F3 2900 2600 3000 2830 2700, interpolated in log frequency, × `size`. `LevelMatch { void init(sr); float gain(const float *in, const float *out, int n); }` (400 ms RMS ratio, ±12 dB) is introduced here for Vowel and reused in Task 3.
- [ ] **Step 1: Failing tests:** `test_shift` (1 kHz sine, `hz` +100, mix 1: 1100 Hz present, 900 Hz ≥ 35 dB under it; `hz` −100 mirrors; ring: both at equal level ± 1 dB; `fb` 0.9 for 20 s: finite, |out| ≤ 4); `test_phase` (white noise, 6 stages, rate frozen by a test hook: three notches, the lowest within 10 % of `hz·2^(−span/2)`…`hz·2^(span/2)` as the LFO phase goes 0→0.5; stages 12 → six notches); `test_vowel` (pink noise, `pos` 0→4 in 9 steps: output level range ≤ 3 dB, spectral centroid range ≥ 30 % — both numbers printed); click metric on every parameter of the three stepped min→max, and on `mix` 0→1→0.
- [ ] **Step 2–5:** RED, implement, GREEN, commit `fx 1: shift, phase, vowel`.

### Task 3: Swarm, Tape, Comb (modulated delays)

- [ ] **Step 1: Failing tests:** `test_swarm` (a 440 Hz sine, depth 1, rate 4n at 120 bpm: the output's instantaneous-frequency deviation is periodic at 0.5 s ± 1 %; `zero` on: at the sweep's crossing the summed module output nulls ≥ 20 dB; `spread` 0 → L == R, 1 → L ≠ R); `test_tape` (1 kHz sine: wow 1 → pitch deviation 1.5–2.5 % peak, flutter 1 → energy at 7.3 Hz ± 0.5 in the deviation; `drive` sweep 0→1 at −30 dBFS **and** −6 dBFS: output level within 3 dB of input at both, third-harmonic rises ≥ 12 dB at both (A-19)); `test_comb` (pink noise, `tune` chord with `chord({50,53,57,60,-1}, 50, 1)`: spectral peaks within ±10 cents of D, F, A, C folded into 55–880 Hz; a chord change glides — click metric; `decay` 0.05→20 s: level range ≤ 3 dB; measured RT60 within ±25 % of `decay` at 0.5, 2, 8 s; `tune` free 110 Hz: peaks at 110, 165, 220, 330, 440 ± 10 cents).
- [ ] **Step 2–5:** RED, implement (reuse `tone::DelayLine`; `LowpassComb` if its interface fits, else a local comb), GREEN, commit `fx 1: swarm, tape, comb`.

### Task 4: Cloud

Seeded xorshift (seed fixed per engine: the same input gives the same output — a walk repeats); grain gain normalised by `1/sqrt(max(1, dens·size))`.
- [ ] **Step 1: Failing tests:** `test_cloud` (an impulse train in: grains per second within ±20 % of `dens` at 4, 12, 40; `pitch` +12 on a 220 Hz sine → energy at 440 Hz; `rev` 1 on a rising chirp → falling chirps; `scatter` 0 → every grain starts `size` behind now, 1 → start times spread over ≥ 1 s; `freeze` on, then silence in for 5 s → output level within 3 dB of its level before the silence, and the toggle passes the click metric; same input twice → identical output; `dens`·`size` from sparse to dense: level range ≤ 4 dB).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `fx 1: cloud`.

### Task 5: Echo and Space (the feedback pair), and stability

Space: 8 lines, lengths {1087, 1283, 1481, 1699, 1907, 2129, 2351, 2579} samples at 48 kHz × `size` (scaled by sr), Householder feedback (`x − (2/8)·Σx`), per-line one-pole at `damp`, per-line gain `10^(−3·len/(decay·sr))`, each length swung ± `mod`·8 samples on its own slow sine (rates {0.13, 0.19, 0.23, 0.29, 0.17, 0.31, 0.37, 0.41} Hz), two input all-passes; odd lines left, even right.
- [ ] **Step 1: Failing tests:** `test_echo` (impulse, `time` 8n at 90 bpm, mix 1: first repeat at 16000 ± 2 samples; repeat k is `fb^k` ± 1 dB at `tone` 0; `shift` +10: repeat k's 1 kHz partial sits at 1000 + 10k Hz ± 2; `tone` −1 darkens each repeat (centroid falls monotonically), +1 thins it; `spread` 1 alternates sides; a `time` change 8n→4n glides — click metric, no zipper); `test_space` (noise burst: RT60 within ±20 % of `decay` at 1, 4, 20 s; held 147 Hz sine (the D3 of the 2026-10-20 room fix): wet level within 6 dB of a 220 Hz sine's); `test_stable` (Review Focus 3: every module mix 1, `echo.fb` 0.98, `shift.fb` 0.9, `space.decay` 60, white noise at 0 dBFS for 120 s: every sample finite, |out| ≤ 8; then a NaN injected into the input: output finite within one block, the engine recovers to within 1 dB of an uninjected run after 3 s); `test_tail` (input stops: `idle()` true within `decay` + 6 s and output exactly 0 after).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `fx 1: echo with a transforming loop, space, stability`.

### Task 6: The modulators and the matrix

LFO phase = `frac(beats / div_beats(rate) + phase)` from `clock()` — never accumulated. `steps`/`drift` hash the period index with the engine seed. Walk sources: EMA with time constant `smooth`; `walk()` not called for 5 s → the last value holds. `resolve()` once a block: value = base + Σ `amt` × range × source (LFOs bipolar −1…1, the rest 0…1), on the row's taper, clamped; the result is the module smoother's target.
- [ ] **Step 1: Failing tests:** `test_lfo` (rate 4n at 120 bpm: zero crossings every 0.5 s ± 1 sample over 100 cycles, tempo error 0.00 %; a tempo change mid-run: no phase jump > one block's worth; each shape's values at phase 0, 0.25, 0.5, 0.75; `steps` holds for a period and is the same on a second run); `test_env` (a burst: rises to 90 % within `attack`·3, falls to 10 % within `release`·3); `test_walk` (`near` 0→1 in one call: the modulated `phase.hz` reaches 63 % of its swing in `smooth` s ± 10 %, click metric; Review Focus 4: sources frozen for 60 s, an LFO-free preset with `near`→`echo.fb` at 1: output level steady ± 1 dB, no parameter pinned by drift); `test_matrix` (two slots on one destination sum; `amt` −1 on a parameter at its minimum stays clamped; a slot on a switch row is ignored; `live()`'s slots 24–31 equal the resolved values 0–1; `chord` source changes only when `chord()`'s serial changes).
- [ ] **Step 2–5:** RED, implement, GREEN, commit `fx 1: lfos, envelope, walk sources, matrix`.

### Task 7: Cost

**Files:** Create `core/tests/fx_bench.cpp`; modify `tools/mac-tests.sh` (a fourth program, same flags, sources `core/tests/fx_bench.cpp` only), `core/tests/run.py` (row "F": runs `fx_test`-equivalent checks compiled into `fx_bench check`, and the cost lines).
- [ ] **Step 1:** `fx_bench` renders 20 s of pink noise in 128-frame blocks through (a) defaults with `ret` 0, (b) "typical": phase + comb + cloud + echo + space at mix 1 with two matrix slots, (c) all nine at mix 1 with eight slots, timing each block with `emscripten_get_now`; prints mean % of realtime, p99.9 and max block ms, and the first-block time of each module switching on; exits 1 past the Global Constraints budgets.
- [ ] **Step 2:** `sh tools/mac-tests.sh && node build/stretch-tests/fx_bench.js` on this PC, alone, three runs; record the numbers in `scratch/fx-ledger.md`. Over budget → optimise the worst module (coefficient updates every 16 samples, not every sample; skip Hilbert when `hz` is 0) until it passes; a budget that cannot be met is reported to the controller, not relaxed.
- [ ] **Step 3: Commit** `fx 1: the engine's cost, measured`.

### Task 8: The master FX in the piece

**Files:** `core/piece.cpp`, `core/fieldscape.h`, `core/test.cpp`, `web/build.sh`, `web/build-lab.sh`.

**Interfaces:** Consumes `mfx::Cfg`, `cfg_of`, `Engine`. Produces `int fs_piece_fx_live(fs_device *d, float *out32)` (1 while a route plays, else 0; `out32` = `mfx::Live::v`), `int fs_engine_fx_live(fs_engine *e, int which, float *out32)` (`which` 0 = master; 1 = the watched point, wired in Part 2 — returns 0 until then), `double fs_piece_tempo(fs_device *d)`.

- `Patch` gains `mfx::Cfg mfx` (read by `cfg_of(p->get("mfx"), P.mfx)` in `patch_of`). `Piece` gains `mfx::Engine master` (init in `prepare`), `float mL[SUB], mR[SUB], wL[SUB], wR[SUB]`.
- `render`: each chain is processed into its own cleared temp and added to `oL/oR` in today's order (`fx`, `fx2`, `fx3` — same additions, same order, so the dry sum is bit-identical); `mL/mR` = Σ `send[q]` (a `Ctl`, 30 ms) × temp q. Unless (`ret` target 0 and `master.idle()`): `master.process(mL, mR, wL, wR, n)`, `oL += ret × wL` — before the `synth_level × route_solo` multiply. `ret` is a `Ctl` ramped 30 ms.
- Per sub-block: `master.clock(bpm, ticks / 4.0 + fraction)`; `master.walk(t_along, pace, near)` with `near = 1 − min(1, dist / leash)` from `fs_piece_walk`'s `dist` (the leash constant the piece already fades by) and `pace` from a new `fs_piece_pace(fs_device*, float mps)` inbox value (0 until Part 2 Task 2 feeds it); `master.chord(...)` when `H.chord` changes. `apply` on `take()` and on `fs_piece_set_route` of the playing route. A route change (`take`) calls `master.reset()` only if the outgoing route's `ret` is 0; otherwise the tail rings on under the route crossfade.
- [ ] **Step 1: Failing tests** (`core/test.cpp`, a new block "FX"): a route with no `mfx` and one with `"mfx":{"ret":0,"phase":{"mix":1}}`: 10 s of output byte-identical to each other and `fs_piece_test_kept` unchanged; `ret` 0.8 + echo: output differs, the dry is still inside it (output − wet-only render == the kept output within 1e-6); `send` [0,0,1]: only voice 3's notes reach the wet (voices 1–2 solo'd → wet under −90 dB); `ret` stepped 0→1 and a patch edit of every `mfx` key mid-note: click metric; the route's leash at 0 → wet at 0; **patch read**: `fs_piece_set_route` with a full `mfx` (all modules, 8 mods) warm ≤ 1 ms (the existing timing harness); composer cost lines with the master all on still within budget; `fs_piece_fx_live` slots 0–3 advance with the transport.
- [ ] **Step 2–4:** RED (build via `sh tools/mac-tests.sh`, run `node build/stretch-tests/core_test.js`), implement, GREEN; add `_fs_piece_fx_live,_fs_engine_fx_live,_fs_piece_tempo,_fs_piece_pace` to both build scripts; `sh tools/mac-wasm.sh` (its `cmp` passes).
- [ ] **Step 5: Commit** `fx 1: the master fx in the piece - sends, return, sources`.

### Task 9: The model on the page

**Files:** Create `web/fx-model.js` (`window.FsFxModel`, also `export`-free CommonJS-safe as `web/compose-model.js` is), `tests/fx-model.test.mjs`.

**Interfaces (Produces):** `FsFxModel.MODULES` (`[{ id, label, note, params: [{ key, label, min, max, def, taper, unit, kind: "knob"|"switch"|"division", options? }] }]`), `DIVISIONS`, `SOURCES`, `LFO_SHAPES`, `defaults(kind)` (`"route"`/`"point"`), `clean(obj)` (drops keys at their default so a dry FX stores as `{}`), `get(obj, key)`, `set(obj, key, value)` (clamps), `fmt(key, value, tempo)` ("2.4 kHz", "8 ms", "1/8. · 0.50 s", "+7 st"), `divSeconds(div, bpm)`, `destinations()` (the 44 continuous keys with labels, for the matrix and Part 3), `summary(obj)` ("Phase · Echo · Space").
- [ ] Tests: every row of `core/modfx.hpp`'s `PARAMS` (read the file, one row per line) has the same key, min, max, default and taper in the model, and nothing extra; `DIVISIONS`' beats equal `div_beats`' (the values of `test_div`); `defaults` round-trip through `clean` to `{}`; `set` clamps; `fmt` cases above; the spec's JSON example survives `clean`. RED → GREEN (`npm test`) → commit `fx 1: the model`.

### Task 10: The master FX mock — HUMAN GATE (C-13)

**Files:** Create `design/fx/master-mock.html` (static, 1440×900 frames, built on `design/points/melody-mock.html`'s tokens and frame; values from `Design System/Tokens.md` only).
- [ ] Frames: (1) the patch screen with "Rhythm effects" gone and the **Master FX** section in its column (return, three sends, the nine-module strip with lamps, Open, the load), plus the legacy line state ("…still pass through the old rhythm effects… Remove"); (2) the **editor** open on Echo: module strip, Echo's seven controls with unit readouts and division · seconds, the four LFO scopes with playheads, envelope, the three walk bars + chord, the matrix (8 rows: source, destination grouped by module, amount, a live bar), in/out meters, load; (3) the editor on Cloud with freeze lit and two parameters showing modulation rings; (4) states: everything dry ("Nothing is on. Turn a module's mix up to hear it."), sound off (scopes on a preview clock, tagged), a route with no voices on, load over budget (a warning tone, not red-on-green), 1024×700; (5) the phone summary (390×844): return, sends, module names, "Edit on a computer".
- [ ] Measure every frame (no spill: each child's right/bottom inside its container; `scrollHeight − innerHeight === 0`), contrast every text/surface pair mechanically (C-7), screenshot, show Kerem, **wait for his yes**; record his words and any change in `scratch/fx-ledger.md`; commit `fx 1: the master fx mock, approved by Kerem`.

### Task 11: The patch screen and the editor

**Files:** Create `web/fx.js` (`window.FsFx.mount(container, { kind, get, set, live, tempo, onOpen })`, `FsFx.scope(canvas, { shape, phase, value, state })`), `web/fx.css`, `core/tests/web_fx.mjs`; modify `index.html` (remove the seven `rfx.*` `PATCH_FIELDS` rows and their section; `defaultPatch().rfx` → the clean values; the Master FX section + Open toggle; the legacy line; `<script>`/`<link>` tags), `web/patch.js` (the phone tab regex: "Master FX" under Effects; drop "Rhythm"), `web/core-worklet.js` (`live()` posts `fx: Float32Array(32)` from `fs_engine_fx_live(e, 0, …)` when the export exists).
- [ ] **Step 1: Failing browser test** `web_fx.mjs` (local server, a draft route, sound on, no Supabase recordings): the patch body has no "Rhythm effects" heading and no `rfx.` control; a new route's stored `rfx` is clean; a route given a non-clean `rfx` shows the legacy line, Remove writes the clean values and the line goes; every knob/switch/division in the editor writes its key into `patch.mfx` (iterate `FsFxModel.MODULES`), stored through `clean`; return 0.8 + Echo mix 1 → an `AnalyserNode` on the output reads a level and the engine's `fx[23]` (out) > 0; return 0 → `fx[23]` falls to 0; the four scopes' playheads move (two samples 300 ms apart differ) and match `fx[0..3]`; shift-drag is fine, double-click resets; one undo step per edit if the patch panel has undo, else none added; keyboard: every control focusable, arrows step; **fit**: 1440×900 and 1024×700, editor open and closed, `scrollHeight − innerHeight === 0` and no child spilling; screenshots `20-masterfx.png`, `21-fx-editor.png`.
- [ ] **Step 2–4:** RED; build to the approved mock; GREEN; also run `node core/tests/web_patch.mjs`, `web_compose.mjs`, `web_core.mjs`, `npm test`, `npm run check`.
- [ ] **Step 5: Commit** `fx 1: the master fx on the patch screen; rhythm effects leave it`.

### Task 12: Builds

- [ ] `TEST_BUILD = 23` (`Chrome.kt`, `App.swift`); `sh tools/mac-wasm.sh`; Android `cd android-app && ./gradlew assembleDebug` → copy to `C:/Users/kerem/OneDrive/Desktop/Fieldscape-test-23.apk`; iOS: tar-sync the whole `core/` + `ios/` to the Mac, `sh ios/build.sh` (rc 0). No app UI changes (the master FX is inside the piece). Commit `fx 1: Test 23`.

### Task 13: Verify, review, release

- [ ] `sh tools/mac-tests.sh && FS_PREBUILT=1 python core/tests/run.py` (alone, idle; all rows incl. F); `npm test`; `web_fx`, `web_patch`, `web_compose`, `web_core` once each (not `web_sound`).
- [ ] Whole-branch review by the most capable model available (give it the diff and the spec, not a summary); one fix dispatch; scoped re-review.
- [ ] `scratch/fx-ledger.md`: built, measured (the cost numbers, the click and level numbers), **unverified** (phone CPU; how it sounds).
- [ ] Test link for Kerem (`node tools/serve.mjs 8765` + the cloudflared tunnel; ask Kerem before starting the tunnel); he runs `fsListen._gate(true)`. Merge `fx-1` → main and push **only on his OK**; verify live (fetch the deployed `web/core.wasm`, byte-identical to the local one; `FS_URL=https://keremaltaylar.github.io/Fieldscape/ node core/tests/web_fx.mjs` once); tag `v8.0-masterfx-live`; `node --env-file=.env.local tools/backup.mjs v8.0-masterfx-live`; copy the APK into the backup; update `Notes/FieldArc/Fieldscape-Features.md` in the vault.

---

# Part 2 — the Loop device (branch `fx-2`)

### Task 1: FX on a mix slot

**Files:** `core/mix.cpp`, `core/fieldscape.h`, `core/devices/stretch.cpp`, `core/test.cpp`, both build scripts.

**Interfaces (Produces):**
```c
void fs_mix_set_fx(fs_mix *m, int slot, const char *fx_json);   /* a point's sound.fx; NULL, "" or fx.on false: off */
void fs_mix_fx_walk(fs_mix *m, int slot, float along, float pace, float near);
void fs_mix_fx_clock(fs_mix *m, double bpm, double beats, const float *chord5, float root, int serial);
int  fs_mix_fx_live(fs_mix *m, int slot, float *out32);        /* 0: this slot has no engine now */
void fs_point_fx_follow(fs_mix *m, fs_device *piece);          /* tempo, beats and chord from the piece -> fs_mix_fx_clock (stretch.cpp, beside fs_stretch_chord) */
double fs_piece_beats(fs_device *d);
```
- `fs_mix_prepare` allocates `FX_POINTS = 2` engines. A slot holds a pending `mfx::Cfg` + an atomic sequence number: `fs_mix_set_fx` (any thread) writes the copy and bumps it; the audio thread takes it when the number changed and is stable (seqlock) — no lock, no allocation.
- `fs_mix_process`, per block: slots with `on` and `wet` > 0 are ranked by `gain.target`; the top two hold the engines (an engine moves only after its previous slot's wet has ramped to 0 and it has been `reset()`); the rest run dry. A slot's signal: device out → EQ → `dry·cos + wet·sin` (the wet `Smoothed`, 350 ms when the cap moves it, 30 ms for an edit) → grit → low-pass → gain. An engine whose slot's gain has been 0 for 0.5 s is `reset()` and released.
- `fs_point_sound` gains: `fs_mix_set_fx(mix, slot, dump(q.get("fx")))`.
- [ ] **Step 1: Failing tests:** a slot with no `fx`: output byte-identical to today; `fx.on` + wet 0.5 + echo: wet present, level within ±3 dB of dry across wet 0→1 on the typical preset; Review Focus 2: three slots on → `fs_mix_fx_live` returns 1 for exactly the two loudest; drop the loudest's gain to 0 → the third gets an engine within 1 s, click metric on the mix output throughout; Review Focus 1: slot A rings a 20 s Space tail, its gain goes to 0, `fs_mix_set_fx` for a new point on the same slot and gain back up → from the first non-zero sample the output contains nothing of A (render the new point alone as the reference: difference under −80 dB); 1000 `fs_mix_set_fx` calls from a second thread during processing: no crash, final config applied (native build only); junk JSON → off.
- [ ] **Step 2–5:** RED, implement, GREEN, exports added, commit `fx 2: fx on a mix slot - two engines, nearest win`.

### Task 2: The web engine feeds it

**Files:** `core/engine.cpp`, `web/core-worklet.js`.
- [ ] `fs_engine_step`: per sounding slot `fs_mix_fx_walk(mix, s, proj.t or 0, pace, fs_point_proximity(d, radius))`; pace = metres between successive steps / seconds between them (engine frames), clamped 0–2 m/s, 0 when the step jumped > 50 m (a scrub or a GPS jump is not walking); the same pace to `fs_piece_pace`. `fs_point_fx_follow(mix, piece)` where `feed_chord` runs. `fs_engine_fx_live(e, 1, out)` → the watched point's slot (`fs_engine_watch`). Worklet `live()` posts `pfx` when watching. Test in `core/tests/engine_walk.cpp`: a walk toward a Loop point raises `near` 0→1 smoothly; a 2 km jump leaves pace at 0. Commit `fx 2: the walk drives a point's fx`.

### Task 3: The Loop panel mock — HUMAN GATE (C-13)

**Files:** Create `design/fx/loop-mock.html`.
- [ ] Frames: the card's device switch with six devices (fits at 1440 and at 390 wide — measure; if six do not fit a phone row, a second row, not a scroll); the Loop panel: waveform with the loop playhead, This point, dry/wet, the FX editor component from the approved master mock (no sends/return; `near` labelled "closeness to this point"), level meter, load; states: no recording, sound off, "two other points' effects are sounding — this one plays dry until you come nearer"; the listener card (web) and the two app cards saying LOOP. Measure, contrast, screenshot, **Kerem's yes**, ledger, commit `fx 2: the loop mock, approved by Kerem`.

### Task 4: Loop on the page

**Files:** `index.html` (a sixth `data-amode="loop"` button; the switch handler: `loop` → `q.shape.plain = 1`, `q.fx = q.fx || {}`, `q.fx.on = true`; any other → `delete q.shape.plain` unless `plain`, `if (q.fx) q.fx.on = false`; `renderLoopPanel(f, q, commitQ, body)` beside `renderPlainPanel`, mounting `FsFx` with `kind: "point"`; the mode string at the `mode =` line near 15326 gains `q.audio_mode === "loop" && q.has_audio ? "loop"`; the three `mode:` maps near 7418/7443 unchanged), `web/listener.js` (the waveform list gains `"loop"`; the card note), `core/tests/web_loop.mjs`.
- [ ] **Step 1: Failing browser test** (as `web_plain.mjs`: a draft point, a WAV generated in the page): set to Loop → `shape.plain` 1 and `fx.on` true stored; its panel has the waveform, This point, dry/wet and the editor; with wet 0 the output's level equals Recording's on the same file ± 0.5 dB; wet 1 + Echo → the engine's `pfx[23]` > 0 and the output differs from Recording's; back to Stretch → `fx.on` false, `plain` gone, settings still stored, and Loop again restores them; back to Recording → plain and dry; the empty state; fit at 1440×900 and 1024×700; screenshots `22-loop.png`.
- [ ] **Step 2–5:** RED, build to the mock, GREEN (+ `web_plain`, `web_melody`, `web_mixer`, `npm test`), commit `fx 2: loop, the sixth point device`.

### Task 5: The apps

**Files:** `Walk.kt`, `Cards.kt`, `Core.kt`, `jni.cpp`, `Walk.swift`, `Cards.swift`, `App.swift`.
- [ ] Mode mapping gains `m == "loop" && has -> "loop"` (before the `has -> "stretch"` arm) in `Walk.kt` and `Walk.swift`; card note "Its recording looped through its own slow-moving effects - heard from wherever you are."; JNI `FN(fxWalk)(slot, along, pace, near)` + `Core.fxWalk`, Swift `fxWalk(slot:…)`, both called beside the existing `lowpass(slot, …)` call with the proximity already computed there and a pace computed as Task 2's rule; `fs_point_fx_follow(mix, piece)` beside the existing `fs_stretch_chord` loop (`jni.cpp:178`, `App.swift:184`). `fs_point_sound` already carries `fx`. `TEST_BUILD = 24`; wasm, APK (`Fieldscape-test-24.apk`), iOS build rc 0. Unit test: the mode mapping (`CompSummaryTest.kt`'s neighbour, a new `ModeTest.kt`). Commit `fx 2: the apps play loop (Test 24)`.

### Task 6: Verify, review, release

- [ ] As Part 1 Task 13 (run.py once, the browser suites once, whole-branch review, ledger with unverified = phone CPU with two Loop points + the master, test link, merge on Kerem's OK, verify live, tag `v8.1-loop-live`, backup + APK, features note).

---

# Part 3 — the LFOs: targets and the live visual (branch `fx-3`)

### Task 1: Destinations and the live read

**Files:** `core/piece.cpp`, `core/fieldscape.h`, `core/engine.cpp`, `core/test.cpp`, both build scripts, `web/core-worklet.js`.

**Interfaces (Produces):** `int fs_piece_mod(fs_device *d, float *out, int max)` — for each morph of the playing route **by its index in the patch's list** (off ones included): `out[4i]` on (0/1), `[4i+1]` value 0–1, `[4i+2]` phase 0–1 (the fraction of the unwrapped phase), `[4i+3]` its destination's value now in that parameter's own unit (NaN: no destination); returns the count, −1 when no route plays. `fs_engine_mod(e, out, max)` wraps it. The worklet's `live()` posts `mod`.
- `DEST_KEYS` appends, in this order: `sect.gain`, `v3.gain`, `fx.delayWet`, `fx.delayFb`, `fx.revWet`, `fx2.delayWet`, `fx2.delayFb`, `fx2.revWet`, `fx3.delayWet`, `fx3.delayFb`, `fx3.revWet`, `mfx.ret`, `mfx.send1`, `mfx.send2`, `mfx.send3`, then `mfx.` + every `cont` row of `mfx::PARAMS` in table order. Mappings (0–1 → value): gains `0.25 + 0.75·m` (as `voice.gain`); `delayWet`, `revWet`, sends, `ret` `m`; `delayFb` `0.85·m`; `mfx.*` the row's range on its taper. They ramp as their neighbours do (0.2–0.3 s) and are written where the stored value is written today (one writer). `mfx.*` enters `Engine::resolve` as the base value.
- [ ] **Step 1: Failing tests:** every old destination index unchanged (0–15 by name); a morph on `fx.delayFb` (ramp shape) sweeps the pad's echo feedback 0→0.85 (read back through a test getter) and its output's echo level follows; on `v3.gain`; on `mfx.echo.fb` together with a matrix slot on the same parameter → resolved = morph base + offset, clamped; two morphs on one new destination average; an unknown destination string → −1, nothing moves; `fs_piece_mod` returns the list's indexes including an off morph, phase advances 1/period per second, slot 3 equals the parameter read back; `fs_piece_morphs` output for a step-4 route is byte-identical to main; `test_kept` unchanged; patch read ≤ 1 ms with 24 morphs on new destinations.
- [ ] **Step 2–5:** RED, implement, GREEN, exports, commit `fx 3: morph destinations for every voice and the master fx; the live read`.

### Task 2: The LFO row mock — HUMAN GATE (C-13)

**Files:** Create `design/fx/lfo-mock.html`.
- [ ] The Morphs section of the patch screen at 1440×900 with six morphs: each row's scope (the drawn period + moving playhead; drift/tide scrolling under a centre playhead, labelled), value bar, **live target readout in units**, the grouped destination menu open (Voice 1 / Voice 2 / Voice 3 / Master FX › module); animated in the mock by a fake clock so Kerem sees the motion. States: sound off (preview, tagged), not the playing route, morph off, no destination, morphs all off, 24 morphs (fits by a toggle or columns — measured, no scrollbar), reduced motion. Measure, contrast, screenshot + a short screen recording or GIF of the motion, **Kerem's yes**, ledger, commit `fx 3: the lfo mock, approved by Kerem`.

### Task 3: The LFO rows on the page

**Files:** `index.html` (`MORPH_DESTS` and `MORPH_DEST_KEYS` grow in the core's order — one list, the second derived from the first so they cannot drift again; `destLabel` and the grouped `<optgroup>` menu, Master FX labels from `FsFxModel.destinations()`; `renderMorphs`' canvas replaced by `FsFx.scope` + bar + readout; `traceDraw` replaced by a draw from `core.live.mod` when this route is the playing one, else the preview clock; "Add a morph" still picks a free destination), `core/tests/web_fx.mjs` (extended) or `web_patch.mjs`.
- [ ] **Step 1: Failing browser test:** the menu lists every `DEST_KEYS` entry of the core (read `core/piece.cpp` + `core/modfx.hpp` in the test) in the same order under four groups; choosing `mfx.echo.fb` stores it and the readout shows "echo feedback · 0.xx" changing over 2 s with sound on; the playhead's x equals `mod[4i+2]` × width ± 2 px on two samples; with sound off the row is tagged preview and still moves; a morph turned off goes flat; an old route with a stale destination still shows "choose a parameter…"; fit unchanged at 1440×900 and 1024×700 with 6 and with 24 morphs; screenshot `23-lfo.png`.
- [ ] **Step 2–5:** RED, build to the mock, GREEN (+ `web_patch`, `web_compose`, `npm test`), commit `fx 3: the lfos show where they are and what they move`.

### Task 4: The apps' cells, verify, release

- [ ] Android `Cells.kt` and iOS `Cells.swift` read `o[1]` (the voice, for the hue) from `fs_piece_morphs`: confirm a cell aimed at an `mfx.*` or `fx*.` destination draws (hue of voice 0 for `mfx.*`, its voice for `fx`/`fx2`/`fx3` — set in `fs_piece_morphs`' `o[1]`, tested in Task 1) — no app code expected; `TEST_BUILD = 25`, builds. Then verify/review/release as Part 1 Task 13; tag `v8.2-lfos-live`.

---

# Part 4 — the markers (branch `fx-4`; independent — may run before Part 1 if Kerem wants icons first)

### Task 1: The markers mock — HUMAN GATE (C-13)

**Files:** Create `design/fx/icons-mock.html`.
- [ ] The seven badges (Stretch, Rhythm, Grains, Melody, Recording, Loop, quiet) from the spec's proposals, each at 26, 18 and 13 px (the zoom-out sizes: `icon-size` 1.0, 0.7, 0.55), sounding (lamp) and quiet (dark), on swatches of the three map bases the app uses (read them from `index.html`'s style list), in a row of all seven so they can be told apart at 13 px; two or three alternative sets if the first is not distinct; **both web options** side by side on a map crop — (A) the device badge as the marker, nature icon on the card; (B) the badge with the nature glyph inside. Contrast of ink/fill/ring against each base measured (C-7, ≥ 3:1 for graphics). Screenshot, **Kerem's yes and his choice of A or B**, ledger, commit `fx 4: the markers mock, approved by Kerem`.

### Task 2: The markers on the web

**Files:** Create `web/markers.js` (`window.FsMarkers = { GRID: 26, glyphs: { stretch: { badge: "lozenge"|"square"|"circle"|"diamond"|"ring", prims: [["line", x1, y1, x2, y2], ["circle", cx, cy, r, fill01], ["poly", [x, y, …]], ["cubic", x0, y0, c1x, c1y, c2x, c2y, x1, y1]] }, … }, image(id, quiet, ratio) }`), `tests/markers.test.mjs`; modify `index.html` (`buildIcons` adds `mk-<device>` and `mk-<device>-quiet` images; a `point-device` symbol layer with the `icon-image` case: `hits` → rhythm, `grains`, `melody`, `loop`+audio, `plain`+audio → recording, audio → stretch, else quiet; `icon-size` zoom 10 → 0.55, 14 → 1.0; the `point-icon` layer per Kerem's A/B choice), `web/listener.js` if it builds its own layer.
- [ ] Tests: every glyph's primitives lie inside the 26 grid; seven ids; the `icon-image` expression returns the right id for a fixture of each device (evaluate the case by hand in the test). Browser (extend `web_listener.mjs` or `web_fx.mjs`): `map.hasImage` for all 14; a rendered draft point of each device queries back its id (`queryRenderedFeatures` layout); screenshot `24-markers.png`. Commit `fx 4: one marker per device on the web map`.

### Task 3: The markers in the apps

**Files:** `android-app/.../Icons.kt` (`Markers.ids` = the seven; the glyph table as Kotlin data, number for number from `web/markers.js`; one renderer over the four primitives replaces the `when`), `MainActivity.kt:582` (the same case expression as the web), `ios/MapView.swift:311` and `:383` likewise.
- [ ] `tests/markers.test.mjs` grows: the numeric tables extracted from `Icons.kt` and `MapView.swift` equal `web/markers.js`'s, glyph by glyph; the three `icon-image` expressions are the same JSON. `TEST_BUILD = 26`; builds; emulator screenshots (`npm run emulator`) of a place with every device, compared with the mock by eye **and** stated as such. Commit `fx 4: the same markers on Android and iOS (Test 26)`.

### Task 4: Verify, review, release

- [ ] As Part 1 Task 13 (no `run.py` needed if `core/` is untouched — say so in the ledger); tag `v8.3-markers-live`; backup + APK.

---

## Unverified until Kerem's devices say otherwise

Phone CPU for the master + two Loop points (the estimate is 3–4× desktop); how any of it sounds (every number above is a meter, not an ear); the markers' legibility in sunlight. Each part's ledger repeats its own list.
