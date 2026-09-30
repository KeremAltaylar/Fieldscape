# Sample harmony 2a: the Resonator and the bench on the engine — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An engine synth that makes unpitched recordings play notes — the Resonator (String, Tube, Bell bodies; Plucked or Bowed) — and the lab's audition bench playing it through the engine in an AudioWorklet.

**Architecture:** `core/samplers.hpp` holds `sampler::Resonator`, a `tone::Synth` (attack / release / timbre / render at device times) with `set_source`. `core/bench.cpp` is a new engine device `"bench"` holding one Resonator, its recording and settings, with C calls to schedule notes, stop, read the clock, and measure how far a pitch stands out ("pitch created"). The lab page gets a Synth choice; Resonator notes go to a `fieldscape-core` AudioWorkletNode running `web/core-lab.wasm` with device `"bench"`. The live site's `web/core.wasm` and pages are untouched.

**Tech Stack:** C++17 core (Emscripten 6 → standalone wasm at -O1), AudioWorklet (`web/core-worklet.js`), plain browser JS, Node test runner + CDP browser tests.

**Spec:** `docs/superpowers/specs/2026-09-30-non-pitch-2a-resonator-design.md` (R1–R5), parent `docs/superpowers/specs/2026-09-30-sample-harmony-design.md`.

## Global Constraints

- Live site and apps unchanged (D1): `web/core.wasm`, `index.html`, `sw.js` untouched; the route engine (`core/piece.cpp`) untouched in 2a.
- Pitch: every note's fundamental within **±3 cents**, 50 Hz – 4 kHz, every body except Bell's upper modes.
- Focus = T60 from **0.2 s** (Focus 0) to **10 s** (Focus 1), `T60 = 0.2 × 50^Focus`.
- Tune 0 = the recording itself (the resonator out of the path); Tune 1 = fully resonated.
- Envelope floors: attack ≥ **5 ms**, release ≥ **30 ms**, release exponential to silence (A-3); a steal fades over **5 ms** (A-5); loop gain < 1; DC blocker; no denormals.
- 6 voices per synth.
- Bowed level follows the recording's level within **±1 dB** (per-voice automatic gain, 0.3 s).
- Wasm builds link at -O1 (Windows blocks wasm-opt).
- Commits end with the session's attribution lines.

## Review Focus

- **No recording loaded, then a note** → silence, no crash. (Task 3 test)
- **A note at an extreme frequency** (Octave −3/+3: 5 Hz, 12 kHz) → clamped, stable, no NaN. (Task 3 test)
- **Body / Excite / Focus / Colour changed while voices sound** → applies to the next note; sounding notes are untouched (no click). (Task 3 test)
- **A 7th note** → the quietest voice fades over 5 ms and the new note takes it. (Task 3 test)
- **The bench asked for "pitch created" before any sound** → a finite number (no NaN, no crash). (Task 4 test)

---

### Task 1: The Resonator — String body, Plucked and Bowed

**Files:**
- Create: `core/samplers.hpp`
- Test: `core/test.cpp` (helpers above `int main()`, a block before `core ok`)

**Interfaces:**
- Produces (namespace `sampler`, header-only): `enum Body { STRING, TUBE, BELL }`, `enum Excite { BOWED, PLUCKED }`, `struct Resonator : tone::Synth` with fields `body, excite` (int), `focus, colour, tune, att, rel, offset_s` (double), methods `init(double sr)`, `set_source(int ch, long long frames, const float *const *p)`, `set_source_i16(int ch, long long frames, const int16_t *const *p)`, `attack(double f, double t, double vel)`, `release(double t)`, `timbre(int which, double v, double ramp, double now)`, `render(float *L, float *R, int n, double t0)` (adds into L, R), `stop_all()`, `static double t60(double focus)`.

- [ ] **Step 1: Write the failing tests**

In `core/test.cpp` add `#include "samplers.hpp"` and `#include "devices/fft.hpp"` with the other includes, these helpers above `int main()`:

```cpp
/* sample harmony 2a: render a Resonator straight, and measure what it made */
static std::vector<float> noise_src(double secs, float amp, uint32_t seed) {
    std::vector<float> v((size_t)(secs * 48000));
    for (auto &x : v) { seed = seed * 1664525u + 1013904223u; x = amp * (((int)(seed >> 8) - 8388608) / 8388608.0f); }
    return v;
}
static std::vector<float> res_render(int body, int excite, double focus, double colour, double tune, double f, double secs,
                                     const std::vector<float> &src, double att = 0.02, double rel = 0.6) {
    sampler::Resonator r; r.init(48000);
    r.body = body; r.excite = excite; r.focus = focus; r.colour = colour; r.tune = tune; r.att = att; r.rel = rel;
    const float *p[1] = { src.data() }; r.set_source(1, (long long)src.size(), p);
    r.attack(f, 0.0, 0.5); r.release(secs + 10);
    std::vector<float> L((size_t)(secs * 48000), 0.0f), R(L.size(), 0.0f);
    for (size_t i = 0; i < L.size(); i += 128) r.render(L.data() + i, R.data() + i, (int)std::min<size_t>(128, L.size() - i), i / 48000.0);
    return L;
}
/* the strongest frequency within f x (1 +- span): a 65536-point Hann FFT of the last 1.37 s, log-parabola peak */
static double peak_near(const std::vector<float> &x, double sr, double f, double span) {
    const int N = 65536; FFT fft; fft.reserve(N); fft.plan(N); fft.twiddles(0, N);
    std::vector<float> b(4 * N); float *ar = b.data(), *ai = ar + N, *br = ai + N, *bi = br + N;
    size_t s0 = x.size() - N;
    for (int i = 0; i < N; i++) { ar[i] = (float)(x[s0 + i] * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N))); ai[i] = 0; }
    for (int p = 0; p < fft.passes; p++) { if (p % 2 == 0) fft.pass(p, ar, ai, br, bi, 0, fft.butterflies(p)); else fft.pass(p, br, bi, ar, ai, 0, fft.butterflies(p)); }
    const float *re = fft.passes % 2 ? br : ar, *im = fft.passes % 2 ? bi : ai;
    auto mag = [&](int k) { return std::sqrt((double)re[k] * re[k] + (double)im[k] * im[k]) + 1e-20; };
    int lo = std::max(2, (int)(f * (1 - span) * N / sr)), hi = std::min(N / 2 - 2, (int)(f * (1 + span) * N / sr) + 1), pk = lo;
    for (int k = lo; k <= hi; k++) if (mag(k) > mag(pk)) pk = k;
    double a = std::log(mag(pk - 1)), c = std::log(mag(pk)), d = std::log(mag(pk + 1)), den = a - 2 * c + d;
    return (pk + (den != 0 ? 0.5 * (a - d) / den : 0)) * sr / N;
}
/* the decay time: RMS in 20 ms windows, a line through the part 6-26 dB under the peak, extended to -60 dB */
static double t60_of(const std::vector<float> &x) {
    const int W = 960; std::vector<double> db;
    for (size_t i = 0; i + W <= x.size(); i += W) { double e = 0; for (int k = 0; k < W; k++) e += (double)x[i + k] * x[i + k]; db.push_back(10 * std::log10(e / W + 1e-30)); }
    size_t pk = std::max_element(db.begin(), db.end()) - db.begin(); double top = db[pk], sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
    for (size_t i = pk; i < db.size(); i++) if (db[i] <= top - 6 && db[i] >= top - 26) { double t = i * 0.02; sx += t; sy += db[i]; sxx += t * t; sxy += t * db[i]; m++; }
    double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    return -60 / slope;
}
```

