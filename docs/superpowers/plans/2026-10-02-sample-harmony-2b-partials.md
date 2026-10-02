# Sample harmony 2b — Harmonic filter and Formant (Bank / Spectral / Comb) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two new non-pitch synths — Harmonic filter and Formant — each playable three ways (Bank, Spectral, Comb) and two ways in time (Dry, Ringing), on the lab bench beside the Resonator.

**Architecture:** The 2a `sampler::Resonator` already owns the voice system (24 voices, steals, envelopes, level matching); it gains `synth`, `method`, `mode` (per note, like Body/Excite) and three per-voice engines dispatched from `resonate()`. With `synth == RESONATE` every sample is computed exactly as in 2a (a locked output hash proves it). The bench gets three parameters, the lab two menus. Each method lands green and published to lab2 before the next.

**Tech Stack:** C++17 core (`core/samplers.hpp`, `core/bench.cpp`), the core's `FFT` (`core/devices/fft.hpp`), em++ → wasm (`core/tests/run.py`, `web/build-lab.sh`), the lab page (`lab.html`, `web/lab.js`), Puppeteer browser checks (`core/tests/web_lab.mjs`), lab2 on claude.ai (`scratch/make_lab2.py`).

**Spec:** `docs/superpowers/specs/2026-10-02-non-pitch-2b-partials-design.md` (P1–P5).

## Global Constraints

- The recording is the only input; nothing is synthesised (parent spec; P2 "harmonic partials over the fundamental").
- `synth == RESONATE` renders bit-identically to 2a (P5: "the Resonator stays exactly as accepted").
- Partials: n = 1 … min(24, ⌊0.45·sr / f⌋); none at or above 0.45·sr (no fold-back).
- Harmonic weight `n^(−(2 − 2·Colour))`. Formant: peak at partial `1 + 15·Colour`, half-width `1 + 3·(1 − Focus)` partials, raised-cosine bump over a −24 dB floor (0.063).
- Bandwidth: Dry `max(73 Hz, 0.5·f·(1 − Focus))`; Ringing `2.2 / T60`, T60 = `0.2·50^Focus` s.
- Spectral: 2048-point Hann frames, hop 512, Hann synthesis, OLA scale 1/1.5, read ahead (no added delay).
- Comb: delay one period (fractional all-pass tuning as 2a), Colour low-pass in the delayed path; Ringing = the 2a String loop; Formant adds a +18 dB peak (×7.9) at the peak partial.
- Shared: 24 voices, steal rules, 50 ms steal fade, S-curve attack ≥ 8 ms, release ≤ 10 s, Tune glide, Synth/Method/Mode/Focus/Colour per note.
- Level within 1 dB of the recording; Dry silent (−60 dB) within 30 ms (Bank, Comb) / 43 ms (Spectral); Ringing T60 within 20 %.
- Budget: 24 voices of each method < 1.33 ms per 128-sample block (×1.5 in the −O1 test build).
- No clicks (A-2/A-3), no runaway, silence on no recording.
- The live site is untouched; lab2 (private claude.ai page) is where Kerem listens.

## Review Focus

1. **A gust and a lull in a real recording** — the slow level-matching (3 s) must not boost a lull into a swell nor flatten gusts shorter than ~1 s. Pinned in Task 2 (onset swell test; Dry decay test proves no boost in silence).
2. **Changing Method/Mode/Synth while notes sound** — sounding notes keep theirs (bit-identical), as Body/Excite. Pinned in Task 2.
3. **High notes with few partials** (a 3.5 kHz note has 3 partials below 0.45·sr) — must still sound and stay level-matched, never fold. Pinned in Task 2 (pitch sweep to 3.5 kHz, stability sweep to 12 kHz).
4. **Low Dry notes** — partials closer than 73 Hz blur (physics, spec); pitch is checked from 220 Hz in Dry, from 55 Hz in Ringing. Pinned in Task 2.
5. **Spectral in budget when many voices start at once** — frames staggered across voices; budget test starts all 24 together. Pinned in Task 4.

---

### Task 1: Lock the Resonator, add Synth / Method / Mode

**Files:**
- Modify: `core/samplers.hpp` (enums, fields, per-voice copies, dispatch hooks)
- Modify: `core/bench.cpp` (params 8–10)
- Test: `core/test.cpp`

**Interfaces:**
- Produces: `sampler::Synth { RESONATE=0, HARMONIC=1, FORMANT=2 }`, `sampler::Method { BANK=0, SPECTRAL=1, COMB=2 }`, `sampler::Mode { DRY=0, RINGING=1 }`; `Resonator::synth, method, mode` (int); `Voice::synth, method, mode`; bench params 8 synth, 9 method, 10 mode.

- [ ] **Step 1: Write the guard test** — after the 2a resonator blocks in `core/test.cpp`:

