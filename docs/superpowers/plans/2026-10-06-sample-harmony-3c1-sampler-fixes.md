# Sample harmony 3c.1 — samplers that sound with real recordings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sampler roles sound with sparse, high field recordings (bird calls) on every role, and the lab-mode patch panel
shows each sampler role's sample, notes and level without layout faults.

**Architecture:** A JS compaction step (`FsRoles.prepare`) cuts the silence out of every role sample before it reaches an
engine (site and lab). The core folds each sampler note by octaves into the sample's band (`fold_c` from the analysis'
f0 or centroid) and reports each role's level and sounding notes (`fs_engine_roles`) in the engine's live message. The
site's patch panel gets a full-width sample block (upload, name, warning, waveform with Freeze's Moment) and a live row
(notes + meter) per sampler role.

**Tech Stack:** C++ core → wasm (emscripten `-O1`), AudioWorklet, plain browser JS (`index.html`, `web/*.js`), Canvas 2D,
Node test runner, headless Chrome over CDP.

**Spec:** `docs/superpowers/specs/2026-10-06-samples-3c1-sampler-fixes-design.md`

## Global Constraints

- D1: the public keeps `web/core.wasm` (never rebuilt here) and the live patch; lab mode off or signed out: no change.
- Stored samples and stored analyses are unchanged; compaction runs at decode.
- Silence rule: 50 ms blocks; sounding = energy ≥ the loudest block's −40 dB; silent runs shorter than 0.25 s are kept.
- Joins: 5 ms fade-out at a segment's end and 5 ms fade-in at the next's start (no overlap, so positions map exactly).
- Fold: centre `fold_c` = `f0` when > 0, else `centroid_hz`; 0 → no folding; band `[fold_c/√2, fold_c·√2]`, whole octaves.
- `fs_engine_roles` and the `roles` live data exist only in `core-lab.wasm`.
- Build: `W=/c/Users/kerem/AppData/Local/Temp/emb; EM_CONFIG=C:/Users/kerem/tools/emsdk/.emscripten PATH="$W:$PATH" sh web/build-lab.sh`.
- Suites that create users run one after another (auth rate limit).

## Review Focus

1. **A sample with almost no sound** (one 30 ms click, or all silence) — compaction must not produce an empty or
   zero-length buffer; expect the click kept (with its fades) or the buffer unchanged, and silence, no error.
2. **Retune on a pitched sample after compaction** — its pitch track must stay aligned to the audio; expect Retune in
   tune (the same as before compaction on a continuous tone).
3. **Walking from one route onto another while the live view runs** — expect the notes and meter of the new route, never
   the old route's stuck on screen.
4. **Lab mode off** — no live rows, no canvases, no per-frame work in the panel.
5. **A phone-width panel (one column)** — the sample block fits its column; nothing overflows horizontally.

Tests: 1 → Task 1 unit; 2 → Task 1 unit (track alignment); 3 → Task 4 browser (route change clears notes);
4 → Task 4 browser; 5 → Task 3 browser (390 px).

---

### Task 1: `FsRoles.prepare` — the silence cut out, everywhere a sample reaches an engine

**Files:**
- Modify: `web/lab-roles.js` (new `compactPlan`, `prepare`; `decodeSample`, `fetchRoute`, `session.load`, `session.upload` use it; `session.raw(role)`, `session.kept(role)`)
- Modify: `index.html` (`labRoleSamples`: the prepared object)
- Test: `tests/lab-roles-unit.test.mjs`

**Interfaces:**
- Produces:
  - `FsRoles.compactPlan(channels: Float32Array[], sampleRate) -> { block: number (frames), kept: Uint8Array (per block), segments: [startFrame, endFrame][] }`
  - `FsRoles.compactData(channels, sampleRate, analysis) -> { channels: Float32Array[], analysis, kept, block }` (pure; Node-testable)
  - `FsRoles.prepare(ctx, audioBuffer, analysis) -> { buf: AudioBuffer (compacted), analysis (compacted), raw: AudioBuffer (as decoded, trimmed), kept: Uint8Array, block: number }`
  - `FsRoles.decodeSample(sb, ctx, smp) -> Promise<prepared>` (was `Promise<AudioBuffer>`)
  - `session.buf(r)` / `session.ana(r)` return the compacted buffer / analysis; `session.raw(r)`, `session.kept(r)`, `session.block(r)` for the waveform.

- [ ] **Step 1: Write the failing unit tests** (append to `tests/lab-roles-unit.test.mjs`)