and before `std::printf("core ok\n");`:

```cpp
    {   /* 2a: the Resonator's String - its notes land within 3 cents, Focus is the ring time, Tune 0 is the raw recording */
        const std::vector<float> wind = noise_src(10, 0.5f, 42);
        const double F[7] = { 55, 110, 220, 440, 880, 1760, 3520 };
        double worst = 0;
        for (int ex = 0; ex < 2; ex++) for (double f : F) {
            double got = peak_near(res_render(sampler::STRING, ex, 0.9, 0.5, 1, f, 3, wind), 48000, f, 0.04);
            worst = std::max(worst, std::fabs(1200 * std::log2(got / f)));
        }
        std::printf("resonator string: worst pitch error %.2f cents over 50 Hz - 4 kHz, plucked and bowed\n", worst);
        assert(worst < 3);
        double t0 = t60_of(res_render(sampler::STRING, sampler::PLUCKED, 0, 1, 1, 220, 1.5, wind));
        double t1 = t60_of(res_render(sampler::STRING, sampler::PLUCKED, 1, 1, 1, 220, 6, wind));
        std::printf("resonator string: T60 at Focus 0 %.2f s, at Focus 1 %.2f s\n", t0, t1);
        assert(std::fabs(t0 / 0.2 - 1) < 0.2 && std::fabs(t1 / 10 - 1) < 0.2);
        std::vector<float> a = res_render(sampler::STRING, sampler::BOWED, 0.1, 0.2, 0, 220, 1, wind);
        std::vector<float> b = res_render(sampler::STRING, sampler::BOWED, 0.9, 0.9, 0, 440, 1, wind);
        assert(a == b);                                                  /* Tune 0: the resonator is out of the path */
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `python core/tests/run.py`
Expected: build fails, `'samplers.hpp' file not found`.

- [ ] **Step 3: Write `core/samplers.hpp`**

```cpp
/* The synths that play recordings (sample harmony, docs/superpowers/specs/2026-09-30-sample-harmony-design.md).
   2a: the Resonator (docs/superpowers/specs/2026-09-30-non-pitch-2a-resonator-design.md) - an unpitched
   recording feeds a tuned resonator per note, so wind or water plays the chord from its own material.
   A tone::Synth: notes at times on the caller's clock, rendered by adding into the buffers given. */
#pragma once
#include "synths.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace sampler {

enum Body { STRING = 0, TUBE = 1, BELL = 2 };
enum Excite { BOWED = 0, PLUCKED = 1 };
static const double BELL_RATIO[4] = { 1.0, 2.76, 5.40, 8.93 };   /* a bar's / bell's modes */
static const double BELL_DECAY[4] = { 1.0, 0.6, 0.35, 0.2 };     /* the higher modes ring shorter */
static const double PI = 3.141592653589793;

/* The recording, owned by the host, read as mono and looped. */
struct Source {
    const float *f[2] = { nullptr, nullptr };
    const int16_t *s[2] = { nullptr, nullptr };
    int nch = 0; long long frames = 0;
    float at(long long i) const {
        if (frames <= 0) return 0;
        i %= frames; if (i < 0) i += frames;
        if (f[0]) return nch > 1 ? 0.5f * (f[0][i] + f[1][i]) : f[0][i];
        if (s[0]) return (nch > 1 ? 0.5f * (s[0][i] + s[1][i]) : s[0][i]) * (1.0f / 32768.0f);
        return 0;
    }
};

/* Phase delay (samples) and gain at w of the loop's two filters: the tuning all-pass and the colour low-pass. */
inline double ap_delay(double c, double w) { std::complex<double> z = std::polar(1.0, -w); return -std::arg((c + z) / (1.0 + c * z)) / w; }
inline double lp_delay(double a, double w) { std::complex<double> z = std::polar(1.0, -w); return -std::arg((1.0 - a) / (1.0 - a * z)) / w; }
inline double lp_gain(double a, double w) { std::complex<double> z = std::polar(1.0, -w); return std::abs((1.0 - a) / (1.0 - a * z)); }
/* the all-pass coefficient whose delay at w is `want` samples (its delay falls as c rises) */
inline double solve_ap(double want, double w) {
    double lo = -0.999, hi = 0.999;
    for (int i = 0; i < 60; i++) { double m = 0.5 * (lo + hi); if (ap_delay(m, w) > want) lo = m; else hi = m; }
    return 0.5 * (lo + hi);
}

struct Voice {
    bool active = false, started = false, releasing = false, stealing = false;
    double f = 0, on_t = 0, off_t = 1e300; double vel = 0;
    bool has_next = false; double nf = 0, nt = 0, noff = 1e300, nvel = 0;   /* the note waiting for a steal's fade */
    double env = 0;
    long long pos = 0; int burst = 0, burst_len = 0;
    std::vector<float> line; unsigned w = 0; int N = 1;
    double c = 0, a = 0, g = 0, ap_x = 0, ap_y = 0, lp = 0;
    int modes = 0; double b0[4] = {}, a1[4] = {}, a2[4] = {}, y1[4] = {}, y2[4] = {}, wt[4] = {};
    double dc_x = 0, dc_y = 0, rin = 0, rout = 0, agc = 1;
};

struct Resonator : tone::Synth {
    static const int VOICES = 6;
    static const unsigned MASK = (1u << 13) - 1;                  /* 8192-sample lines: down to ~6 Hz */
    Source src;
    int body = STRING, excite = BOWED;
    double focus = 0.5, colour = 0.5, tune = 1, att = 0.02, rel = 0.6, offset_s = 0;
    Voice v[VOICES]; int last = -1;

    static double t60(double focus) { return 0.2 * std::pow(50.0, std::fmin(1.0, std::fmax(0.0, focus))); }

