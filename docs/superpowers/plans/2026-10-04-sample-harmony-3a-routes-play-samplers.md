# Sample harmony 3a — routes play sampler synths Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route roles can be played by the six sampler synths beside the digital ones, every note on the harmony core's pitch; heard in the lab on real routes.

**Architecture:** The pitch sampler joins `sampler::Resonator` as Synth value `RETUNE = 5` (sharing voices, envelopes, steals; no level matching — it keeps the recording's dynamics), reading the recording at `(target ÷ f0(t))^Tune` from the analysis's pitch track. The route engine (`core/piece.cpp`) extends its synth list with six `s-*` types made as `sampler::Resonator` instances (6 voices per role) fed by a per-role recording (normalised to −20 dBFS RMS) and analysis given by the host. The lab gains a Route panel driving a `piece` device in its worklet.

**Tech Stack:** C++17 core (`core/samplers.hpp`, `core/synths.hpp`, `core/piece.cpp`, `core/fieldscape.h`), em++ → wasm, the lab (`lab.html`, `web/lab.js`, `web/core-worklet.js`, `web/build-lab.sh`), Puppeteer (`core/tests/web_lab.mjs`), lab2.

**Spec:** `docs/superpowers/specs/2026-10-04-instruments-3a-routes-play-samplers-design.md` (I1–I4).

## Global Constraints

- Digital synths and routes without sampler roles render bit-identically (the route-notes and swap-walk hashes, the 2a / 2b hashes).
- Every sampler note's pitch is the harmony core's Hz (the engine passes the same `f` to both families).
- Pitch sampler: speed `(f ÷ f0(t))^tune`, `f0(t)` = track f0 at the read position when confidence ≥ 0.8, else the last confident one (start: `analysis.f0`); a 10 ms glide on speed; start at the clearest moment (the most confident frame within 50 c of `analysis.f0`); 4-point Hermite reading; Timbre 2 = brightness, a one-pole low-pass at `f·(1.5 + 30·b²)` (open at 1).
- 6 sampler voices per role; S-curve attack ≥ 8 ms, exponential release, 50 ms steal fade (2a–2c).
- A role's recording normalised once to −20 dBFS RMS (its sounding part); a role without one is silent.
- Morph mapping for sampler roles: `timbre(0, harm)` → Focus = clamp((harm − 0.5) / 3.5, 0, 1); `timbre(1, index)` → Colour = clamp(index / 10, 0, 1).
- Level: per-type `SYNTH_TRIM` measured so a role swapped digital (`fm`) ↔ sampler stays within ±1 dB.
- Budget: 4 roles × 6 sampler voices < 1.33 ms per 128-sample block (×1.5 −O1), chord starts included.
- Lab only; the live site and apps untouched until Kerem approves.

## Review Focus

1. **A route switching a role between digital and sampler mid-walk** — the old instance releases (the 2.5 s tail), no click. Task 3.
2. **Recordings arriving after the route started / replaced while playing** — set off the audio thread; sounding notes finish, new notes use the new recording. Task 3.
3. **Pitch sampler at the recording's end** (one-shot reads past the last sample) — silence, not a wrap click. Task 1.
4. **Unpitched recording on the pitch sampler** (`f0 = 0`) — it plays at speed `f / 261.63` (C4) rather than dividing by zero. Task 1.
5. **Morph values out of the digital ranges** — clamped. Task 3.

---

### Task 1: The pitch sampler (Synth `RETUNE`)

**Files:** `core/samplers.hpp`, `core/bench.cpp` (synth range 0–5), `core/test.cpp`.

**Interfaces:** Produces `sampler::RETUNE = 5`; `Resonator::nv` (voices in use, default `VOICES`); `Resonator::set_track(double f0, double hop_s, const float *f0s, const float *confs, int n)`; per-voice `double rpos, rspd, rlp; long long rend;`.

- [ ] **Step 1: Failing tests** (block before the 2c guard):
  - steady: a 440 Hz harmonic tone recording with its track (all frames f0 440, conf 0.95); notes 220, 330, 660, 880 → `line_peak` within **1 cent** of each;
  - drifting: a recording gliding 440 × 2^(±30 c) sinusoidally over 2 s with the matching track; note 330 → the output's instantaneous pitch (zero-crossing period over 20 ms windows, 1–3 s) within **±2 c** of 330 throughout (glide lag aside, the 10 ms glide);
  - unconfident moments: the track's confidence 0.3 for 0.5 s mid-file → speed holds (no jump: largest 1 ms pitch step < 5 c);
  - unpitched (`f0 = 0`, no track): a note plays (RMS > −40 dBFS), finite;
  - end of a one-shot: a 0.3 s recording, a 2 s note → silent after the end, no step over the envelope bound;
  - brightness: Timbre 2 = 0 vs 1 → partials 4–8 vs fundamental ≥ 10 dB apart;
  - voices: `nv = 6`, 8 notes → 6 sound, steals click-free (the 2a envelope bound);
  - the 2a / 2b hashes unchanged.
  Run → FAIL.
- [ ] **Step 2: Implement.** `enum Synth { …, FREEZE = 4, RETUNE = 5 };`. `Resonator` gains `int nv = VOICES;` — every `for (int i = 0; i < VOICES; i++)` in `attack()` becomes `i < nv`; and the track: `double tf0 = 0, thop = 0.02; std::vector<float> tf, tc; long long tclear = 0;` filled by `set_track` (the clear moment as in `lab.js clearMoment`). `start()`: `if (synth == RETUNE) { start_retune(x, f); return; }`:

```cpp
    void start_retune(Voice &x, double f) {
        x.rpos = (double)(offset_s > 0 ? (long long)(offset_s * sr) : tclear); x.rend = src.frames;
        x.rf0 = tf0 > 0 ? tf0 : 261.63; x.rspd = std::pow(f / x.rf0, tune); x.sf = f;
        const double b = std::fmin(1.0, std::fmax(0.0, colour));
        x.rlpa = b >= 0.999 ? 0 : std::exp(-2 * PI * std::fmin(f * (1.5 + 30 * b * b), 0.45 * sr) / sr); x.rlp = 0;
        x.agc = 1;
    }
    double retune(Voice &x) {
        if (x.rpos >= x.rend - 2) return 0;                          /* a one-shot ends in silence */
        const int fr = tf.empty() ? -1 : (int)(x.rpos / sr / thop);
        if (fr >= 0 && fr < (int)tf.size() && tc[fr] >= 0.8f && tf[fr] > 0) x.rf0 = tf[fr];   /* else the last confident */
        const double want = std::pow(x.sf / x.rf0, tune);
        x.rspd += (want - x.rspd) * kglide;                            /* 10 ms */
        const long long i = (long long)x.rpos; const double u = x.rpos - i;
        const double y0 = src.at(i - 1), y1 = src.at(i), y2 = src.at(i + 1), y3 = src.at(i + 2);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        const double y = ((c3 * u + c2) * u + c1) * u + y1;
        x.rpos += x.rspd;
        x.rlp = (1 - x.rlpa) * y + x.rlpa * x.rlp;
        return x.rlp;
    }
```
  (`Voice` gains `double rpos = 0, rspd = 1, rf0 = 261.63, rlp = 0, rlpa = 0; long long rend = 0;`; `kglide = 1 − exp(−1/(0.01·sr))` a member set in `init`.) `resonate()`: `if (x.synth == RETUNE) return retune(x);` first. `render()`: for RETUNE, no level matching and no dry blend — `const double mix = x.synth == RETUNE ? 1 : tu[i];` in the output line, and skip the AGC block when `x.synth == RETUNE`. `timbre(0, v)`: for RETUNE the start position (`offset_s = v · frames / sr`, v < 0 → the clear moment).
- [ ] **Step 3:** Run → passes; hashes unchanged. Commit "3a: the pitch sampler (RETUNE) on the shared voices, tuned from the recording's pitch track".

---

### Task 2: Sampler types in the route engine

**Files:** `core/synths.hpp` (names, trims, self-voiced), `core/piece.cpp`, `core/fieldscape.h`, `core/test.cpp`.

**Interfaces:** Produces `SynthType` entries `S_RETUNE … S_FREEZE` after `FORMANT` (`NSYNTH` = 17), names `s-retune s-resonator s-harmonic s-formant s-pulsar s-freeze`; `SELF_VOICED` true for them; `fs_piece_role_source(fs_device*, int role, int channels, long long frames, const float *const *pcm)` (role 0 = Voice, 1 = Sections, 2 = Third voice); `fs_piece_role_analysis(fs_device*, int role, const char *json)`; a role's `sampler` config parsed from the patch.

- [ ] **Step 1: Failing tests:**
  - the route-notes hash and swap-walk hash unchanged (existing tests — must stay green);
  - a patch `{"v3":{"synth":"s-harmonic","sampler":{"mode":1,"focus":0.8}}}` with no recording: renders finite, the Third voice silent (its notes logged, its output < −100 dBFS), the other roles as before;
  - with a −20 dBFS noise recording on role 2: the Third voice sounds, and every note the log records for role 2 appears in the output within **1 cent** (line_peak on a 4 s render while one chord holds — `fs_piece_chord_notes` + `harmony::hz` give the expected Hz);
  - consonance: Voice = `simple` (digital), Third voice = `s-harmonic` Ringing on the same chord: the chord root's Hz from both families identical (both from the engine's `hz()`); a Just fifth rendered digital root + sampler fifth: the 1–20 Hz envelope fluctuation of the 1.5 × root band < 1 dB;
  - a role switched digital → sampler → digital mid-walk: no step above the envelope bound;
  - morphs: a `v3.harm` morph to 10 and `v3.index` to −5 on a sampler role: clamped (Focus 1, Colour 0), finite;
  - budget: 4 roles × 6 voices of `s-freeze` (the costliest) and chords starting: within the block budget.
  Run → FAIL (the names are unknown, the API missing).
