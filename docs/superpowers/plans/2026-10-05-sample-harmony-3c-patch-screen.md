# Sample harmony 3c — samplers on the real patch screen, in lab mode — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A setter turns on "Samplers (lab)" in the site's own patch panel, sets any route role to a sampler with its
controls and sample, and hears the whole site (map walk, simulation, bed, zones) play it, while the live patch, the public
and the apps stay exactly as they are.

**Architecture:** One shared browser module, `web/lab-roles.js` (global `FsRoles`), holds the 3b role-setup logic
(load/save `lab_route_roles`, 30 s WAV upload, sample download/decode, overlay of a roles row on a patch). `lab.js` is
moved onto it. The site's patch panel gains a lab mode whose sampler rows read and write an `FsRoles` session, never the
live patch. In lab mode the site's engine boots `web/core-lab.wasm` instead of `web/core.wasm`, is given each route's
patch with the lab overlay, and is sent the walker's route's role samples through two new engine calls.

**Tech Stack:** C++ core → wasm (emscripten, `-O1`), AudioWorklet (`web/core-worklet.js`), plain ES5-style browser JS
in `index.html` and `web/*.js`, supabase-js v2, Node test runner, headless Chrome over CDP (`tools/cdp.mjs`).

**Spec:** `docs/superpowers/specs/2026-10-05-samples-3c-patch-screen-design.md`

## Global Constraints

- D1: the live site and the apps do not change until Kerem approves. The public keeps `web/core.wasm` (not rebuilt in
  3c) and the live patch; signed out or lab mode off, nothing new is requested or fetched.
- The live patch never contains an `s-*` synth. Sampler choices, controls and samples live only in `lab_route_roles` and
  `recordings/lab/<route id>/`.
- Lab mode off: the patch panel renders exactly as today (same rows, same order, same DOM).
- `lab_route_roles` row format (3b): `roles = { voice|sect|v3: { synth, gain, harm, index, sampler: { body, excite,
  method, mode, focus, colour, tune }, sample: { path, name, analysis } | null } }`.
- Autosave of lab role changes 0.6 s after the last change; Save refused while the route's roles load, after a failed
  load, or while a role's sample has not uploaded (3b final review I2/I3).
- Uploads are the first 30 s as 16-bit WAV (3b I4); sample names are text, never markup (3b I6); a sampler role with no
  sample plays silence (3b I5).
- Builds: `W=/c/Users/kerem/AppData/Local/Temp/emb; EM_CONFIG=C:/Users/kerem/tools/emsdk/.emscripten PATH="$W:$PATH" sh web/build-lab.sh`
  (rebuild `web/core-lab.wasm` only; never run `web/build.sh` in 3c).
- Test server: `node tools/serve.mjs 8765` at the repo root. Supabase tests need `.env.local`; run suites that create
  users one after another (auth rate limit).

## Review Focus