```cpp
    {   /* 2b guard: the Resonator renders exactly as in 2a (P5) - a hash of every body x excite */
        const std::vector<float> wind = noise_src(3, 0.5f, 77); const float *p[1] = { wind.data() };
        uint64_t h = 1469598103934665603ull;
        for (int body = 0; body < 3; body++) for (int ex = 0; ex < 2; ex++) {
            sampler::Resonator r; r.init(48000); r.set_source(1, (long long)wind.size(), p);
            r.body = body; r.excite = ex; r.focus = 0.7; r.colour = 0.4;
            r.attack(196, 0, 0.5); r.release(1.2);
            std::vector<float> a(48000 * 2, 0.0f), b(a.size(), 0.0f);
            for (size_t i = 0; i < a.size(); i += 128) r.render(a.data() + i, b.data() + i, 128, i / 48000.0);
            for (float s : a) { uint32_t u; std::memcpy(&u, &s, 4); h = (h ^ u) * 1099511628211ull; }
        }
        std::printf("resonator 2a hash %016llx\n", (unsigned long long)h);
        assert(h == 0x0000000000000000ull);   /* Step 2 replaces this with the printed value */
    }
```

- [ ] **Step 2: Run, read the hash, pin it** — `python core/tests/run.py; node build/stretch-tests/core_test.js | grep "2a hash"`. Expected: FAIL at the assert, printing the hash. Replace `0x0000000000000000ull` with the printed value; rerun → passes. (This is a characterisation lock taken *before* any change.)

- [ ] **Step 3: Add the enums, fields and per-voice copies** in `core/samplers.hpp`:

```cpp
enum Synth { RESONATE = 0, HARMONIC = 1, FORMANT = 2 };
enum Method { BANK = 0, SPECTRAL = 1, COMB = 2 };
enum Mode { DRY = 0, RINGING = 1 };
```
In `Voice`: `int synth = 0, method = 0, mode = 0;`. In `Resonator`: `int synth = RESONATE, method = BANK, mode = DRY;`. In `start()` beside `x.body = body; x.excite = excite;`: `x.synth = synth; x.method = method; x.mode = mode;`.

- [ ] **Step 4: Bench params** in `core/bench.cpp`: append to `BENCH_PARAMS`

```cpp
    { "synth", "Synth", "", 0.0f, 2.0f, 0.0f },        /* Resonator, Harmonic filter, Formant */
    { "method", "Method", "", 0.0f, 2.0f, 0.0f },      /* Bank, Spectral, Comb */
    { "mode", "Mode", "", 0.0f, 1.0f, 0.0f },          /* Dry, Ringing */
```
`params()` returns `n = 11`; `set_param` gains `case 8: res.synth = (int)std::lround(v); break; case 9: res.method = ...; case 10: res.mode = ...;`.

- [ ] **Step 5: Run** `python core/tests/run.py` and the core test. Expected: 19/19, hash unchanged, `core ok`.

- [ ] **Step 6: Commit** `git add core/samplers.hpp core/bench.cpp core/test.cpp && git commit -m "2b: Synth / Method / Mode on the sampler and the bench; the Resonator locked by a hash"`

---

### Task 2: Bank — Harmonic filter and Formant, Dry and Ringing

**Files:**
- Modify: `core/samplers.hpp`
- Test: `core/test.cpp`

**Interfaces:**
- Consumes: Task 1 enums/fields.
- Produces: `sampler::partial_weight(int synth, int n, double colour, double focus)`, `sampler::band_hz(int mode, double f, double focus, double T60)`, `Voice` bank state (`np, pb0[24], pa1[24], pa2[24], py1[24], py2[24], pw[24]`), `Resonator::bank(Voice&, double)`, slow level matching for partial synths (`KA_SLOW_S = 3`).

- [ ] **Step 1: Write the method test helper and block** in `core/test.cpp` (after the guard). It runs for every method in `methods` — Tasks 4 and 5 add theirs to this list.