```js
/* 3c.1 F1: the silence of a recording cut out (50 ms blocks, -40 dB of the loudest, gaps under 0.25 s kept) */
const calls = (sr) => {      /* three 0.3 s calls of a 5 kHz tone, 2 s of silence between, 0.5 s of silence first */
  const n = Math.round(sr * 7.4), x = new Float32Array(n);
  for (const s0 of [0.5, 2.8, 5.1]) for (let i = 0; i < 0.3 * sr; i++) x[Math.round(s0 * sr) + i] = 0.5 * Math.sin(2 * Math.PI * 5000 * i / sr);
  return x;
};
test("lab-roles: compactData keeps the calls, joined, and drops the silence between", () => {
  const sr = 48000, x = calls(sr);
  const track = Array.from({ length: Math.round(7.4 / 0.02) }, (_, i) => [i, 0.9, 5000]);   /* hop i carries its own index */
  const c = FsRoles.compactData([x], sr, { f0: 5000, hop_s: 0.02, track, frames: x.length });
  const secs = c.channels[0].length / sr;
  assert.ok(secs > 0.85 && secs < 1.25, String(secs));                       /* ~0.9 s of calls (+ block rounding) */
  let peak = 0; for (const v of c.channels[0]) peak = Math.max(peak, Math.abs(v));
  assert.ok(peak > 0.45);
  let step = 0; for (let i = 1; i < c.channels[0].length; i++) step = Math.max(step, Math.abs(c.channels[0][i] - c.channels[0][i - 1]));
  assert.ok(step < 0.2, "a join steps by " + step);                          /* faded, no click */
  assert.equal(c.analysis.frames, c.channels[0].length);
  /* the track keeps the hops inside kept blocks, in order: hop 25 (0.5 s) is the first call's first hop */
  assert.equal(c.analysis.track[0][0], 25);
  assert.ok(c.analysis.track.length > 40 && c.analysis.track.length < 65, String(c.analysis.track.length));
  assert.equal(x.length, Math.round(sr * 7.4));                               /* the input untouched */
});
test("lab-roles: compactData keeps a gap under 0.25 s, and leaves an all-silent or tiny buffer whole", () => {
  const sr = 48000, x = new Float32Array(sr * 2);
  for (let i = 0; i < 0.4 * sr; i++) x[i] = 0.5 * Math.sin(i / 3);
  for (let i = Math.round(0.6 * sr); i < sr; i++) x[i] = 0.5 * Math.sin(i / 3);   /* a 0.2 s gap, then 0.4 s more */
  assert.ok(FsRoles.compactData([x], sr, null).channels[0].length >= sr * 0.95);
  const z = new Float32Array(sr); assert.equal(FsRoles.compactData([z], sr, null).channels[0].length, sr);
  const click = new Float32Array(sr); click[24000] = 0.9;
  const cc = FsRoles.compactData([click], sr, null).channels[0];
  assert.ok(cc.length > 0 && cc.length <= sr);
});
```

- [ ] **Step 2: Run to see them fail**

Run: `node --test tests/lab-roles-unit.test.mjs`
Expected: FAIL — `FsRoles.compactData is not a function`.

- [ ] **Step 3: Implement in `web/lab-roles.js`** (inside the IIFE, before `api`)

```js
  /* 3c.1 F1: the sounding parts of a recording, in order. 50 ms blocks; a block sounds when its energy is at least the
     loudest block's -40 dB (the analyser's silence rule); a silent run under 0.25 s is kept (the breath in a call) */
  var BLOCK_S = 0.05, KEEP_GAP_S = 0.25, FADE_S = 0.005;
  function compactPlan(chs, sr) {
    var B = Math.max(1, Math.round(BLOCK_S * sr)), n = chs[0].length, nb = Math.ceil(n / B), e = new Float64Array(nb), top = 0;
    for (var b = 0; b < nb; b++) {
      var s = 0, z = Math.min(n, (b + 1) * B);
      for (var c = 0; c < chs.length; c++) { var d = chs[c]; for (var i = b * B; i < z; i++) { s += d[i] * d[i]; } }
      e[b] = s / ((z - b * B) * chs.length); if (e[b] > top) { top = e[b]; }
    }
    var kept = new Uint8Array(nb);
    for (b = 0; b < nb; b++) { kept[b] = top > 0 && e[b] >= top * 1e-4 ? 1 : 0; }
    if (top <= 0) { kept.fill(1); }
    var gap = Math.ceil(KEEP_GAP_S / BLOCK_S);          /* silent runs shorter than this between sounding blocks: kept */
    for (b = 0; b < nb; b++) {
      if (kept[b]) { continue; }
      var r = b; while (r < nb && !kept[r]) { r++; }
      if (b > 0 && r < nb && r - b < gap) { for (var k = b; k < r; k++) { kept[k] = 1; } }
      b = r;
    }
    var segs = [];
    for (b = 0; b < nb; b++) { if (kept[b] && (b === 0 || !kept[b - 1])) { var q = b; while (q < nb && kept[q]) { q++; } segs.push([b * B, Math.min(n, q * B)]); } }
    return { block: B, kept: kept, segments: segs };
  }
  function compactData(chs, sr, analysis) {
    var plan = compactPlan(chs, sr), n = 0, F = Math.max(1, Math.round(FADE_S * sr));
    plan.segments.forEach(function (sg) { n += sg[1] - sg[0]; });
    var whole = plan.segments.length === 1 && plan.segments[0][0] === 0 && plan.segments[0][1] === chs[0].length;
    var out = chs.map(function () { return new Float32Array(n); });
    var o = 0;
    plan.segments.forEach(function (sg, si) {
      var len = sg[1] - sg[0];
      for (var c = 0; c < chs.length; c++) {
        out[c].set(chs[c].subarray(sg[0], sg[1]), o);
        if (whole) { continue; }
        for (var i = 0; i < Math.min(F, len); i++) {
          var g = 0.5 - 0.5 * Math.cos(Math.PI * (i + 0.5) / F);       /* raised cosine: no step at a join */
          if (si > 0 || sg[0] > 0) { out[c][o + i] *= g; }
          if (si < plan.segments.length - 1 || sg[1] < chs[0].length) { out[c][o + len - 1 - i] *= g; }
        }
      }
      o += len;
    });
    var a = analysis ? JSON.parse(JSON.stringify(analysis)) : null;
    if (a && a.track && a.hop_s) {
      a.track = a.track.filter(function (fr, i) { var b = Math.floor(i * a.hop_s * sr / plan.block); return b < plan.kept.length && plan.kept[b]; });
    }
    if (a) { a.frames = n; }
    return { channels: out, analysis: a, kept: plan.kept, block: plan.block };
  }
  function prepare(ctx, b, analysis) {
    var chs = []; for (var c = 0; c < b.numberOfChannels; c++) { chs.push(b.getChannelData(c)); }
    var cd = compactData(chs, b.sampleRate, analysis), out = ctx.createBuffer(chs.length, Math.max(1, cd.channels[0].length), b.sampleRate);
    for (c = 0; c < chs.length; c++) { out.copyToChannel(cd.channels[c], c); }
    return { buf: out, analysis: cd.analysis, raw: b, kept: cd.kept, block: cd.block };
  }
```