- [ ] **Step 2: Implement.** `synths.hpp`: extend `SynthType`, `SYNTH_NAMES`, `SYNTH_TRIM` (0 for the new ones until Task 4 measures them), `SELF_VOICED`. `piece.cpp`:
  - `make(type)`: for `type >= S_RETUNE` a `sampler::Resonator` with `synth = type − S_RETUNE` mapped `{RETUNE, RESONATE, HARMONIC, FORMANT, PULSAR, FREEZE}`, `nv = 6`, the role's `SamplerCfg` (body, excite, method, mode, focus, colour, tune), and the role's recording / track if set;
  - a `SamplerCfg` per role parsed beside its synth (`v->get("sampler")`), defaults = the bench's;
  - per role: `std::vector<float> rec` (mono, normalised to −20 dBFS RMS over frames within 40 dB of the loudest 50 ms), `std::vector<float> tf, tc; double tf0, thop` — set by the two new API calls on the calling thread, then handed to that role's existing sampler instances (`set_source`, `set_track`);
  - timbre for sampler types: a thin adapter mapping (harm, index) → (Focus, Colour) with the clamps in Global Constraints (Pulsar / Freeze / RETUNE read Focus/Colour as their own controls).
  `fieldscape.h`: declare the two calls; `build-lab.sh` exports them with `_fs_piece_add_route,_fs_piece_set_route,_fs_piece_walk,_fs_piece_chord,_fs_piece_chord_notes`.
