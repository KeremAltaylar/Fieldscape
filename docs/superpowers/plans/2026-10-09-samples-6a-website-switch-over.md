# Sample harmony 6a: the website switch-over - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The public website plays everything built in lab mode (sampler voices, octave, EQ, follow, resonate), set by setters in the real panels and saved in the route / point itself; Kerem's lab setups carried over on release day.

**Architecture:** The public engine `web/core.wasm` becomes the lab build. Lab mode's data source moves from the `lab_route_roles` table to the route's `properties.patch` (sampler settings, EQ, sample path) and the point's `properties.sound.shape` / `properties.rhythm` (follow, resonate, body), saved through the page's own `commitPatch` / `claimEdit` + Publish. Each recording's pitch analysis lives in a JSON file beside it in Storage; listeners download it, never analyse. A one-time carry-over script moves Kerem's lab rows in.

**Tech Stack:** C++17 core → wasm (emscripten, -O1), vanilla JS (`index.html`, `web/lab-roles.js`), Supabase (Postgres + Storage), node test scripts driving headless Chrome over CDP.

**Spec:** `docs/superpowers/specs/2026-10-09-samples-6a-website-switch-over-design.md`

## Global Constraints

- K1 digital synths stay beside the samplers; K2 website only (apps are 6b); K3 lab setups carried over; K4 settings in the route/point, analysis in a JSON file beside each recording; K5 follow/resonate/body in a stretch point's **Pitch & harmony** beside glide.
- Routes and points without the new keys sound exactly as today (core note fingerprints, `web_core` walk).
- `index.html` never contains `signInWithPassword` (`tests/auth.test.mjs`); tests sign in through `__fa.sb`.
- Ask Kerem before: the release push to main, the carry-over's real run, any live DB migration (none planned).
- Storage paths: samples `recordings/<route id>/<role>-<ms>.wav` + `.json`; point tracks `recordings/<point id>/track-<ms>.json` (hits: `track-<slot>-<ms>.json`). Anon reads them only once the feature is published (policies 0012/0016, unchanged).
- Patch keys per role (`voice`/`sect`/`v3`): `synth`, `sampler {body, excite, method, mode, focus, colour, tune, octave}`, `eq [[hz,db,q]x5]`, `sample {path, name, analysis_path, f0}`. Point keys: `sound.shape.follow|resonate|body`, `sound.track_path`, `rhythm.follow`, `hits[slot].track_path`.

## Review Focus