Then:
- `decodeSample(sb, ctx, smp)` returns `trimmed(ctx, b)` → `prepare(ctx, trimmed(ctx, b), smp.analysis || null)`.
- `fetchRoute`: `buf[r] = p.buf; ana[r] = p.analysis;`.
- `session`: keep `raws`, `kepts`, `blocks` beside `bufs`/`anas`; in `load`'s decode success: `bufs[r] = p.buf; anas[r] = p.analysis; raws[r] = p.raw; kepts[r] = p.kept; blocks[r] = p.block;`; in `upload` after analyse: `var p = prepare(o.ctx(), res.buf, res.analysis);` and set those five from `p` (the uploaded WAV is still `wav(res.buf)`, the uncompacted trimmed recording; `x.sample.analysis` stays the uncompacted `res.analysis`). Missing sample: `raws[r] = null`.
  Add `s.raw = function (r) { return raws[r] || null; }; s.kept = function (r) { return kepts[r] || null; }; s.block = function (r) { return blocks[r] || 0; };` and reset them in `load`.
- Export `compactPlan, compactData, prepare` in `api`.
- `index.html` `labRoleSamples`: `FsRoles.decodeSample(...)).then(function (p) { … labSend(port, r, p.buf, p.analysis); }`.

- [ ] **Step 4: Run the tests**

Run: `node --test tests/lab-roles-unit.test.mjs` → 6/6. `node core/tests/web_lab.mjs` → 67/67;
`node --env-file=.env.local core/tests/web_lab_save.mjs` → 15/15; `node --env-file=.env.local core/tests/web_site_lab.mjs` → 27/27.

- [ ] **Step 5: Commit**

```bash
git add web/lab-roles.js tests/lab-roles-unit.test.mjs index.html
git commit -m "3c.1: the silence cut out of every role sample before it reaches an engine"
```

---

### Task 2: Core — notes folded into the sample, and each role's level and notes

**Files:**
- Modify: `core/samplers.hpp` (`Resonator`: `double fold_c = 0;`, fold at the top of `start`, `int sounding(double *f, int max)`)
- Modify: `core/piece.cpp` (`RoleRec.fold_c`; `fs_piece_role_analysis` reads it; `bind` passes it; per-role level; `fs_piece_roles`)
- Modify: `core/engine.cpp`, `core/fieldscape.h` (`fs_engine_roles`), `web/core-worklet.js` (live `roles`), `web/build-lab.sh` (exports)
- Test: `core/test.cpp`

**Interfaces:**
- Produces (C):
  - `int fs_piece_roles(fs_device *d, float *out, int max_notes);` and `int fs_engine_roles(fs_engine *e, float *out, int max_notes);`
    — writes, for role 0 (voice), 1 (sect), 2 (v3): `[level_db, count, f_1 … f_max_notes]` (`level_db` ≥ −120; unused
    frequencies 0). Returns `3 * (2 + max_notes)`.
- Produces (worklet): the `live` message carries `roles: Float32Array(3 * (2 + 6))` when `x.fs_engine_roles` exists.

- [ ] **Step 1: Write the failing tests** (in `core/test.cpp`, after the 3c engine role-source block)

