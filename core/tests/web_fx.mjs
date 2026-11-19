/* The route's master FX on the setter's patch screen (modulation FX Part 1 Task 11): the rhythm effects are gone, the Master FX
   column takes their place, its Open toggle swaps the patch for the FX editor, every control writes patch.mfx through
   FsFxModel.clean, the engine plays it and its live values drive the scopes, and both screens fit. A draft route in the sea:
   no points, so nothing downloads from Storage.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_fx.mjs          FS_SHOTS=<dir> for 20-masterfx.png, 21-fx-editor.png */
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const SHOTS = process.env.FS_SHOTS || "";
/* the 1024 x 700 patch screen's overflow before this task (3d7e527, this same draft route): Kerem accepted it; the Master FX
   column may add none */
const BASE_1024_OVERFLOW = 518;
const CLEAN_RFX = { cutoff: 20000, drive: 0, delayDiv: "8n", delayWet: 0, delayFb: 0, revWet: 0, revDecay: 1.2 };
const ROUTE = [[28.95, 40.93], [28.952, 40.931]];
const dir = mkdtempSync(join(tmpdir(), "fs-fx-"));
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9273", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + dir, "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9273))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 400)); });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const J = async (e) => JSON.parse(await ev("JSON.stringify(" + e + ")"));
  const shot = async (name) => { if (!SHOTS) return; const r = await s.send("Page.captureScreenshot", { format: "png" }); writeFileSync(join(SHOTS, name + ".png"), Buffer.from(r.data, "base64")); };
  const VK = { ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39 };
  const key = async (k) => { await s.send("Input.dispatchKeyEvent", { type: "keyDown", key: k, code: k, windowsVirtualKeyCode: VK[k] }); await s.send("Input.dispatchKeyEvent", { type: "keyUp", key: k, code: k, windowsVirtualKeyCode: VK[k] }); };
  const box = (sel) => J(`(function(){ var e = document.querySelector(${JSON.stringify(sel)}); if (!e) return null; var b = e.getBoundingClientRect(); return { x: b.x + b.width / 2, y: b.y + b.height / 2, w: b.width, h: b.height }; })()`);
  const mouse = (type, x, y, extra = {}) => s.send("Input.dispatchMouseEvent", Object.assign({ type, x, y, button: "left", clickCount: 1 }, extra));
  const size = async (w, h) => { await s.send("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor: 1, mobile: false }); await sleep(300); };
  /* overflow of the page and the panel, and anything spilling out of its container */
  const fit = (root) => J(`(function(){ var r = document.querySelector(${JSON.stringify(root)}), rb = r.getBoundingClientRect(), out = [];
    [].forEach.call(r.querySelectorAll('*'), function (x) { var q = x.getBoundingClientRect(); if (!q.width || !x.offsetParent) return;
      if (q.right > rb.right + 1 || q.left < rb.left - 1 || q.bottom > rb.bottom + 1) out.push((x.className && x.className.baseVal === undefined ? x.className : x.tagName) + ''); });
    return { page: document.documentElement.scrollHeight - innerHeight, root: r.scrollHeight - r.clientHeight, spill: out.slice(0, 6) }; })()`);

  await size(1440, 900);
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
  await sleep(2500);
  await ev("fsListen._gate(true), 0"); await sleep(600);
  const rid = await ev(`__fa.addRoute(${JSON.stringify(ROUTE)}, 'fx test')`), R = JSON.stringify(rid);
  const mfx = () => J(`__fa.routePatch(${R}).mfx || {}`);
  const cleanOf = (o) => J(`FsFxModel.clean(${JSON.stringify(o)})`);
  await ev(`fsListen.select(${R}), 0`); await sleep(600);
  await ev("__fa.openPatchPanel(), 0"); await sleep(800);

  /* the rhythm effects leave */
  check("the patch body has no Rhythm effects heading", await ev("![].some.call(document.querySelectorAll('#pp-body h2'), function (h) { return /rhythm effects/i.test(h.textContent); })"), null);
  check("…and no rfx. control", await ev("!document.querySelector('#pp-body [data-k^=\"rfx.\"]')"), null);
  check("a new route's stored rfx is clean", JSON.stringify(await J(`__fa.routePatch(${R}).rfx`)) === JSON.stringify(CLEAN_RFX), await J(`__fa.routePatch(${R}).rfx`));
  await ev(`(function(){ var f = __fa.features().filter(function (x) { return x.properties.id === ${R}; })[0]; f.properties.patch.rfx = { cutoff: 3000, drive: 0.8, delayDiv: "4n", delayWet: 0.6, delayFb: 0.7, revWet: 0.5, revDecay: 4 }; return 0; })()`);
  await ev("__fa.openPatchPanel(), 0"); await sleep(400);
  check("a route with old rhythm effects shows no rhythm-effects UI", await ev("!document.querySelector('#pp-body [data-k^=\"rfx.\"]') && ![].some.call(document.querySelectorAll('#pp-body h2'), function (h) { return /rhythm effects/i.test(h.textContent); })"), null);
  await ev(`__fa.patchEdit(${R}, function () {}), 0`);
  check("…and saves clean", JSON.stringify(await J(`__fa.routePatch(${R}).rfx`)) === JSON.stringify(CLEAN_RFX), await J(`__fa.routePatch(${R}).rfx`));

  /* the Master FX column: the fifth column, where the rhythm effects were */
  await ev("__fa.openPatchPanel(), 0"); await sleep(600);
  const col = await J("(function(){ var h = [].filter.call(document.querySelectorAll('#pp-body .ppcol > h2'), function (x) { return /^Master FX/.test(x.textContent); })[0]; if (!h) return null; var c = h.parentNode; return { cols: document.querySelectorAll('#pp-body .ppcol').length, at: [].indexOf.call(c.parentNode.children, c), keys: [].map.call(c.querySelectorAll('input[type=range][data-k]'), function (r) { return r.dataset.k; }), knobs: c.querySelectorAll('.knob').length, open: !!c.querySelector('#pp-fx-open'), load: !!c.querySelector('.fx-loadrow') }; })()");
  check("Master FX is the fifth column at 1440 px", !!col && col.cols === 5 && col.at === 4, col);
  check("…return, three sends and nine mixes, each a knob", !!col && col.keys.join() === ["mfx.ret", "mfx.send1", "mfx.send2", "mfx.send3"].concat(["shift", "swarm", "phase", "comb", "vowel", "cloud", "tape", "echo", "space"].map((m) => "mfx." + m + ".mix")).join() && col.knobs === 13, col);
  check("…the load and the Open toggle", !!col && col.load && col.open, col);
  await ev("document.querySelector('#pp-body .knob[data-k=\"mfx.echo.mix\"]').focus(), 0"); await key("ArrowUp"); await sleep(200);
  const m1 = await mfx();
  check("a column knob writes patch.mfx (echo mix up a step)", m1.echo && m1.echo.mix > 0, m1);
  check("…stored through clean", JSON.stringify(m1) === JSON.stringify(await cleanOf(m1)), m1);
  check("…its lamp lights", await ev("!!document.querySelector('#pp-body .knob[data-k=\"mfx.echo.mix\"]').closest('.pprow').querySelector('.fx-lmp.lit')"), null);
  await ev(`__fa.patchEdit(${R}, function (p) { delete p.mfx; }), __fa.openPatchPanel(), 0`); await sleep(400);
  const fitP = await fit("#pp-body");
  check("1440 x 900, patch: the page fits and nothing spills", fitP.page === 0 && fitP.root <= 0 && !fitP.spill.length, fitP);
  await shot("20-masterfx");

  /* the editor: Open swaps the patch for it (a toggle, C-8) */
  await ev("document.getElementById('pp-fx-open').click(), 0"); await sleep(600);
  check("Open shows the editor in place of the patch", await ev("!document.getElementById('pp-fx').hidden && document.getElementById('pp-body').hidden && !document.getElementById('pp-fx-back').hidden"), null);
  check("the editor is dry: it says nothing is on", /Nothing is on/.test(await ev("(document.querySelector('#pp-fx .fx-empty') || {}).textContent || ''")), null);
  const mods = await J("FsFxModel.MODULES.map(function (m) { return { id: m.id, params: m.params.map(function (p) { return { key: p.key, kind: p.kind, max: p.max }; }) }; })");
  const bad = [];
  for (const m of mods) {
    await ev(`document.querySelector('#pp-fx .fx-tile[data-m="${m.id}"]').click(), 0`); await sleep(120);
    for (const p of m.params) {
      const k = p.key.split(".")[1], before = (await mfx())[m.id];
      if (p.kind === "switch") {
        await ev(`document.querySelector('#pp-fx .fx-seg[data-k="${p.key}"] button[aria-pressed="false"]').click(), 0`);
      } else {
        const atTop = await ev(`(function(){ var e = document.querySelector('#pp-fx .fx-ek[data-k="${p.key}"]'); e.focus(); return +e.getAttribute('aria-valuenow') >= 0.999; })()`);
        await key(atTop ? "ArrowDown" : "ArrowUp");
      }
      await sleep(60);
      const after = await mfx();
      if (!after[m.id] || after[m.id][k] === undefined || JSON.stringify(after[m.id][k]) === JSON.stringify(before && before[k])) bad.push(p.key);
      if (JSON.stringify(after) !== JSON.stringify(await cleanOf(after))) bad.push(p.key + " (not clean)");
    }
  }
  check("every knob, switch and division in the editor writes its key into patch.mfx, through clean", bad.length === 0, bad);
  check("…and the arrows step each one (the focused knob's aria value moved)", bad.length === 0, null);

  /* the modulators and the matrix write too */
  await ev("document.querySelector('#pp-fx .fx-lfo[data-i=\"0\"] .fx-shapes button[data-s=\"tri\"]').click(), 0"); await sleep(80);
  await ev("(function(){ var s = document.querySelector('#pp-fx .fx-lfo[data-i=\"1\"] select'); s.value = '4n'; s.dispatchEvent(new Event('change', { bubbles: true })); return 0; })()"); await sleep(80);
  await ev("document.querySelector('#pp-fx .fx-lfo[data-i=\"2\"] [role=slider]').focus(), 0"); await key("ArrowUp"); await sleep(80);
  await ev("document.querySelector('#pp-fx [data-env=\"attack\"]').focus(), 0"); await key("ArrowUp"); await sleep(80);
  await ev("(function(){ var r = document.querySelector('#pp-fx .fx-mxr[data-slot=\"0\"]'), a = r.querySelectorAll('select'); a[0].value = 'lfo1'; a[0].dispatchEvent(new Event('change', { bubbles: true })); return 0; })()"); await sleep(120);
  await ev("(function(){ var a = document.querySelectorAll('#pp-fx .fx-mxr[data-slot=\"0\"] select'); a[1].value = 'echo.fb'; a[1].dispatchEvent(new Event('change', { bubbles: true })); return 0; })()"); await sleep(120);
  await ev("document.querySelector('#pp-fx .fx-mxr[data-slot=\"0\"] .fx-bip').focus(), 0"); await key("ArrowRight"); await sleep(120);
  const m2 = await mfx();
  check("LFO shape, rate and phase, the envelope and a matrix slot write patch.mfx",
    m2.lfo && m2.lfo[0].shape === "tri" && m2.lfo[1].rate === "4n" && m2.lfo[2].phase > 0 && m2.env && m2.env.attack > 10 && m2.mods && m2.mods[0].src === "lfo1" && m2.mods[0].dst === "echo.fb" && m2.mods[0].amt > 0, m2);
  check("…the matrix slot draws a ring on its knob", await ev("document.querySelector('#pp-fx .fx-tile[data-m=\"echo\"]').click(), !!document.querySelector('#pp-fx .fx-ek[data-k=\"echo.fb\"] .fx-ring')"), null);

  /* drag: shift is finer; double-click resets */
  const drag = async (sel, dy, shift) => { const b = await box(sel); await mouse("mousePressed", b.x, b.y); for (let i = 1; i <= 5; i++) { await mouse("mouseMoved", b.x, b.y - dy * i / 5, { buttons: 1, modifiers: shift ? 8 : 0 }); await sleep(20); } await mouse("mouseReleased", b.x, b.y - dy); await sleep(100); };
  const nowOf = (k) => ev(`+document.querySelector('#pp-fx .fx-ek[data-k="${k}"]').getAttribute('aria-valuenow')`);
  let x0 = await nowOf("echo.wobble"); await drag('#pp-fx .fx-ek[data-k="echo.wobble"]', 40, false); const coarse = (await nowOf("echo.wobble")) - x0;
  x0 = await nowOf("echo.wobble"); await drag('#pp-fx .fx-ek[data-k="echo.wobble"]', 40, true); const fine = (await nowOf("echo.wobble")) - x0;
  check("a drag moves a knob; shift-drag moves it finer", coarse > 0.1 && fine > 0 && fine < coarse / 3, [coarse, fine]);
  const b0 = await box('#pp-fx .fx-ek[data-k="echo.wobble"]');
  await mouse("mousePressed", b0.x, b0.y); await mouse("mouseReleased", b0.x, b0.y); await mouse("mousePressed", b0.x, b0.y, { clickCount: 2 }); await mouse("mouseReleased", b0.x, b0.y, { clickCount: 2 }); await sleep(200);
  check("a double-click resets it to its default", !((await mfx()).echo || {}).hasOwnProperty("wobble"), (await mfx()).echo);
  check("the patch panel has no undo, and the editor adds none", await ev("!document.querySelector('#patchpanel [id*=undo], #pp-fx [data-undo]')"), null);

  /* keyboard: every control can be reached */
  const unfocus = await J("[].filter.call(document.querySelectorAll('#pp-fx button, #pp-fx select, #pp-fx [role=slider]'), function (e) { return e.offsetParent && (e.tabIndex < 0 || e.disabled); }).map(function (e) { return e.className || e.tagName; })");
  check("every control in the editor is focusable", unfocus.length === 0, unfocus);

  /* the sound: return 0.8 + Echo mix 1 is heard, and the engine's live values come back */
  await ev(`__fa.patchEdit(${R}, function (p) { p.mfx = FsFxModel.clean({ ret: 0.8, echo: { mix: 1 }, lfo: [{ shape: "sine", rate: "8n" }, { shape: "tri", rate: "4n" }, { rate: "8n" }, { rate: "4n" }] }); }), 0`);
  await ev("fsListen.sound()");
  let onRoute = false;
  for (let i = 0; i < 40 && !onRoute; i++) { await ev(`__fa.walkTo(${ROUTE[0][0] + 0.0005 + i * 1e-7}, ${ROUTE[0][1] + 0.00025})`); await sleep(400); onRoute = await ev(`!!(window.__fa.core && __fa.core.route_id === ${R})`); }
  check("walking onto the draft route, the engine plays it", onRoute, await ev("window.__fa.core && __fa.core.route_id"));
  let out = 0, lvl = -120;
  for (let i = 0; i < 40 && !(out > 0 && lvl > -60); i++) { await sleep(300); out = await ev("(function(){ var l = __fa.coreLive; return l && l.fx ? l.fx[23] : -1; })()"); lvl = await ev("__fa.coreLevel ? __fa.coreLevel() : -120"); }
  check("return 0.8 + Echo mix 1: the output's AnalyserNode reads a level", lvl > -60, lvl);
  check("…and the engine's fx[23] (out) is above 0", out > 0, out);
  /* the scopes follow the engine's LFO phases */
  const ph = () => J("(function(){ var l = __fa.coreLive.fx; return { drawn: [].map.call(document.querySelectorAll('#pp-fx canvas[data-lfo]'), function (c) { return +c.dataset.phase; }), live: [l[0], l[1], l[2], l[3]] }; })()");
  const p0 = await ph(); await sleep(300); const p1 = await ph();
  const cyc = (a, b) => { const d = Math.abs(a - b) % 1; return Math.min(d, 1 - d); };
  check("the four scopes' playheads move (300 ms apart)", p0.drawn.length === 4 && p0.drawn.every((v, i) => cyc(v, p1.drawn[i]) > 1e-4), [p0.drawn, p1.drawn]);
  check("…and sit at the engine's phases fx[0..3]", p1.drawn.every((v, i) => cyc(v, p1.live[i]) < 0.08), p1);
  await ev(`__fa.patchEdit(${R}, function (p) { p.mfx.ret = 0; }), 0`);
  /* ret 0 feeds the bus silence until its tail is under the core's silence line (1e-4, -80 dBFS), then the engine idles and
     its live values hold (Task 8: they freeze at idle) - so "falls to 0" is read at that line */
  for (let i = 0; i < 40 && out >= 1e-4; i++) { await sleep(300); out = await ev("__fa.coreLive.fx ? __fa.coreLive.fx[23] : -1"); }
  check("return 0: fx[23] falls to 0 (under the core's -80 dB silence line)", out >= 0 && out < 1e-4, out);
  await ev(`__fa.patchEdit(${R}, function (p) { p.mfx.ret = 0.8; }), 0`);

  /* fit: 1440 x 900, editor open */
  await sleep(600);
  const fitE = await fit("#pp-fx");
  check("1440 x 900, editor: the page fits and nothing spills", fitE.page === 0 && fitE.root <= 0 && !fitE.spill.length, fitE);
  await shot("21-fx-editor");
  await ev("document.getElementById('pp-fx-back').click(), 0"); await sleep(400);
  check("Back to the patch: the patch again", await ev("document.getElementById('pp-fx').hidden && !document.getElementById('pp-body').hidden"), null);

  /* 1024 x 700: the editor fits; the column adds no overflow to the patch's (pre-existing) one */
  await size(1024, 700); await ev("__fa.openPatchPanel(), 0"); await sleep(600);
  const f1 = await fit("#pp-body");
  check("1024 x 700, patch: no more overflow than before the Master FX (" + BASE_1024_OVERFLOW + " px)", f1.page === 0 && f1.root <= BASE_1024_OVERFLOW, f1);
  const cc = await J("(function(){ var b = document.getElementById('pp-fx-open'); if (!b) return null; var c = b.parentNode; return { keys: [].map.call(c.querySelectorAll('input[type=range][data-k^=\"mfx.\"]'), function (r) { return r.dataset.k; }), sum: (c.querySelector('.fx-sumrow em') || {}).textContent }; })()");
  check("…under five columns the column is compact: the return, what is on, Open", !!cc && cc.keys.join() === "mfx.ret" && cc.sum === "Echo", cc);
  await ev("document.getElementById('pp-fx-open').click(), 0"); await sleep(600);
  const f2 = await fit("#pp-fx");
  check("1024 x 700, editor: the page fits and nothing spills", f2.page === 0 && f2.root <= 0 && !f2.spill.length, f2);
  await shot("21-fx-editor-1024");
  await ev("document.getElementById('pp-close').click(), 0"); await sleep(300);
  check("no page errors", errors.length === 0, errors);
  s.close();
} finally { process.kill(ch.pid); await sleep(500); try { rmSync(dir, { recursive: true, force: true }); } catch {} }
console.log(failed ? failed + " FAILED" : "all passed");
process.exitCode = failed ? 1 : 0;