1. A point's recording replaced (re-attached or re-recorded) after its track was made: the old `track_path` must be dropped, never sent with the new recording.
2. A setter with an unpublished local edit of a route the carry-over updates on the server: publishing that older local copy later would overwrite the carried-over sampler keys. Covered by Task 6 Step 3: Kerem publishes or discards every pending edit on every device before the carry-over runs, and the dry run lists the routes it will touch.
3. A listener on a slow or failing download of a sample or its analysis file: the voice stays silent (never the previous route's sample), and the walk keeps playing.
4. An unpublished route with a sampler voice: a signed-out listener never requests its files (and could not read them).
5. A sampler voice switched back to a digital synth: its `sample`/`sampler` keys stay in the patch (harmless) but the engine plays the digital synth and no sample is downloaded.

1, 3, 4, 5 have tests in Tasks 3 and 4; 2 is a release step (Task 6 Step 3).

---

### Task 1: The public engine is the lab engine

**Files:**
- Modify: `web/build.sh` (exports = `web/build-lab.sh`'s; sources add `core/analysis.cpp`; `-O1`)
- Create: `core/tests/exports.mjs`
- Rebuild: `web/core.wasm`

**Interfaces:**
- Produces: `web/core.wasm` exporting everything `web/core-lab.wasm` does, incl. `fs_analyse`, `fs_engine_role_source`, `fs_engine_role_analysis`, `fs_engine_roles`, `fs_engine_source_track`.

- [ ] **Step 1: Write the failing test**

```js
/* core/tests/exports.mjs - the public engine exports what the page's sampler code calls (6a) */
import { readFileSync } from "node:fs";
const want = ["fs_analyse", "fs_engine_role_source", "fs_engine_role_analysis", "fs_engine_roles", "fs_engine_source_track", "fs_engine_create", "malloc", "free"];
const mod = new WebAssembly.Module(readFileSync("web/core.wasm"));
const got = new Set(WebAssembly.Module.exports(mod).map((e) => e.name));
const missing = want.filter((n) => !got.has(n));
console.log(missing.length ? "FAIL core.wasm lacks " + missing.join(", ") : "PASS core.wasm exports the sampler calls");
process.exit(missing.length ? 1 : 0);
```

- [ ] **Step 2: Run it** - `node core/tests/exports.mjs` → Expected: `FAIL core.wasm lacks fs_analyse, ...`
- [ ] **Step 3: Implement** - in `web/build.sh` copy the `-sEXPORTED_FUNCTIONS=` list and the source list from `web/build-lab.sh` (it adds `core/analysis.cpp`), keep `-o web/core.wasm`, and change `-O2` to `-O1` with the comment: `# -O1: Windows App Control blocks wasm-opt.exe (-O2); the lab build has run at -O1 since 2026-09-30`. Build: `W=/c/Users/kerem/AppData/Local/Temp/emb; EM_CONFIG=C:/Users/kerem/tools/emsdk/.emscripten PATH="$W:$PATH" sh web/build.sh`
- [ ] **Step 4: Run** - `node core/tests/exports.mjs` → PASS; `python core/tests/run.py` → 19/19; `node core/tests/web_core.mjs` → exit 0 (the site's walk on the new engine, no FAIL).
- [ ] **Step 5: Commit** - `git add web/build.sh web/core.wasm core/tests/exports.mjs && git commit -m "6a: the public engine is the lab engine (all sampler exports, -O1)"`

### Task 2: Role setups read from and written to the route's patch (`web/lab-roles.js`)

**Files:**
- Modify: `web/lab-roles.js`
- Test: `tests/lab-roles-unit.test.mjs`

**Interfaces:**
- Produces:
  - `FsRoles.fromPatch(patch) -> roles` (the session's json shape: `{voice|sect|v3: {synth, gain, harm, index, sampler, eq, sample}}`, sampler roles only carry sample/sampler)
  - `FsRoles.toPatch(patch, roles) -> patch` (a copy: writes `synth` for sampler roles, `sampler`, `eq` for every role, `sample {path, name, analysis_path, f0}` - never an inline `analysis`)
  - `session(o)` options: `o.folder(id) -> "lab/<id>/" | "<id>/"` (default lab), `o.analysisFiles: true` (upload the analysis JSON beside the WAV and set `analysis_path`, `f0`); `s.fromPatch(id, patch)` loads state from a patch without the table (same generation rules as `s.load`)
  - `decodeSample(sb, ctx, smp)` reads `smp.analysis` or downloads `smp.analysis_path`

- [ ] **Step 1: Write the failing tests** (append to `tests/lab-roles-unit.test.mjs`)

```js
test("6a: toPatch writes sampler roles and EQ into a copy of the patch, never an inline analysis", () => {
  const patch = { version: 17, voice: { synth: "fm", gain: 0.4, harm: 1, index: 4 }, sect: { synth: "fm" }, v3: { synth: "am" } };
  const eq = [[80, 3, 0.7], [250, 0, 1], [1000, 0, 1], [4000, 0, 1], [10000, 0, 0.7]];
  const roles = { voice: { synth: "s-retune", sampler: { focus: 0.3, octave: 1 }, eq, sample: { path: "R/voice-1.wav", name: "a.wav", analysis_path: "R/voice-1.json", f0: 220, analysis: { track: [1] } } },
    sect: { synth: "fm", eq }, v3: { synth: "am" } };
  const out = FsRoles.toPatch(patch, roles);
  assert.equal(out.voice.synth, "s-retune");
  assert.deepEqual(out.voice.sampler, { focus: 0.3, octave: 1 });
  assert.deepEqual(out.voice.sample, { path: "R/voice-1.wav", name: "a.wav", analysis_path: "R/voice-1.json", f0: 220 });
  assert.equal(out.voice.gain, 0.4);
  assert.deepEqual(out.sect.eq, eq); assert.equal(out.sect.synth, "fm");
  assert.ok(!("eq" in out.v3) && !("sample" in out.v3));
  assert.equal(patch.voice.synth, "fm");                       /* the original untouched */
});
test("6a: fromPatch reads them back", () => {
  const patch = { voice: { synth: "s-freeze", gain: 0.5, sampler: { focus: 0.7 }, sample: { path: "R/voice-1.wav", name: "a", analysis_path: "R/voice-1.json", f0: 0 } }, sect: { synth: "fm", eq: [[80, 1, 0.7]] } };
  const r = FsRoles.fromPatch(patch);
  assert.equal(r.voice.synth, "s-freeze"); assert.equal(r.voice.sampler.focus, 0.7); assert.equal(r.voice.sample.analysis_path, "R/voice-1.json");
  assert.deepEqual(r.sect.eq, [[80, 1, 0.7]]); assert.equal(r.sect.synth, "fm");
});
test("6a: decodeSample fetches the analysis file when the sample has none inline", async () => {
  const got = [];
  const sb = { storage: { from: () => ({ download: async (p) => { got.push(p); return { data: p.endsWith(".json") ? new Blob([JSON.stringify({ f0: 220, hop_s: 0.02, track: [] })]) : new Blob([new Uint8Array(8)]) }; } }) } };
  const chan = new Float32Array(4800);
  const buf = { numberOfChannels: 1, length: chan.length, sampleRate: 48000, duration: 0.1, getChannelData: () => chan, copyToChannel() {} };
  const ctx = { sampleRate: 48000, decodeAudioData: async () => buf, createBuffer: () => buf };
  const p = await FsRoles.decodeSample(sb, ctx, { path: "R/voice-1.wav", analysis_path: "R/voice-1.json" });
  assert.deepEqual(got.sort(), ["R/voice-1.json", "R/voice-1.wav"]);
  assert.equal(p.analysis.f0, 220);
});
```

- [ ] **Step 2: Run** - `node --test tests/lab-roles-unit.test.mjs` → 3 new FAIL (`FsRoles.toPatch is not a function`, ...)
- [ ] **Step 3: Implement** in `web/lab-roles.js`:

```js
  /* 6a: a route's role setups live in its own patch (K4): the sampler keys, every role's EQ, the sample's files */
  function fromPatch(patch) {
    var out = {};
    ROLES.forEach(function (r) {
      var q = (patch && patch[r]) || {}, x = { synth: q.synth || "" };
      if (q.sampler) { x.sampler = JSON.parse(JSON.stringify(q.sampler)); }
      if (Array.isArray(q.eq)) { x.eq = JSON.parse(JSON.stringify(q.eq)); }
      if (q.sample && q.sample.path) { x.sample = { path: q.sample.path, name: q.sample.name || "", analysis_path: q.sample.analysis_path || null, f0: q.sample.f0 || 0 }; }
      out[r] = x;
    });
    return out;
  }
  function toPatch(patch, roles) {
    var p = JSON.parse(JSON.stringify(patch || {}));
    ROLES.forEach(function (r) {
      var rr = roles && roles[r]; if (!rr) { return; }
      p[r] = p[r] || {};
      if (Array.isArray(rr.eq)) { p[r].eq = JSON.parse(JSON.stringify(rr.eq)); } else { delete p[r].eq; }
      if (!isSampler(rr.synth)) { return; }
      p[r].synth = rr.synth;
      p[r].sampler = JSON.parse(JSON.stringify(rr.sampler || {}));
      var sm = rr.sample;
      if (sm && sm.path) { p[r].sample = { path: sm.path, name: sm.name || "", analysis_path: sm.analysis_path || null, f0: (sm.analysis && sm.analysis.f0) || sm.f0 || 0 }; }
      else { delete p[r].sample; }
    });
    return p;
  }
```

  - `decodeSample`: before decoding, `var ana = smp.analysis ? Promise.resolve(smp.analysis) : smp.analysis_path ? sb.storage.from("recordings").download(smp.analysis_path).then(function (d) { if (d.error || !d.data) { throw new Error("analysis " + ((d.error && d.error.message) || "not found")); } return d.data.text(); }).then(JSON.parse) : Promise.resolve(null);` and run both downloads in `Promise.all`, preparing with the fetched analysis.
  - `session`: `var folder = o.folder || function (id) { return "lab/" + id + "/"; };` in `s.upload` use `folder(id) + r + "-" + ms + ".wav"`; when `o.analysisFiles`, also upload `folder(id) + r + "-" + ms + ".json"` (`new Blob([JSON.stringify(res.analysis)], { type: "application/json" })`) before marking uploaded, and set `x.sample.analysis_path`, `x.sample.f0 = res.analysis.f0 || 0`.
  - `s.fromPatch(id, patch)`: as `s.load`'s body after the query, with `roles = fromPatch(patch)` (sampler roles' `sampler` keys copied into state, `eq`, `sample`; samples decoded through `decodeSample`), `s.loadedFor = id`, synchronous `changed(null)`.
  - `s.json()` adds `analysis_path` and `f0` to `sample` (keeps `analysis` in memory only).
  - export `fromPatch`, `toPatch`.
- [ ] **Step 4: Run** - `node --test tests/lab-roles-unit.test.mjs` → all PASS; `npm test` → all pass.
- [ ] **Step 5: Commit** - `git add web/lab-roles.js tests/lab-roles-unit.test.mjs && git commit -m "6a: role setups to and from the route's patch; analysis files beside samples"`

### Task 3: The real patch panel and the listener's route sound

**Files:**
- Modify: `index.html` (lab mode removed for routes: `labOn`, `labToggle`, `#pp-lab`, `labRoutes` overlay, `labWantWasm`, `labEngineSync`'s engine swap, `renderPatchPanel`, `labFields`, `labSaveSoon`, `labSampleBlock`, `labRoleSamples`, `coreFeature`), `sw.js` (`SHELL` v14)
- Create: `core/tests/web_site_samplers.mjs` (from `core/tests/web_site_lab.mjs`; the old file is deleted in this task's commit)

**Interfaces:**
- Consumes: Task 1 `web/core.wasm`; Task 2 `FsRoles.fromPatch/toPatch`, `session({folder, analysisFiles})`, `s.fromPatch`.
- Produces: `samplersOn() -> bool` (a signed-in setter with `FsRoles`: the sampler panel); `routeSamples(id)` (the engine's role samples from the route's patch, for every listener); `__fa.coreWasm === "web/core.wasm"` always; test hooks kept: `__fa.coreSentPatch`, `__fa.labRoleSends` (renamed `__fa.roleSends`), `__fa.labRolesFor` (renamed `__fa.rolesFor`); added: `__fa.pendingCount()` (features waiting to publish), `__fa.publishNow()` (the Publish button's action), `__fa.coreRoles()` (the engine's last roles array: level and notes per voice, 8 floats each).

- [ ] **Step 1: Write the failing browser test** - copy `core/tests/web_site_lab.mjs` to `core/tests/web_site_samplers.mjs` and change it as follows (every other check stays as it is, minus its lab-toggle clicks):
  - delete every `document.querySelector('#pp-lab').click()` and every check about the switch, `core-lab.wasm`, `lab mode off/on`, and `lab_route_roles` rows of the draft route;
  - "autosaved" becomes a patch check:

```js
  await setSel("v3.synth", "s-freeze"); await sleep(400);
  /* ...the upload as before... */
  await setRange("v3.focus", "0.77"); await sleep(900);
  const lp = JSON.parse(await livePatch(DRAFT));
  check("6a: a sampler voice is saved in the route's own patch: synth, its controls, the sample's two files in the route's folder",
    lp.v3.synth === "s-freeze" && lp.v3.sampler.focus === 0.77 && lp.v3.sample.path.indexOf(DRAFT + "/v3-") === 0 && /\.json$/.test(lp.v3.sample.analysis_path) && !("analysis" in lp.v3.sample), lp.v3);
  const files = (await db.storage.from("recordings").list(DRAFT)).data.map((o) => o.name);
  check("6a: the WAV and its analysis JSON are in Storage beside each other", files.some((n) => /^v3-\d+\.wav$/.test(n)) && files.some((n) => /^v3-\d+\.json$/.test(n)), files);
```

  - new listener checks (after publishing the draft through the page's Publish, signed out in a fresh page):

```js
  /* Review Focus 4 + the listener: published, signed out, the voice's files are fetched and heard */
  await ev("__fa.publishNow ? __fa.publishNow() : document.querySelector('#publish').click(), 0"); await until(`__fa.pendingCount() === 0`, 30000);
  await ev("__fa.sb.auth.signOut(), 0"); await open();
  await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await ev("fsListen.sound()");
  for (let i = 0; i < 40 && !((await ev("__fa.roleSends || 0")) > 0); i++) { await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await sleep(400); }
  const meter = await ev("JSON.stringify(__fa.coreRoles ? __fa.coreRoles() : null)");
  check("6a: a signed-out listener's engine gets the route's sampler sample and plays it (v3 meter above -60 dB)", (await ev("__fa.roleSends")) > 0 && JSON.parse(meter)[16] > -60, meter);
  check("6a: the listener's engine is the public core.wasm", (await ev("__fa.coreWasm")) === "web/core.wasm", await ev("__fa.coreWasm"));
```

  - Review Focus 3: with the draft's `sample.path` pointed at a missing file (set through `livePatch` + `commitPatch` hook), the v3 meter stays under -60 dB, the walk keeps playing (`__fa.coreLevel() > -60`), no page error;
  - Review Focus 5: v3 set back to a digital synth: no new role send (`__fa.roleSends` unchanged), the sent patch's `v3.synth` is the digital name.
- [ ] **Step 2: Run** - `node tools/serve.mjs 8765` (another shell), `node --env-file=.env.local core/tests/web_site_samplers.mjs` → FAILs on the 6a checks.
- [ ] **Step 3: Implement** in `index.html`:
  - `samplersOn()` = `setter.signedIn && !!sb && typeof FsRoles !== "undefined"`; replace every `labOn()` used for the patch panel with it; delete `labToggle`, `LAB_KEY`, `#pp-lab` and its button markup, `labRoutes`, `labRoutesLoad`, `labWantWasm` (the engine is always `web/core.wasm`), the engine swap in `labEngineSync`, and the route branch of `coreFeature` (routes go to the engine as they are).
  - `labSession()` → `FsRoles.session({ sb, ctx, analyse: analyseMono, folder: function (id) { return id + "/"; }, analysisFiles: true, onChange })`; `renderPatchPanel` calls `S.fromPatch(id, patchOf(f))` when `S.loadedFor !== id`.
  - `labSaveSoon(f)` → `var p = patchOf(f), np = FsRoles.toPatch(p, S.json()); Object.keys(np).forEach(function (k) { p[k] = np[k]; }); commitPatch(f, p); engineSync();` (no table writes; heard at once).
  - The instrument menu's `set` for a sampler also writes `pset(patch, role + ".synth", v)` through the same path.
  - `routeSamples(id)` (was `labRoleSamples`): roles from `FsRoles.fromPatch(feature(id).properties.patch)`; runs for every listener whenever `core.state.route_id` changes or the route's patch changes; the setter's panel copy wins when it is that route's session (as now); downloads by `decodeSample` (analysis from `analysis_path`), cached by path; on failure the role gets silence (Review Focus 3).
  - Rename `labAnalyseMono` → `analyseMono`, fetching `web/core.wasm`.
  - `sw.js`: `SHELL = "fieldarc-shell-v14"` with the comment `/* v14: 6a - the public engine plays samplers; lab-roles.js reads the patch. */`.
- [ ] **Step 4: Run** - `node --env-file=.env.local core/tests/web_site_samplers.mjs` → all passed; `node core/tests/web_patch.mjs`, `node core/tests/web_lab.mjs` (lab.html untouched), `npm test`, `python core/tests/run.py`, `node core/tests/web_core.mjs` → all pass.
- [ ] **Step 5: Commit** - `git add index.html sw.js core/tests/web_site_samplers.mjs && git rm core/tests/web_site_lab.mjs && git commit -m "6a: samplers in the real patch panel, saved in the route; every listener hears them"`

### Task 4: Follow, resonate and body in the point panels; tracks as files

**Files:**
- Modify: `index.html` (`renderSoundscapePanel` colHarmony, `renderGrainPanel` cloud, `renderRhythmPanel` hits line, `labFollowControl`/`labPointSave`/`labPointTrack` removed, `coreFeature` point branch removed, the `need` handler's track step, the audio re-attach paths)
- Test: `core/tests/web_site_samplers.mjs` (the point checks)

**Interfaces:**
- Consumes: Task 3 `samplersOn()`, `analyseMono(mono, sr) -> Promise<{f0, hop_s, track}>`.
- Produces: `ensureTrack(f) -> Promise` (setter: for each of the point's recordings without a track file, analyse its first 60 s, upload `<id>/track-<ms>.json` (hits: `track-<slot>-<ms>.json`), set `properties.sound.track_path` or `properties.hits[slot].track_path`, `claimEdit(f); save()`); `pointTrack(eng, kind, index, sub, id)` (every listener: download the track file, post `{type:"track"}`); `__fa.trackFor(path) -> Promise<track JSON>` (download a published recording, analyse its first 60 s - for the carry-over); test hooks `__fa.tracksSent` (track files posted to the engine), `__fa.analyses` (analyses run on this page), `__fa.reattachTestAudio(id)` (attaches a generated 2 s WAV as the point's new recording through the page's own re-attach path).

- [ ] **Step 1: Write the failing browser test** (in `web_site_samplers.mjs`, replacing the lab "5:"/"7:" point checks; the point is a probe point the test creates and publishes, never Kerem's):

```js
  /* 6a points: a probe stretch point with a recording, published by the test, removed after */
  await ev(`fsListen.select('${PTP}'), 0`); await sleep(900);
  await ev("(function(){ var b = document.querySelector('#ls-card #f-rhythm') || document.querySelector('#f-rhythm'); b && b.click(); return 0; })()");
  await until("!!document.querySelector('#rp-body input[data-k=\"follow\"]')", 6000);
  check("6a: a stretch point's Pitch & harmony has follow, resonate and body beside glide",
    await ev("['follow','resonate','glide'].every(function (k) { return !!document.querySelector('#rp-body input[data-k=\"' + k + '\"]'); }) && !!document.querySelector('#rp-body select[data-k=\"body\"]')"), null);
  await ev("(function(){ var s = document.querySelector('#rp-body input[data-k=\"follow\"]'); s.value = '0.8'; s.dispatchEvent(new Event('input')); return 1; })()");
  await until(`!!(__fa.features().filter(function (f) { return f.properties.id === '${PTP}'; })[0].properties.sound.track_path)`, 30000);
  const pp = (await ev(`JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === '${PTP}'; })[0].properties)`));
  const P = JSON.parse(pp);
  check("6a: follow is saved in the point's shape, and its track file is made beside its recording", P.sound.shape.follow === 0.8 && P.sound.track_path.indexOf(PTP + "/track-") === 0, P.sound);
  /* Review Focus 1: a new recording drops the old track */
  await ev(`__fa.reattachTestAudio('${PTP}'), 0`); await sleep(500);
  check("6a: replacing the recording drops its track_path", !(await ev(`__fa.features().filter(function (f) { return f.properties.id === '${PTP}'; })[0].properties.sound.track_path`)), null);
  /* the listener: published, signed out, walking to it, the track comes from the file (no analysis on the page) */
  check("6a: a signed-out listener's engine gets the point's track from its file", (await ev("__fa.tracksSent")) > 0 && (await ev("__fa.analyses || 0")) === 0, [await ev("__fa.tracksSent"), await ev("__fa.analyses")]);
```

- [ ] **Step 2: Run** → FAIL on the 6a point checks.
- [ ] **Step 3: Implement**:
  - `renderSoundscapePanel`: `SHD` gains `follow: 0, resonate: 0, body: 0`; in `colHarmony` after `transpose`:

```js
      R({ label: "follow chord", k: "follow", min: 0, max: 1, step: 0.05, def: 0, fmt: fx.pct,
        hint: "a pitched recording moves onto the chord's nearest note, gliding (0 = as recorded)" }, sh),
      R({ label: "resonate", k: "resonate", min: 0, max: 1, step: 0.05, def: 0, fmt: fx.pct,
        hint: "the chord rung by the recording, inside the stretch, before the cloud" }, sh),
      buildPxSelect({ label: "body", k: "body", options: [[0, "String"], [1, "Tube"], [2, "Bell"]], def: 0 }, sh, commitLive),
```

    (`buildPxSelect`: the panel's select row in `buildPxRow`'s style - `label.pprow` > `span` + `select[data-k]`; numeric value written to `obj[k]`, then `commit()`.) `commitLive` gains `if (sh.follow > 0) { ensureTrack(f); }`.
  - `renderGrainPanel` cloud and the hits panel's `line`: `buildRhythmRow({ k: "follow", label: "follow chord", min: 0, max: 1, step: 0.05, hint: "pitched grains / hits onto the chord's notes" }, r, function () { commitR(); if (r.follow > 0) { ensureTrack(f); } })`.
  - `ensureTrack(f)` (setter only, `samplersOn()`): recordings = stretch/grains `[{key: id, path: storage_path, slot: null}]`, hits `HIT_SLOTS` with `hits[slot]`; for each without `track_path`: `audioBlob(key, path)` → `decodeAudioData` on the page's context → mono first 60 s → `analyseMono` → upload JSON → set the path → `claimEdit(f); save(); coreSync()`. One at a time; a failure leaves the path unset and says so in a toast.
  - Re-attach: wherever a point's audio blob is replaced (`has_audio` set by a new take, a hit slot re-attached), delete `properties.sound.track_path` / that slot's `track_path` in the same edit.
  - `pointTrack(eng, kind, index, sub, id)` in the `need` handler (replacing `labPointTrack`): when the point follows (`S`: `sound.shape.follow > 0`; `R`: `rhythm.follow > 0`) and has the track path, download it (cached by path), post `{type: "track", ...}`, `__fa.tracksSent++`. No path: nothing (a setter's `ensureTrack` makes one).
  - `__fa.trackFor(path)`: download (`sb.storage`), decode, mono first 60 s, `analyseMono` → the JSON (for Task 5).
  - Delete `labFollowControl`, `labPointSave`, `labPointTrack`, the point branch of `coreFeature`, `.rp-lab` CSS.
- [ ] **Step 4: Run** the site test and every suite → all pass.
- [ ] **Step 5: Commit** - `git commit -am "6a: follow, resonate and body in the point panels; tracks as files beside the recordings"`

### Task 5: The carry-over script

**Files:**
- Create: `tools/carry-over-6a.mjs`
- Test: `core/tests/carry_over.mjs` (runs the script against probe rows only)

**Interfaces:**
- Consumes: Task 2 `toPatch` shape (re-implemented in node: same keys); Task 4 `__fa.trackFor(path)`.
- Produces: `node --env-file=.env.local tools/carry-over-6a.mjs [--run] [--only <id,id>]` - dry run by default (prints every change); `--run` applies.

- [ ] **Step 1: Write the failing test** - `core/tests/carry_over.mjs`: creates a probe route (published, copy of Koşuyolu's geometry shifted, id `...0006a1`) with a `lab_route_roles` row whose `voice` is `s-freeze` with a sample uploaded to `lab/<id>/voice-1.wav` and an inline `analysis`, `sect` digital with an `eq`; a probe published stretch point (`...0006a2`) with a recording and a row `{point: {follow: 1, resonate: 0.5, body: 2}}`; runs `tools/carry-over-6a.mjs --run --only <both ids>`; then asserts:

```js
check("route: voice's sampler keys and EQ in its patch", r.properties.patch.voice.synth === "s-freeze" && r.properties.patch.voice.sample.path.startsWith(RID + "/voice-") && r.properties.patch.sect.eq.length === 5, r.properties.patch.voice);
check("route: the WAV copied and the analysis JSON written beside it", files.some((n) => /^voice-\d+\.wav$/.test(n)) && files.some((n) => /^voice-\d+\.json$/.test(n)), files);
check("point: follow/resonate/body in its shape, its track file made", p.properties.sound.shape.follow === 1 && p.properties.sound.shape.resonate === 0.5 && p.properties.sound.shape.body === 2 && p.properties.sound.track_path.startsWith(PID + "/track-"), p.properties.sound);
check("a second run changes nothing", second.trim().endsWith("0 changes"), second);
check("the lab rows are left as they were", JSON.stringify(rowAfter.roles) === JSON.stringify(rowBefore.roles), null);
```

  and removes every probe row and file after (finally block).
- [ ] **Step 2: Run** - `node --env-file=.env.local core/tests/carry_over.mjs` → FAIL (script missing).
- [ ] **Step 3: Implement** `tools/carry-over-6a.mjs`: service-key client; read `lab_route_roles`; for a route row: for each sampler role with `sample.path` under `lab/`: `storage.copy(path, "<id>/<role>-<ms>.wav")`, upload `<id>/<role>-<ms>.json` from the row's inline analysis; merge `{synth, sampler, eq, sample {path, name, analysis_path, f0}}` into `features.properties.patch.<role>` (digital role: `eq` only); for a point row: merge follow/resonate/body into `properties.sound.shape` (stretch) or `properties.rhythm.follow` (grains/hits); when follow > 0, open headless Chrome on `http://localhost:8765/` (as `web_site_samplers.mjs` does), `await __fa.trackFor(storage_path)` per recording, upload `track-<ms>.json`, set the path. Skip a key already carried (idempotent: a role whose `sample.path` is already under `<id>/`, a point that already has the shape keys and track). Each change printed; the last line `N changes`. `update features set properties = ...` through the client (the `features_touch` trigger bumps `updated_at`).
- [ ] **Step 4: Run** the carry-over test → all PASS.
- [ ] **Step 5: Commit** - `git add tools/carry-over-6a.mjs core/tests/carry_over.mjs && git commit -m "6a: the carry-over script (dry run by default)"`

### Task 6: Release (each step with Kerem's OK)

**Files:** none new.

- [ ] **Step 1:** Final whole-branch review (fresh Opus reviewer) and its one fix pass, every suite green.
- [ ] **Step 2 (Kerem's OK):** `node --env-file=.env.local tools/backup.mjs v6.0-samplers-live`; `git tag v6.0-samplers-live`; merge the branch to main; push; wait for Pages; fetch the live `web/core.wasm` and compare its hash to the local file; a signed-out headless walk of Koşuyolu: no page errors, levels as before (no route has the keys yet).
- [ ] **Step 3:** Kerem publishes or discards every pending edit on every device (Review Focus 2); then `node --env-file=.env.local tools/carry-over-6a.mjs` (dry run) → show Kerem the list of changes.
- [ ] **Step 4 (Kerem's OK):** `... carry-over-6a.mjs --run`; then the signed-out headless walk of Koşuyolu: its three voices' meters above -60 dB, the samplers sounding, the points' tracks sent (`__fa.tracksSent > 0` near Stretch 2), no page errors.
- [ ] **Step 5:** memory note, ledger, report to Kerem: what to test on each device, how to roll back (revert the release merge; restore the touched rows from the backup).