```cpp
static std::vector<float> part_render(int synth, int method, int mode, double focus, double colour, double tune, double f,
                                      double secs, const std::vector<float> &src, double off_at = 1e300) {
    sampler::Resonator r; r.init(48000);
    r.synth = synth; r.method = method; r.mode = mode; r.focus = focus; r.colour = colour; r.tune = tune; r.att = 0.02; r.rel = 0.03;
    const float *p[1] = { src.data() }; r.set_source(1, (long long)src.size(), p);
    r.attack(f, 0.0, 0.5); r.release(off_at);
    std::vector<float> L((size_t)(secs * 48000), 0.0f), R(L.size(), 0.0f);
    for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, (int)std::min<size_t>(128, L.size() - i), i / 48000.0);
    return L;
}
```
```cpp
    {   /* 2b: the Harmonic filter and the Formant, every method in `methods` (spec "How we know it works") */
        const int methods[] = { sampler::BANK };
        const std::vector<float> wind = noise_src(16, 0.5f, 101);
        std::vector<float> gust(48000 * 6, 0.0f); for (size_t i = 0; i < 48000 * 3; i++) gust[i] = wind[i];   /* 3 s, then silence */
        auto rms = [](const std::vector<float> &x, double a, double z) { double e = 0; size_t i0 = (size_t)(a * 48000), i1 = (size_t)(z * 48000); for (size_t i = i0; i < i1; i++) e += (double)x[i] * x[i]; return 10 * std::log10(e / (i1 - i0) + 1e-30); };
        for (int m : methods) {
            const double tol = m == sampler::COMB ? 3 : 1;
            for (int syn : { sampler::HARMONIC, sampler::FORMANT }) {
                /* pitch: Ringing from 55 Hz, Dry from 220 Hz (closer partials blur in Dry - spec) */
                double worst = 0;
                for (int mode : { sampler::DRY, sampler::RINGING })
                    for (double f : { 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0 }) {
                        if (mode == sampler::DRY && f < 220) continue;
                        double got = centre_near(part_render(syn, m, mode, 0.9, syn == sampler::FORMANT ? 0.0 : 0.5, 1, f, 12, wind), 48000, f, 8);
                        worst = std::max(worst, std::fabs(1200 * std::log2(got / f)));
                    }
                std::printf("2b method %d synth %d: worst pitch error %.2f cents\n", m, syn, worst);
                assert(worst <= tol);
                /* Dry follows the recording: down 60 dB within 30 ms (Spectral: one frame, 43 ms) of silence */
                std::vector<float> d = part_render(syn, m, sampler::DRY, 1.0, 0.5, 1, 440, 5, gust);
                const double lim = m == sampler::SPECTRAL ? 0.043 : 0.030, drop = rms(d, 2.5, 3.0) - rms(d, 3.0 + lim, 3.0 + lim + 0.01);
                std::printf("2b method %d synth %d: Dry down %.1f dB %.0f ms after the recording stops\n", m, syn, drop, lim * 1000);
                assert(drop >= 60);
                /* Ringing sustains its Focus T60 within 20 % */
                std::vector<float> g = part_render(syn, m, sampler::RINGING, 0.5, 0.5, 1, 440, 6, gust);
                std::vector<float> tail(g.begin() + 3 * 48000, g.end());
                double t60 = t60_of(tail), want = sampler::Resonator::t60(0.5);
                std::printf("2b method %d synth %d: Ringing T60 %.2f s (want %.2f)\n", m, syn, t60, want);
                assert(std::fabs(t60 / want - 1) <= 0.2);
                /* level within 1 dB of the recording, settled; and no swell at the onset (first 3 s <= settled + 3 dB) */
                for (int mode : { sampler::DRY, sampler::RINGING }) {
                    std::vector<float> o = part_render(syn, m, mode, 0.7, 0.5, 1, 220, 15, wind), dry = part_render(syn, m, mode, 0.7, 0.5, 0, 220, 15, wind);
                    double set = rms(o, 10, 14), ref = rms(dry, 10, 14), onset = -200;
                    for (double s = 0; s < 3; s += 0.25) onset = std::max(onset, rms(o, s, s + 0.25));
                    std::printf("2b method %d synth %d mode %d: level %.2f dB vs the recording %.2f, onset peak %.2f\n", m, syn, mode, set, ref, onset);
                    assert(std::fabs(set - ref) <= 1 && onset <= set + 3);
                }
            }
            /* Harmonic filter: Colour 1 lifts partials 4-8 over the fundamental by >= 12 dB against Colour 0 */
            auto lift = [&](double col) {
                std::vector<float> o = part_render(sampler::HARMONIC, m, sampler::RINGING, 0.9, col, 1, 220, 10, wind);
                auto band = [&](double f) { std::vector<float> t(o.end() - 65536, o.end()); return 20 * std::log10(peak_amp(t, 48000, f)); };
                double hi = 0; for (int n = 4; n <= 8; n++) hi += band(220.0 * n) / 5;
                return hi - band(220);
            };
            double l0 = lift(0), l1 = lift(1);
            std::printf("2b method %d: Harmonic filter partials 4-8 vs fundamental %.1f dB at Colour 0, %.1f at 1\n", m, l0, l1);
            assert(l1 - l0 >= 12);
            /* Formant: among partials 2-16 the loudest is the one Colour names (+-1) */
            int ok = 0;
            for (int want : { 3, 6, 10, 14 }) {
                std::vector<float> o = part_render(sampler::FORMANT, m, sampler::RINGING, 0.9, (want - 1) / 15.0, 1, 110, 10, wind);
                std::vector<float> t(o.end() - 65536, o.end()); int best = 2;
                for (int n = 2; n <= 16; n++) if (peak_amp(t, 48000, 110.0 * n) > peak_amp(t, 48000, 110.0 * best)) best = n;
                ok += std::abs(best - want) <= 1;
            }
            std::printf("2b method %d: Formant peak on the named partial %d/4\n", m, ok);
            assert(ok == 4);
        }
    }
```
Add the helper `peak_amp` beside `peak_near` (the Hann-windowed DFT magnitude at f, one 65536-sample window):

