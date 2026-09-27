# Web listener W1 — engine live data, cells from the engine, the listener shell

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The website's listener sees the apps' frame — top bar, walk panel (bottom on a phone, floating left on a computer), fold bar, and morph cells that show what the core engine is actually playing.

**Architecture:** The core engine gains three read calls; the AudioWorklet posts them to the page ~30 times a second as a `live` message. `index.html` exposes a small bridge (`window.fsListen`) and a `body.listener` class; a new `web/listener.js` + `web/listener.css` build the shell against that bridge only. The old chrome stays for setters.

**Tech Stack:** C++17 core → WASM (em++), AudioWorklet, plain ES5-style browser JS (as index.html), CSS on the existing `:root` tokens, MapLibre (already loaded), Chrome DevTools Protocol for tests.

**Spec:** `docs/superpowers/specs/2026-09-27-web-listener-design.md` (W1 = its sections 1, 2 (morphs/chord/route only), 3 rows Top bar / Walk panel / Folded bar / Cells, 4, 5).

## Global Constraints

- Every visual value from the `:root` tokens in index.html (Tokens.md; rulebook C-1). No new literal colours except the cells' existing `rgba(190,205,192,·)`.
- Faces: Cormorant Garamond (display), Newsreader (body), Courier Prime (mono).
- Touch targets ≥ 44 px (M-4). Phone layout ≤ 860 px wide; floating panel above 860 px.
- Panel ~380 px wide on a computer; cells 136 px on a phone, 240 px (15rem) on a computer.
- Nothing scrolls on the listener surface at 390×844 and 1440×900 (C-8): `scrollHeight - innerHeight === 0`.
- Copy is the apps' copy, word for word (ios/WalkPanel.swift, ios/App.swift CollapsedBar).
- Signed-in setters see today's interface unchanged.
- Motion 150–250 ms ease-out; reduced motion honoured (C-6).

## Review Focus

1. **No route near the walker** (open world, standing in a park with no route): cells show "no route", panel has no route line, no errors. → Task 2 test walks 5 km away.
2. **Sound never pressed**: the engine is not running, so no `live` messages — the panel must still render (place, "Sound", GPS chip) and cells must not throw. → Task 4 step checks the first screen before Sound.
3. **Signing in and out on the same page**: `body.listener` flips both ways without a reload; the shell hides, the old chrome returns. → Task 3 test toggles `fsListen._gate(true/false)`.
4. **Window resized across 860 px**: the panel moves between bottom and floating without losing the fold state; cells resize (cellsResize). → Task 5 measures both widths in one session.
5. **`?core=0` (Tone fallback) as a listener**: no `live` data; cells fall back to the old JS clock and the shell still works. → Task 2 test runs once with `?core=0`.

---

### Task 1: Engine read calls (morphs, chord, route)

**Files:**
- Modify: `core/fieldscape.h` (engine section, after `fs_engine_state`)
- Modify: `core/engine.cpp` (end of `extern "C"` block)
- Modify: `web/build.sh` (EXPORTED_FUNCTIONS)
- Test: `core/test.cpp` (new block before `core ok`)

**Interfaces:**
- Produces:
  - `int fs_engine_morphs(fs_engine *e, double *out, int max, double *clock, int *root, int *shown);` — exactly `fs_piece_morphs` on the engine's piece (7 doubles a cell; -1 no route playing).
  - `int fs_engine_chord(fs_engine *e, int *count, char *label, int size);` — `fs_piece_chord` on the piece, but -1 when no route is playing (`route_name` empty).
  - `int fs_engine_route(fs_engine *e);` — the playing route's index in feature order (`fs_piece_route`), -1 none.

- [ ] **Step 1: Write the failing test** — in `core/test.cpp`, before `std::printf("core ok\n");`:

```cpp
    {   /* the engine's live reads: morphs, chord and route of the piece it drives */
        fs_engine *e = fs_engine_create(SR, B);
        fs_engine_features(e, "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
            "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[29.0,41.0],[29.002,41.0]]},"
            "\"properties\":{\"id\":\"r1\",\"kind\":\"route\",\"name\":\"R\",\"patch\":{}}}]}");
        double o[7 * 24], clock = 0; int root = -1, shown = -1, count = 0; char label[32];
        assert(fs_engine_route(e) == -1 && fs_engine_chord(e, &count, label, 32) == -1);
        for (int b = 0; b < (int)(4.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.001, 41.0); fs_engine_process(e, B); }
        int n = fs_engine_morphs(e, o, 24, &clock, &root, &shown);
        int ch = fs_engine_chord(e, &count, label, 32);
        std::printf("engine live: route %d, chord %d of %d %s, %d morphs at %.2f s\n", fs_engine_route(e), ch, count, label, n, clock);
        assert(fs_engine_route(e) == 0 && ch >= 0 && count == 16 && label[0] && n == 6 && clock > 3);
        for (int b = 0; b < (int)(2.0 * SR / B); b++) { if (b % 40 == 0) fs_engine_step(e, 29.1, 41.1); fs_engine_process(e, B); }
        assert(fs_engine_route(e) == -1 && fs_engine_chord(e, &count, label, 32) == -1);
        assert(fs_engine_morphs(e, o, 24, &clock, &root, &shown) == -1);   /* 8 km away: nothing playing */
        fs_engine_destroy(e);
    }
```

- [ ] **Step 2: Run it to verify it fails** — `python core/tests/run.py` → row 0 FAIL (build error: `fs_engine_morphs` undeclared).

- [ ] **Step 3: Implement** — `core/fieldscape.h`, after `const char *fs_engine_state(fs_engine *e);`:

```c
/* live reads for the screen, ~30 times a second: the piece's morph cells (fs_piece_morphs), the chord
   and the route playing (-1: none) */
int fs_engine_morphs(fs_engine *e, double *out, int max, double *clock, int *root, int *shown);
int fs_engine_chord(fs_engine *e, int *count, char *label, int size);
int fs_engine_route(fs_engine *e);
```

`core/engine.cpp`, after `fs_engine_state`:

```cpp
int fs_engine_route(fs_engine *e) { return e->route_name.empty() ? -1 : fs_piece_route(e->piece); }
int fs_engine_chord(fs_engine *e, int *count, char *label, int size) {
    if (e->route_name.empty()) { if (count) *count = 0; if (label && size) label[0] = 0; return -1; }
    return fs_piece_chord(e->piece, count, label, size);
}
int fs_engine_morphs(fs_engine *e, double *out, int max, double *clock, int *root, int *shown) {
    if (e->route_name.empty()) { if (clock) *clock = 0; if (root) *root = 0; if (shown) *shown = 0; return -1; }
    return fs_piece_morphs(e->piece, out, max, clock, root, shown);
}
```

`web/build.sh`: append `,_fs_engine_morphs,_fs_engine_chord,_fs_engine_route` after `_fs_engine_state`.

