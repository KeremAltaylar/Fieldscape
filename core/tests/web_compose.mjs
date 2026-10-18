/* Composition step 1 in the browser: the dock opens from a route's patch panel, its cells and bands select scopes, the
   inspector writes overrides into the route's comp and the engine gets them, undo works, the screen fits. Local edits only.
     node tools/serve.mjs 8765 ; node core/tests/web_compose.mjs      FS_SHOTS=<dir> */
import { spawn } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";
const URL = process.env.FS_URL || "http://localhost:8765/", SHOTS = process.env.FS_SHOTS || "";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe", ["--headless=new", "--remote-debugging-port=9297", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-cmp-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0; const check = (n, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + n + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9297))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready; await s.send("Runtime.enable"); await s.send("Page.enable");
  const errors = []; s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 300)); });
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const shot = async (n) => { if (!SHOTS) return; const r = await s.send("Page.captureScreenshot", { format: "png" }); writeFileSync(join(SHOTS, n + ".png"), Buffer.from(r.data, "base64")); };
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
  await sleep(2000); await ev("fsListen._gate(true), 0"); await sleep(600);
  const rid = await ev("(__fa.features().filter(function (f) { return f.properties.kind === 'route' && (f.properties.patch || {}).prog; })[0] || {properties:{}}).properties.id");
  await ev(`fsListen.select(${JSON.stringify(rid)}), 0`); await sleep(800);
  await ev("__fa.openPatchPanel && __fa.openPatchPanel(), 0"); await sleep(800);
  check("the patch panel has Compose", await ev("!!document.getElementById('pp-compose')"), null);
  await ev("document.getElementById('pp-compose').click(), 0"); await sleep(800);
  check("the dock opens: 16 chord cells, 3 lanes + the bass row", await ev("document.querySelectorAll('#cd-strip .cd-chord').length === __fa.routePatch(" + JSON.stringify(rid) + ").prog.length && document.querySelectorAll('#cd-lanes .cd-lane').length === 4"), null);
  const fit = await ev("JSON.stringify({ doc: document.documentElement.scrollHeight - innerHeight, dock: (function(){ var d = document.getElementById('compose-dock'); return d ? d.scrollHeight - d.clientHeight : 99; })() })");
  check("one screen: nothing scrolls", JSON.parse(fit).doc <= 0 && JSON.parse(fit).dock <= 1, fit);
  const spill = await ev(`(function(){ var out = [];
    [["#compose-dock", "#compose-dock > *"], [".cd-main", ".cd-main > *, .cd-cell, .cd-chord"], ["#cd-insp", "#cd-insp > *, #cd-insp .cd-row2 > *"]].forEach(function (k) {
      var c = document.querySelector(k[0]); if (!c) { out.push(k[0] + " missing"); return; } var r = c.getBoundingClientRect();
      [].forEach.call(document.querySelectorAll(k[1]), function (x) { var q = x.getBoundingClientRect();
        if (q.width && (q.right > r.right + 1 || q.bottom > r.bottom + 1 || q.left < r.left - 1)) out.push(k[0] + " > " + x.className); }); });
    return out; })()`);
  check("nothing in the dock spills", spill.length === 0, spill.slice(0, 5));
  await shot("1-open");
  /* chord 5, voice 1: the inspector shows route values dimmed; style -> strum writes an override */
  await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-cell[data-i=\"4\"]').click(), 0"); await sleep(300);
  check("the inspector names the scope", /chord 5/.test(await ev("document.getElementById('cd-insp-title').textContent")), await ev("document.getElementById('cd-insp-title').textContent"));
  check("inherited settings are dimmed with their source", await ev("document.querySelector('#cd-insp .cd-row2[data-k=\"rate\"]').classList.contains('inh') && /route/.test(document.querySelector('#cd-insp .cd-row2[data-k=\"rate\"] .cd-src').textContent)"), null);
  await ev("(function(){ var m = document.querySelector('#cd-insp .cd-row2[data-k=\"style\"] select'); m.value = 'strum'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(500);
  const comp = JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${JSON.stringify(rid)}).comp)`));
  check("chord 5's override is saved in the route's comp", comp && comp.over.chord["4"] && comp.over.chord["4"].voice.style === "strum", comp && comp.over);
  check("the cell shows it, outlined (a chord's)", await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-cell[data-i=\"4\"]').classList.contains('src-chord')"), null);
  check("the row is now this chord's own, with a reset", await ev("!document.querySelector('#cd-insp .cd-row2[data-k=\"style\"]').classList.contains('inh') && !!document.querySelector('#cd-insp .cd-row2[data-k=\"style\"] .cd-reset')"), null);
  /* undo */
  await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(400);
  check("Ctrl+Z takes it back", !JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${JSON.stringify(rid)}).comp.over.chord)`))["4"], null);
  /* a template */
  await ev("(function(){ var m = document.getElementById('cd-template'); m.value = 'arp-strum-pedal'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(500);
  check("a template writes the route's styles", await ev(`__fa.routePatch(${JSON.stringify(rid)}).comp.voice.style === 'arpeggio'`), null);
  await shot("2-template");
  /* the engine has it */
  await ev("fsListen.sound()"); for (let i = 0; i < 30 && !(await ev("!!__fa.coreSentFeature")); i++) await sleep(300);
  await sleep(800);
  check("the engine got the comp", await ev(`(function(){ var s = __fa.coreSentFeature(${JSON.stringify(rid)}); return !!s && JSON.parse(s).properties.patch.comp.voice.style === 'arpeggio'; })()`), null);
  check("the chord's stretch is lit on the map", await ev("(function(){ var m = __fa.map, s = m.getSource('compsel'); var d = s && s.serialize().data; return !!m.getLayer('compsel-line') && !!d && d.features.length === 1 && d.features[0].geometry.type === 'LineString'; })()"), null);
  /* a zone: its band selects it, a style there tints the lanes it covers */
  const zoned = await ev("(function(){ var b = document.querySelector('#cd-lanes') && document.querySelector('.cd-band.zone'); if (!b) return false; b.click(); return true; })()"); await sleep(300);
  if (zoned) {
    check("a zone's inspector has when and blend", await ev("!!document.querySelector('#cd-insp .cd-row2[data-k=\"when\"]') && !!document.querySelector('#cd-insp .cd-row2[data-k=\"blend\"]')"), null);
    await ev("(function(){ var m = document.querySelector('#cd-insp .cd-row2[data-k=\"style\"] select'); m.value = 'euclid'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400);
    check("the zone's cells are tinted and outlined", await ev("document.querySelectorAll('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-cell.src-zone.lit').length > 0"), null);
    await shot("3-zone");
  }
  /* a route with no chords: the dock says so and writes nothing */
  const empty = await ev(`(function(){ var f = __fa.features().filter(function (g) { return g.properties.kind === 'route'; })[0]; var c = JSON.parse(JSON.stringify(f)); c.properties.patch = Object.assign({}, c.properties.patch, { prog: [] });
    FsComposeDock.open({ route: c, comp: function () { return FsCompose.defaults(); }, save: function () { window.__saved = true; }, chords: [], coords: c.geometry.coordinates,
      sections: function () { return null; }, zones: function () { return []; }, light: function () {}, listenAt: function () {} });
    return document.querySelector('#compose-dock .cd-empty') ? document.querySelector('#compose-dock .cd-empty').textContent : null; })()`);
  check("no chords: the dock asks for them and saves nothing", /no chords yet/.test(empty || "") && !(await ev("!!window.__saved")), empty);
  await ev("FsComposeDock.close(), 0");
  /* desk only: a phone's patch panel has no Compose */
  await s.send("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 2, mobile: true }); await sleep(600);
  await ev("__fa.openPatchPanel(), 0"); await sleep(600);
  check("phones: no Compose", await ev("!document.getElementById('pp-compose')"), null);
  check("no page errors", errors.length === 0, errors.slice(0, 3));
} finally { ch.kill(); }
console.log(failed ? failed + " FAILED" : "all passed"); process.exit(failed ? 1 : 0);