```cpp
static double peak_amp(const std::vector<float> &x, double sr, double f) {
    double re = 0, im = 0; const size_t N = x.size();
    for (size_t i = 0; i < N; i++) { double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N), ph = 2 * 3.141592653589793 * f * i / sr; re += x[i] * w * std::cos(ph); im += x[i] * w * std::sin(ph); }
    return std::sqrt(re * re + im * im) / N + 1e-30;
}
```

- [ ] **Step 2: Run, watch it fail** — Expected: FAIL (the Harmonic synth renders as a String: pitch fine but "Formant peak 0–1/4", level or Dry decay fails).

- [ ] **Step 3: Implement Bank** in `core/samplers.hpp`.

Free functions after `solve_ap`:
```cpp
static const int PARTIALS = 24;
/* partial n's weight (2b spec, Colour): the Harmonic filter's overtone balance, or the Formant's peak on a partial */
inline double partial_weight(int synth, int n, double colour, double focus) {
    colour = std::fmin(1.0, std::fmax(0.0, colour)); focus = std::fmin(1.0, std::fmax(0.0, focus));
    if (synth == HARMONIC) return std::pow((double)n, -(2 - 2 * colour));
    const double p = 1 + 15 * colour, h = 1 + 3 * (1 - focus), d = std::fabs(n - p);
    return 0.063 + (1 - 0.063) * (d < h ? 0.5 + 0.5 * std::cos(PI * d / h) : 0);
}
/* a partial's bandwidth in Hz: Dry never narrower than a 30 ms decay (60 dB in 2.2 / B s); Ringing from T60 */
inline double band_hz(int mode, double f, double focus, double T60) { return mode == RINGING ? 2.2 / T60 : std::fmax(73.0, 0.5 * f * (1 - focus)); }
```
`Voice` gains: `int np = 0; double pb0[PARTIALS] = {}, pa1[PARTIALS] = {}, pa2[PARTIALS] = {}, py1[PARTIALS] = {}, py2[PARTIALS] = {}, pw[PARTIALS] = {};`

In `start()`, after the per-voice resets and before `if (body == BELL)`:
```cpp
        if (synth != RESONATE) { start_partials(x, f, T); return; }
```
New member:
```cpp
    /* Harmonic filter / Formant (2b): the engines' state, and the level expected for a white input, so the slow
       level matching starts close (no swell while it settles) */
    void start_partials(Voice &x, double f, double T) {
        const double B = band_hz(x.mode, f, focus, T);
        double G = 0; x.np = 0;
        if (x.method == BANK) {
            const double r = std::exp(-PI * B / sr);
            for (int n = 1; n <= PARTIALS && n * f < 0.45 * sr; n++, x.np++) {
                const int k = x.np; const double wn = 2 * PI * n * f / sr;
                x.pa1[k] = -2 * r * std::cos(wn); x.pa2[k] = r * r; x.pb0[k] = (1 - r) * std::abs(1.0 - r * std::polar(1.0, -2 * wn));
                x.py1[k] = x.py2[k] = 0; x.pw[k] = partial_weight(x.synth, n, colour, focus);
                G += x.pw[k] * x.pw[k] * PI * B / sr;              /* a peak-1 two-pole band passes pi B / sr of white power */
            }
        }
        x.agc = G > 0 ? std::fmin(1000.0, 1 / std::sqrt(G)) : 1;
    }
    double bank(Voice &x, double in) {
        double y = 0;
        for (int k = 0; k < x.np; k++) {
            double o = x.pb0[k] * in - x.pa1[k] * x.py1[k] - x.pa2[k] * x.py2[k];
            if (std::fabs(o) < 1e-20) o = 0;
            x.py2[k] = x.py1[k]; x.py1[k] = o; y += x.pw[k] * o;
        }
        return y;
    }
```
In `resonate()` first line: `if (x.synth != RESONATE) return bank(x, in);` (Tasks 4–5 turn this into a dispatch).

In `render()`: `const double ks = 1 - std::exp(-1.0 / (3.0 * sr));` beside `ka`; the excitation/level code becomes:
```cpp
                const bool part = x.synth != RESONATE;
                double exc = in;
                if (!part && x.excite == PLUCKED) { ... unchanged ... }
                double wet = resonate(x, !part && x.excite == BOWED && x.body != BELL ? exc * std::sqrt(std::fmax(0.0, 1 - x.g60 * x.g60)) : exc);
                if (part || x.excite == BOWED) {
                    const double k = part ? ks : ka;               /* partial synths: 3 s, so gusts keep their shape */
                    x.rin += (exc * exc - x.rin) * k; x.rout += (wet * wet - x.rout) * k;
                    if (x.rin > 1e-10) { double tgt = x.rout > 1e-14 ? std::fmin(1000.0, std::sqrt(x.rin / x.rout)) : 1; if (!part || x.rout > 1e-14) x.agc += (tgt - x.agc) * k; }
                    wet *= x.agc;
                }
```
(`!part` paths are byte-for-byte the 2a expressions — the guard hash proves it.)