(If the test shows `route_name` stays set 8 km away, read `fs_engine_step`'s route block and clear by the same rule the state JSON uses; the test is the arbiter.)

- [ ] **Step 4: Run to verify it passes** — `python core/tests/run.py` → 19/19, and the core_test output line `engine live: route 0, chord N of 16 …, 6 morphs`.

- [ ] **Step 5: Build the wasm** — `PATH="/c/Users/kerem/tools/emsdk/upstream/emscripten:$PATH" EM_CONFIG=C:/Users/kerem/tools/emsdk/.emscripten sh web/build.sh` → `web/core.wasm` rebuilt; `node -e` check that `WebAssembly.Module.exports(new WebAssembly.Module(fs.readFileSync('web/core.wasm')))` lists the three names.

- [ ] **Step 6: Commit** — `git add core/fieldscape.h core/engine.cpp core/test.cpp web/build.sh web/core.wasm && git commit -m "W1: engine live reads - morphs, chord, route"`

---

### Task 2: The `live` message, and the cells drawn from the engine (the live-site fix)

**Files:**
- Modify: `web/core-worklet.js` (FieldscapeEngine: `process` posts `live`)
- Modify: `index.html` — `coreMessage` (stores `core.live`), the Cells section (`morphClock`, `cellsDraw` inputs)
- Test: `core/tests/web_core.mjs` (new assertions)

**Interfaces:**
- Consumes: Task 1's three calls.
- Produces: page-side `core.live = { t: performance.now(), clock, root, shown, route, chord: {step, count, label}, morphs: Float64Array(7n) , n }` updated ~30 Hz; `window.__fa.coreLive` = same object; `liveMorphs()` returning cell objects `{ id, on:true, shape, dest, seed, v, dv, ph }` or `null` when not on the core.

- [ ] **Step 1: Write the failing test** — in `core/tests/web_core.mjs`, after the walk loop's first sample, add:

```js
  const live = await ev("JSON.stringify(window.__fa.coreLive && { n: __fa.coreLive.n, clock: __fa.coreLive.clock, chord: __fa.coreLive.chord })");
  console.log("live:", live);
  const L = JSON.parse(live || "null");
  if (!L || L.n < 1 || !(L.clock > 0) || !L.chord || L.chord.count !== 16) { console.error("FAIL live data"); process.exitCode = 1; }
  const c1 = await ev("__fa.coreLive.clock"); await sleep(1000); const c2 = await ev("__fa.coreLive.clock");
  if (!(c2 - c1 > 0.8 && c2 - c1 < 1.2)) { console.error("FAIL live clock advances", c1, c2); process.exitCode = 1; }
  /* the cells read the engine: first cell's value equals the engine's to the digit */
  const same = await ev("(function(){ var m = liveCellsForTest(); return m && Math.abs(m[0].v - __fa.coreLive.morphs[4]) < 1e-12; })()");
  if (!same) { console.error("FAIL cells read the engine"); process.exitCode = 1; }
  await ev("__fa.walkTo(29.09, 41.05)"); await sleep(1500);            /* 6 km from any route */
  const none = await ev("__fa.coreLive.n");
  if (none !== -1) { console.error("FAIL no route: n", none); process.exitCode = 1; }
```

and expose `window.liveCellsForTest = liveMorphs;` in index.html (Step 3).

- [ ] **Step 2: Run to verify it fails** — `python -m http.server 8765` (repo root, background), then `node core/tests/web_core.mjs 10` → `FAIL live data`.

- [ ] **Step 3: Implement.** `web/core-worklet.js`, FieldscapeEngine constructor: `this.liveEvery = Math.round(sampleRate / 128 / 30); this.liveN = 0; this.morphs = x.malloc(7 * 24 * 8); this.nums = x.malloc(16); this.label = x.malloc(32);`. In `process`, after `fs_engine_process`:

```js
    if (++this.liveN >= this.liveEvery) {
      this.liveN = 0;
      const n = x.fs_engine_morphs(this.e, this.morphs, 24, this.nums, this.nums + 8, this.nums + 12);
      const v = new DataView(x.memory.buffer, this.nums, 16);
      const count = x.malloc(4), step = x.fs_engine_chord(this.e, count, this.label, 32);
      const m = new Float64Array(7 * Math.max(0, n));
      m.set(new Float64Array(x.memory.buffer, this.morphs, m.length));
      this.port.postMessage({ type: "live", n, clock: v.getFloat64(0, true), root: v.getInt32(8, true), shown: v.getInt32(12, true),
        route: x.fs_engine_route(this.e), chord: { step, count: new Int32Array(x.memory.buffer, count, 1)[0], label: this.cstr(this.label) },
        morphs: m }, [m.buffer]);
      x.free(count);
    }
```

(`label` arrives as bytes; the page decodes it.) `index.html` `coreMessage`, new branch:

```js
    } else if (m.type === "live") {
      m.t = performance.now();
      m.chord.label = dec.decode(m.chord.label);
      core.live = m;
      window.__fa.coreLive = m;
```

Cells section: a list builder and a clock that follow the engine when it is running.

```js
  var MORPH_SHAPES = ["drift", "breath", "pulse", "ramp", "tide"];
  var MORPH_DESTS = ["voice.cutoff", "sect.cutoff", "sect.index", "voice.gain", "voice.index", "voice.harm", "voice.drive",
    "voice.warp", "sect.harm", "sect.drive", "sect.warp", "v3.cutoff", "v3.harm", "v3.index", "v3.drive", "v3.warp"];
  /* On the core the morphs are the engine's (fs_engine_morphs): the numbers the voices take.
     null when the Tone engine is playing, and the patch's own list is drawn as before. */
  function liveMorphs() {
    var L = CORE && core && core.on && core.live;
    if (!L) { return null; }
    var out = [];
    for (var i = 0; i < Math.max(0, L.n); i++) {
      var b = 7 * i;
      out.push({ id: "e" + i, on: true, shape: MORPH_SHAPES[L.morphs[b]] || "drift", dest: MORPH_DESTS[L.morphs[b + 2]] || "voice.cutoff",
                 seed: L.morphs[b + 3], v: L.morphs[b + 4], dv: L.morphs[b + 5], ph: L.morphs[b + 6] });
    }
    return out;
  }
  window.liveCellsForTest = liveMorphs;
```

`morphClock()`: first line `if (CORE && core && core.on && core.live) { return core.live.clock + (performance.now() - core.live.t) / 1000; }`.
`morphSeed(m)`: first line `if (m.seed !== undefined) { return m.seed; }`.
In `cellsDraw`: replace `var all = morphList(), list = [];` with `var live = liveMorphs(), all = live || morphList(), list = [];`; replace `var v = morphValue(m, t); var dv = (v - morphValue(m, t - 0.6)) / 0.6;` with `var v = live ? m.v : morphValue(m, t); var dv = live ? m.dv : (v - morphValue(m, t - 0.6)) / 0.6;`; everywhere `cellsDraw` calls `morphPhase(m, t)` / `morphPhase(e.m, t)`, use `(live ? M.ph : morphPhase(M, t))` for that morph `M`. The chord root: `var pc = live && core.live.n >= 0 ? core.live.root : (existing expression)`. When `live && core.live.n < 0` draw the empty state with text `"no route"` instead of `"no morphs"`.

- [ ] **Step 4: Run to verify** — `node core/tests/web_core.mjs 10` → no FAIL lines; then `FS_URL="http://localhost:8765/?core=0" node core/tests/web_core.mjs 5` → runs without exceptions (Review Focus 5; the live assertions are expected to report FAIL there — check only that `errors` is empty).

- [ ] **Step 5: `npm run check` and `npm test`** → both pass (as before the change).

- [ ] **Step 6: Commit** — `git commit -am "W1: the cells draw the engine's morphs (they ran on the wall clock since the core became default)"`

---

### Task 3: The bridge and the listener gate in index.html

**Files:**
- Modify: `index.html` — mode gating (`applyModeGating`, the `SETTER_ONLY` block) and a new `/* ---------- Listener bridge ---------- */` section just before `/* ---------- Mobile chrome` (line ~13751); `<head>` gets `<link rel="stylesheet" href="web/listener.css">`; after the main `<script>` block: `<script src="web/listener.js"></script>`.
- Test: `tests/listener-shell.test.mjs` (new, headless, same harness style as `core/tests/web_core.mjs`)

**Interfaces:**
- Produces `window.fsListen`:
  - `state()` → `{ signedIn, place, sound, gps: { on, held, acc }, byHand, core: core && core.state || null, live: core && core.live || null, nearest: {name, dist, dir} | null, rows: [{id,name,level,dist,loaded}] }`
  - `sound(on)` → `bedStart()` / `bedStop()` (the existing Sound path)
  - `useLocation()` → `gpsSet(true)`
  - `onChange(fn)` → called (at most every 250 ms) after any `state`/`live` message, GPS fix, sign-in change, Sound change
  - `cells()` → the `#cells` canvas element, and `cellsSet(on)` / `cellsOn()` pass-throughs
  - `_gate(signedIn)` → test hook: runs the gate as if signed in/out
- `document.body.classList` has `listener` exactly when `!setter.signedIn`.

- [ ] **Step 1: Write the failing test** `tests/listener-shell.test.mjs`:

```js
import { test } from "node:test";
import assert from "node:assert/strict";
import { openPage } from "./clients.mjs";   /* if clients.mjs has no page helper, inline the CDP launch from core/tests/web_core.mjs */

test("listener gate and bridge", async () => {
  const { ev, close } = await openPage("http://localhost:8765/");
  try {
    assert.equal(await ev("document.body.classList.contains('listener')"), true);
    assert.equal(await ev("typeof fsListen.state().place"), "string");
    await ev("fsListen._gate(true)");
    assert.equal(await ev("document.body.classList.contains('listener')"), false);
    assert.equal(await ev("getComputedStyle(document.getElementById('panel')).display !== 'none'"), true);
    await ev("fsListen._gate(false)");
    assert.equal(await ev("document.body.classList.contains('listener')"), true);
    assert.equal(await ev("getComputedStyle(document.querySelector('header')).display"), "none");
  } finally { await close(); }
});
```

- [ ] **Step 2: Run** — `node --test tests/listener-shell.test.mjs` → FAIL (`listener` class missing).

- [ ] **Step 3: Implement.** In the gating function, after `var show = setter.signedIn;`: `document.body.classList.toggle("listener", !show); if (window.fsListen) { fsListen._changed(); }`. The bridge section:

```js
  /* ---------- Listener bridge ----------
     What web/listener.js may read and do, and nothing else: the listener shell never reaches
     into the page's own state, so the setter's redesign (C) can split the pages along this line. */
  (function () {
    var subs = [], queued = false;
    function changed() {
      if (queued) { return; }
      queued = true;
      setTimeout(function () { queued = false; subs.forEach(function (f) { try { f(); } catch (e) { console.error(e); } }); }, 250);
    }
    window.fsListen = {
      state: function () {
        return { signedIn: setter.signedIn, place: $("#place-name").textContent, sound: !!((core && core.on) || (bed && bed.on)),
                 gps: { on: gps.on, held: gps.held, acc: gps.acc || null }, byHand: !gps.on,
                 core: (core && core.state) || null, live: (core && core.live) || null,
                 nearest: typeof nearestHint === "function" ? nearestHint() : null };
      },
      sound: function (on) { wantSound = !!on; return on ? bedStart() : bedStop(); },
      useLocation: function () { gpsSet(true); changed(); },
      onChange: function (fn) { subs.push(fn); },
      cells: function () { return $("#cells"); },
      cellsSet: function (on) { cellsSet(on); changed(); },
      cellsOn: function () { return cells.on; },
      _changed: changed,
      _gate: function (on) { setter.signedIn = on; applyModeGating(); }
    };
  })();
```

Call `fsListen._changed()` at the end of the `state` and `live` branches of `coreMessage`, in `gpsFix`, in `gpsSet`, and where `#patch-play`'s label is set (`coreButton`, `bedStart`, `bedStop`). Store `gps.acc = acc` in `gpsFix`. If `nearestHint` does not exist, write it: nearest published point with audio to `pacer.pos` by `segment()`, with a compass word from the bearing (`north`, `north-east`, …), matching the apps' "Walk toward X, south-east."; use the name of the function the old panel already uses for "Nothing in range here" if one exists (grep `Nothing in range`).

`web/listener.css`: `body.listener header, body.listener #panel, body.listener #patchbar, body.listener #pacerbar, body.listener #readout, body.listener #markbar, body.listener #locate, body.listener #sheet { display: none !important; }` (confirm each id with grep; add any other listener-visible old control found in the screenshot: Locate button, zoom stays).

- [ ] **Step 4: Run** → PASS. `npm test` still green.

- [ ] **Step 5: Commit** — `git commit -am "W1: listener bridge and gate"` (plus the new files).

---

### Task 4: The shell — top bar, walk panel, fold, cells slot

**Files:**
- Create: `web/listener.js`, `web/listener.css` (extends Task 3's file)
- Test: `tests/listener-shell.test.mjs` (extend)

**Interfaces:**
- Consumes: `window.fsListen` (Task 3), `core.live` shape (Task 2).
- Produces DOM: `#ls` (root, `role="region" aria-label="Walk"`), `#ls-top` (`#ls-place` button, `#ls-layers`, `#ls-account` round buttons), `#ls-panel` (`#ls-grip`, `#ls-walk` | `#ls-bar`), `#ls-cells` (slot the `#cells` canvas is moved into). W2 fills `#ls-panel` with sheets; W3 with cards.

- [ ] **Step 1: Extend the test**:

```js
test("walk panel: first screen before Sound, then hearing", async () => {
  const { ev, close } = await openPage("http://localhost:8765/");
  try {
    await ev("new Promise(r => setTimeout(r, 4000))");
    assert.match(await ev("document.querySelector('#ls-walk').textContent"), /Sound/);           /* Review Focus 2 */
    assert.equal(await ev("document.querySelector('#ls-cells canvas#cells') !== null"), true);
    await ev("document.querySelector('#ls-sound').click()");
    await ev("__fa.walkTo(29.038879, 41.00771)");
    await ev("new Promise(r => setTimeout(r, 6000))");
    assert.match(await ev("document.querySelector('#ls-walk').textContent"), /Route .*chord \d+ of 16/);
    assert.match(await ev("document.querySelector('#ls-sound').textContent"), /Stop/);
    await ev("document.querySelector('#ls-grip').click()");
    assert.equal(await ev("!!document.querySelector('#ls-bar:not([hidden])')"), true);
    assert.equal(await ev("document.documentElement.scrollHeight - innerHeight"), 0);
  } finally { await close(); }
});
```

- [ ] **Step 2: Run** → FAIL (`#ls-walk` missing).

- [ ] **Step 3: Implement `web/listener.js`** (IIFE, ES5 style like index.html). Structure:

```js
/* The listener's frame on the website: the apps' views (ios/App.swift, WalkPanel.swift) over the
   same map and engine. It reads and acts only through window.fsListen (index.html). */
(function () {
  var L = window.fsListen; if (!L) { return; }
  function h(tag, attrs, kids) { var e = document.createElement(tag); for (var k in attrs || {}) { if (k === "text") { e.textContent = attrs[k]; } else { e.setAttribute(k, attrs[k]); } } (kids || []).forEach(function (c) { if (c) { e.appendChild(c); } }); return e; }
  var root = h("div", { id: "ls", role: "region", "aria-label": "Walk" });
  var top = h("div", { id: "ls-top" }, [
    h("button", { id: "ls-place", type: "button", "aria-label": "Places" }, [h("span", { id: "ls-place-name" }), h("span", { class: "caret", text: "▾" })]),
    h("span", { class: "ls-gap" }),
    h("button", { id: "ls-layers", type: "button", class: "ls-round", "aria-label": "Layers", text: "≋" }),
    h("button", { id: "ls-account", type: "button", class: "ls-round", "aria-label": "Account", text: "◯" })]);
  var cellsSlot = h("div", { id: "ls-cells", hidden: "" });
  var panel = h("section", { id: "ls-panel" }, [
    h("button", { id: "ls-grip", type: "button", "aria-label": "Hide the panel" }, [h("span")]),
    h("div", { id: "ls-walk" }), h("div", { id: "ls-bar", hidden: "" })]);
  root.appendChild(top); root.appendChild(cellsSlot); root.appendChild(panel);
  document.getElementById("map").appendChild(root);
  cellsSlot.appendChild(L.cells());
  var folded = false;
  /* … render(): builds #ls-walk / #ls-bar from L.state() with the apps' copy:
       header: place name (display face), Sound/Stop button (#ls-sound), GPS chip
               ("±N m" live, "±N m · HOLDING" held, "BY HAND" when gps off)
       route line (when st.core.route or rhythms): "Route <b>name</b> · chord N of 16, Label · rhythm <b>A, B</b>"
               + Cells toggle (#ls-cells-btn, aria-pressed) when a route plays
       rows: the two nearest st.core.rows, each: lamp dot, name, distance or "Preparing"/"Loading N%", level bar
       empty: "Nothing in range here. The nearest recording is X, 120 m south." / "No recordings are published yet."
       by hand: note + "Use my location" (#ls-locate) → L.useLocation()
       holding: "GPS is too rough here, so the sound holds where you last were until the fix is better than 40 m."
     #ls-bar: place (or "Listening to …"), chord, Sound/Stop.
     Cells slot shown when L.cellsOn() && a route plays; the route's own morph.cells state
     (live.shown) is applied once per route change via L.cellsSet(live.shown). */
  L.onChange(render); render();
})();
```

Write `render()` in full following that comment (each string copied from ios/WalkPanel.swift; distances formatted `N m` under 1000, `N.N km` above, in the mono face). The Sound button calls `L.sound(!L.state().sound)`. The grip toggles `folded`, swaps `#ls-walk`/`#ls-bar`, sets its label "Hide the panel"/"Show the panel".

`web/listener.css`:

```css
#ls { position: absolute; inset: 0; pointer-events: none; z-index: 5; font-family: var(--font-body); color: var(--ink); }
#ls > * { pointer-events: auto; }
#ls-top { position: absolute; top: var(--s-3); left: var(--s-4); right: var(--s-4); display: flex; gap: var(--s-2); align-items: center; }
.ls-gap { flex: 1; }
#ls-place { min-height: 48px; padding: 0 var(--s-4); border-radius: 999px; background: color-mix(in oklch, var(--panel) 92%, transparent); border: 1px solid var(--hairline); font-family: var(--font-display); font-size: var(--t-md); color: var(--ink); }
.ls-round { width: 48px; height: 48px; border-radius: 50%; background: color-mix(in oklch, var(--panel) 92%, transparent); border: 1px solid var(--hairline); color: var(--ink); }
#ls-panel { position: absolute; left: 0; right: 0; bottom: 0; background: var(--panel); border-radius: 20px 20px 0 0; border-top: 1px solid var(--hairline); padding: 0 var(--s-4) calc(var(--s-5) + env(safe-area-inset-bottom)); display: flex; flex-direction: column; gap: var(--s-3); }
#ls-grip { height: 44px; background: none; border: 0; display: grid; place-items: center; }
#ls-grip span { width: 38px; height: 4px; border-radius: 2px; background: var(--hairline); }
#ls-cells { position: absolute; left: var(--s-4); width: 136px; height: 136px; }
#ls-cells #cells { position: static; width: 100%; height: 100%; }
@media (min-width: 861px) {
  #ls-panel { left: var(--s-4); right: auto; top: calc(48px + var(--s-3) * 2); bottom: auto; width: 380px; border-radius: 16px; border: 1px solid var(--hairline); padding-bottom: var(--s-4); max-height: calc(100% - 48px - 240px - var(--s-3) * 4); overflow: hidden; }
  #ls-cells { bottom: var(--s-4); width: 240px; height: 240px; }
}
@media (prefers-reduced-motion: reduce) { #ls * { transition: none !important; } }
```

(Token names: confirm each `var(--…)` exists in index.html `:root` — `grep -o "\-\-[a-z0-9-]*:" index.html | sort -u` — and use the real names; the ones above are the Tokens.md names.) On a phone the cells slot's `bottom` is set in JS each render to `panel.offsetHeight + 12px` so it rides the panel.

- [ ] **Step 4: Run** → PASS. `npm test` green, `npm run check` green.

- [ ] **Step 5: Commit** — `git add web/listener.js web/listener.css tests/listener-shell.test.mjs && git commit -am "W1: the listener shell - top bar, walk panel, fold, cells"`

---

### Task 5: Measure, look, hand over

**Files:** none new (scratch screenshots only).

- [ ] **Step 1: Fit** — with the scratch CDP script (`shot.mjs`, viewports 390×844 touch + 1440×900), for each: before Sound, hearing on a route, folded; assert `scrollHeight - innerHeight === 0` and save the screenshot. Also resize 1440→390 in one session (Review Focus 4): fold state kept, cells resized.
- [ ] **Step 2: Contrast** — in the page, for every text element under `#ls`, compute WCAG contrast of `color` against the nearest opaque background (panel/raised); all body text ≥ 4.5:1, large ≥ 3:1. Record numbers.
- [ ] **Step 3: Look** — read every screenshot; compare with `design/app` mock and the Test 9 phone screenshot; fix anything that doesn't read as the same product.
- [ ] **Step 4: Engine check** — `node core/tests/web_core.mjs 30`: levels as before (-17..-21 dB on the route), no errors.
- [ ] **Step 5: Hand over** — send Kerem the screenshots and a local URL (`python -m http.server 8765`, LAN IP) to open on his phone. Deploy to `main` only on his OK; then fetch the live `index.html`, `web/listener.js` and `web/core.wasm` and confirm they are the new ones (V-5), and run `FS_URL=https://keremaltaylar.github.io/Fieldscape/ node core/tests/web_core.mjs 20`.