```cpp
    {   /* 3c.1 F2: a sampler's note folded by octaves into its sample's band (pitch class kept); 0 = as played */
        sampler::Resonator r; r.synth = sampler::HARMONIC; r.init(SR);
        std::vector<float> nz = noise_src(5, 0.5f, 3); const float *np[1] = { nz.data() }; r.set_source(1, (long long)nz.size(), np);
        auto played = [&](double fold) {
            r.fold_c = fold; r.release_all(0); std::vector<float> L(B), R(B);
            for (int b = 0; b < 400; b++) r.render(L.data(), R.data(), B, b * B / SR);    /* the last notes gone */
            r.attack(110, 1, 0.5); double f[4] = {}; r.render(L.data(), R.data(), B, 1.0);
            return r.sounding(f, 4) > 0 ? f[0] : 0.0;
        };
        const double a = played(5000), b = played(0);
        const double oct = std::log2(a / 110);
        std::printf("3c.1 fold: 110 Hz -> %.1f Hz (fold 5000), %.1f Hz (no fold)\n", a, b);
        assert(a > 5000 / std::sqrt(2.0) - 1 && a < 5000 * std::sqrt(2.0) + 1 && std::fabs(oct - std::round(oct)) < 1e-9 && std::fabs(b - 110) < 1e-9);
    }
    {   /* 3c.1 F4: each role's level and sounding notes, from the whole engine */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
            "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
            "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{\"version\":17,"
            "\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"}],\"bed\":{\"on\":false},\"sect\":{\"on\":false},"
            "\"zones\":{\"on\":false},\"v3\":{\"on\":false},\"voice\":{\"synth\":\"s-freeze\"}}}}]}");
        std::vector<float> nz = noise_src(20, 0.5f, 7); const float *np[1] = { nz.data() };
        fs_engine_role_source(e, 0, 1, (long long)nz.size(), np);
        for (int b = 0; b < (int)(5.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.001, 41.0); fs_engine_process(e, B); }
        float out[3 * (2 + 6)] = {};
        const int w = fs_engine_roles(e, out, 6);
        std::printf("3c.1 roles: voice %.1f dB %d notes (%.1f Hz), sect %.1f dB, v3 %.1f dB\n", out[0], (int)out[1], out[2], out[8], out[16]);
        assert(w == 24 && out[0] > -60 && out[1] >= 1 && out[2] > 20 && out[8] <= -100 && out[16] <= -100);
        fs_engine_destroy(e);
    }
```

- [ ] **Step 2: Run to see it fail**

Run: `python core/tests/run.py`
Expected: FAIL — compile errors (`fold_c`, `sounding`, `fs_engine_roles` undeclared).

- [ ] **Step 3: Implement**

`core/samplers.hpp`, in `struct Resonator` beside `nv`: `double fold_c = 0;   /* 3c.1 F2: the sample's centre (Hz); notes fold into [c/√2, c·√2] by octaves; 0 = as played */`.
At the very top of `start(Voice &x, double f, double t, double vel)`:
```cpp
        if (fold_c > 0) { const double lo = fold_c / std::sqrt(2.0), hi = fold_c * std::sqrt(2.0); while (f < lo) f *= 2; while (f > hi) f *= 0.5; }
```
A reader for the live view:
```cpp
    /* 3c.1 F4: the notes sounding now (active, not releasing), up to max */
    int sounding(double *f, int max) const { int k = 0; for (int i = 0; i < nv && k < max; i++) if (v[i].active && !v[i].releasing) f[k++] = v[i].f; return k; }
```

`core/piece.cpp`:
- `struct RoleRec { …; double fold_c = 0; };`
- `fs_piece_role_analysis`: after `k.f0 = …`: `const double cen = j.n("centroid_hz", 0); k.fold_c = k.f0 > 0 ? k.f0 : cen;`
- `bind`: after `set_track_view(...)`: `r->fold_c = k.fold_c;`
- In `Piece`, per-role meters: `double lvl[3] = {};`. In the render, after `run_role(v3r, v3L.L, v3L.R);` (before `pad.process`):
```cpp
        {   /* 3c.1 F4: each role's own output (before its effects), ~0.1 s mean square */
            const double k = 1 - std::exp(-n / (0.1 * sr));
            float *Ls[3] = { pad.L, sectL.L, v3L.L }, *Rs[3] = { pad.R, sectL.R, v3L.R };
            for (int q = 0; q < 3; q++) { double s = 0; for (int i = 0; i < n; i++) s += 0.5 * ((double)Ls[q][i] * Ls[q][i] + (double)Rs[q][i] * Rs[q][i]); lvl[q] += (s / n - lvl[q]) * k; }
        }
```
- After `fs_piece_role_analysis`:
```cpp
int fs_piece_roles(fs_device *d, float *out, int max_notes) {
    Piece *p = P(d); if (!p || !out || max_notes < 0) return 0;
    Role *rs[3][2] = { { &p->bass, &p->top }, { &p->sectr, nullptr }, { &p->v3r, nullptr } };
    const int W = 2 + max_notes;
    for (int q = 0; q < 3; q++) {
        float *o = out + q * W; o[0] = (float)std::fmax(-120.0, 10 * std::log10(p->lvl[q] + 1e-30)); o[1] = 0;
        for (int i = 0; i < max_notes; i++) o[2 + i] = 0;
        int c = 0;
        for (Role *r : rs[q]) {
            if (!r || !is_sampler(r->cur) || !r->inst[r->cur]) continue;
            std::vector<double> f(max_notes);
            const int got = static_cast<sampler::Resonator *>(r->inst[r->cur].get())->sounding(f.data(), max_notes - c);
            for (int i = 0; i < got; i++) o[2 + c++] = (float)f[i];
        }
        o[1] = (float)c;
    }
    return 3 * W;
}
```
- `core/engine.cpp` after `fs_engine_role_analysis`: `int fs_engine_roles(fs_engine *e, float *out, int max_notes) { return fs_piece_roles(e->piece, out, max_notes); }`
- `core/fieldscape.h`: declare `fs_piece_roles` beside `fs_piece_role_analysis`, `fs_engine_roles` beside `fs_engine_role_analysis`.
- `web/core-worklet.js`, `FieldscapeEngine`: in the constructor `this.rolesP = x.fs_engine_roles ? x.malloc(4 * 24) : 0;`; in `live()` before the post:
  `let roles = null; if (this.rolesP) { x.fs_engine_roles(this.e, this.rolesP, 6); roles = new Float32Array(24); roles.set(new Float32Array(x.memory.buffer, this.rolesP, 24)); }`,
  add `roles` to the posted object, and to the transfer list when present.