    void init(double s) override { sr = s; for (auto &x : v) x.line.assign(MASK + 1, 0.0f); }
    void set_source(int ch, long long n, const float *const *p) {
        src = Source(); if (!p || n <= 0) return;
        src.nch = std::min(ch, 2); for (int k = 0; k < src.nch; k++) src.f[k] = p[k]; src.frames = n;
    }
    void set_source_i16(int ch, long long n, const int16_t *const *p) {
        src = Source(); if (!p || n <= 0) return;
        src.nch = std::min(ch, 2); for (int k = 0; k < src.nch; k++) src.s[k] = p[k]; src.frames = n;
    }

    /* a voice made ready for a note: the loop tuned to f (String/Tube) or the modes set (Bell) */
    void start(Voice &x, double f, double t, double vel) {
        std::fill(x.line.begin(), x.line.end(), 0.0f);
        f = std::fmin(std::fmax(f, 6.0), 0.45 * sr);                 /* extreme octaves: clamped, never unstable */
        x.active = true; x.started = false; x.releasing = false; x.stealing = false; x.has_next = false;
        x.f = f; x.on_t = t; x.off_t = 1e300; x.vel = vel; x.env = 0;
        x.pos = (long long)(offset_s * sr); x.burst = 0; x.burst_len = (int)(0.025 * sr);
        x.w = 0; x.ap_x = x.ap_y = x.lp = 0; x.dc_x = x.dc_y = 0; x.rin = x.rout = 0; x.agc = 1;
        const double w = 2 * PI * f / sr, T = t60(focus);
        x.a = 0.7 * (1 - std::fmin(1.0, std::fmax(0.0, colour)));
        if (body == BELL) {
            x.modes = 0;
            const double cw = 0.15 + 0.85 * colour;                  /* Colour: how strongly the upper modes speak */
            for (int k = 0; k < 4; k++) {
                double fk = f * BELL_RATIO[k]; if (fk > 0.45 * sr) break;
                double wk = 2 * PI * fk / sr, r = std::pow(10.0, -3.0 / (T * BELL_DECAY[k] * sr));
                x.a1[k] = -2 * r * std::cos(wk); x.a2[k] = r * r;
                x.b0[k] = (1 - r) * std::abs(1.0 - r * std::polar(1.0, -2 * wk));   /* about unity at the peak */
                x.y1[k] = x.y2[k] = 0; x.wt[k] = std::pow(cw, k); x.modes++;
            }
        } else {
            const double P = sr / f / (body == TUBE ? 2 : 1);        /* Tube: half a period, feedback inverted */
            const double lpd = lp_delay(x.a, w);
            int N = (int)std::floor(P - lpd - 0.2); N = std::max(1, std::min(N, (int)MASK - 2));
            x.N = N; x.c = solve_ap(P - N - lpd, w);
            const double g = std::pow(10.0, -3.0 * (P / sr) / T);    /* one pass of the loop, in T60 terms */
            x.g = std::fmin(0.99995, g / lp_gain(x.a, w));           /* the colour filter's loss made good at f */
        }
    }

    void attack(double f, double t, double vel) override {
        int q = -1;
        for (int i = 0; i < VOICES; i++) if (!v[i].active) { q = i; break; }
        if (q >= 0) { start(v[q], f, t, vel); last = q; return; }
        q = 0; for (int i = 1; i < VOICES; i++) if (v[i].env < v[q].env) q = i;   /* the quietest gives way (A-5) */
        Voice &x = v[q]; x.stealing = true; x.has_next = true; x.nf = f; x.nt = t; x.nvel = vel; x.noff = 1e300; last = q;
    }
    void release(double t) override {
        if (last < 0) return;
        Voice &x = v[last];
        if (x.has_next) x.noff = t; else x.off_t = t;
    }
    /* the two timbre slots the morphs drive (spec D10): Focus and Colour, for the next note */
    void timbre(int which, double val, double, double) override { if (which == 0) focus = val; else colour = val; }
    void stop_all() { for (auto &x : v) if (x.active) { x.stealing = true; x.has_next = false; } }

    double resonate(Voice &x, double in) {
        if (body == BELL) {
            double y = 0;
            for (int k = 0; k < x.modes; k++) {
                double o = x.b0[k] * in - x.a1[k] * x.y1[k] - x.a2[k] * x.y2[k];
                if (std::fabs(o) < 1e-20) o = 0;
                x.y2[k] = x.y1[k]; x.y1[k] = o; y += x.wt[k] * o;
            }
            return y;
        }
        double read = x.line[(x.w - (unsigned)x.N) & MASK];
        double ap = x.c * read + x.ap_x - x.c * x.ap_y; x.ap_x = read; x.ap_y = std::fabs(ap) < 1e-20 ? 0 : ap;
        x.lp = (1 - x.a) * x.ap_y + x.a * x.lp; if (std::fabs(x.lp) < 1e-20) x.lp = 0;
        double y = in + (body == TUBE ? -1 : 1) * x.g * x.lp;
        x.line[x.w & MASK] = (float)y; x.w++;
        double o = y - x.dc_x + 0.995 * x.dc_y; x.dc_x = y; x.dc_y = std::fabs(o) < 1e-20 ? 0 : o;   /* DC blocker */
        return x.dc_y;
    }

