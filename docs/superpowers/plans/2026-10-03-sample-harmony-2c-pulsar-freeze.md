# Sample harmony 2c — Pulsar grains and Spectral freeze Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two more non-pitch synths on the lab bench — Pulsar (grains of the recording fired f times a second) and Freeze (one moment's colour placed on the note's partials).

**Architecture:** Both join the `sampler::Resonator` voice system as `Synth` values 3 and 4 (as 2b's Harmonic filter and Formant did), each with its own per-voice engine dispatched from `resonate()`. Freeze reuses 2b's Spectral machinery (real FFT, hop, OLA, global-clock stagger). The 2a and 2b synths are locked by output hashes. The bench's Synth range grows to 4; the lab adds two menu entries and two Colour labels.

**Tech Stack:** C++17 core (`core/samplers.hpp`, `core/bench.cpp`), em++ → wasm (`core/tests/run.py`, `web/build-lab.sh`), the lab (`lab.html`, `web/lab.js`), Puppeteer checks (`core/tests/web_lab.mjs`), lab2 (`scratch/make_lab2.py`).

**Spec:** `docs/superpowers/specs/2026-10-03-non-pitch-2c-pulsar-freeze-design.md` (Q1–Q4).

## Global Constraints

- The recording is the only input; notes from the chord system (parent spec).
- 2a Resonator and 2b Harmonic filter / Formant render bit-identically (hashes).
- Pulsar: grain k at `t_k = k·P` (fractional, P = sr/f), length `D = max(4, d·P)`, `d = 0.05 + 0.95·Colour`, Hann; slice refreshed every `R = 0.001·10^(4·Focus)` s, never at Focus 1; level matching slow (3 s) from `G = 0.375·d`.
- Freeze: moment at `Colour·(frames − 2048)`; 4 frames (2048 Hann, hop 512) power-averaged; partial bins within `h = 1 + 3·(1 − Focus)` of n f, raised cosine × magnitude; phase per bin advances at n f per hop plus a random turn up to `(1 − Focus)·π`; level fixed at the moment's (target = the moment's power, not the live recording's).
- Partials n f < 0.45·sr, up to 24; f clamped 6 Hz – 0.45·sr.
- Shared: 24 voices, steals, 50 ms steal fade, S-curve attack ≥ 8 ms, release ≤ 10 s, Tune, per-note Synth/Focus/Colour.
- Budget: 24 voices < 1.33 ms per 128-sample block (×1.5 for −O1), together and lined-up starts; an 8-note Freeze chord start < 1 ms native (×1.5).
- Lab: *Pulsar*, *Freeze* menu entries; Colour labels **Grain N %** / **Moment m:ss**; Method/Mode hidden for them.

## Review Focus

1. **Pulsar at high notes** — P down to ~14 samples at 3.5 kHz: grains of 4 samples, many per block; pitch still exact, no aliasing burst. Pinned in Task 2 (pitch to 3.5 kHz, stability to 12 kHz).
2. **Pulsar refresh vs the recording's loop point** — a slice starting near the file's end reads across the loop. Pinned in Task 2 (a short looped source in the stability sweep).
3. **Freeze moment past the recording's end / a recording shorter than a frame** — clamped, no crash. Pinned in Task 3.
4. **Freeze's level when the recording is silent at the chosen moment** — silence in, silence out (never boosted). Pinned in Task 3.
5. **Freeze note starts inside render (steals)** — 4 forward FFTs at capture; budget with 8 starts per block. Pinned in Task 3.

---

### Task 1: Lock 2b, add the synth values

**Files:** Modify `core/samplers.hpp` (enum), `core/bench.cpp` (synth max 4); Test `core/test.cpp`.

**Interfaces:** Produces `sampler::PULSAR = 3`, `sampler::FREEZE = 4`; `line_peak(x, sr, f, span)` (test helper: Hann-windowed DTFT magnitude maximised over f(1±span), scan + golden section).

- [ ] **Step 1:** Add a "2b guard" hash beside the 2a one: Harmonic and Formant × Bank / Spectral / Comb × Dry / Ringing, Focus 0.7, Colour 0.4, 196 Hz, 2 s on `noise_src(3, 0.5f, 77)`, FNV-1a over the float bits; `assert(h == 0)` first.
- [ ] **Step 2:** Run, read the printed hash, pin it; rerun → passes (characterisation lock taken before any change).
- [ ] **Step 3:** `enum Synth { RESONATE = 0, HARMONIC = 1, FORMANT = 2, PULSAR = 3, FREEZE = 4 };` — bench `{ "synth", "Synth", "", 0.0f, 4.0f, 0.0f }` (comment: `Resonator, Harmonic filter, Formant, Pulsar, Freeze`). Add `line_peak` to the test helpers:

```cpp
static double line_peak(const std::vector<float> &x, double sr, double f, double span) {
    const size_t N = 65536, s0 = x.size() - N;
    auto mag = [&](double q) { double re = 0, im = 0; for (size_t i = 0; i < N; i++) { double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / N), ph = 2 * 3.141592653589793 * q * i / sr; re += x[s0 + i] * w * std::cos(ph); im += x[s0 + i] * w * std::sin(ph); } return re * re + im * im; };
    double step = std::fmax(0.1, f * span / 40), bf = f, bm = -1;
    for (double q = f * (1 - span); q <= f * (1 + span); q += step) { double m = mag(q); if (m > bm) { bm = m; bf = q; } }
    double lo = bf - step, hi = bf + step;
    for (int i = 0; i < 40; i++) { double a = lo + (hi - lo) * 0.382, b = lo + (hi - lo) * 0.618; if (mag(a) > mag(b)) hi = b; else lo = a; }
    return 0.5 * (lo + hi);
}
```
- [ ] **Step 4:** Run `python core/tests/run.py; node build/stretch-tests/core_test.js` → 19/19, both hashes, `core ok`. Commit "2c: Pulsar / Freeze synth values; 2b locked by a hash".

---

### Task 2: Pulsar

**Files:** Modify `core/samplers.hpp`; Test `core/test.cpp`.

**Interfaces:** Consumes Task 1's enum. Produces per-voice `double pP, pD; long long pr, plast, pk;` and `Resonator::pulsar(Voice&)`.

- [ ] **Step 1: Failing tests** (a new block after the 2b block):
  - pitch: `part_render(PULSAR, 0, 0, 1.0, 0.5, 1, f, 8, wind)` for f ∈ {55, 110, 220, 440, 880, 1760, 3520}; `line_peak(o, 48000, f, 0.02)` within **1 cent**;
  - colour: partials 4–8 vs fundamental (band max ±0.5 % as 2b) at Colour 0 minus at Colour 1 **≥ 6 dB**, f 220, Focus 1;
  - content: Focus 1 → share of the last 65536 samples' power within ±2 bins of the harmonics of 220 **≥ 0.9**; Focus 0 → **< 0.6**;
  - level: Focus 0.5 Colour 0.5 220 Hz 15 s: settled (10–14 s) within **1 dB** of the dry render, first 3 s ≤ settled + 3, 0.25–2 s ≥ settled − 2;
  - stability: Focus {0, 0.5, 1} × Colour {0, 0.5, 1} × f {5, 41, 80, 3500, 12000} on full-scale noise and on a 0.3 s looped noise file: finite, |s| < 4;
  - budget: 24 voices; 8-note chord starts; lined-up starts (as 2b).
  Run → FAIL (Pulsar renders as a Bank Harmonic filter or silence).
- [ ] **Step 2: Implement.** `start()`: `if (synth == PULSAR) { start_pulsar(x, f); return; }` before the 2b branch; `resonate()`: `if (x.synth == PULSAR) return pulsar(x);` first.

```cpp
    /* Pulsar (2c): a grain of the recording every period, fractional, Hann-shaped, d of a period long; its slice
       refreshed every R(Focus), never at Focus 1 */
    void start_pulsar(Voice &x, double f) {
        const double d = 0.05 + 0.95 * std::fmin(1.0, std::fmax(0.0, colour));
        x.pP = sr / f; x.pD = std::fmax(4.0, d * x.pP); x.sk = 0; x.pk = -1;
        x.pr = x.pos; x.plast = 0;
        x.prefresh = focus >= 0.999 ? -1 : (long long)std::llround(0.001 * std::pow(10.0, 4 * focus) * sr);
        x.agc = std::fmin(1000.0, 1 / std::sqrt(0.375 * std::fmin(1.0, x.pD / x.pP)));
    }
    double pulsar(Voice &x) {
        const double n = (double)x.sk++;
        const long long k = (long long)std::floor(n / x.pP);
        if (k != x.pk) {                                           /* a new grain: refresh its slice when it is due */
            x.pk = k;
            if (x.prefresh >= 0 && (long long)n - x.plast >= x.prefresh) { x.pr = x.pos - 1; x.plast = (long long)n; }
        }
        const double tau = n - k * x.pP;
        if (tau >= x.pD) return 0;
        return src.at(x.pr + (long long)tau) * (0.5 - 0.5 * std::cos(2 * PI * tau / x.pD));
    }
```
(`Voice` gains `double pP = 1, pD = 1; long long pr = 0, plast = 0, pk = -1, prefresh = -1;`.) In `render()`, `part` already covers it (slow level matching, no feedback pre-scale).
- [ ] **Step 3:** Run → Pulsar block passes; both hashes unchanged. Commit "2c Pulsar".

---

### Task 3: Freeze

**Files:** Modify `core/samplers.hpp`; Test `core/test.cpp`.

**Interfaces:** Consumes the 2b Spectral members (`rf`, `xin`, `Xr`, `Xi`, `yout`, `win`, `ola`, `hold`, `ph`, `mask`, `owner`, `sk`, `soff`, `sf`). Produces `start_freeze(Voice&, double f)`, `freeze_frame(Voice&)`, per-voice `double fpow; uint32_t rng;`.

- [ ] **Step 1: Failing tests:**
  - pitch at Focus 1: f ∈ {55 … 3520}, `line_peak` within **1 cent**;
  - moment: a 10 s file, 0–5 s dark (one-pole low-pass noise, a = 0.95), 5–10 s white; Colour 0.1 vs 0.9 at 220 Hz: partials 4–8 vs fundamental differ by **≥ 6 dB**;
  - holds: 3 s noise then 5 s silence, Colour 0, a 6 s note: RMS 1–2.5 s vs 4–5.5 s within **0.5 dB**;
  - purity: the −6 dB width of the 220 Hz peak (averaged spectrum) at Focus 0 **≥ 3×** at Focus 1;
  - level: stationary noise, settled within **1 dB** of the dry render; 0.25–2 s ≥ settled − 2, first 3 s ≤ settled + 3;
  - silent moment: a moment in silence → output RMS < −100 dBFS (never boosted);
  - edges: Colour 1 on a file shorter than a frame, and on a 2048-sample file: finite, no crash;
  - stability (as Pulsar) and budget (24 voices; 8 starts in one block < 1.33·slack; lined-up starts).
  Run → FAIL.
- [ ] **Step 2: Implement.** `start()`: `if (synth == FREEZE) { start_freeze(x, f); return; }`; `spectral_frame()` first line: `if (x.synth == FREEZE) { freeze_frame(x); return; }`; `resonate()`: `if (x.synth == FREEZE) return spectral(x);`.

```cpp
    /* Freeze (2c): one moment's spectrum (4 frames averaged), its level read at each partial, held for ever */
    void start_freeze(Voice &x, double f) {
        std::fill(x.ola.begin(), x.ola.end(), 0.0f); std::fill(x.ph.begin(), x.ph.end(), 0.0f);
        std::fill(x.mask.begin(), x.mask.end(), 0.0f); std::fill(x.owner.begin(), x.owner.end(), 0);
        const long long len = std::max<long long>(src.frames, 1), at = (long long)(std::fmin(1.0, std::fmax(0.0, colour)) * std::max<long long>(0, len - SPN));
        std::vector<double> &pw = freeze_pw; std::fill(pw.begin(), pw.end(), 0.0); double tp = 0;
        for (int fr = 0; fr < 4; fr++) {
            for (int j = 0; j < SPN; j++) { const float s = src.at(at + fr * SPH + j); xin[j] = s * win[j]; tp += (double)s * s; }
            rf.forward(xin.data(), Xr.data(), Xi.data());
            for (int k = 0; k <= SPN / 2; k++) pw[k] += (double)Xr[k] * Xr[k] + (double)Xi[k] * Xi[k];
        }
        x.fpow = tp / (4.0 * SPN);                                     /* the moment's own power: the level target */
        const double bin = sr / SPN, h = 1 + 3 * (1 - std::fmin(1.0, std::fmax(0.0, focus)));
        double coh = 0, inc = 0;
        for (int n = 1; n <= PARTIALS && n * f < 0.45 * sr; n++) {
            const double c = n * f / bin;
            for (int k = std::max(1, (int)std::ceil(c - h)); k <= std::min(SPN / 2 - 1, (int)std::floor(c + h)); k++) {
                const double m = (0.5 + 0.5 * std::cos(PI * (k - c) / h)) * std::sqrt(pw[k] / 4);
                if (m > x.mask[k]) { x.mask[k] = (float)m; x.owner[k] = n; }
            }
        }
        /* the output's expected power, coherent bins (Focus 1) to independent ones (Focus 0), so the level starts near
           the moment's and the fixed-target matching only fine-tunes it */
        for (int n = 1; n <= PARTIALS && n * f < 0.45 * sr; n++) { double a = 0; for (int k = 1; k < SPN / 2; k++) if (x.owner[k] == n) { a += x.mask[k]; inc += x.mask[k] * x.mask[k]; } coh += a * a; }
        const double unit = 2.0 / SPN * 2.0 / 1.5 * 0.5, est = focus * coh * unit * unit / 2 + (1 - focus) * inc * unit * unit / 2 * 1.5 / 2;
        x.agc = est > 1e-30 && x.fpow > 1e-20 ? std::fmin(1000.0, std::sqrt(x.fpow / est)) : (x.fpow > 1e-20 ? 1 : 0);
        const long long s0 = (long long)std::ceil(x.on_t * sr - 1e-9);
        x.sf = f; x.sk = 0; x.soff = (int)((((&x - v) * SPH / VOICES - s0) % SPH + SPH) % SPH); x.rng = 0x9e3779b9u ^ (uint32_t)(&x - v);
    }
    void freeze_frame(Voice &x) {
        const double jit = (1 - std::fmin(1.0, std::fmax(0.0, focus_of(x)))) * PI;
        for (int k = 0; k <= SPN / 2; k++) {
            double re = 0, im = 0;
            if (x.mask[k] > 0) {
                x.rng ^= x.rng << 13; x.rng ^= x.rng >> 17; x.rng ^= x.rng << 5;
                const double turn = 2 * PI * x.owner[k] * x.sf * SPH / sr + jit * ((x.rng >> 8) / 8388608.0 - 1);
                x.ph[k] = (float)std::fmod(x.ph[k] + turn, 2 * PI);
                re = x.mask[k] * std::cos(x.ph[k]); im = x.mask[k] * std::sin(x.ph[k]);
            }
            Xr[k] = (float)re; Xi[k] = (float)im;
        }
        rf.inverse(Xr.data(), Xi.data(), yout.data());
        for (int j = 0; j < SPN; j++) x.ola[(size_t)((x.sk + j) & (SPN - 1))] += yout[j] / SPN * win[j] / 1.5f;
    }
```
(`focus_of(x)`: the voice's own Focus — `Voice` gains `double focus = 0.5;` set in `start()` beside `x.synth`. `freeze_pw` is a `std::vector<double>` sized `SPN/2+1` in `init()`.) Per-frame trigonometry is only on kept bins (≤ ~24·9).
In `render()`: for `x.synth == FREEZE` the level matching uses `x.rin = x.fpow` (fixed) and the 0.3 s rate `ka`, and is skipped when `x.fpow <= 1e-20` (silence stays silence).
- [ ] **Step 3:** Run → Freeze block passes; both hashes unchanged. Commit "2c Freeze".

---

### Task 4: The lab, then lab2

**Files:** Modify `lab.html`, `web/lab.js`; Test `core/tests/web_lab.mjs`.

- [ ] **Step 1: Failing browser checks:** the menu has `pulsar` and `freeze`; with Pulsar, `#lab-part` and `#lab-res` hidden; Colour 0.5 reads **Grain 53 %**; with Freeze on the NOISE file (2 s), Colour 0.5 reads **Moment 0:00**… (computed: `(c · (duration − 2048/sr))` → `m:ss`); a Pulsar chord (Focus 1) and a Freeze chord (Focus 1) each create pitch ≥ 10 dB per note. Run → FAIL.
- [ ] **Step 2:** `lab.html` options `<option value="pulsar">Pulsar</option><option value="freeze">Freeze</option>`; `lab.js`: `ENGINE` gains `pulsar: 3, freeze: 4`; `#lab-part` shows only for `harmonic`/`formant`; `colourLabel()`:

```js
    var c = +$("#lab-colour").value;
    $("#lab-colour-label").textContent = synth === "harmonic" ? "Overtones" : synth === "formant" ? "Partial " + Math.round(1 + 15 * c)
      : synth === "pulsar" ? "Grain " + Math.round(100 * (0.05 + 0.95 * c)) + " %"
      : synth === "freeze" ? (function () { var s = buffer ? Math.max(0, c * (buffer.duration - 2048 / buffer.sampleRate)) : 0; return "Moment " + Math.floor(s / 60) + ":" + ("0" + Math.floor(s % 60)).slice(-2); })()
      : "Colour";
```
  and `colourLabel()` is also called after a recording loads. Kept verdicts: `" focus … colour …"` for every engine synth (already).
- [ ] **Step 3:** Rebuild `web/core-lab.wasm`; `node core/tests/web_lab.mjs` → all pass. Commit. `python scratch/make_lab2.py`; the lab2 variant test; republish lab2.

---

## Self-review notes

- Spec coverage: Q1 (Focus refresh) T2; Q2 (relative grain) T2; Q3 (moment on partials) T3; Q4 (purity) T3; level rules T2/T3; lab labels T4; checks table → T2/T3 tests; budget and safety T2/T3; hashes T1.
- The Freeze level estimate is a start value only; the fixed-target matching (0.3 s) settles it — the fade-up/swell checks guard the start.