- `web/build-lab.sh`: add `_fs_piece_roles,_fs_engine_roles` to the exports.

- [ ] **Step 4: Run the tests and rebuild**

Run: `python core/tests/run.py` → 19/19 (the new checks inside core_test). Lab build. `node core/tests/web_lab.mjs` → 67/67.

- [ ] **Step 5: Commit**

```bash
git add core/samplers.hpp core/piece.cpp core/engine.cpp core/fieldscape.h core/test.cpp web/core-worklet.js web/build-lab.sh web/core-lab.wasm
git commit -m "3c.1 core: sampler notes folded into the sample's band; each role's level and notes"
```

---

### Task 3: The sample block — upload, name, warning, waveform with Freeze's Moment; the Colour row

**Files:**
- Modify: `index.html` (`labFields`, `labSampleRow` → `labSampleBlock`, CSS for `.ppsample`)
- Test: `core/tests/web_site_lab.mjs`

**Interfaces:**
- Consumes: `session.raw(r)`, `session.kept(r)`, `session.block(r)`, `session.buf(r)` (Task 1).
- Produces (DOM): per sampler role `#pp-<role>-upload` (button), `#pp-<role>-file` (hidden input), `#pp-<role>-name`,
  `#pp-<role>-warn`, `#pp-<role>-wave` (canvas), all inside `.ppsample[data-role=<role>]`.

- [ ] **Step 1: Write the failing checks** (append to `web_site_lab.mjs` after "Freeze on the Third voice …", and change
  the upload steps to use `#pp-v3-file` — unchanged id — and wait on `#pp-v3-name`)

```js
  /* 3c.1: the sample block - inside its column, a button, a warning when a role is silent, a waveform */
  const inCol = (sel) => ev(`(function(){ var e = document.querySelector('${sel}'); if (!e) return false; var c = e.closest('.ppcol').getBoundingClientRect(), b = e.getBoundingClientRect(); return b.left >= c.left - 1 && b.right <= c.right + 1; })()`);
  check("a sampler role's sample block: an Upload button, its name and warning inside its column", await ev("!!document.querySelector('#pp-v3-upload')") && await inCol("#pp-v3-name") && await inCol("#pp-v3-warn"), null);
  check("no sample yet: the warning says the role is silent", /no sample.*silent/i.test(await ev("(document.querySelector('#pp-v3-warn') || {}).textContent || ''")), await ev("(document.querySelector('#pp-v3-warn') || {}).textContent"));
  check("Freeze's Colour row is labelled 'moment' and its readout is the time only", await ev("(function(){ var i = document.querySelector('#pp-body input[data-k=\"v3.colour\"]'), r = i && i.closest('.pprow'); return !!r && /^moment$/i.test(r.querySelector('span').textContent.trim()) && /^\\d+:\\d\\d$/.test(r.querySelector('i').textContent.trim()); })()"), null);
```

and after the upload is done ("autosaved: …"):

```js
  check("uploaded: the name inside its column, no warning, the waveform drawn", /probe-site\.wav/.test(await ev("(document.querySelector('#pp-v3-name') || {}).textContent || ''")) && await inCol("#pp-v3-name") && !(await ev("(document.querySelector('#pp-v3-warn') || {}).textContent || ''")) && await ev("(function(){ var c = document.querySelector('#pp-v3-wave'); if (!c || c.width < 100) return false; var d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data, n = 0; for (var i = 3; i < d.length; i += 4) if (d[i]) n++; return n > 200; })()"), null);
  const col0 = await ev("+document.querySelector('#pp-body input[data-k=\"v3.colour\"]').value");
  await ev("(function(){ var c = document.querySelector('#pp-v3-wave'), b = c.getBoundingClientRect(); ['pointerdown','pointerup'].forEach(function (t) { c.dispatchEvent(new PointerEvent(t, { clientX: b.left + b.width * 0.8, clientY: b.top + b.height / 2, bubbles: true })); }); return 1; })()");
  await sleep(300);
  const col1 = await ev("+document.querySelector('#pp-body input[data-k=\"v3.colour\"]').value");
  check("a click on Freeze's waveform moves its Moment (Colour)", col1 > 0.6 && col1 !== col0, [col0, col1]);
  /* Retune on an unpitched sample: the warning */
  await setSel("v3.synth", "s-retune"); await sleep(400);
  check("Retune with an unpitched sample warns", /Retune needs a pitched sample/i.test(await ev("(document.querySelector('#pp-v3-warn') || {}).textContent || ''")), await ev("(document.querySelector('#pp-v3-warn') || {}).textContent"));
  await setSel("v3.synth", "s-freeze"); await sleep(400);
  /* phone width: the block fits its column (Review Focus 5) */
  await s.send("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 2, mobile: true }); await sleep(800);
  check("phone width: the sample block fits its column", await inCol("#pp-v3-name") && await inCol("#pp-v3-wave") && await ev("document.documentElement.scrollWidth <= innerWidth + 1"), null);
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await sleep(800);
```

- [ ] **Step 2: Run to see it fail**

Run: `node --env-file=.env.local core/tests/web_site_lab.mjs`
Expected: FAIL at "a sampler role's sample block" (`#pp-v3-upload` missing).

- [ ] **Step 3: Implement in `index.html`**