    void render(float *L, float *R, int n, double t0) override {
        const double up = 1.0 / (std::fmax(0.005, att) * sr), fade = 1.0 / (0.005 * sr);
        const double kr = std::pow(1e-4, 1.0 / (std::fmax(0.03, rel) * sr)), ka = 1 - std::exp(-1.0 / (0.3 * sr));
        for (auto &x : v) {
            if (!x.active) continue;
            for (int i = 0; i < n; i++) {
                const double t = t0 + i / sr;
                if (!x.started) { if (t < x.on_t) continue; x.started = true; }
                if (!x.releasing && t >= x.off_t) x.releasing = true;
                if (x.stealing) {
                    x.env -= fade;
                    if (x.env <= 0) {
                        x.env = 0;
                        if (!x.has_next) { x.active = false; break; }
                        double nf = x.nf, nt = std::fmax(x.nt, t), nv = x.nvel, no = x.noff;
                        start(x, nf, nt, nv); x.off_t = no; continue;
                    }
                } else if (x.releasing) { x.env *= kr; if (x.env < 1e-4) { x.active = false; break; } }
                else if (x.env < 1) x.env = std::fmin(1.0, x.env + up);
                const double in = src.at(x.pos++);
                double exc = in;
                if (excite == PLUCKED) { exc = x.burst < x.burst_len ? in * 0.5 * (1 - std::cos(2 * PI * x.burst / x.burst_len)) : 0; x.burst++; }
                double wet = resonate(x, excite == BOWED && body != BELL ? exc * (1 - x.g) : exc);
                if (excite == BOWED) {                                /* the bowed level follows the recording's */
                    x.rin += (exc * exc - x.rin) * ka; x.rout += (wet * wet - x.rout) * ka;
                    double tgt = x.rout > 1e-12 ? std::fmin(50.0, std::sqrt(x.rin / x.rout)) : 1;
                    x.agc += (tgt - x.agc) * ka; wet *= x.agc;
                }
                const double o = x.env * x.vel * vol * ((1 - tune) * exc + tune * wet);
                L[i] += (float)o; R[i] += (float)o;
            }
        }
    }
};

}  // namespace sampler
```

- [ ] **Step 4: Run to verify it passes**

Run: `python core/tests/run.py` then `node build/stretch-tests/core_test.js 2>&1 | grep -E "^resonator|Assertion|core ok"`
Expected: `resonator string: worst pitch error <3 cents…`, `resonator string: T60 at Focus 0 ~0.20 s, at Focus 1 ~10 s`, `core ok`, `19/19 passed`. If a pitch misses, the loop's delay accounting is wrong (N + all-pass + low-pass must equal the period at f) — fix the accounting, never widen the ±3 cents.

- [ ] **Step 5: Commit**

```bash
git add core/samplers.hpp core/test.cpp
git commit -m "The Resonator's String: an unpitched recording rings at the note, plucked or bowed"
```

---

### Task 2: Tube and Bell bodies

**Files:**
- Test: `core/test.cpp`
- (Implementation already in `core/samplers.hpp` from Task 1: Tube and Bell paths in `start` / `resonate`.)

**Interfaces:**
- Consumes: `sampler::Resonator`, `res_render`, `peak_near` (Task 1).

- [ ] **Step 1: Write the tests**

Before `core ok`:

```cpp
    {   /* 2a: Tube rings the odd harmonics, Bell its bar modes; both land on the note */
        const std::vector<float> wind = noise_src(10, 0.5f, 7);
        const double F[7] = { 55, 110, 220, 440, 880, 1760, 3520 };
        double worst = 0;
        for (int ex = 0; ex < 2; ex++) for (double f : F) {
            double got = peak_near(res_render(sampler::TUBE, ex, 0.9, 0.5, 1, f, 3, wind), 48000, f, 0.04);
            worst = std::max(worst, std::fabs(1200 * std::log2(got / f)));
            double bell = peak_near(res_render(sampler::BELL, ex, 0.9, 0.5, 1, f, 3, wind), 48000, f, 0.04);
            worst = std::max(worst, std::fabs(1200 * std::log2(bell / f)));
        }
        std::printf("resonator tube and bell: worst pitch error %.2f cents\n", worst);
        assert(worst < 3);
        /* Tube: the odd harmonics stand >= 15 dB over the even */
        std::vector<float> tube = res_render(sampler::TUBE, sampler::BOWED, 0.8, 0.6, 1, 220, 3, wind);
        auto level_at = [&](double f) {
            const int N = 65536; double re = 0, im = 0; size_t s0 = tube.size() - N;
            for (int i = 0; i < N; i++) { double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N), ph = 2 * 3.141592653589793 * f * i / 48000; re += tube[s0 + i] * w * std::cos(ph); im += tube[s0 + i] * w * std::sin(ph); }
            return 10 * std::log10(re * re + im * im + 1e-30);
        };
        double odd = 0.5 * (level_at(peak_near(tube, 48000, 220, 0.02)) + level_at(peak_near(tube, 48000, 660, 0.02)));
        double even = 0.5 * (level_at(440) + level_at(880));
        std::printf("resonator tube: odd harmonics %.1f dB over even\n", odd - even);
        assert(odd - even >= 15);
        /* Bell: its modes at 2.76, 5.40, 8.93 x f within 1% */
        std::vector<float> bell = res_render(sampler::BELL, sampler::BOWED, 0.9, 1, 1, 220, 3, wind);
        for (int k = 1; k < 4; k++) {
            double want = 220 * sampler::BELL_RATIO[k], got = peak_near(bell, 48000, want, 0.05);
            assert(std::fabs(got / want - 1) < 0.01);
        }
    }
```

- [ ] **Step 2: Run**

Run: `python core/tests/run.py` then `node build/stretch-tests/core_test.js 2>&1 | grep -E "^resonator|Assertion|core ok"`
Expected: `resonator tube and bell: worst pitch error <3`, `resonator tube: odd harmonics ≥15 dB over even`, `core ok`. (Task 1 already carries the Tube/Bell code, so these may pass at once; if one fails, the failure is the test's to explain before touching code — record which in the ledger.)

- [ ] **Step 3: Commit**

```bash
git add core/test.cpp
git commit -m "Resonator tests: Tube's odd harmonics, Bell's modes, both on the note"
```

---

### Task 3: Voices, envelopes, safety, level, speed

**Files:**
- Test: `core/test.cpp`

**Interfaces:**
- Consumes: `sampler::Resonator` (`attack`, `release`, `stop_all`, `render`, fields).

- [ ] **Step 1: Write the tests**

Before `core ok`:

```cpp
    {   /* 2a: no clicks (attack, release, a steal, stop), no runaway, bowed level = the recording's, fast enough */
        const double SR = 48000;
        std::vector<float> dc((size_t)(SR * 10), 0.5f);               /* a flat recording: every step is the envelope's */
        sampler::Resonator r; r.init(SR); r.tune = 0; r.att = 0.005; r.rel = 0.03;
        const float *p[1] = { dc.data() }; r.set_source(1, (long long)dc.size(), p);
        for (int k = 0; k < 7; k++) { r.attack(110 * (k + 1), 0.05 * k, 0.5); r.release(0.05 * k + 0.2); }   /* the 7th steals */
        std::vector<float> L((size_t)SR, 0.0f), R(L.size(), 0.0f);
        for (size_t i = 0; i < L.size(); i += 128) { if (i == 24000) r.stop_all(); r.render(L.data() + i, R.data() + i, 128, i / SR); }
        double step = 0; for (size_t i = 1; i < L.size(); i++) step = std::max(step, (double)std::fabs(L[i] - L[i - 1]));
        const double bound = 0.5 * 0.5 * std::max(1.0 / (0.005 * SR), 1 - std::pow(1e-4, 1.0 / (0.03 * SR))) * 1.05;
        std::printf("resonator envelopes: largest step %.5f (bound %.5f)\n", step, bound);
        assert(step <= bound);
        double tail = 0; for (size_t i = 24000 + 480; i < L.size(); i++) tail = std::max(tail, (double)std::fabs(L[i]));
        assert(tail == 0);                                             /* stop_all: silent 10 ms later */
        /* no recording: silence, no crash */
        sampler::Resonator q; q.init(SR); q.attack(220, 0, 0.5);
        std::vector<float> z(4096, 0.0f), z2(4096, 0.0f); q.render(z.data(), z2.data(), 4096, 0);
        for (float s : z) assert(s == 0);
        /* extreme notes and a minute at Focus 1 on full-scale noise: finite and bounded */
        const std::vector<float> loud = noise_src(10, 1.0f, 3);
        for (double f : { 5.0, 12000.0, 55.0 }) {
            std::vector<float> o = res_render(sampler::STRING, sampler::BOWED, 1, 1, 1, f, f == 55.0 ? 60 : 2, loud);
            double mx = 0; for (float s : o) { assert(std::isfinite(s)); mx = std::max(mx, (double)std::fabs(s)); }
            assert(mx < 4);
        }
        /* bowed level follows the recording's within 1 dB, every body */
        const std::vector<float> wind = noise_src(10, 0.5f, 11);
        auto rms = [](const std::vector<float> &x) { double e = 0; size_t a = 2 * 48000, b = 4 * 48000; for (size_t i = a; i < b; i++) e += (double)x[i] * x[i]; return 10 * std::log10(e / (b - a)); };
        for (int body = 0; body < 3; body++) {
            double wet = rms(res_render(body, sampler::BOWED, 0.5, 0.5, 1, 220, 4, wind)), dry = rms(res_render(body, sampler::BOWED, 0.5, 0.5, 0, 220, 4, wind));
            std::printf("resonator body %d: bowed %.2f dB vs the recording %.2f dB\n", body, wet, dry);
            assert(std::fabs(wet - dry) <= 1);
        }
        /* a setting changed while voices sound touches only the next note */
        sampler::Resonator s; s.init(SR); const float *wp[1] = { wind.data() }; s.set_source(1, (long long)wind.size(), wp);
        s.attack(220, 0, 0.5); std::vector<float> a1(4800, 0.0f), a2(4800, 0.0f); s.render(a1.data(), a2.data(), 4800, 0);
        double g = s.v[0].g; s.focus = 0.1; s.colour = 0.1; s.body = sampler::TUBE; s.render(a1.data(), a2.data(), 4800, 0.1);
        assert(s.v[0].g == g);
        /* six bowed strings inside one 128-sample block's budget */
        sampler::Resonator sp; sp.init(SR); sp.set_source(1, (long long)wind.size(), wp);
        for (int k = 0; k < 6; k++) sp.attack(110 * (k + 1), 0, 0.2);
        std::vector<double> ms; std::vector<float> b1(128), b2(128);
        for (int k = 0; k < (int)(10 * SR / 128); k++) {
            std::fill(b1.begin(), b1.end(), 0.0f); std::fill(b2.begin(), b2.end(), 0.0f);
            auto c0 = std::chrono::steady_clock::now(); sp.render(b1.data(), b2.data(), 128, k * 128 / SR);
            ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count());
        }
        std::sort(ms.begin(), ms.end());