- [ ] **Step 3:** Run → passes; all hashes unchanged. Commit "3a: the route engine plays sampler synths per role".

---

### Task 3: Level trims

**Files:** `core/synths.hpp`, `core/test.cpp`.

- [ ] **Step 1: Failing test:** a route with only Voice on; for each sampler type (default sampler settings, the −20 dBFS noise recording; RETUNE with a −20 dBFS harmonic tone and its track), the Voice role's output RMS over 10 s against `fm`'s: within **±1 dB** — fails with trims 0.
- [ ] **Step 2:** Measure each difference, set `SYNTH_TRIM[S_*]`, rerun → within ±1 dB. Commit "3a: sampler trims measured against fm".

---

### Task 4: The lab's Route panel (then lab2)

**Files:** `lab.html`, `web/lab.js`, `web/core-worklet.js`, `core/tests/web_lab.mjs`.

- [ ] **Step 1: Failing browser checks:** a Route panel lists the routes; Play starts a `piece` engine (the generic worklet processor, device `piece`) and the Now line shows the route's chord; setting Third voice to *Sampler → Harmonic filter* with the loaded noise file keeps sound coming (level above −40 dBFS) and the chord advancing; switching it back to *Digital* — no page errors; Stop silences it.
- [ ] **Step 2:** Worklet `FieldscapeCore` messages (UTF-8 bytes copied into wasm memory, as the engine processor does): `{type:"route", bytes}` → `fs_piece_add_route` + `fs_piece_walk(d, r, 0, 0)`; `{type:"patch", bytes}` → `fs_piece_set_route(d, r, …)`; `{type:"walk", t}` → `fs_piece_walk(d, r, t, 0)`; `{type:"role", role, channels}` → `fs_piece_role_source`; `{type:"analysis", role, bytes}` → `fs_piece_role_analysis`; `{type:"chord"}` → posts `fs_piece_chord`. Lab: the panel (route select; per role a select with `<optgroup label="Digital">` / `<optgroup label="Sampler">`; Play / Stop; a pace slider, minutes for the whole route, default 3); the walk position posted every 250 ms; the chosen roles written into a copy of the route's patch (`synth`, `sampler`) and sent; a sampler role takes the loaded recording and its analysis.
- [ ] **Step 3:** Rebuild `web/core-lab.wasm`; `node core/tests/web_lab.mjs` → all pass. Commit. Final review before publishing; then `python scratch/make_lab2.py`, the lab2 variant test, republish lab2.

---

## Self-review notes

- Spec coverage: I3 (two families) T2/T4; I4-A (pitch from the track) T1; per-role recordings T2; morphs T2; level ±1 dB T3; consonance and pitch checks T1/T2; budget T2; live walk unchanged (hashes) T1/T2; lab panel T4.
- Spec wording "the core's resampler kernel" for the pitch sampler's reading → a 4-point Hermite per voice (the 32-tap kernel is a fixed-rate converter): recorded as a plan decision; quality judged in the 3a session.