1. Colour row: in `labFields`, the colour entry becomes
   `lab("colour", colourWord(st.synth), 0, 1, 0.0001, function (v) { return colourValue(st.synth, v, role); })` with
```js
  function colourWord(syn) { return { "s-retune": "brightness", "s-harmonic": "overtones", "s-formant": "partial", "s-pulsar": "grain", "s-freeze": "moment" }[syn] || "colour"; }
  function colourValue(syn, v, role) {
    if (syn === "s-formant") { return String(Math.round(1 + 15 * v)); }
    if (syn === "s-pulsar") { return Math.round(100 * (0.05 + 0.95 * v)) + " %"; }
    if (syn === "s-freeze") { var t = labMomentSeconds(role, v); return Math.floor(t / 60) + ":" + ("0" + Math.floor(t % 60)).slice(-2); }
    return Math.round(100 * v) + " %";
  }
```
   where `labMomentSeconds(role, v)` maps the Colour on the compacted sample to the time in the recording as recorded:
```js
  /* Freeze's Moment: Colour across the compacted sample (core: at = colour · (len − 2048)), shown at its time in the recording */
  function labMomentSeconds(role, v) {
    var S = labSession(), b = S.buf(role), kept = S.kept(role), B = S.block(role), raw = S.raw(role);
    if (!b || !kept || !raw) { return 0; }
    var pos = Math.max(0, Math.round(v * Math.max(0, b.length - 2048))), acc = 0;
    for (var k = 0; k < kept.length; k++) { if (!kept[k]) { continue; } var len = Math.min(B, raw.length - k * B); if (pos < acc + len) { return (k * B + (pos - acc)) / raw.sampleRate; } acc += len; }
    return raw.duration;
  }
  function labMomentColour(role, seconds) {      /* the inverse, for a click on the waveform: the nearest sounding moment */
    var S = labSession(), b = S.buf(role), kept = S.kept(role), B = S.block(role), raw = S.raw(role);
    if (!b || !kept || !raw) { return null; }
    var at = Math.round(seconds * raw.sampleRate), acc = 0, best = null;
    for (var k = 0; k < kept.length; k++) {
      if (!kept[k]) { continue; }
      var s0 = k * B, len = Math.min(B, raw.length - s0);
      if (at >= s0 && at < s0 + len) { best = acc + (at - s0); break; }
      if (s0 > at) { best = acc; break; }
      acc += len; best = acc - 1;
    }
    return Math.min(1, Math.max(0, best / Math.max(1, b.length - 2048)));
  }
```
2. The sample block replaces `labSampleRow`:
```js
  function labSampleBlock(role, f) {
    var S = labSession(), st = S.state(role), box = document.createElement("div");
    box.className = "ppsample"; box.dataset.role = role;
    var top = document.createElement("div"); top.className = "ppsample-top";
    var label = document.createElement("span"); label.textContent = "sample"; top.appendChild(label);
    var input = document.createElement("input"); input.type = "file"; input.accept = "audio/*"; input.id = "pp-" + role + "-file"; input.hidden = true;
    var btn = document.createElement("button"); btn.type = "button"; btn.className = "ghost"; btn.id = "pp-" + role + "-upload";
    btn.textContent = st.sample ? "Replace sample" : "Upload sample";
    btn.addEventListener("click", function () { input.click(); });
    input.addEventListener("change", function () { if (input.files[0]) { S.upload(role, input.files[0], f.properties.id).then(function () { labSaveSoon(f); }); } });
    top.appendChild(btn); top.appendChild(input); box.appendChild(top);
    var name = document.createElement("div"); name.className = "ppsample-name"; name.id = "pp-" + role + "-name";
    var nm = st.sample ? FsRoles.sampleLabel(st.sample.name, st.sample.analysis, st.sampleNote) : "";
    name.textContent = nm; name.title = nm; box.appendChild(name);
    var warn = document.createElement("div"); warn.className = "ppsample-warn"; warn.id = "pp-" + role + "-warn";
    warn.textContent = !st.sample ? "no sample - this role is silent"
      : /missing|failed|could not/.test(st.sampleNote || "") ? st.sampleNote
      : st.synth === "s-retune" && !(st.sample.analysis && st.sample.analysis.f0 > 0) ? "Retune needs a pitched sample - this one is unpitched" : "";
    box.appendChild(warn);
    var cv = document.createElement("canvas"); cv.id = "pp-" + role + "-wave"; cv.className = "ppsample-wave"; box.appendChild(cv);
    requestAnimationFrame(function () { labDrawWave(cv, role, st); });
    cv.addEventListener("pointerdown", function (e) {
      if (st.synth !== "s-freeze" || !S.raw(role)) { return; }
      var r = cv.getBoundingClientRect(), c = labMomentColour(role, (e.clientX - r.left) / r.width * S.raw(role).duration);
      if (c === null) { return; }
      st.colour = c; var inp = $("#pp-body input[data-k='" + role + ".colour']"); if (inp) { inp.value = c; inp.dispatchEvent(new Event("input")); }
      labDrawWave(cv, role, st);
    });
    return box;
  }
  function labDrawWave(cv, role, st) {
    var S = labSession(), raw = S.raw(role), kept = S.kept(role), B = S.block(role);
    var w = Math.max(100, Math.round(cv.getBoundingClientRect().width || 240)), h = 40, dpr = window.devicePixelRatio || 1;
    cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
    var g = cv.getContext("2d"); g.scale(dpr, dpr); g.clearRect(0, 0, w, h);
    if (!raw) { return; }
    var css = getComputedStyle(document.documentElement), lit = css.getPropertyValue("--accent").trim() || "#cfd8c8", dim = css.getPropertyValue("--dim").trim() || "#6b7468";
    var d = raw.getChannelData(0), per = d.length / w;
    for (var x = 0; x < w; x++) {
      var a = Math.floor(x * per), z = Math.min(d.length, Math.floor((x + 1) * per)), pk = 0;
      for (var i = a; i < z; i++) { var v = Math.abs(d[i]); if (v > pk) { pk = v; } }
      var on = kept && kept[Math.min(kept.length - 1, Math.floor(a / Math.max(1, B)))];
      g.fillStyle = on ? lit : dim; g.globalAlpha = on ? 1 : 0.35;
      var bh = Math.max(1, pk * (h - 2)); g.fillRect(x, (h - bh) / 2, 1, bh);
    }
    g.globalAlpha = 1;
    if (st.synth === "s-freeze") {
      var mx = labMomentSeconds(role, st.colour) / raw.duration * w;
      g.fillStyle = css.getPropertyValue("--warn").trim() || "#e0b25b"; g.fillRect(Math.round(mx) - 1, 0, 2, h);
    }
  }
```
   In the section loop, `fl.sampleRow` builds `labSampleBlock(fl.sampleRow, f)`. In the session `onChange`, a role-level
   change (`role` not null) redraws only that block's name/warning/waveform when the panel is open (`renderPatchPanel()`
   as now is acceptable; it is what happens today).