#ifdef FS_TEST_O1
        const double slack = 1.5;
#else
        const double slack = 1;
#endif
        std::printf("resonator: six bowed strings, 99.9%% of blocks within %.3f ms (budget %.2f)\n", ms[(size_t)(ms.size() * 0.999)], 1.33 * slack);
        assert(ms[(size_t)(ms.size() * 0.999)] < 1.33 * slack);
    }
```

- [ ] **Step 2: Run**

Run: `python core/tests/run.py` then `node build/stretch-tests/core_test.js 2>&1 | grep -E "^resonator|Assertion|core ok"`
Expected: all `resonator …` lines, `core ok`. A failure here is a real defect in Task 1's code (envelope, steal, AGC, clamp): fix it with systematic-debugging, keep the test as written.

- [ ] **Step 3: Commit**

```bash
git add core/samplers.hpp core/test.cpp
git commit -m "Resonator: no clicks, no runaway, bowed level matched, six voices in budget"
```

---

### Task 4: The bench device

**Files:**
- Create: `core/bench.cpp`
- Modify: `core/core.cpp` (declare `make_bench`, REGISTRY row), `core/fieldscape.h` (bench calls), `core/tests/run.py` (core_test sources)
- Test: `core/test.cpp`

**Interfaces:**
- Consumes: `sampler::Resonator` (Tasks 1–3), `Device` (`core/device.hpp`).
- Produces: device id `"bench"`; params (index order) `0 body, 1 excite, 2 focus, 3 colour, 4 tune, 5 attack, 6 release, 7 offset`; C calls
  `void fs_bench_note(fs_device *d, double hz, double at_s, double dur_s, double vel);`
  `void fs_bench_stop(fs_device *d);`
  `double fs_bench_time(fs_device *d);`
  `double fs_bench_created(fs_device *d, double hz);` — dB of the output at `hz` over the median of 12 neighbours (±3–20 %), last 16384 samples; 0 when silent.

- [ ] **Step 1: Write the failing test**

```cpp
    {   /* 2a: the bench device - notes at times, the clock, "pitch created", stop */
        fs_device *b = fs_create("bench");
        assert(b);
        fs_prepare(b, 48000, 128);
        const std::vector<float> wind = noise_src(4, 0.5f, 5);
        const float *wp[1] = { wind.data() };
        fs_set_source(b, 1, (int)wind.size(), wp);
        assert(std::isfinite(fs_bench_created(b, 220)));               /* before any sound */
        fs_bench_note(b, 220, 0.1, 5, 0.5);
        for (int i = 0; i < 48000 * 2 / 128; i++) fs_process(b, 128);
        assert(std::fabs(fs_bench_time(b) - 2.0) < 0.01);
        double made = fs_bench_created(b, 220);
        std::printf("bench: a bowed string on noise, pitch created %+.1f dB at 220 Hz\n", made);
        assert(made >= 10);
        fs_bench_stop(b);
        for (int i = 0; i < 8; i++) fs_process(b, 128);
        double mx = 0; for (int i = 0; i < 128; i++) mx = std::max(mx, (double)std::fabs(fs_out(b, 0)[i]));
        assert(mx == 0);
        fs_destroy(b);
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `python core/tests/run.py`
Expected: build fails, `undefined symbol: fs_bench_note` (or `fs_create("bench")` returns null → assertion).

- [ ] **Step 3: Implement**

`core/fieldscape.h`, after `fs_analyse`:

```c
/* The lab's audition bench on the engine (sample harmony 2a): device "bench" holds one sampler synth (the
   Resonator), its recording (fs_set_source / _i16) and settings (params: body, excite, focus, colour,
   tune, attack, release, offset). Notes at device seconds; the clock; stop (a 5 ms fade); and how far the
   output at hz stands above the spectrum around it, in dB ("pitch created"). */
void fs_bench_note(fs_device *d, double hz, double at_s, double dur_s, double vel);
void fs_bench_stop(fs_device *d);
double fs_bench_time(fs_device *d);
double fs_bench_created(fs_device *d, double hz);
```

`core/bench.cpp`:

```cpp
/* bench: the lab's audition bench on the engine (sample harmony 2a). One sampler synth, its recording and
   settings, notes at device times, and a measure of the pitch it created. */
#include "fieldscape.h"
#include "device.hpp"
#include "samplers.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

static const fs_param BENCH_PARAMS[] = {
    { "body", "Body", "", 0.0f, 2.0f, 0.0f },          /* String, Tube, Bell */
    { "excite", "Excite", "", 0.0f, 1.0f, 0.0f },      /* Bowed, Plucked */
    { "focus", "Focus", "", 0.0f, 1.0f, 0.5f },
    { "colour", "Colour", "", 0.0f, 1.0f, 0.5f },
    { "tune", "Tune", "", 0.0f, 1.0f, 1.0f },
    { "attack", "Attack", "s", 0.005f, 2.0f, 0.02f },
    { "release", "Release", "s", 0.03f, 5.0f, 0.6f },
    { "offset", "Position", "s", 0.0f, 600.0f, 0.0f },
};

struct Bench : Device {
    sampler::Resonator res;
    double sr = 48000, t = 0;
    std::vector<float> ring = std::vector<float>(16384, 0.0f); size_t w = 0;
    void prepare(float s, int) override { sr = s; res.init(s); t = 0; }
    const fs_param *params(int &n) override { n = 8; return BENCH_PARAMS; }
    void set_param(int i, float v) override {
        switch (i) {
            case 0: res.body = (int)std::lround(v); break;       case 1: res.excite = (int)std::lround(v); break;
            case 2: res.focus = v; break;                        case 3: res.colour = v; break;
            case 4: res.tune = v; break;                         case 5: res.att = v; break;
            case 6: res.rel = v; break;                          case 7: res.offset_s = v; break;
        }
    }
    void set_source(int ch, int n, const float *const *s) override { res.set_source(ch, n, s); }
    void set_source_i16(int ch, int n, const int16_t *const *s) override { res.set_source_i16(ch, n, s); }
    void process(int n) override {
        std::fill(out[0].begin(), out[0].begin() + n, 0.0f); std::fill(out[1].begin(), out[1].begin() + n, 0.0f);
        res.render(out[0].data(), out[1].data(), n, t);
        for (int i = 0; i < n; i++) { ring[w] = out[0][i]; w = (w + 1) % ring.size(); }
        t += n / sr;
    }
    void *cast(const char *id) override { return std::strcmp(id, "bench") == 0 ? this : nullptr; }
    /* Hann-windowed power at f over the last 16384 samples, in order */
    double power(double f) const {
        const size_t N = ring.size(); double s1 = 0, s2 = 0, k = 2 * std::cos(2 * sampler::PI * f / sr);
        for (size_t i = 0; i < N; i++) {
            double x = ring[(w + i) % N] * (0.5 - 0.5 * std::cos(2 * sampler::PI * i / N)), s0 = x + k * s1 - s2; s2 = s1; s1 = s0;
        }
        return s1 * s1 + s2 * s2 - k * s1 * s2;
    }
    double created(double f) const {
        const double off[6] = { 0.03, 0.05, 0.08, 0.11, 0.15, 0.2 };
        std::vector<double> nb; for (double o : off) { nb.push_back(power(f * (1 - o))); nb.push_back(power(f * (1 + o))); }
        std::nth_element(nb.begin(), nb.begin() + nb.size() / 2, nb.end());
        double p = power(f), m = nb[nb.size() / 2];
        return p > 1e-20 && m > 1e-30 ? 10 * std::log10(p / m) : 0;
    }
};
Device *make_bench() { return new Bench(); }

static Bench *B(fs_device *d) { return d ? (Bench *)fs_device_impl(d)->cast("bench") : nullptr; }
extern "C" {
void fs_bench_note(fs_device *d, double hz, double at_s, double dur_s, double vel) { Bench *b = B(d); if (b) { b->res.attack(hz, at_s, vel); b->res.release(at_s + dur_s); } }
void fs_bench_stop(fs_device *d) { Bench *b = B(d); if (b) b->res.stop_all(); }
double fs_bench_time(fs_device *d) { Bench *b = B(d); return b ? b->t : 0; }
double fs_bench_created(fs_device *d, double hz) { Bench *b = B(d); return b ? b->created(hz) : 0; }
}
```

`core/core.cpp`: add `Device *make_bench();` beside the other `make_*` declarations and `{ "bench", make_bench },` to `REGISTRY`.

`core/tests/run.py`: add `"core/bench.cpp"` to the `core_test` list after `"core/analysis.cpp"`.

- [ ] **Step 4: Run to verify it passes**

Run: `python core/tests/run.py` then `node build/stretch-tests/core_test.js 2>&1 | grep -E "^bench|Assertion|core ok"`
Expected: `bench: a bowed string on noise, pitch created +≥10 dB at 220 Hz`, `core ok`, `19/19 passed`.

- [ ] **Step 5: Commit**

```bash
git add core/bench.cpp core/core.cpp core/fieldscape.h core/tests/run.py core/test.cpp
git commit -m "The bench device: a sampler synth on the engine, notes at device times, pitch created"
```

---

### Task 5: The lab's engine file plays the bench in an AudioWorklet

**Files:**
- Modify: `web/core-worklet.js` (the `FieldscapeCore` processor's message handler), `web/build-lab.sh` (sources + exports)
- Rebuild: `web/core-lab.wasm`
- Test: `tests/lab-wasm.test.mjs`

**Interfaces:**
- Consumes: `fs_bench_note/stop/time/created` (Task 4).
- Produces: `FieldscapeCore` messages `{ type: "note", hz, in, dur, vel }` (in = seconds from now), `{ type: "stop" }`, `{ type: "created", hz: [..] }` → replies `{ type: "created", db: [..] }`.

- [ ] **Step 1: Write the failing test**

Append to `tests/lab-wasm.test.mjs`:

```js
test("the lab's engine has the bench: a bowed string on noise creates its pitch", () => {
  const name = str("bench"), d = x.fs_create(name);
  assert.ok(d);
  x.fs_prepare(d, 48000, 128);
  const n = 48000 * 3, p = x.malloc(n * 4), f = new Float32Array(x.memory.buffer, p, n);
  let s = 5; for (let i = 0; i < n; i++) { s = (s * 1103515245 + 12345) >>> 0; f[i] = ((s >>> 8) / 16777216 - 0.5); }
  const ptrs = x.malloc(4); new Uint32Array(x.memory.buffer, ptrs, 1)[0] = p;
  x.fs_set_source(d, 1, n, ptrs);
  x.fs_bench_note(d, 220, 0.05, 5, 0.5);
  for (let i = 0; i < 48000 * 1.5 / 128; i++) x.fs_process(d, 128);
  const made = x.fs_bench_created(d, 220);
  assert.ok(made >= 10, "pitch created " + made);
});
```

- [ ] **Step 2: Run to verify it fails**

Run: `node --test tests/lab-wasm.test.mjs`
Expected: FAIL — `x.fs_create(...)` returns 0 for "bench" or `x.fs_bench_note is not a function`.

- [ ] **Step 3: Implement**

`web/build-lab.sh`: add `core/bench.cpp` to the source list, and `_fs_bench_note,_fs_bench_stop,_fs_bench_time,_fs_bench_created` to `-sEXPORTED_FUNCTIONS`.

`web/core-worklet.js`, in `FieldscapeCore`'s `port.onmessage`, before the `stats` branch:

```js
      else if (m.type === "note") { x.fs_bench_note(this.dev, m.hz, x.fs_bench_time(this.dev) + m.in, m.dur, m.vel); }
      else if (m.type === "stop") { x.fs_bench_stop(this.dev); }
      else if (m.type === "created") { this.port.postMessage({ type: "created", db: m.hz.map((h) => x.fs_bench_created(this.dev, h)) }); }
```

Rebuild with the Windows wrapper: `EM_CONFIG=C:/Users/kerem/tools/emsdk/.emscripten PATH="$W:$PATH" sh web/build-lab.sh`.

- [ ] **Step 4: Run to verify it passes**

Run: `node --test tests/lab-wasm.test.mjs` then `npm test`
Expected: 4 pass in the file; the whole suite passes.

- [ ] **Step 5: Commit**

```bash
git add web/core-worklet.js web/build-lab.sh web/core-lab.wasm tests/lab-wasm.test.mjs
git commit -m "The lab engine carries the bench; the worklet takes notes, stop and pitch-created requests"
```

---

### Task 6: The lab page plays the Resonator

**Files:**
- Modify: `lab.html` (Synth choice and Resonator controls), `web/lab.js`
- Test: `core/tests/web_lab.mjs`

**Interfaces:**
- Consumes: the worklet messages (Task 5), `web/core-lab.wasm`.
- Produces: `fsLab.setSynth("retune" | "resonator")`, `fsLab.engineReady` (a Promise resolved when the bench node runs), `fsLab.created()` (Promise of the last "pitch created" dB list for the sounding notes), and `play()` working for both synths.

- [ ] **Step 1: Write the failing browser tests**

Append before the final `no page errors` check in `core/tests/web_lab.mjs`:

```js
  /* 2a: the Resonator on the engine - an unpitched "wind" file plays a chord whose notes stand out */
  await ev(`fsLab.load(${NOISE}).then(function () { return 1; })`);
  await ev("fsLab.setSynth('resonator'), fsLab.engineReady");
  check("with the Resonator, an unpitched recording is playable", await ev("!document.querySelector(\"[data-play='chord']\").disabled"), null);
  const rc = await ev("(function(){ var r = fsLab.play('chord', { tuning: 'just', step: 0 }); return r; })()");
  await sleep(1500);
  const made = await ev("fsLab.created()");
  let rl = -120; for (let i = 0; i < 5; i++) { await sleep(100); rl = Math.max(rl, await ev("fsLab.level()")); }
  const rnow = await ev("document.querySelector('#lab-now').textContent");
  await ev("fsLab.stop()");
  check("the Resonator's chord is heard (above -40 dBFS)", rl > -40, rl);
  check("each chord note stands >= 10 dB over the noise around it (pitch created)", made && made.length === rc.hz.length && made.every(function (d) { return d >= 10; }), made);
  check("the Resonator plays the chord in its written register (octave 0)", rc.hz[0] > 60 && rc.hz[0] < 400, rc.hz);
  check("the playing view and Now line follow the Resonator", /[A-G]#?\d/.test(rnow), rnow);
  await ev("fsLab.setSynth('retune'), 0");
```

- [ ] **Step 2: Run to verify it fails**

Run: `node tools/serve.mjs 8765` (another shell), `node core/tests/web_lab.mjs`
Expected: FAIL (`fsLab.setSynth` undefined).

- [ ] **Step 3: `lab.html`**

In the Play section, before the Octave/Tune row:

```html
  <div class="row"><label>Synth <select id="lab-synth"><option value="retune" selected>Retune (pitched)</option><option value="resonator">Resonator</option></select></label>
    <span id="lab-res" hidden>
      <label>Body <select id="lab-body"><option value="0">String</option><option value="1">Tube</option><option value="2">Bell</option></select></label>
      <label>Excite <select id="lab-excite"><option value="0">Bowed</option><option value="1">Plucked</option></select></label>
      <label>Focus <input type="range" id="lab-focus" min="0" max="1" step="0.01" value="0.5"></label>
      <label>Colour <input type="range" id="lab-colour" min="0" max="1" step="0.01" value="0.5"></label>
    </span></div>
  <p id="lab-made" style="color: var(--dim); margin: 4px 0; min-height: 1.45em;"></p>
```

- [ ] **Step 4: `web/lab.js`**

Add, beside the other state: `var synth = "retune", node = null, wasmBytes = null, engineReady = null, madeTimer = null, lastMade = null;`

In `ready`, keep the bytes before instantiating: `.then(function (b) { wasmBytes = b.slice(0); return WebAssembly.instantiate(b, …` (the existing import object).

Add:

```js
  /* The Resonator runs in the engine (spec R4): the lab's wasm, device "bench", in an AudioWorklet. */
  var P_BODY = 0, P_EXCITE = 1, P_FOCUS = 2, P_COLOUR = 3, P_TUNE = 4, P_ATTACK = 5, P_RELEASE = 6, P_OFFSET = 7;
  function ensureEngine() {
    if (engineReady) { return engineReady; }
    var c = audio();
    engineReady = c.audioWorklet.addModule("web/core-worklet.js?v=" + Date.now()).then(function () {
      node = new AudioWorkletNode(c, "fieldscape-core", { numberOfInputs: 0, outputChannelCount: [2],
        processorOptions: { wasm: wasmBytes, device: "bench", params: [] } });
      node.connect(bus);
      node.port.onmessage = function (e) { if (e.data && e.data.type === "created") { lastMade = e.data.db; } };
      sendParams(); sendSource();
    });
    return engineReady;
  }
  function sendParams() {
    if (!node) { return; }
    [[P_BODY, +$("#lab-body").value], [P_EXCITE, +$("#lab-excite").value], [P_FOCUS, +$("#lab-focus").value],
     [P_COLOUR, +$("#lab-colour").value], [P_TUNE, +$("#lab-tune").value], [P_ATTACK, Math.max(0.005, +$("#lab-attack").value)],
     [P_RELEASE, Math.max(0.03, +$("#lab-release").value)], [P_OFFSET, loudAt]].forEach(function (pv) { node.port.postMessage(pv); });
  }
  function sendSource() {
    if (!node || !buffer) { return; }
    var ch = []; for (var c = 0; c < buffer.numberOfChannels; c++) { ch.push(buffer.getChannelData(c).slice(0)); }
    node.port.postMessage({ type: "source", channels: ch });
  }
  function setSynth(s) {
    synth = s; $("#lab-synth").value = s; $("#lab-res").hidden = s !== "resonator";
    if (s === "resonator") { ensureEngine(); }
    gate();
  }
```

In `load()`, after the analysis: compute the loudest 100 ms (the Resonator's start position) and hand the recording to the engine —

```js
      var win = Math.round(0.1 * sr), bestE = -1; loudAt = 0;
      for (var s0 = 0; s0 + win <= n; s0 += Math.round(win / 2)) { var e = 0; for (var k = 0; k < win; k++) { e += mono[s0 + k] * mono[s0 + k]; } if (e > bestE) { bestE = e; loudAt = s0 / sr; } }
      sendSource(); sendParams();
```

(declare `var loudAt = 0;` with the state).

In `gate()`: an unpitched recording is playable with the Resonator — replace the unpitched condition with `!(analysis.f0 > 0) && synth === "retune"`; add to the why-text for the Resonator: `synth === "resonator" && !node ? "Starting the engine…" : ""`.

In `play()`, after `stop(); last = [];` and the `chord`/`beat` setup, before the Retune paths:

```js
    if (synth === "resonator") {
      if (!node) { return { silent: true }; }
      var oct = octaveFactor(), A = Math.max(0.005, +$("#lab-attack").value), R = Math.max(0.03, +$("#lab-release").value), list = [];
      var notes = kind === "progression" ? prog.chords.map(function (ch, i) { return { hz: ch.hz, at: i * beat * 4, dur: beat * 4 - 0.1, name: noteName(ch.root).replace(/-?[0-9]+.*$/, "") + " " + ch.label }; })
        : [{ hz: kind === "note" ? [chord.hz[0]] : kind === "scale" ? chord.scale_hz : chord.hz, at: 0, dur: kind === "scale" ? beat * 0.9 : beat * 4, scale: kind === "scale" }];
      var lvr = level(kind === "progression" ? 12 : kind === "scale" ? 2 : Math.max(1, notes[0].hz.length));
      notes.forEach(function (grp) {
        grp.hz.forEach(function (h0, i) {
          var h = h0 * oct, at = t + grp.at + (grp.scale ? i * beat : 0);
          node.port.postMessage({ type: "note", hz: h, in: at - c.currentTime, dur: A + grp.dur, vel: lvr });
          var v = { engine: true, hz: h, base: h / oct, rate: 1, level: lvr, A: A, dur: grp.dur, R: R, offset: loudAt, start: at, stopAt: at + A + grp.dur + R, chord: grp.name };
          playing.push(v); last.push(v); list.push(h);
        });
      });
      clearInterval(madeTimer);
      madeTimer = setInterval(function () {
        var now = c.currentTime, hz = last.filter(function (v) { return now >= v.start && now < v.stopAt; }).map(function (v) { return v.hz; });
        if (!hz.length) { $("#lab-made").textContent = ""; return; }
        node.port.postMessage({ type: "created", hz: hz });
        if (lastMade && lastMade.length) { $("#lab-made").textContent = "pitch created: +" + Math.round(lastMade.reduce(function (a, d) { return a + d; }, 0) / lastMade.length) + " dB"; }
      }, 300);
      view();
      return { rates: list.map(function () { return 1; }), hz: list, gains: list.map(function () { return lvr; }) };
    }
```

In `stop()`: before the per-voice loop, `if (node) { node.port.postMessage({ type: "stop" }); } clearInterval(madeTimer); $("#lab-made").textContent = "";`; in the loop, skip engine voices' audio calls: `if (v.engine) { v.stopAt = now; return; }`.

In the now-line, show `(res)` instead of the rate for engine voices: `(v.engine ? "res" : v.rate.toFixed(2) + "x")`.

Controls: `$("#lab-synth").addEventListener("change", function () { setSynth(this.value); });`, and for `#lab-body`, `#lab-excite`, `#lab-focus`, `#lab-colour`, `#lab-tune`, `#lab-attack`, `#lab-release`: `addEventListener("input"/"change", sendParams)`.

Keep line: append `", synth " + synth + (synth === "resonator" ? " " + ["string", "tube", "bell"][+$("#lab-body").value] + " " + ["bowed", "plucked"][+$("#lab-excite").value] + " focus " + $("#lab-focus").value + " colour " + $("#lab-colour").value : "")`.

Bridge: add to `window.fsLab`: `setSynth: setSynth, get engineReady() { return engineReady || Promise.resolve(); }, created: function () { if (!node) { return Promise.resolve(null); } return new Promise(function (res) { var h = last.filter(function (v) { return ctx.currentTime >= v.start && ctx.currentTime < v.stopAt; }).map(function (v) { return v.hz; }); node.port.onmessage = function (e) { if (e.data && e.data.type === "created") { lastMade = e.data.db; res(e.data.db); } }; node.port.postMessage({ type: "created", hz: h }); }); }`.

- [ ] **Step 5: Run to verify it passes**

Run: `node core/tests/web_lab.mjs`
Expected: all PASS, including the 2a checks.

- [ ] **Step 6: The live site is unchanged**

Run: `git diff --stat main -- index.html web/core.wasm sw.js web/listener.js`, `node core/tests/web_listener.mjs`, `node core/tests/web_setter.mjs`
Expected: no diff; both pass.

- [ ] **Step 7: Commit**

```bash
git add lab.html web/lab.js core/tests/web_lab.mjs
git commit -m "Lab: the Resonator on the engine - Body, Excite, Focus, Colour, pitch created"
```

---

### Task 7: Hand it to Kerem (listening session 2a)

- [ ] **Step 1: Run everything**

Run: `python core/tests/run.py`, `npm test`, `node core/tests/web_lab.mjs`, `node core/tests/web_listener.mjs`, `node core/tests/web_setter.mjs`.
Expected: all pass.

- [ ] **Step 2: Ask before it goes online** (a merge to `main` publishes the lab). On his OK: merge, push, wait for Pages, confirm the live `lab.html`, `web/lab.js`, `web/core-lab.wasm`, `web/core-worklet.js` equal the committed files (V-5), run `FS_URL=https://keremaltaylar.github.io/Fieldscape/ node core/tests/web_lab.mjs`.

- [ ] **Step 3: Listening session 2a**

What to try: a wind or water recording; Synth → Resonator; each Body (String, Tube, Bell) × Bowed / Plucked; Chord in Just then Equal; the Progression; Focus low → high; Colour dark → bright; Tune 0 → 1. Record his verdicts under "Listening sessions" in the parent spec; they decide 2b's defaults. State plainly what was not verified (his ear on his machine).