- [ ] **Step 4: Run** — Expected: the 2b block prints and passes for BANK; guard hash unchanged; 19/19. If Dry pitch at 220 Hz misses ±1 c by the band's skirt from partial 2, record the measured figure and ledger a ruling (spec tolerance vs physics) rather than widening silently.

- [ ] **Step 5: Mid-note change and stability, extend** — in the existing "Body / Excite changed mid-note" test add `r.synth = sampler::FORMANT; r.method = sampler::BANK; r.mode = sampler::RINGING;` beside the body/excite change (still bit-identical). In the stability sweep add a loop over `synth ∈ {HARMONIC, FORMANT} × mode × colour {0,0.5,1} × f {5, 55, 3500, 12000}` with Focus 1 on full-scale noise for 2 s: every sample finite and `|s| < 4`. Add the budget test: 24 voices of `HARMONIC`/`BANK`/`RINGING` at `55·(k+1)` Hz, 99.9 % of blocks `< 1.33 * slack` ms (same code as the Resonator's).

- [ ] **Step 6: Run** — Expected: all pass; record the Bank budget figure.

- [ ] **Step 7: Commit** `git commit -am "2b Bank: the Harmonic filter and the Formant, Dry and Ringing, level-matched"`

---

### Task 3: The bench and the lab menus (then lab2)

**Files:**
- Modify: `lab.html`, `web/lab.js`
- Test: `core/tests/web_lab.mjs`

**Interfaces:**
- Consumes: bench params 8 synth, 9 method, 10 mode.
- Produces: lab synth values `"retune" | "resonator" | "harmonic" | "formant"`; `#lab-part` (Method, Mode); `#lab-colour-label`.

- [ ] **Step 1: Failing browser checks** — before "no page errors" in `core/tests/web_lab.mjs`:

```js
  /* 2b: the Harmonic filter and the Formant on the bench */
  await ev(`fsLab.load(${NOISE}).then(function () { return 1; })`);
  check("the Synth menu offers the Harmonic filter and the Formant", await ev("['harmonic','formant'].every(function (v) { return !!document.querySelector(\"#lab-synth option[value='\" + v + \"']\"); })"), null);
  await ev("fsLab.setSynth('harmonic'), fsLab.engineReady");
  check("... with Method and Mode beside Focus, and Body/Excite hidden", await ev("!document.querySelector('#lab-part').hidden && document.querySelector('#lab-res').hidden"), null);
  check("Colour reads Overtones for the Harmonic filter", /Overtones/.test(await ev("document.querySelector('#lab-colour-label').textContent")), null);
  const hc = await ev("fsLab.play('chord', { tuning: 'just', step: 0 })");
  await sleep(2500);
  const hm = await ev("fsLab.created()");
  await ev("fsLab.stop()");
  check("the Harmonic filter creates pitch on a noise file (each note >= 10 dB)", hm && hm.length === hc.hz.length && hm.every(function (d) { return d >= 10; }), hm);
  await ev("fsLab.setSynth('formant'), 0");
  await ev("(function(){ var s = document.querySelector('#lab-colour'); s.value = '0.2667'; s.dispatchEvent(new Event('input')); return 1; })()");
  check("Colour reads Partial 5 for the Formant at 0.27", /Partial 5/.test(await ev("document.querySelector('#lab-colour-label').textContent")), await ev("document.querySelector('#lab-colour-label').textContent"));
  await ev("fsLab.setSynth('retune'), 0");
```

- [ ] **Step 2: Run** `node core/tests/web_lab.mjs` (server: `node tools/serve.mjs 8765`). Expected: the 2b checks FAIL.

- [ ] **Step 3: Markup** in `lab.html` — the synth menu and a split panel:

```html
<label>Synth <select id="lab-synth"><option value="retune" selected>Retune (pitched)</option><option value="resonator">Resonator</option><option value="harmonic">Harmonic filter</option><option value="formant">Formant</option></select></label>
<span id="lab-np" hidden>
  <span id="lab-res" hidden>
    <label>Body <select id="lab-body">…unchanged…</select></label>
    <label>Excite <select id="lab-excite">…unchanged…</select></label>
  </span>
  <span id="lab-part" hidden>
    <label>Method <select id="lab-method"><option value="0">Bank</option><option value="1">Spectral</option><option value="2">Comb</option></select></label>
    <label>Mode <select id="lab-mode"><option value="0">Dry</option><option value="1">Ringing</option></select></label>
  </span>
  <label>Focus <input type="range" id="lab-focus" min="0" max="1" step="0.01" value="0.5"></label>
  <label><span id="lab-colour-label">Colour</span> <input type="range" id="lab-colour" min="0" max="1" step="0.0001" value="0.5"></label>
</span>
```
CSS: `#lab-np:not([hidden]), #lab-res:not([hidden]), #lab-part:not([hidden]) { display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: center; }` (replaces the `#lab-res` rule).

- [ ] **Step 4: Script** in `web/lab.js`:

```js
  var ENGINE = { resonator: 0, harmonic: 1, formant: 2 };          /* bench param "synth" */
  var P_SYNTH = 8, P_METHOD = 9, P_MODE = 10;
  function colourLabel() {
    $("#lab-colour-label").textContent = synth === "harmonic" ? "Overtones" : synth === "formant" ? "Partial " + Math.round(1 + 15 * +$("#lab-colour").value) : "Colour";
  }
```
`sendParams()` adds `[P_SYNTH, ENGINE[synth] || 0], [P_METHOD, +$("#lab-method").value], [P_MODE, +$("#lab-mode").value]` to its list.
`setSynth(s)`:
```js
    synth = s; $("#lab-synth").value = s;
    var eng = s in ENGINE; $("#lab-np").hidden = !eng; $("#lab-res").hidden = s !== "resonator"; $("#lab-part").hidden = !(s === "harmonic" || s === "formant");
    colourLabel(); if (eng) { ensureEngine().then(sendParams); }
    gate();
```
Every `synth === "resonator"` test that means "an engine synth" becomes `synth in ENGINE` (lines `play()`, `gate()`); the unpitched-file message names "Resonator, Harmonic filter or Formant". The kept line: for `resonator` as now; for the two new ones `" " + ["bank","spectral","comb"][+$("#lab-method").value] + " " + ["dry","ringing"][+$("#lab-mode").value] + " focus … colour …"`. Listeners: `["#lab-body", "#lab-excite", "#lab-method", "#lab-mode"]` on change → `sendParams`; `#lab-colour` input also calls `colourLabel()`.

- [ ] **Step 5: Run** `W=/c/Users/kerem/AppData/Local/Temp/emb; EM_CONFIG=C:/Users/kerem/tools/emsdk/.emscripten PATH="$W:$PATH" sh web/build-lab.sh` then `node core/tests/web_lab.mjs`. Expected: all pass (44 + 6).

- [ ] **Step 6: Commit, then lab2** — `git add lab.html web/lab.js web/core-lab.wasm core/tests/web_lab.mjs && git commit -m "Lab: Harmonic filter and Formant - Method, Mode, Overtones / Partial N"`; `python scratch/make_lab2.py`; run the lab2 variant of the browser test (URL `lab2.html`, worklet `web/lab2-worklet.js`; the "versioned address" check is expected to fail on the name); republish the lab2 artifact (`lab2.html` with `web/lab2.js`, `web/lab2-wasm.js`, `web/lab2-worklet.js`).

---

### Task 4: Spectral

**Files:**
- Modify: `core/samplers.hpp`
- Test: `core/test.cpp`

**Interfaces:**
- Consumes: `partial_weight`, `band_hz`, `start_partials`, the dispatch in `resonate()`.
- Produces: `Resonator::spectral(Voice&)`, `Resonator::spectral_frame(Voice&)`, per-voice `ola, hold, ph, mask` (vectors sized in `init`), `sk, soff, sd`, shared `FFT fft` and scratch.

- [ ] **Step 1: Failing test** — `const int methods[] = { sampler::BANK, sampler::SPECTRAL };` and add a Spectral budget case (24 voices `HARMONIC`/`SPECTRAL`/`RINGING` started together). Run: Expected FAIL (Spectral renders as Bank or silence → pitch/Dry/level asserts).

- [ ] **Step 2: Implement** — `#include "devices/fft.hpp"` at the top of `samplers.hpp`; constants `static const int SPN = 2048, SPH = 512;`.

`Voice` gains: `std::vector<float> ola, hold, ph, mask; long long sk = 0; int soff = 0; double sd = 1; double pf[PARTIALS + 1] = {};` (pf: partial frequency per bin owner, see mask).
Also `std::vector<int> owner;` (bin → partial index, −1 none).

`init()`: for each voice `ola.assign(SPN, 0); hold.assign(SPN/2+1, 0); ph.assign(SPN/2+1, 0); mask.assign(SPN/2+1, 0); owner.assign(SPN/2+1, -1);` and the shared `fft.reserve(SPN); fft.plan(SPN); fft.twiddles(0, SPN); fb.assign(4 * SPN, 0); win.resize(SPN); win[j] = 0.5 − 0.5·cos(2πj/SPN)`.

`start_partials()`, `SPECTRAL` branch:
```cpp
        if (x.method == SPECTRAL) {
            std::fill(x.ola.begin(), x.ola.end(), 0.0f); std::fill(x.hold.begin(), x.hold.end(), 0.0f);
            std::fill(x.mask.begin(), x.mask.end(), 0.0f); std::fill(x.owner.begin(), x.owner.end(), -1);
            const double bin = sr / SPN, half = x.mode == RINGING ? 2.0 : std::fmax(3.0, 0.5 * B / bin);   /* bins */
            for (int n = 1; n <= PARTIALS && n * f < 0.45 * sr; n++) {
                const double c = n * f / bin, wn = partial_weight(x.synth, n, colour, focus);
                for (int k = std::max(1, (int)std::ceil(c - half)); k <= std::min(SPN / 2 - 1, (int)std::floor(c + half)); k++) {
                    const double m = wn * (0.5 + 0.5 * std::cos(PI * (k - c) / half));
                    if (m > x.mask[k]) { x.mask[k] = (float)m; x.owner[k] = n; }
                }
            }
            for (int k = 0; k <= SPN / 2; k++) G += x.mask[k] * x.mask[k] / (SPN / 2);
            x.pf[0] = f; x.sk = 0; x.soff = (int)((&x - v) * SPH / VOICES); x.sd = std::pow(10.0, -3.0 * SPH / (sr * T));
        }
```
Per sample and per frame:
```cpp
    /* one output sample; a frame is analysed when this voice's hop comes round (voices staggered across the hop,
       so 24 voices never all transform in one block). The recording is a file, so the frame reads ahead: no delay. */
    double spectral(Voice &x) {
        if ((x.sk + x.soff) % SPH == 0) spectral_frame(x);
        float &o = x.ola[(size_t)(x.sk % SPN)]; const double y = o; o = 0; x.sk++;
        return y;
    }
    void spectral_frame(Voice &x) {
        float *ar = fb.data(), *ai = ar + SPN, *br = ai + SPN, *bi = br + SPN;
        const long long base = x.pos - 1;                              /* this sample's place in the recording */
        for (int j = 0; j < SPN; j++) { ar[j] = src.at(base + j) * win[j]; ai[j] = 0; }
        auto fwd = [&]() { for (int p = 0; p < fft.passes; p++) { if (p % 2 == 0) fft.pass(p, ar, ai, br, bi, 0, fft.butterflies(p)); else fft.pass(p, br, bi, ar, ai, 0, fft.butterflies(p)); }
                           if (fft.passes % 2) { std::copy(br, br + SPN, ar); std::copy(bi, bi + SPN, ai); } };
        fwd();
        for (int k = 0; k <= SPN / 2; k++) {
            double re = 0, im = 0;
            if (x.mask[k] > 0) {
                const double m = x.mask[k], mag = std::hypot(ar[k], ai[k]) * m;
                if (x.mode == DRY) { re = ar[k] * m; im = ai[k] * m; }
                else {                                                  /* a spectral sustain at the partial's own frequency */
                    const double held = x.hold[k] * x.sd;
                    if (mag >= held) { x.hold[k] = (float)mag; x.ph[k] = (float)std::atan2(ai[k], ar[k]); }
                    else { x.hold[k] = (float)held; x.ph[k] = (float)std::fmod(x.ph[k] + 2 * PI * x.owner[k] * x.pf[0] * SPH / sr, 2 * PI); }
                    re = x.hold[k] * std::cos(x.ph[k]); im = x.hold[k] * std::sin(x.ph[k]);
                }
            }
            ar[k] = (float)re; ai[k] = (float)-im;                       /* conjugate: the inverse by a forward transform */
            if (k > 0 && k < SPN / 2) { ar[SPN - k] = (float)re; ai[SPN - k] = (float)im; }
        }
        fwd();
        const long long at = x.sk;
        for (int j = 0; j < SPN; j++) x.ola[(size_t)((at + j) % SPN)] += ar[j] / SPN * win[j] / 1.5f;
    }
```
Dispatch in `resonate()`: `if (x.synth != RESONATE) return x.method == SPECTRAL ? spectral(x) : bank(x, in);`.

- [ ] **Step 3: Run** — Expected: Spectral passes pitch (Ringing from 55 Hz exact by phase advance; Dry ≥ 220 Hz), Dry 43 ms, T60, level, Formant, budget. Record the Spectral budget figure. If Dry pitch misses ±1 c by the bin grid, raise the Dry half-width (≥ 3 bins already) before any ruling.

- [ ] **Step 4: Commit + lab2** — `git commit -am "2b Spectral: partials kept in the spectrum, read ahead, a spectral sustain for Ringing"`; rebuild `web/core-lab.wasm`, `node core/tests/web_lab.mjs`, `python scratch/make_lab2.py`, lab2 variant test, republish.

---

### Task 5: Comb

**Files:**
- Modify: `core/samplers.hpp`
- Test: `core/test.cpp`

**Interfaces:**
- Consumes: the 2a loop (`resonate` String path), `solve_ap`, `lp_delay`, `lp_gain`, `dc_*`.
- Produces: `Resonator::comb(Voice&, double)`; Formant peak state `fb0, fa1, fa2, fy1, fy2` on `Voice`.

- [ ] **Step 1: Failing test** — `methods` gains `sampler::COMB`; budget case for Comb. Run: Expected FAIL.

- [ ] **Step 2: Implement** — refactor the 2a String/Tube body of `resonate()` into `double loop(Voice &x, double in)` (same statements, unchanged order; `resonate()` calls it — the guard hash must stay). `start_partials()` COMB branch:
```cpp
        if (x.method == COMB) {
            std::fill(x.line.begin(), x.line.end(), 0.0f);
            if (x.mode == RINGING) {                     /* the 2a String loop, T60 from Focus */
                const int b = body; body = STRING; start_loop(x, f, T); body = b;
                G = 1;                                   /* input scaled by sqrt(1 - g60^2) in render, as bowed */
            } else {                                     /* feed-forward: the recording plus itself one period later */
                const double w = 2 * PI * f / sr, P = sr / f, col = std::fmin(1.0, std::fmax(0.0, colour));
                x.a = col >= 0.999 ? 0 : std::exp(-2 * PI * std::fmin(f * (1.5 + 30 * col * col), 0.45 * sr) / sr);
                const double lpd = lp_delay(x.a, w); int N = (int)std::floor(P - lpd - 0.2); N = std::max(1, std::min(N, (int)MASK - 2));
                x.N = N; x.c = solve_ap(P - N - lpd, w); G = 2;
            }
            if (x.synth == FORMANT) {                    /* one peak, +18 dB, at the named partial */
                const double fp = std::fmin((1 + 15 * colour) * f, 0.44 * sr), Bp = std::fmax(x.mode == DRY ? 73.0 : 20.0, (1 + 3 * (1 - focus)) * 0.5 * f), r = std::exp(-PI * Bp / sr), wp = 2 * PI * fp / sr;
                x.fa1 = -2 * r * std::cos(wp); x.fa2 = r * r; x.fb0 = (1 - r) * std::abs(1.0 - r * std::polar(1.0, -2 * wp)); x.fy1 = x.fy2 = 0;
            }
        }
```
where `start_loop(x, f, T)` is the existing String/Tube setup moved out of `start()` unchanged (the guard hash must stay).
```cpp
    double comb(Voice &x, double in) {
        double y;
        if (x.mode == RINGING) y = loop(x, in);
        else {
            double read = x.line[(x.w - (unsigned)x.N) & MASK];
            double ap = x.c * read + x.ap_x - x.c * x.ap_y; x.ap_x = read; x.ap_y = std::fabs(ap) < 1e-20 ? 0 : ap;
            x.lp = (1 - x.a) * x.ap_y + x.a * x.lp; if (std::fabs(x.lp) < 1e-20) x.lp = 0;
            x.line[x.w & MASK] = (float)in; x.w++;
            y = in + x.lp;
        }
        if (x.synth == FORMANT) {
            double o = x.fb0 * y - x.fa1 * x.fy1 - x.fa2 * x.fy2; if (std::fabs(o) < 1e-20) o = 0;
            x.fy2 = x.fy1; x.fy1 = o; y += 6.9 * o;
        }
        return y;
    }
```
Dispatch: `if (x.synth != RESONATE) return x.method == SPECTRAL ? spectral(x) : x.method == COMB ? comb(x, in) : bank(x, in);`. In `render()`, the bowed pre-scale also applies to `part && x.method == COMB && x.mode == RINGING` (`exc * sqrt(1 − g60²)`, as the 2a loop needs).

- [ ] **Step 3: Run** — Expected: Comb within ±3 c, Dry 30 ms (one period ≤ 18 ms at 55 Hz plus the 73 Hz peak), T60, level, Harmonic lift ≥ 12 dB (the Colour low-pass: a tilt), Formant peak 4/4, budget, stability sweep (Comb added to it), guard hash unchanged. If the Harmonic-lift or Formant check cannot hold for Comb (its balance is a tilt — spec), record the measured value and ledger a ruling rather than loosening silently.

- [ ] **Step 4: Commit + lab2** — `git commit -am "2b Comb: the recording plus itself a period later; Ringing is the 2a loop; a Formant peak after it"`; rebuild the lab wasm; all suites (`python core/tests/run.py`, `npm test`, `web_lab`, `web_listener`, `web_setter`, `web_parks`, `web_drafts`, `web_sound`, `web_patch`, `web_remote_delete`, `web_core`); `python scratch/make_lab2.py`; lab2 variant test; republish lab2.

---

## Self-review notes

- Spec coverage: P1 (Dry/Ringing) T2; P2/P3 (partials, one peak) T2 weights; P4 (three methods) T2/T4/T5; P5 (Resonator locked) T1 guard + T5 refactor kept under the hash; lab and lab2 T3–T5; checks table → T2 block run per method; budget per method T2/T4/T5; stability T2/T5.
- Spec clarifications carried as constraints: Formant half-width `1 + 3·(1 − Focus)` (spec said 0.5–4; at 0.5 a fractional peak would reach neither neighbour — at 1 it blends the two); Dry pitch checked from 220 Hz (spec: low Dry notes blur); Spectral Dry 43 ms (spec).