3. CSS (beside the other `#pp-body` rules):
```css
#pp-body .ppsample { display: grid; gap: 4px; margin: 2px 0 var(--s-1); min-width: 0; }
#pp-body .ppsample-top { display: flex; align-items: center; justify-content: space-between; gap: var(--s-1); }
#pp-body .ppsample-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; min-width: 0; font-size: 0.85em; }
#pp-body .ppsample-warn:empty { display: none; }
#pp-body .ppsample-warn { color: var(--warn); font-size: 0.85em; }
#pp-body .ppsample-wave { width: 100%; height: 40px; display: block; cursor: pointer; }
```
   (Use the tokens that exist in `index.html`'s `:root`; if `--warn`/`--accent` are named differently, use those names.)

- [ ] **Step 4: Run the tests**

Run: `node --env-file=.env.local core/tests/web_site_lab.mjs` → all PASS. `node core/tests/web_patch.mjs` → 36/36.

- [ ] **Step 5: Commit**

```bash
git add index.html core/tests/web_site_lab.mjs
git commit -m "3c.1: the sample block - upload, name, warning, waveform with Freeze's Moment; the Colour row's own word"
```

---

### Task 4: The live row (notes + meter), and every sampler sounding with bird-like samples

**Files:**
- Modify: `index.html` (`labFields` adds `{ liveRow: role }` after a sampler role's instrument row; `labLiveRow`; `coreMessage` "live" → `labLiveUpdate(m.roles)`)
- Test: `core/tests/web_site_lab.mjs`

**Interfaces:**
- Consumes: the live message's `roles` (Task 2), `noteName(midi)` (index.html).
- Produces (DOM): `#pp-<role>-notes` (text), `#pp-<role>-meter` (a bar element with `data-db`).

- [ ] **Step 1: Write the failing checks**

At the top of the test, two generated bird-like samples (written by the test to WAV `File`s in the page):
```js
const birdish = (kind, name) => `(function(){ var sr = 48000, n = sr * 12, b = new ArrayBuffer(44 + n * 2), v = new DataView(b), r = 11;
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
  var y1 = 0, y2 = 0;
  for (var i = 0; i < n; i++) {
    var t = i / sr, on = (t % 2.5) < 0.35 ? Math.sin(Math.PI * (t % 2.5) / 0.35) : 0, x;
    if (${JSON.stringify(kind)} === "tone") { x = on * 0.5 * Math.sin(2 * Math.PI * (5000 + 150 * Math.sin(2 * Math.PI * 30 * t)) * t); }
    else { r ^= r << 13; r >>>= 0; r ^= r >>> 17; r ^= r << 5; r >>>= 0; var w = (r / 4294967296) * 2 - 1; var hp = w - y1; y1 = w; y2 = 0.6 * y2 + 0.4 * hp; x = on * 0.6 * (hp - y2); }
    v.setInt16(44 + i * 2, Math.max(-32767, Math.min(32767, Math.round(32767 * x))), true);
  }
  return new File([b], ${JSON.stringify(name)}, { type: "audio/wav" }); })()`;
```
After the "… and on again: core-lab.wasm, one engine" check (sound on, lab on, walker on the draft), upload `birdish("noise", "probe-titmouse.wav")` to the Voice and `birdish("tone", "probe-blackbird.wav")` to the Second and Third voice through `#pp-<role>-file` (wait for each `#pp-<role>-name` to read "· unpitched" / "Hz"), then:
```js
  const R = ["voice", "sect", "v3"], quiet = {};
  for (const role of R) {
    for (const o of R) await setRange(o + ".gain", o === role ? "0.8" : "0");
    for (const syn of ["s-resonator", "s-harmonic", "s-formant", "s-pulsar", "s-freeze"]) {
      await setSel(role + ".synth", syn); await sleep(4000);
      const l = await lvl(); if (!(l > -45)) quiet[role + " " + syn] = +l.toFixed(1);
    }
  }
  check("bird-like samples (sparse calls, high band): every sampler sounds on every role (role alone, above -45 dB)", Object.keys(quiet).length === 0, quiet);
  for (const o of R) await setRange(o + ".gain", "0.55");
  await setSel("v3.synth", "s-freeze"); await sleep(3000);
  check("the live row: a sounding sampler role shows its notes and a meter above -60 dB",
    /[A-G]#?\d/.test(await ev("(document.querySelector('#pp-v3-notes') || {}).textContent || ''")) && (await ev("+(document.querySelector('#pp-v3-meter') || {}).dataset.db")) > -60,
    [await ev("(document.querySelector('#pp-v3-notes') || {}).textContent"), await ev("(document.querySelector('#pp-v3-meter') || {}).dataset.db")]);
  await setRange("v3.gain", "0"); await sleep(2500);
  check("... muted, its meter falls", (await ev("+document.querySelector('#pp-v3-meter').dataset.db")) < -70, await ev("document.querySelector('#pp-v3-meter').dataset.db"));
  await setRange("v3.gain", "0.55");
```
After the walk onto PUB2 (Review Focus 3):
```js
  check("walked onto another route: the draft's live notes are not left on screen", !(await ev("/[A-G]#?\\d/.test((document.querySelector('#pp-v3-notes') || {}).textContent || '')")), await ev("(document.querySelector('#pp-v3-notes') || {}).textContent"));
```
And after "lab mode off again" (Review Focus 4):
```js
  check("lab mode off: no live rows, no sample canvases", await ev("!document.querySelector('[id$=\"-notes\"], .ppsample-wave')"), null);
```

- [ ] **Step 2: Run to see it fail**

Run: `node --env-file=.env.local core/tests/web_site_lab.mjs`
Expected: FAIL at "the live row" (no `#pp-v3-notes`) — and the bird-like check fails before Tasks 1–2 land (they are in).

- [ ] **Step 3: Implement in `index.html`**

```js
  function labLiveRow(role) {
    var row = document.createElement("div"); row.className = "pprow pplive"; row.dataset.role = role;
    var name = document.createElement("span"); name.textContent = "now"; row.appendChild(name);
    var notes = document.createElement("span"); notes.className = "pplive-notes"; notes.id = "pp-" + role + "-notes"; row.appendChild(notes);
    var meter = document.createElement("i"); meter.className = "pplive-meter"; meter.id = "pp-" + role + "-meter"; meter.dataset.db = "-120";
    var bar = document.createElement("b"); meter.appendChild(bar); row.appendChild(meter);
    return row;
  }
  /* the engine's roles, ~30 a second (core-lab only): notes by name, the meter -60 … 0 dB */
  var LAB_ROLE_AT = { voice: 0, sect: 1, v3: 2 };
  function labLiveUpdate(roles) {
    if (!labOn() || $("#patchpanel").hidden) { return; }
    ["voice", "sect", "v3"].forEach(function (r) {
      var nEl = document.getElementById("pp-" + r + "-notes"), mEl = document.getElementById("pp-" + r + "-meter");
      if (!nEl || !mEl) { return; }
      var o = LAB_ROLE_AT[r] * 8, on = roles && core && core.state && core.state.route_id === selected;
      var db = on ? roles[o] : -120, cnt = on ? roles[o + 1] : 0, names = [];
      for (var i = 0; i < cnt; i++) { names.push(noteName(Math.round(69 + 12 * Math.log2(roles[o + 2 + i] / 440)))); }
      nEl.textContent = names.join(" ");
      mEl.dataset.db = String(Math.round(db));
      mEl.firstChild.style.width = Math.max(0, Math.min(100, (db + 60) / 60 * 100)) + "%";
    });
  }
```
- `labFields`: after the instrument entry of a sampler role push `{ liveRow: role }`; the section loop builds
  `labLiveRow(fl.liveRow)` for it.
- `coreMessage` "live" branch: after `window.__fa.coreLive = m;` add `if (m.roles) { labLiveUpdate(m.roles); }`.
- A route without the engine on it (the walker elsewhere) clears the row: `on` is false when the engine's route is not
  the panel's route.
- CSS:
```css
#pp-body .pplive-notes { font-size: 0.85em; min-width: 0; overflow: hidden; white-space: nowrap; text-overflow: ellipsis; }
#pp-body .pplive-meter { display: block; height: 6px; background: var(--line); border-radius: 3px; overflow: hidden; }
#pp-body .pplive-meter b { display: block; height: 100%; width: 0; background: var(--accent); transition: width 80ms linear; }
```

- [ ] **Step 4: Run the tests**

Run: `node --env-file=.env.local core/tests/web_site_lab.mjs` → all PASS. Then the whole regression one after another:
`python core/tests/run.py`, `node core/tests/web_patch.mjs`, `node core/tests/web_core.mjs`, `node core/tests/web_lab.mjs`,
`node --env-file=.env.local core/tests/web_lab_save.mjs`, `npm test`.

- [ ] **Step 5: Commit**

```bash
git add index.html core/tests/web_site_lab.mjs
git commit -m "3c.1: each sampler role's live notes and meter; bird-like samples sound on every role"
```