1. **A setter whose route has a sampler role walks off that route into another** — the engine must not keep playing the
   first route's sample on the second route's sampler role (expect: the second route's own samples, or silence).
2. **Lab mode toggled while sound plays** — the engine restarts on the other build without a stuck voice or a doubled
   engine (expect: one engine, the right wasm, sound continues after the switch).
3. **A digital role choice made in lab mode** — it must still reach the live patch exactly as with lab mode off
   (expect: `properties.patch.<role>.synth` updated, saved, and the lab row's `synth` the same digital value).
4. **The session expires while lab mode is on** — autosave must say "Not saved: …" in the panel and never write the
   live patch instead (expect: message, live patch unchanged).
5. **A route with no `lab_route_roles` row** — lab mode shows the live patch's digital synths and plays exactly the
   live sound (expect: the engine's patch for that route byte-identical to the live one).

Tests for 1, 3 and 5 are in Task 4 / Task 3; 2 and 4 are in Task 4 and Task 3.

---

### Task 1: Core — role samples through the engine, and the walker's route id

**Files:**
- Modify: `core/engine.cpp` (state JSON ~line 410; new functions after `fs_engine_solo` ~line 443)
- Modify: `core/fieldscape.h` (declarations beside `fs_engine_solo`)
- Modify: `web/core-worklet.js` (`FieldscapeEngine` message handler)
- Modify: `web/build-lab.sh` (exported functions)
- Test: `core/test.cpp` (after the "engine's live reads" block ~line 465)

**Interfaces:**
- Produces (C): `void fs_engine_role_source(fs_engine *e, int role, int channels, long long frames, const float *const *pcm);`
  `void fs_engine_role_analysis(fs_engine *e, int role, const char *analysis_json);` — both forward to the engine's piece
  (`fs_piece_role_source` / `fs_piece_role_analysis`).
- Produces (state JSON): `"route_id":"<feature id>"` beside `"route"` (empty string off any route).
- Produces (worklet): `FieldscapeEngine` accepts `{ type: "role", role, channels: Float32Array[] }` and
  `{ type: "analysis", role, bytes: Uint8Array }`, same shapes as `FieldscapeCore` (`web/core-worklet.js:44-56`).

- [ ] **Step 1: Write the failing test** (in `core/test.cpp`, after the engine live-reads block)

```cpp
    {   /* 3c: a route's sampler role, played by the whole engine, reads the role recording it is given; the state
           names the route by id */
        auto run = [&](bool give) {
            fs_engine *e = fs_engine_create(SR, B);
            fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
                "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
                "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{\"version\":17,"
                "\"prog\":[{\"r\":0,\"q\":\"m9\"},{\"r\":5,\"q\":\"maj7#11\"}],\"bed\":{\"on\":false},\"sect\":{\"on\":false},"
                "\"zones\":{\"on\":false},\"v3\":{\"on\":false},\"voice\":{\"synth\":\"s-freeze\"}}}}]}");
            std::vector<float> nz = noise_src(20, 0.5f, 7); const float *np[1] = { nz.data() };
            if (give) fs_engine_role_source(e, 0, 1, (long long)nz.size(), np);
            double en = 0; long n = 0;
            for (int b = 0; b < (int)(6.0 * SR / B); b++) {
                if (b % 40 == 0) fs_engine_step(e, 29.001, 41.0);
                fs_engine_process(e, B);
                if (b > (int)(2.0 * SR / B)) { const float *l = fs_engine_out(e, 0); for (int k = 0; k < B; k++) { en += (double)l[k] * l[k]; n++; } }
            }
            std::string st = fs_engine_state(e);
            fs_engine_destroy(e);
            return std::make_pair(10 * std::log10(en / n + 1e-30), st);
        };
        const auto off = run(false), on = run(true);
        std::printf("3c engine role source: %.1f dB without, %.1f dB with | %s\n", off.first, on.first, on.second.c_str());
        assert(on.first > off.first + 20 && on.second.find("\"route_id\":\"r1\"") != std::string::npos);
    }
```

- [ ] **Step 2: Run it to see it fail**

Run: `python core/tests/run.py`
Expected: FAIL — compile error, `fs_engine_role_source` not declared.

- [ ] **Step 3: Implement**

`core/fieldscape.h`, beside `fs_engine_solo`:
```c
/* Sample harmony 3c: a role's recording and its analysis for the engine's route sound (lab mode). role 0 voice,
   1 second voice, 2 third voice; the page sends the walker's route's samples when that route changes. */
void fs_engine_role_source(fs_engine *e, int role, int channels, long long frames, const float *const *pcm);
void fs_engine_role_analysis(fs_engine *e, int role, const char *analysis_json);
```

`core/engine.cpp`, after `fs_engine_solo`:
```cpp
void fs_engine_role_source(fs_engine *e, int role, int channels, long long frames, const float *const *pcm) { fs_piece_role_source(e->piece, role, channels, frames, pcm); }
void fs_engine_role_analysis(fs_engine *e, int role, const char *json) { fs_piece_role_analysis(e->piece, role, json); }
```

`core/engine.cpp`, the state line (~410): add the route id. Replace
```cpp
    e->state = "{\"route\":" + esc(e->route_name) + ",\"place\":" +
```
with
```cpp
    const std::string rid = e->route_name.empty() || r < 0 || r >= (int)e->route_ids.size() ? "" : e->route_ids[r];
    e->state = "{\"route\":" + esc(e->route_name) + ",\"route_id\":" + esc(rid) + ",\"place\":" +
```
(`r` is the route index computed above the `route_name` line at ~395; if it is out of scope there, hoist it.)

`web/core-worklet.js`, in `FieldscapeEngine`'s handler, before `} else if (m.type === "source") {`:
```js
        } else if (m.type === "role") {           /* 3c lab mode: a role's recording (as FieldscapeCore) */
          const n = m.channels[0].length, ptrs = x.malloc(4 * m.channels.length), bufs = m.channels.map(() => x.malloc(4 * n));
          if (!ptrs || bufs.some((p) => !p)) { bufs.forEach((p) => p && x.free(p)); if (ptrs) { x.free(ptrs); } this.port.postMessage({ type: "error", message: "role: out of memory" }); return; }
          bufs.forEach((p, k) => new Float32Array(x.memory.buffer, p, n).set(m.channels[k]));
          new Uint32Array(x.memory.buffer, ptrs, bufs.length).set(bufs);
          x.fs_engine_role_source(this.e, m.role, bufs.length, BigInt(n), ptrs);
          bufs.forEach((p) => x.free(p)); x.free(ptrs);
        } else if (m.type === "analysis") {
          const p = this.bytes(m.bytes); x.fs_engine_role_analysis(this.e, m.role, p); x.free(p);
```

`web/build-lab.sh`: append `_fs_engine_role_source,_fs_engine_role_analysis` to `-sEXPORTED_FUNCTIONS` (before
`_malloc`).

- [ ] **Step 4: Run tests and rebuild the lab wasm**

Run: `python core/tests/run.py` then the lab build (Global Constraints).
Expected: 20/20 (19 before + this one); `web/core-lab.wasm` rebuilt. `node core/tests/web_lab.mjs` 67/67.

- [ ] **Step 5: Commit**

```bash
git add core/engine.cpp core/fieldscape.h core/test.cpp web/core-worklet.js web/build-lab.sh web/core-lab.wasm
git commit -m "3c core: role samples through the whole engine, and the route id in its state"
```

---

### Task 2: `web/lab-roles.js` — the shared role-setup module, and lab.js on it

**Files:**
- Create: `web/lab-roles.js`
- Create: `tests/lab-roles-unit.test.mjs`
- Modify: `web/lab.js` (the 3b block ~lines 573-720 and `sendRolesAndPatch`), `lab.html` (load `web/lab-roles.js` before `web/lab.js`)
- Test: `core/tests/web_lab.mjs`, `core/tests/web_lab_save.mjs` (unchanged; they are the regression gate)

**Interfaces:**
- Produces (global `FsRoles`, also `module.exports` under Node):
  - `FsRoles.ROLES` = `["voice","sect","v3"]`, `FsRoles.ROLE_INDEX` = `{voice:0,sect:1,v3:2}`, `FsRoles.MAX_S` = 30
  - `FsRoles.SAMPLER` = `[["s-retune","Retune"],["s-resonator","Resonator"],["s-harmonic","Harmonic filter"],["s-formant","Formant"],["s-pulsar","Pulsar"],["s-freeze","Freeze"]]`
  - `FsRoles.isSampler(synth) -> boolean`
  - `FsRoles.esc(text) -> string` (HTML-escaped)
  - `FsRoles.sampleLabel(name, analysis, note) -> string`
  - `FsRoles.colourName(synth, colour, seconds) -> string` (seconds = the role's sample length, for Freeze's Moment)
  - `FsRoles.wav(audioBuffer) -> Blob` (16-bit PCM WAV)
  - `FsRoles.wavBytes(channels: Float32Array[], sampleRate) -> Uint8Array` (pure; `wav` wraps it)
  - `FsRoles.trimmed(ctx, audioBuffer) -> AudioBuffer` (first `MAX_S` s)
  - `FsRoles.overlay(patch, roles) -> patch` (deep copy; for each role whose `synth` is a sampler: `p[role].synth = synth`,
    `p[role].sampler = {...}`, `delete p[role].harm/index`; everything else, gain included, untouched; `roles` null → copy)
  - `FsRoles.session({ sb, ctx: () => AudioContext, analyse: (mono: Float32Array, sr: number) => Promise<analysis>|analysis, onChange: (role|null) => void })`
    returning `s` with:
    `s.state(role)` (lazily `{ synth: "", gain, harm, index, body:0, excite:0, method:0, mode:0, focus:0.5, colour:0.5, tune:1, sample:null, sampleNote:"" }`, defaults from `s.defaults[role]` which the page may set),
    `s.load(routeId) -> Promise` (generation-guarded; sets `s.loading`, `s.failed`, `s.loadedFor`; downloads samples),
    `s.json() -> roles`,
    `s.upload(role, file, routeId) -> Promise` (analyse, set buffer, upload WAV to `lab/<routeId>/<role>-<ms>.wav`),
    `s.save(routeId, userId) -> Promise<string|null>` (null on success, else the message; the 3b guards),
    `s.buf(role) -> AudioBuffer|null`, `s.ana(role) -> analysis|null`, `s.recId(role) -> number`, `s.gen` (generation).
  - `FsRoles.fetchRoute(sb, ctx, routeId) -> Promise<{ roles, buf: {role: AudioBuffer|null}, ana: {role: analysis|null} }>`
    (the row and every sample decoded and trimmed; a missing sample → a 128-frame silent buffer; no row → `roles: null`)
  - `FsRoles.sendRole(port, role, audioBuffer, analysis)` (posts `role` then `analysis` if any)

- [ ] **Step 1: Write the failing unit test** `tests/lab-roles-unit.test.mjs`

```js
/* Sample harmony 3c: the pure parts of the shared role-setup module (web/lab-roles.js) */
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
const FsRoles = createRequire(import.meta.url)("../web/lab-roles.js");

test("lab-roles: overlay puts a sampler role's synth and controls on a copy, leaving gain and the original alone", () => {
  const patch = { version: 17, voice: { synth: "fm", gain: 0.4, harm: 1, index: 4 }, sect: { synth: "fm", gain: 0.8, harm: 2, index: 7 } };
  const roles = { voice: { synth: "s-freeze", gain: 0.9, sampler: { focus: 0.8, colour: 0.2, tune: 1, body: 0, excite: 0, method: 0, mode: 0 } },
    sect: { synth: "am", gain: 0.1 } };
  const out = FsRoles.overlay(patch, roles);
  assert.equal(out.voice.synth, "s-freeze");
  assert.equal(out.voice.sampler.focus, 0.8);
  assert.equal(out.voice.gain, 0.4);
  assert.ok(!("harm" in out.voice) && !("index" in out.voice));
  assert.deepEqual(out.sect, patch.sect);                     /* a digital role in the row: the live patch's */
  assert.equal(patch.voice.synth, "fm");                      /* the original untouched */
  assert.deepEqual(FsRoles.overlay(patch, null), patch);
});

test("lab-roles: wavBytes writes a 16-bit PCM WAV, clipped", () => {
  const b = FsRoles.wavBytes([new Float32Array([0, 0.5, -1, 2])], 48000);
  const v = new DataView(b.buffer);
  assert.equal(String.fromCharCode(...b.slice(0, 4)), "RIFF");
  assert.equal(v.getUint32(24, true), 48000);
  assert.equal(v.getUint16(34, true), 16);
  assert.equal(v.getUint32(40, true), 8);
  assert.deepEqual([v.getInt16(44, true), v.getInt16(46, true), v.getInt16(48, true), v.getInt16(50, true)], [0, 16384, -32767, 32767]);
});

test("lab-roles: names are escaped, samplers recognised", () => {
  assert.equal(FsRoles.esc(`<img src="x">&'`), "&lt;img src=&quot;x&quot;&gt;&amp;&#39;");
  assert.ok(FsRoles.isSampler("s-pulsar") && !FsRoles.isSampler("fm") && !FsRoles.isSampler(""));
  assert.equal(FsRoles.sampleLabel("a.wav", { f0: 220.4 }, "saved"), "a.wav · 220 Hz · saved");
  assert.equal(FsRoles.colourName("s-freeze", 0.5, 62), "Moment 0:30");
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test tests/lab-roles-unit.test.mjs`
Expected: FAIL — `Cannot find module '../web/lab-roles.js'`.

- [ ] **Step 3: Implement `web/lab-roles.js`**

Move the 3b functions out of `web/lab.js` (current code at `web/lab.js` ~573-720: `esc`, `sampleLabel`, `trimmed`,
`wav`, `onRoleFile`, `rolesJson`, `saveRoute`, `loadRoles`, `loadSample`, the generation fields) into the module, with
these shapes. DOM is never touched here; the page re-renders on `onChange(role)`.

```js
/* Sample harmony 3b/3c: a route's role setups (lab_route_roles) and their samples (recordings/lab/<route>/), shared by
   lab.html and the site's patch panel in lab mode (docs/superpowers/specs/2026-10-05-samples-3c-patch-screen-design.md). */
(function (root) {
  "use strict";
  var ROLES = ["voice", "sect", "v3"], ROLE_INDEX = { voice: 0, sect: 1, v3: 2 }, MAX_S = 30;
  var SAMPLER = [["s-retune", "Retune"], ["s-resonator", "Resonator"], ["s-harmonic", "Harmonic filter"], ["s-formant", "Formant"],
    ["s-pulsar", "Pulsar"], ["s-freeze", "Freeze"]];
  var NAMES = { voice: "Voice", sect: "Sections", v3: "Third voice" };
  function isSampler(s) { return typeof s === "string" && s.indexOf("s-") === 0; }
  function esc(t) { return String(t).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/'/g, "&#39;").replace(/"/g, "&quot;"); }
  function sampleLabel(name, a, note) { return name + " · " + (a && a.f0 > 0 ? Math.round(a.f0) + " Hz" : "unpitched") + (note ? " · " + note : ""); }
  function colourName(syn, c, seconds) {
    if (syn === "s-retune") { return "Brightness"; }
    if (syn === "s-harmonic") { return "Overtones"; }
    if (syn === "s-formant") { return "Partial " + Math.round(1 + 15 * c); }
    if (syn === "s-pulsar") { return "Grain " + Math.round(100 * (0.05 + 0.95 * c)) + " %"; }
    if (syn === "s-freeze") { var s = Math.max(0, c * ((seconds || 0) - 2048 / 48000)); return "Moment " + Math.floor(s / 60) + ":" + ("0" + Math.floor(s % 60)).slice(-2); }
    return "Colour";
  }
  function wavBytes(chs, sr) {   /* ponytail: 16-bit; 24-bit if quiet samples ever sound grainy */
    var ch = chs.length, n = chs[0].length, v = new DataView(new ArrayBuffer(44 + n * ch * 2));
    var w = function (o, t) { for (var i = 0; i < t.length; i++) { v.setUint8(o + i, t.charCodeAt(i)); } };
    w(0, "RIFF"); v.setUint32(4, 36 + n * ch * 2, true); w(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, ch, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * ch * 2, true); v.setUint16(32, ch * 2, true); v.setUint16(34, 16, true); w(36, "data"); v.setUint32(40, n * ch * 2, true);
    for (var i = 0, o = 44; i < n; i++) { for (var c = 0; c < ch; c++, o += 2) { v.setInt16(o, Math.round(32767 * Math.max(-1, Math.min(1, chs[c][i]))), true); } }
    return new Uint8Array(v.buffer);
  }
  function wav(b) { var chs = []; for (var c = 0; c < b.numberOfChannels; c++) { chs.push(b.getChannelData(c)); } return new Blob([wavBytes(chs, b.sampleRate)], { type: "audio/wav" }); }
  function trimmed(ctx, b) {
    var n = Math.min(b.length, Math.round(MAX_S * b.sampleRate)), t = ctx.createBuffer(b.numberOfChannels, n, b.sampleRate);
    for (var c = 0; c < b.numberOfChannels; c++) { t.copyToChannel(b.getChannelData(c).subarray(0, n), c); }
    return t;
  }
  function silence(ctx) { return ctx.createBuffer(1, 128, ctx.sampleRate); }
  function overlay(patch, roles) {
    var p = JSON.parse(JSON.stringify(patch || {}));
    if (!roles) { return p; }
    ROLES.forEach(function (r) {
      var rr = roles[r]; if (!rr || !isSampler(rr.synth)) { return; }
      p[r] = p[r] || {}; p[r].synth = rr.synth; p[r].sampler = JSON.parse(JSON.stringify(rr.sampler || {}));
      delete p[r].harm; delete p[r].index;
    });
    return p;
  }
  function decodeSample(sb, ctx, smp) {
    return sb.storage.from("recordings").download(smp.path).then(function (d) {
      if (d.error || !d.data) { throw new Error((d.error && d.error.message) || "not found"); }
      return d.data.arrayBuffer();
    }).then(function (ab) { return ctx.decodeAudioData(ab); }).then(function (b) { return trimmed(ctx, b); });
  }
  function fetchRoute(sb, ctx, id) {
    return sb.from("lab_route_roles").select("roles").eq("route_id", id).maybeSingle().then(function (q) {
      if (q.error) { throw new Error(q.error.message); }
      var roles = q.data ? q.data.roles || {} : null, buf = {}, ana = {};
      if (!roles) { return { roles: null, buf: buf, ana: ana }; }
      return Promise.all(ROLES.map(function (r) {
        var rr = roles[r]; if (!rr || !isSampler(rr.synth)) { return null; }
        if (!rr.sample || !rr.sample.path) { buf[r] = silence(ctx); ana[r] = null; return null; }
        return decodeSample(sb, ctx, rr.sample).then(function (b) { buf[r] = b; ana[r] = rr.sample.analysis || null; },
          function () { buf[r] = silence(ctx); ana[r] = null; });
      })).then(function () { return { roles: roles, buf: buf, ana: ana }; });
    });
  }
  function sendRole(port, role, b, a) {
    var ch = []; for (var k = 0; k < b.numberOfChannels; k++) { ch.push(b.getChannelData(k).slice(0)); }
    port.postMessage({ type: "role", role: role, channels: ch });
    if (a) { port.postMessage({ type: "analysis", role: role, bytes: new TextEncoder().encode(JSON.stringify(a)) }); }
  }
  function session(o) {
    var s = { defaults: { voice: { gain: 0.45, harm: 1, index: 4 }, sect: { gain: 0.8, harm: 2.02, index: 7.5 }, v3: { gain: 0.55, harm: 1.5, index: 3 } },
      gen: 0, loading: false, failed: false, loadedFor: null };
    var st = {}, bufs = {}, anas = {}, recIds = { voice: 0, sect: 0, v3: 0 }, uploading = [];
    var changed = function (r) { if (o.onChange) { o.onChange(r); } };
    s.state = function (r) {
      if (!st[r]) { var d = s.defaults[r]; st[r] = { synth: "", gain: d.gain, harm: d.harm, index: d.index, body: 0, excite: 0, method: 0, mode: 0, focus: 0.5, colour: 0.5, tune: 1, sample: null, sampleNote: "" }; }
      return st[r];
    };
    s.buf = function (r) { return bufs[r] || null; };
    s.ana = function (r) { return anas[r] || null; };
    s.recId = function (r) { return recIds[r]; };
    s.json = function () {
      var out = {};
      ROLES.forEach(function (r) { var x = s.state(r);
        out[r] = { synth: x.synth, gain: x.gain, harm: x.harm, index: x.index,
          sampler: { body: x.body, excite: x.excite, method: x.method, mode: x.mode, focus: x.focus, colour: x.colour, tune: x.tune },
          sample: x.sample && x.sample.path ? { path: x.sample.path, name: x.sample.name, analysis: x.sample.analysis } : null }; });
      return out;
    };
    /* each route choice is a generation: a load or upload from an earlier one never lands on the route now shown (3b I2/I5) */
    s.load = function (id) {
      st = {}; bufs = {}; anas = {}; var gen = ++s.gen; s.failed = false; s.loadedFor = null;
      if (!o.sb || !id) { s.loading = false; changed(null); return Promise.resolve(); }
      s.loading = true; changed(null);
      return o.sb.from("lab_route_roles").select("roles").eq("route_id", id).maybeSingle().then(function (q) {
        if (gen !== s.gen) { return; }
        s.loading = false;
        if (q.error) { s.failed = true; s.loadError = q.error.message; changed(null); return; }
        s.loadedFor = id;
        var roles = q.data ? q.data.roles || {} : {};
        ROLES.forEach(function (r) {
          var rr = roles[r]; if (!rr) { return; }
          var x = s.state(r); x.synth = rr.synth || "";
          ["gain", "harm", "index"].forEach(function (k) { if (rr[k] != null) { x[k] = rr[k]; } });
          if (rr.sampler) { Object.keys(rr.sampler).forEach(function (k) { x[k] = rr.sampler[k]; }); }
          x.sample = rr.sample || null; x.sampleNote = rr.sample ? "loading…" : "";
          if (rr.sample && rr.sample.path) {
            decodeSample(o.sb, o.ctx(), rr.sample).then(function (b) {
              if (gen !== s.gen) { return; } bufs[r] = b; anas[r] = rr.sample.analysis; recIds[r]++; x.sampleNote = ""; changed(r);
            }, function (e) {
              if (gen !== s.gen) { return; } bufs[r] = silence(o.ctx()); anas[r] = null; recIds[r]++;
              x.sampleNote = "sample missing (" + (e && e.message || e) + ") - this role is silent"; changed(r);
            });
          }
        });
        changed(null);
      });
    };
    s.upload = function (r, file, id) {
      var x = s.state(r), gen = s.gen;
      return file.arrayBuffer().then(function (ab) { return o.ctx().decodeAudioData(ab); }).then(function (b) {
        var buf = trimmed(o.ctx(), b), n = buf.length, mono = new Float32Array(n);
        for (var c = 0; c < buf.numberOfChannels; c++) { var d = buf.getChannelData(c); for (var i = 0; i < n; i++) { mono[i] += d[i] / buf.numberOfChannels; } }
        return Promise.resolve(o.analyse(mono, buf.sampleRate)).then(function (a) { return { buf: buf, analysis: a }; });
      }).then(function (res) {
        if (gen !== s.gen) { return; }
        bufs[r] = res.buf; anas[r] = res.analysis; recIds[r]++;
        x.sample = { path: null, name: file.name, analysis: res.analysis };
        if (!o.sb || !id) { x.sampleNote = "not saved: log in as a setter on the site to save"; changed(r); return; }
        var path = "lab/" + id + "/" + r + "-" + Date.now() + ".wav";
        x.sampleNote = "uploading…"; changed(r);
        var up = o.sb.storage.from("recordings").upload(path, wav(res.buf), { upsert: false, contentType: "audio/wav" }).then(function (u) {
          if (u.error) { x.sampleNote = "upload failed: " + u.error.message; } else { x.sample.path = path; x.sampleNote = "uploaded"; }
          changed(r);
        });
        uploading.push(up);
        return up;
      }).catch(function (e) { x.sampleNote = "could not read " + file.name + ": " + (e && e.message || e); changed(r); });
    };
    s.save = function (id, userId) {
      if (!o.sb || !userId) { return Promise.resolve("Log in as a setter on the site to save."); }
      if (!id) { return Promise.resolve("Choose a route first."); }
      if (s.loading) { return Promise.resolve("Not saved: the route's saved roles are still loading - try again in a moment."); }
      if (s.failed) { return Promise.resolve("Not saved: the route's saved roles did not load, so saving could overwrite them - choose the route again."); }
      var gen = s.gen, waiting = uploading.slice(); uploading = [];
      return Promise.all(waiting).then(function () {
        if (gen !== s.gen) { return "Not saved: the route changed while the sample uploaded"; }
        var bad = ROLES.filter(function (r) { var x = s.state(r); return x.sample && !x.sample.path; })[0];
        if (bad) { return "Not saved: the " + NAMES[bad] + " sample did not upload - choose it again"; }
        return o.sb.from("lab_route_roles").upsert({ route_id: id, roles: s.json(), updated_by: userId }).then(function (q) { return q.error ? "Not saved: " + q.error.message : null; });
      }).catch(function (e) { return "Not saved: " + (e && e.message || e); });
    };
    return s;
  }
  var api = { ROLES: ROLES, ROLE_INDEX: ROLE_INDEX, MAX_S: MAX_S, SAMPLER: SAMPLER, NAMES: NAMES, isSampler: isSampler, esc: esc,
    sampleLabel: sampleLabel, colourName: colourName, wavBytes: wavBytes, wav: wav, trimmed: trimmed, overlay: overlay,
    fetchRoute: fetchRoute, sendRole: sendRole, session: session };
  if (typeof module !== "undefined" && module.exports) { module.exports = api; } else { root.FsRoles = api; }
})(typeof self !== "undefined" ? self : this);
```

Then move `web/lab.js` onto it:
- `lab.html`: `<script src="web/lab-roles.js"></script>` before the lab.js loader.
- In `web/lab.js`, delete the moved functions and state (`roleBuf`, `roleAna`, `roleRecId`, `uploading`, `rolesGen`,
  `rolesLoading`, `rolesFailed`, `rolesFor`, `rtState`, `esc`, `sampleLabel`, `trimmed`, `wav`, `rolesJson`,
  `loadSample`); create `var roles = FsRoles.session({ sb: sb, ctx: audio, analyse: analyseMono, onChange: onRolesChange })`
  where `analyseMono(mono, sr)` is the wasm part of the current `analyseFile`, and `onRolesChange(role)` rebuilds that
  role's controls (`buildRoleCtl(role)`; on `null` set every role menu from `roles.state(r).synth` and rebuild all), then
  `rtResend()`.
- `roleState(role)` becomes `roles.state(role)` (with `roles.defaults[role]` filled from `route.patch[role]` in
  `setRoute`); `roleSynth(role)` reads the menu as now.
- `onRoleFile(role, file)` → `roles.upload(role, file, me && route ? route.id : null)`.
- `saveRoute()` → `roles.save(route && route.id, me && me.id).then(function (m) { rtMsg(m || "Saved - " + name + "'s roles and samples (the live route is unchanged)."); })`
  with `name` captured before the call.
- `loadRoles(id)` → `roles.load(id)`; `fsLab.rolesFor` → `roles.loadedFor`.
- `sendRolesAndPatch`: `own = roles.buf(r)`, `ana = own ? roles.ana(r) : buffer ? analysis : null`,
  `key = own ? "r" + roles.recId(r) : buffer ? "g" + recId : "none"`, and send with `FsRoles.sendRole(pnode.port, r[1], buf, ana)`.
- `buildRoleCtl` labels: `FsRoles.esc(FsRoles.sampleLabel(...))`; colour name via
  `FsRoles.colourName(syn, st.colour, roles.buf(role) ? roles.buf(role).duration : buffer ? buffer.duration : 0)`.

- [ ] **Step 4: Run the tests**

Run: `node --test tests/lab-roles-unit.test.mjs` → 3/3 PASS.
Run: `node core/tests/web_lab.mjs` → 67/67; `node --env-file=.env.local core/tests/web_lab_save.mjs` → 15/15.
Expected: all pass (the lab behaves exactly as before; these two suites are the gate).

- [ ] **Step 5: Commit**

```bash
git add web/lab-roles.js tests/lab-roles-unit.test.mjs web/lab.js lab.html
git commit -m "3c: the role-setup logic in one shared module (web/lab-roles.js); the lab on it"
```

---

### Task 3: The site's patch panel in lab mode

**Files:**
- Modify: `index.html` — the patch panel header (~line 1921: `#pp-reset`, `#pp-close`), `buildRow` (~12149),
  `renderPatchPanel` (~12065), the setter state (`setter`, ~2564), `window.__fa` (test hooks), and a `<script src="web/lab-roles.js">` beside the supabase script.
- Create: `core/tests/web_site_lab.mjs`

**Interfaces:**
- Consumes: `FsRoles.session`, `FsRoles.SAMPLER`, `FsRoles.isSampler`, `FsRoles.esc`, `FsRoles.sampleLabel`, `FsRoles.colourName` (Task 2).
- Produces (page): `labMode` (boolean, `localStorage["fs-lab-mode"] === "1"` and `setter.signedIn`), `labRoles`
  (the `FsRoles.session` for the selected route), `labRoutes` (`{ routeId: roles }` cache used by Task 4),
  `labChanged()` (fires after any lab role change; Task 4 hooks it to resync the engine), test hooks
  `window.__fa.signIn(email, password) -> Promise<string|null>`, `window.__fa.labMode` (getter),
  `window.__fa.labRolesFor` (getter: `labRoles.loadedFor`).

- [ ] **Step 1: Write the failing browser test** `core/tests/web_site_lab.mjs`

Setup as `core/tests/web_lab_save.mjs`: a probe setter (`probe-site-lab@fieldarc.test`; delete any leftover user with
that email first), a DRAFT route `00000000-0000-4000-8000-00000000003d` in place `LAB3C` with a v17 patch
(`defaultPatch`-like: copy `properties.patch` from a published route via the service client, set `published: false`,
`name: "lab site draft"`), headless Chrome on port 9274, the site at `FS_URL || http://localhost:8765/`. Wait for
`window.__fa && window.fsListen`. Cleanup in `finally` exactly as `web_lab_save.mjs` (objects under `lab/<DRAFT>/`, the
roles row, the feature, audit, setter, user). Checks:

```js
  /* signed out: no switch, no lab request */
  const reqs = []; s.on((m) => { if (m.method === "Network.requestWillBeSent") reqs.push(m.params.request.url); });
  await s.send("Network.enable");
  await open();
  await ev(`fsListen.select(${JSON.stringify(PUB)}), 0`); await sleep(900);   /* a published route; the panel opens for setters only */
  check("signed out: no Samplers (lab) switch", !(await ev("!!document.querySelector('#pp-lab')")), null);
  check("signed out: nothing asked of lab_route_roles, core-lab.wasm never fetched", !reqs.some((u) => /lab_route_roles|core-lab\.wasm/.test(u)), reqs.filter((u) => /lab/.test(u)));

  /* signed in, lab mode off: the panel as before */
  await ev(`__fa.signIn(${JSON.stringify(EMAIL)}, ${JSON.stringify(PASSWORD)})`);
  await until(`__fa.features().some(function (f) { return f.properties.id === '${DRAFT}'; })`, 10000);
  await ev(`fsListen.select('${DRAFT}'), 0`); await sleep(900);
  await ev("document.querySelector('#f-patch').click(), 0"); await sleep(900);
  const rowsOff = await ev("[].map.call(document.querySelectorAll('#pp-body .pprow'), function (r) { var i = r.querySelector('input,select,button'); return (r.querySelector('span') || {}).textContent + '|' + (i && (i.dataset.k || i.tagName)); }).join(';')");
  check("signed in: the Samplers (lab) switch is in the panel header, off", await ev("!!document.querySelector('#pp-lab') && document.querySelector('#pp-lab').getAttribute('aria-pressed') === 'false'"), null);
  check("lab mode off: no Sampler group in any instrument menu", await ev("!document.querySelector(\"#pp-body optgroup[label='Sampler']\")"), null);

  /* lab mode on: Digital and Sampler groups; Freeze shows its rows, hides harm/index */
  const patch0 = (await db.from("features").select("properties").eq("id", DRAFT).single()).data.properties.patch;
  await ev("document.querySelector('#pp-lab').click(), 0");
  await until("__fa.labRolesFor === '" + DRAFT + "'", 8000);
  check("lab mode on: each role's instrument menu has Digital and Sampler groups",
    await ev("['voice','sect','v3'].every(function (r) { var s = document.querySelector('#pp-body select[data-k=\"' + r + '.synth\"]'); return s && s.querySelector(\"optgroup[label='Digital'] option[value='fm']\") && s.querySelectorAll(\"optgroup[label='Sampler'] option\").length === 6; })"), null);
  await ev("(function(){ var s = document.querySelector('#pp-body select[data-k=\"v3.synth\"]'); s.value = 's-freeze'; s.dispatchEvent(new Event('change')); return 1; })()");
  await sleep(400);
  check("Freeze on the Third voice: Focus, Colour, Tune and a Sample row; no harm/index row",
    await ev("['v3.focus','v3.colour','v3.tune'].every(function (k) { return !!document.querySelector('#pp-body [data-k=\"' + k + '\"]'); }) && !!document.querySelector('#pp-v3-file') && !document.querySelector('#pp-body [data-k=\"v3.harm\"]') && !document.querySelector('#pp-body [data-k=\"v3.index\"]')"), null);

  /* upload, move Focus: autosaved to the lab row; the live patch unchanged byte for byte */
  await ev(`(function(){ var f = ${noise(3, "probe-site.wav")}, dt = new DataTransfer(); dt.items.add(f); var i = document.querySelector('#pp-v3-file'); i.files = dt.files; i.dispatchEvent(new Event('change')); return 1; })()`);
  await until("/uploaded|failed/.test((document.querySelector('#pp-v3-sample') || {}).textContent || '')", 15000);
  await ev("(function(){ var s = document.querySelector('#pp-body input[data-k=\"v3.focus\"]'); s.value = '0.77'; s.dispatchEvent(new Event('input')); return 1; })()");
  await sleep(2500);
  const row = (await db.from("lab_route_roles").select("roles").eq("route_id", DRAFT).maybeSingle()).data;
  check("autosaved: the lab row has Freeze, Focus 0.77 and the sample", row && row.roles.v3.synth === "s-freeze" && row.roles.v3.sampler.focus === 0.77 && row.roles.v3.sample && row.roles.v3.sample.path.indexOf(`lab/${DRAFT}/v3-`) === 0, row);
  const patch1 = (await db.from("features").select("properties").eq("id", DRAFT).single()).data.properties.patch;
  check("the live patch is unchanged byte for byte (no s-* synth in it)", JSON.stringify(patch1) === JSON.stringify(patch0), [patch0.v3, patch1.v3]);

  /* reload: lab mode remembered, the role back */
  await open(); await ev(`fsListen.select('${DRAFT}'), 0`); await sleep(900);
  await ev("document.querySelector('#f-patch').click(), 0");
  await until("__fa.labRolesFor === '" + DRAFT + "'", 8000);
  const back = await until("(document.querySelector('#pp-body select[data-k=\"v3.synth\"]') || {}).value === 's-freeze' && +(document.querySelector('#pp-body input[data-k=\"v3.focus\"]') || {}).value === 0.77 && /probe-site\\.wav/.test((document.querySelector('#pp-v3-sample') || {}).textContent || '')", 10000);
  check("reloaded: lab mode still on, the Third voice Freeze again with its Focus and sample", back, null);

  /* back to digital: written to the live patch as today */
  await ev("(function(){ var s = document.querySelector('#pp-body select[data-k=\"v3.synth\"]'); s.value = 'pluck'; s.dispatchEvent(new Event('change')); return 1; })()");
  await sleep(2500);
  const patch2 = (await db.from("features").select("properties").eq("id", DRAFT).single()).data.properties.patch;
  const row2 = (await db.from("lab_route_roles").select("roles").eq("route_id", DRAFT).maybeSingle()).data;
  check("a digital choice in lab mode reaches the live patch, and the lab row follows", patch2.v3.synth === "pluck" && row2.roles.v3.synth === "pluck" && row2.roles.v3.sample, [patch2.v3.synth, row2 && row2.roles.v3.synth]);

  /* lab mode off: the panel exactly as before */
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(600);
  const rowsOff2 = await ev(/* same expression as rowsOff */ "[].map.call(document.querySelectorAll('#pp-body .pprow'), function (r) { var i = r.querySelector('input,select,button'); return (r.querySelector('span') || {}).textContent + '|' + (i && (i.dataset.k || i.tagName)); }).join(';')");
  check("lab mode off again: the panel's rows are exactly those before (with the new pluck)", rowsOff2.replace(/v3\.harm|v3\.index/g, "") === rowsOff.replace(/v3\.harm|v3\.index/g, ""), [rowsOff, rowsOff2]);

  /* an expired session: autosave says so and never touches the live patch (Review Focus 4) */
  await ev("document.querySelector('#pp-lab').click(), 0"); await until("__fa.labRolesFor === '" + DRAFT + "'", 8000);
  await ev("__fa.signOutQuietly && __fa.signOutQuietly()");
  await ev("(function(){ var s = document.querySelector('#pp-body select[data-k=\"v3.synth\"]'); s.value = 's-pulsar'; s.dispatchEvent(new Event('change')); return 1; })()");
  await sleep(2500);
  const patch3 = (await db.from("features").select("properties").eq("id", DRAFT).single()).data.properties.patch;
  check("signed out mid-edit: the panel says Not saved, the live patch has no s-* synth", /Not saved|Log in/.test(await ev("(document.querySelector('#pp-lab-msg') || {}).textContent || ''")) && !/"s-/.test(JSON.stringify(patch3)), await ev("(document.querySelector('#pp-lab-msg') || {}).textContent"));
  check("no page errors", errors.length === 0, errors);
```

(`noise`, `open`, `until`, `ev`, `check` as in `core/tests/web_lab_save.mjs`. `PUB` = the first published route id from
`public_features`. `__fa.signOutQuietly` signs the client out without reloading: `sb.auth.signOut({ scope: "local" })`.)

- [ ] **Step 2: Run it to see it fail**

Run: `node --env-file=.env.local core/tests/web_site_lab.mjs`
Expected: FAIL at "signed in: the Samplers (lab) switch is in the panel header" (`__fa.signIn` and `#pp-lab` missing).

- [ ] **Step 3: Implement in `index.html`**

1. Load the module: `<script src="web/lab-roles.js"></script>` right after the supabase-js script tag.
2. Test hooks in `window.__fa`:
   `signIn: function (e, p) { return sb ? sb.auth.signInWithPassword({ email: e, password: p }).then(function (r) { return r.error ? r.error.message : null; }) : Promise.resolve("no client"); }`,
   `signOutQuietly: function () { return sb ? sb.auth.signOut({ scope: "local" }) : null; }`,
   getters `labMode` and `labRolesFor`.
3. Header button beside `#pp-reset`: `<button type="button" id="pp-lab" class="ghost" aria-pressed="false" hidden>Samplers (lab)</button>`
   and below the header `<p id="pp-lab-msg" aria-live="polite" hidden></p>`.
4. State and switch (near `commitPatch`):

```js
  /* Sample harmony 3c: samplers on this panel, setters only, apart from the live patch (lab_route_roles + recordings/lab/),
     docs/superpowers/specs/2026-10-05-samples-3c-patch-screen-design.md. Off: the panel is exactly as before. */
  var LAB_KEY = "fs-lab-mode", labWanted = false, labRoles = null, labSaveTimer = null, labAnalyse = null;
  try { labWanted = localStorage.getItem(LAB_KEY) === "1"; } catch (e) { /* none */ }
  function labOn() { return labWanted && setter.signedIn && !!sb && typeof FsRoles !== "undefined"; }
  function labMsg(t) { var m = $("#pp-lab-msg"); if (m) { m.textContent = t || ""; m.hidden = !t; } }
  function labChanged() { if (typeof labEngineSync === "function") { labEngineSync(); } }    /* Task 4 */
  function labSession() {
    if (!labRoles) {
      labRoles = FsRoles.session({ sb: sb, ctx: labCtx, analyse: labAnalyseMono, onChange: function () { if (!$("#patchpanel").hidden) { renderPatchPanel(); } labChanged(); } });
    }
    return labRoles;
  }
  function labCtx() { if (!labCtx.c) { labCtx.c = new (window.AudioContext || window.webkitAudioContext)(); } return labCtx.c; }
  /* the analyser runs on this page from the lab build (fs_analyse), loaded on the first upload only */
  function labAnalyseMono(mono, sr) {
    if (!labAnalyse) {
      labAnalyse = fetch("web/core-lab.wasm").then(function (r) { return r.arrayBuffer(); }).then(function (b) {
        return WebAssembly.instantiate(b, { env: new Proxy({}, { get: function () { return function () { return 0; }; } }), wasi_snapshot_preview1: new Proxy({}, { get: function () { return function () { return 0; }; } }) });
      }).then(function (r) { var x = r.instance.exports; if (x._initialize) { x._initialize(); } return x; });
    }
    return labAnalyse.then(function (x) {
      var p = x.malloc(mono.length * 4); new Float32Array(x.memory.buffer, p, mono.length).set(mono);
      var need = x.fs_analyse(p, BigInt(mono.length), sr, 0, 0), out = x.malloc(need + 1);
      x.fs_analyse(p, BigInt(mono.length), sr, out, need + 1);
      var a = JSON.parse(new TextDecoder().decode(new Uint8Array(x.memory.buffer, out, need))); x.free(out); x.free(p);
      return a;
    });
  }
  function labSaveSoon(f) {
    clearTimeout(labSaveTimer);
    labSaveTimer = setTimeout(function () {
      labSession().save(f.properties.id, setter.id).then(function (m) { labMsg(m); if (!m) { labRoutesSet(f.properties.id, labSession().json()); } });
    }, 600);
    labChanged();
  }
```

   (`labRoutesSet(id, roles)` is defined in Task 4; in this task define it as a stub that stores into `labRoutes[id]`:
   `var labRoutes = {}; function labRoutesSet(id, r) { labRoutes[id] = r; labChanged(); }`.)

   The switch: shown when `setter.signedIn` (in `applyModeGating` or wherever `#f-patch` visibility is set, add
   `$("#pp-lab").hidden = !setter.signedIn;`). Click handler:
   `labWanted = !labWanted; try { localStorage.setItem(LAB_KEY, labWanted ? "1" : "0"); } catch (e) {} $("#pp-lab").setAttribute("aria-pressed", String(labOn())); labMsg(""); if (labOn()) { labSession().load(feature(selected).properties.id); } renderPatchPanel(); labChanged();`
   In `renderPatchPanel`, set `aria-pressed` from `labOn()`, and when `labOn()` and `labSession().gen === 0 || labSession().loadedFor !== f.properties.id && !labSession().loading`, call `labSession().load(f.properties.id)`.
   Fill `labSession().defaults[role] = { gain: patch[role].gain, harm: patch[role].harm, index: patch[role].index }` before loading.

5. `buildRow`: accept field-level `get`/`set` and option groups. In the toggle/select/range branches replace
   `pget(patch, fl.k)` with `(fl.get ? fl.get() : pget(patch, fl.k))`; in the select `change` and range `input`
   handlers, when `fl.set` exists call `fl.set(value)` and skip `pset`/`commitPatch` (and the `.synth` defaults block);
   set `input.dataset.k = fl.k` on selects too. Groups: when `fl.groups` (an array of `[label, [[value, text], …]]`),
   build `<optgroup label=…>` children instead of the flat options.

6. `labFields(patch, f)`: when `labOn()`, `renderPatchPanel` maps `PATCH_FIELDS` through it before gathering sections:

```js
  function labFields(fields, patch, f) {
    var S = labSession(), out = [];
    fields.forEach(function (fl) {
      var m = fl.k && /^(voice|sect|v3)\.(synth|harm|index)$/.exec(fl.k);
      if (!m) { out.push(fl); return; }
      var role = m[1], st = S.state(role), smp = FsRoles.isSampler(st.synth);
      if (m[2] !== "synth") { if (!smp) { out.push(fl); } return; }
      out.push({ k: fl.k, label: "instrument",
        groups: [["Digital", SYNTH_TYPES.map(function (t) { return [t, SYNTH_LABEL[t] || t]; })], ["Sampler", FsRoles.SAMPLER]],
        get: function () { return smp ? st.synth : patch[role].synth; },
        set: function (v) {
          st.synth = v;
          if (!FsRoles.isSampler(v)) {                 /* digital: the live patch, exactly as with lab mode off */
            pset(patch, role + ".synth", v); var P = paramsFor(v);
            if (P.harm) { pset(patch, role + ".harm", P.harm.def); } if (P.index) { pset(patch, role + ".index", P.index.def); }
            commitPatch(f, patch);
          }
          labSaveSoon(f); renderPatchPanel();
        } });
      if (!smp) { return; }
      var lab = function (k, label, min, max, step, fmt) { return { k: role + "." + k, label: label, min: min, max: max, step: step, fmt: fmt,
        get: function () { return st[k]; }, set: function (v) { st[k] = +v; labSaveSoon(f); } }; };
      var menu = function (k, label, opts) { return { k: role + "." + k, label: label, options: function () { return opts.map(String); },
        fmt: function (v) { return opts.length === 2 && k === "mode" ? ["Dry", "Ringing"][v] : ({ body: ["String", "Tube", "Bell"], excite: ["Bowed", "Plucked"], method: ["Bank", "Spectral", "Comb"], mode: ["Dry", "Ringing"] })[k][v]; },
        get: function () { return String(st[k]); }, set: function (v) { st[k] = +v; labSaveSoon(f); } }; };
      if (st.synth === "s-resonator") { out.push(menu("body", "body", [0, 1, 2]), menu("excite", "excite", [0, 1])); }
      if (st.synth === "s-harmonic" || st.synth === "s-formant") { out.push(menu("method", "method", [0, 1, 2]), menu("mode", "mode", [0, 1])); }
      if (st.synth !== "s-retune") { out.push(lab("focus", "focus", 0, 1, 0.01)); }
      out.push(lab("colour", "colour", 0, 1, 0.0001, function (v) { var b = S.buf(role); return FsRoles.colourName(st.synth, v, b ? b.duration : 0); }));
      out.push(lab("tune", "tune", 0, 1, 0.01));
      out.push({ sampleRow: role });
    });
    return out;
  }
```

   In the section loop, a `{ sampleRow: role }` entry is built by `labSampleRow(role, f)` instead of `buildRow`:

```js
  function labSampleRow(role, f) {
    var S = labSession(), st = S.state(role), row = document.createElement("label");
    row.className = "pprow";
    var name = document.createElement("span"); name.textContent = "sample"; row.appendChild(name);
    var input = document.createElement("input"); input.type = "file"; input.accept = "audio/*"; input.id = "pp-" + role + "-file";
    input.addEventListener("change", function () { if (input.files[0]) { S.upload(role, input.files[0], f.properties.id).then(function () { labSaveSoon(f); }); } });
    row.appendChild(input);
    var val = document.createElement("i"); val.id = "pp-" + role + "-sample";
    val.textContent = st.sample ? FsRoles.sampleLabel(st.sample.name, st.sample.analysis, st.sampleNote) : "no sample - this role is silent";
    row.appendChild(val);
    return row;
  }
```

   (`textContent` everywhere: names are never markup. The value column `<i>` is the panel's existing readout slot.)

- [ ] **Step 4: Run the tests**

Run: `node --env-file=.env.local core/tests/web_site_lab.mjs` → all PASS.
Run: `node core/tests/web_patch.mjs` (the existing panel suite; lab mode off) → as before.
Run: `npm test` → all pass (plus the 3 new unit tests).

- [ ] **Step 5: Commit**

```bash
git add index.html core/tests/web_site_lab.mjs
git commit -m "3c: the site's patch panel in lab mode - Digital/Sampler per role, sampler controls and samples, autosaved apart from the live patch"
```

---

### Task 4: The site's sound in lab mode

**Files:**
- Modify: `index.html` — `coreStart` (~8205), `coreSync` (~8277), `coreMessage` "state" branch (~8352), the Task 3 `labRoutesSet` stub
- Modify: `core/tests/web_site_lab.mjs` (sound checks appended before "no page errors")

**Interfaces:**
- Consumes: `FsRoles.overlay`, `FsRoles.fetchRoute`, `FsRoles.sendRole`, `FsRoles.ROLE_INDEX` (Task 2);
  engine `role`/`analysis` messages and `state.route_id` (Task 1); `labOn()`, `labRoutes`, `labRoutesSet`, `labChanged` (Task 3).
- Produces: `labEngineSync()` (called by `labChanged`), `window.__fa.coreWasm` (the wasm URL the running engine booted).

- [ ] **Step 1: Write the failing checks** (append to `core/tests/web_site_lab.mjs`, after "lab mode off again" and
  before the expired-session block; the DRAFT route's geometry must be walkable: give it the same coordinates as the
  published route it copied its patch from, offset 0.01° north so they do not overlap)

```js
  /* the site's sound in lab mode: core-lab.wasm, the role's sample heard; lab mode off: core.wasm */
  await ev("(function(){ var s = document.querySelector('#pp-body select[data-k=\"v3.synth\"]'); s.value = 's-freeze'; s.dispatchEvent(new Event('change')); return 1; })()");
  await sleep(2500);
  const mid = await ev(`(function(){ var c = __fa.features().filter(function (f) { return f.properties.id === '${DRAFT}'; })[0].geometry.coordinates; return c[Math.floor(c.length / 2)]; })()`);
  await ev("fsListen.sound()");
  for (let i = 0; i < 40 && !(await ev("!!(window.__fa.core && __fa.core.route_id === '" + DRAFT + "')")); i++) { await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await sleep(400); }
  check("lab mode on: the site's engine is core-lab.wasm and the walker is on the draft route", (await ev("__fa.coreWasm")) === "web/core-lab.wasm" && (await ev("__fa.core.route_id")) === DRAFT, [await ev("__fa.coreWasm"), await ev("__fa.core && __fa.core.route_id")]);
  const lvl = async () => { let e = 0; for (let i = 0; i < 16; i++) { await sleep(250); await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); e += Math.pow(10, (await ev("__fa.coreLevel()")) / 10); } return 10 * Math.log10(e / 16); };
  await sleep(3000); const withRole = await lvl();
  await ev("(function(){ var s = document.querySelector('#pp-body input[data-k=\"v3.gain\"]'); s.value = '0'; s.dispatchEvent(new Event('input')); return 1; })()");
  await sleep(4000); const muted = await lvl();
  await ev("(function(){ var s = document.querySelector('#pp-body input[data-k=\"v3.gain\"]'); s.value = '0.55'; s.dispatchEvent(new Event('input')); return 1; })()");
  check("walking the route, the Freeze role with its sample sounds (muting it drops the level >= 3 dB)", withRole > muted + 3, [withRole, muted]);
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(4000);
  check("lab mode off while sound plays: the engine is core.wasm again, still sounding", (await ev("__fa.coreWasm")) === "web/core.wasm" && (await ev("__fa.coreLevel()")) > -60, [await ev("__fa.coreWasm"), await ev("__fa.coreLevel()")]);
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(4000);
  check("... and on again: core-lab.wasm, one engine", (await ev("__fa.coreWasm")) === "web/core-lab.wasm" && (await ev("__fa.coreNodes")) === 1, [await ev("__fa.coreWasm"), await ev("__fa.coreNodes")]);
  /* another route without a lab row: its sampler role is not the draft's sample (Review Focus 1, 5) */
  await ev(`(function(){ var c = __fa.features().filter(function (f) { return f.properties.id === '${PUB}'; })[0].geometry.coordinates; var m = c[Math.floor(c.length / 2)]; __fa.walkTo(m[0], m[1]); return 0; })()`);
  await until("__fa.core && __fa.core.route_id === '" + PUB + "'", 15000);
  check("on a route with no lab row, the engine's patch is the live one", await ev(`__fa.coreSentPatch('${PUB}') === JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === '${PUB}'; })[0].properties.patch)`), null);
  await ev("fsListen.sound()");    /* off */
```

  (Before the sound block, set the draft's `voice` and `sect` gains to their defaults; nothing else changes.)

- [ ] **Step 2: Run to see it fail**

Run: `node --env-file=.env.local core/tests/web_site_lab.mjs`
Expected: FAIL at "lab mode on: the site's engine is core-lab.wasm" (`__fa.coreWasm` undefined).

- [ ] **Step 3: Implement**

1. `coreStart`: pick the build and record it.
```js
    var wasmUrl = labOn() ? "web/core-lab.wasm" : "web/core.wasm";
    core = { ctx: ctx, on: true, pending: {}, wasm: wasmUrl, roleKey: {} };
    ... fetch(wasmUrl).then(...)
```
   after `core.node = node`: `window.__fa.coreWasm = wasmUrl; window.__fa.coreNodes = (window.__fa.coreNodes || 0) + 1;`
   A failed `core-lab.wasm` fetch: `labMsg("Samplers (lab): the lab engine did not load - " + e.message)`, set
   `labWanted = false`, and start again on `web/core.wasm`.
2. The features the engine is given go through one function:
```js
  function coreFeature(f) {
    if (!labOn() || f.properties.kind !== "route" || !labRoutes[f.properties.id]) { return f; }
    var g = JSON.parse(JSON.stringify(f)); g.properties.patch = FsRoles.overlay(f.properties.patch, labRoutes[f.properties.id]);
    return g;
  }
```
   Use it in `coreStart` (`visible().map(coreFeature)`) and `coreSync` (`var s = JSON.stringify(coreFeature(f))`).
   Test hook: `window.__fa.coreSentPatch = function (id) { var s = core && core.sent && core.sent[id]; return s ? JSON.stringify(JSON.parse(s).properties.patch) : null; };`
3. Lab rows for every route, loaded once when lab mode turns on (setters may read them all):
```js
  function labRoutesLoad() {
    if (!labOn()) { return Promise.resolve(); }
    return sb.from("lab_route_roles").select("route_id, roles").then(function (q) {
      if (q.error) { labMsg("Samplers (lab): " + q.error.message); return; }
      labRoutes = {}; (q.data || []).forEach(function (r) { labRoutes[r.route_id] = r.roles; });
      labChanged();
    });
  }
  function labRoutesSet(id, roles) { labRoutes[id] = roles; delete labSound[id]; labChanged(); }
```
4. Role samples for the walker's route, sent when it changes:
```js
  var labSound = {}, labSentFor = null;
  function labRoleSamples(id) {
    if (!labOn() || !core || !core.node || core.wasm !== "web/core-lab.wasm" || !id || !labRoutes[id]) { labSentFor = id; return; }
    if (labSentFor === id) { return; }
    labSentFor = id;
    var ctx = core.ctx;
    /* first: silence on every sampler role, so the last route's samples never play on this one (Review Focus 1) */
    FsRoles.ROLES.forEach(function (r) { FsRoles.sendRole(core.node.port, FsRoles.ROLE_INDEX[r], ctx.createBuffer(1, 128, ctx.sampleRate), null); });
    (labSound[id] = labSound[id] || FsRoles.fetchRoute(sb, ctx, id)).then(function (snd) {
      if (labSentFor !== id || !core || !core.node) { return; }
      FsRoles.ROLES.forEach(function (r) { if (snd.buf[r]) { FsRoles.sendRole(core.node.port, FsRoles.ROLE_INDEX[r], snd.buf[r], snd.ana[r]); } });
    }, function (e) { labMsg("Samplers (lab): " + (e && e.message || e)); });
  }
```
   In `coreMessage`'s `"state"` branch, after `core.state = st;`: `labRoleSamples(st.route_id || null);`.
5. `labEngineSync()` (what `labChanged` calls):
```js
  function labEngineSync() {
    if (!core || !core.node) { return; }
    var want = labOn() ? "web/core-lab.wasm" : "web/core.wasm";
    if (core.wasm !== want) {                 /* the other build: one engine, restarted (Review Focus 2) */
      var wasOn = core.on; core.node.disconnect(); core.ctx.close(); core = null; window.__fa.coreNodes = 0; labSentFor = null;
      if (wasOn) { coreStart(); }
      return;
    }
    labSentFor = null;                        /* a changed role: resend this route's samples */
    if (core.state) { labRoleSamples(core.state.route_id || null); }
    coreSyncSoon();
  }
```
   The lab-mode click handler (Task 3) also calls `labRoutesLoad()` when turning on.

- [ ] **Step 4: Run the tests**

Run: `node --env-file=.env.local core/tests/web_site_lab.mjs` → all PASS.
Run: `node core/tests/web_patch.mjs`, `node core/tests/web_core.mjs` (the site engine suite) → as before.
Run: `python core/tests/run.py`, `node core/tests/web_lab.mjs`, `node --env-file=.env.local core/tests/web_lab_save.mjs`, `npm test` → all pass.

- [ ] **Step 5: Commit**

```bash
git add index.html core/tests/web_site_lab.mjs
git commit -m "3c: lab mode plays the whole site on the lab engine - each route with its role setups, the walker's route's samples"
```
