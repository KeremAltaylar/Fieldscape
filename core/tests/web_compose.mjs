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
  const R = JSON.stringify(rid);
  const key = async (k, code, mods = 0) => { await s.send("Input.dispatchKeyEvent", { type: "keyDown", key: k, code, windowsVirtualKeyCode: { " ": 32, Delete: 46, Escape: 27, z: 90 }[k], modifiers: mods, text: k === " " ? " " : undefined });
    await s.send("Input.dispatchKeyEvent", { type: "keyUp", key: k, code, windowsVirtualKeyCode: { " ": 32, Delete: 46, Escape: 27, z: 90 }[k], modifiers: mods }); await sleep(300); };
  const spillOf = () => ev(`(function(){ var out = [];
    [["#compose-dock", "#compose-dock > *"], [".cd-main", ".cd-main > *, .cd-cell, .cd-chord"], ["#cd-insp", "#cd-insp > *, #cd-insp .cd-row2 > *"]].forEach(function (k) {
      var c = document.querySelector(k[0]); if (!c) { out.push(k[0] + " missing"); return; } var r = c.getBoundingClientRect();
      [].forEach.call(document.querySelectorAll(k[1]), function (x) { var q = x.getBoundingClientRect();
        if (q.width && (q.right > r.right + 1 || q.bottom > r.bottom + 1 || q.left < r.left - 1)) out.push(k[0] + " > " + x.className); }); });
    var d = document.getElementById('compose-dock'); if (d && d.scrollHeight - d.clientHeight > 1) out.push('dock scrolls');
    if (document.documentElement.scrollHeight - innerHeight > 0) out.push('page scrolls');
    return out; })()`);
  await ev(`fsListen.select(${R}), 0`); await sleep(800);
  /* a route with no chords, through the real path: the dock says so, writes nothing, and its button opens the patch */
  const hadComp = await ev(`!!__fa.routePatch(${R}).comp`);
  await ev(`__fa.patchEdit(${R}, function (p) { window.__prog = p.prog; p.prog = []; }), 0`); await sleep(300);
  await ev("__fa.openPatchPanel(), 0"); await sleep(500);
  await ev("document.getElementById('pp-compose').click(), 0"); await sleep(600);
  check("no chords: the dock asks for them", /no chords yet/.test((await ev("(document.querySelector('#compose-dock .cd-empty') || {}).textContent")) || ""), null);
  check("no chords: openCompose writes no comp", hadComp || !(await ev(`!!__fa.routePatch(${R}).comp`)), null);
  await ev("[].filter.call(document.querySelectorAll('#compose-dock .cd-empty button'), function (b) { return b.textContent === 'Open the patch'; })[0].click(), 0"); await sleep(500);
  check("no chords: Open the patch opens the route's patch panel", await ev("!document.getElementById('patchpanel').hidden && !FsComposeDock.isOpen()"), null);
  await ev(`__fa.patchEdit(${R}, function (p) { p.prog = window.__prog; }), 0`); await sleep(300);
  await ev("__fa.openPatchPanel && __fa.openPatchPanel(), 0"); await sleep(800);
  check("the patch panel has Compose", await ev("!!document.getElementById('pp-compose')"), null);
  /* final review M6: opening and closing the dock without an edit writes nothing (the route would publish "Free · Free · Free") */
  await ev(`__fa.patchEdit(${R}, function (p) { window.__comp0 = p.comp; delete p.comp; }), 0`); await sleep(300);
  await ev("__fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0"); await sleep(800);
  await ev("FsComposeDock.close(), 0"); await sleep(300);
  check("open and close without an edit: the route's patch has no comp", !(await ev(`!!__fa.routePatch(${R}).comp`)), null);
  await ev("__fa.openPatchPanel(), 0"); await sleep(500);
  await ev("document.getElementById('pp-compose').click(), 0"); await sleep(800);
  /* final review I3: the bass's fixed pedal has its note */
  const pick = (k, v) => ev(`(function(){ var m = document.querySelector('#cd-insp .cd-row2[data-k="${k}"] select'); m.value = '${v}'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0`);
  await ev("[].filter.call(document.querySelectorAll('#cd-insp .cd-vtabs button'), function (b) { return b.textContent === 'bass'; })[0].click(), 0"); await sleep(300);
  await pick("mode", "pedal"); await sleep(300); await pick("pedal", "fixed"); await sleep(300);
  await ev(`(function(){ var k = document.querySelector('#cd-insp .cd-row2[data-k="note"] input'); k.value = 45; k.dispatchEvent(new Event('change', { bubbles: true })); })(), 0`); await sleep(400);
  const bc = JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${R}).comp.bass)`));
  check("bass pedal fixed: its note is written (patch.comp.bass.note)", bc && bc.mode === "pedal" && bc.pedal === "fixed" && bc.note === 45, bc);
  check("the dock opens: 16 chord cells, 3 lanes + the bass row", await ev("document.querySelectorAll('#cd-strip .cd-chord').length === __fa.routePatch(" + JSON.stringify(rid) + ").prog.length && document.querySelectorAll('#cd-lanes .cd-lane').length === 4"), null);
  /* 2026-10-19: the dock's foot says whether the audio has dropped out (the engine's slow blocks, polled once a second) */
  await sleep(1500);
  const au = await ev("(function(){ var a = document.getElementById('cd-audio'); return a ? { text: a.textContent, title: a.title, n: window.__fa.dropouts && __fa.dropouts.n, polls: window.__fa.dropouts && __fa.dropouts.polls } : null; })()");
  check("the foot's audio readout: 'audio ok' or a count of dropouts", !!au && (au.text === "audio ok" ? au.n === 0 : /^\d+ dropouts? in the last minute$/.test(au.text) && au.n > 0) && /slowest block/.test(au.title), au);
  check("the readout polls while the dock is open (__fa.dropouts)", !!au && au.polls >= 1, au);
  const spill = await spillOf();
  check("one screen: nothing scrolls or spills", spill.length === 0, spill.slice(0, 5));
  check("the selects read as menus (the chevron)", await ev("/gradient/.test(getComputedStyle(document.getElementById('cd-template')).backgroundImage)"), null);
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
  check("the selected cell keeps its word", await ev("(function(){ var c = document.querySelector('#cd-lanes .cd-cell.on'); return !!c && !c.classList.contains('cont') && getComputedStyle(c.querySelector('span')).color !== 'rgba(0, 0, 0, 0)'; })()"), null);
  /* the keyboard leaves a focused button its own keys */
  await ev("window.__pp = 0; document.getElementById('patch-play').addEventListener('click', function () { window.__pp++; }, true), 0");
  await ev("[].filter.call(document.querySelectorAll('#cd-insp .cd-vtabs button'), function (b) { return b.textContent === 'voice 2'; })[0].focus(), 0");
  await key(" ", "Space");
  check("Space on an inspector button presses it, not the sound", await ev("window.__pp === 0 && /voice 2/i.test(document.getElementById('cd-insp-title').textContent)"), await ev("[window.__pp, document.getElementById('cd-insp-title').textContent]"));
  await ev("[].filter.call(document.querySelectorAll('#cd-insp .cd-vtabs button'), function (b) { return b.textContent === 'voice 1'; })[0].click(), 0"); await sleep(200);
  await ev("document.querySelector('#cd-insp .cd-vtabs button').focus(), 0");
  await key("Delete", "Delete");
  check("Delete on an inspector button clears nothing (and the page does not delete the route)", await ev(`!!__fa.routePatch(${R}) && !!__fa.routePatch(${R}).comp.over.chord["4"]`), null);
  await ev("document.querySelector('#compose-dock .cd-head input').focus(), 0");
  await key("z", "KeyZ", 2);
  check("Ctrl+Z in a field is the field's", await ev(`!!__fa.routePatch(${R}).comp.over.chord["4"]`), null);
  await ev("document.querySelector('#cd-lanes .cd-cell.on').focus(), 0");
  await key(" ", "Space");
  check("Space on a cell is the sound", await ev("window.__pp === 1"), await ev("window.__pp"));
  /* an edit keeps keyboard focus where it was */
  await ev("(function(){ var m = document.querySelector('#cd-insp .cd-row2[data-k=\"rate\"] select'); m.focus(); m.value = '16n'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(300);
  check("an edit keeps focus on its control", await ev("document.activeElement === document.querySelector('#cd-insp .cd-row2[data-k=\"rate\"] select') && document.activeElement.value === '16n'"), await ev("document.activeElement && document.activeElement.outerHTML.slice(0, 80)"));
  /* a save that fails says so and offers Retry; Retry clears it */
  await ev(`(function(){ var o = Storage.prototype.setItem; Storage.prototype.setItem = function () { Storage.prototype.setItem = o; throw new DOMException('full', 'QuotaExceededError'); };
    var m = document.querySelector('#cd-insp .cd-row2[data-k=\"rate\"] select'); m.value = '8n'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0`); await sleep(300);
  check("a failed save: Not saved, with Retry", await ev("(function(){ var w = document.querySelector('#compose-dock .cd-saved.warn'); return !!w && /^Not saved: /.test(w.textContent) && !!w.querySelector('.cd-retry'); })()"), await ev("(document.querySelector('#compose-dock .cd-saved') || {}).textContent"));
  await shot("4-save-failed");
  await ev("document.querySelector('#compose-dock .cd-retry').click(), 0"); await sleep(300);
  check("Retry saves and the note goes", await ev("!document.querySelector('#compose-dock .cd-saved.warn') && /saved/.test(document.querySelector('#compose-dock .cd-saved').textContent)"), null);
  /* undo */
  /* undo, from the page: the three edits on chord 5 (style, rate 1/16, rate 1/8) back out one by one */
  await ev("document.body.focus(), 0");
  for (let i = 0; i < 3; i++) { await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(250); }
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
  /* Generate form (generate-brief.md): the header's control opens its panel; Generate writes a whole piece in one undo step */
  check("the foot's key hints read in full at 1440", await ev("(function(){ var k = document.querySelector('#compose-dock .cd-keys'); return !!k && k.scrollWidth <= k.clientWidth; })()"),
    await ev("(function(){ var k = document.querySelector('#compose-dock .cd-keys'); return k && [k.scrollWidth, k.clientWidth]; })()"));
  const gBefore = await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`);
  await ev("document.querySelector('#compose-dock .cd-gen').click(), 0"); await sleep(300);
  check("Generate form opens its panel: the forms, contrast, Generate", await ev("/Generate form/.test(document.getElementById('cd-insp-title').textContent) && [].map.call(document.querySelectorAll('#cd-insp .cd-row2[data-k=\"form\"] button'), function (b) { return b.textContent; }).join('|') === 'AABA|ABAB|ABAC|a a′ b|ABACA' && !!document.querySelector('#cd-insp .cd-row2[data-k=\"contrast\"] input') && !!document.getElementById('cd-generate')"), null);
  const gspill = await spillOf();
  check("the Generate panel fits one screen at 1440x900", gspill.length === 0, gspill.slice(0, 5));
  await ev("document.getElementById('cd-generate').focus(), document.getElementById('cd-generate').click(), 0"); await sleep(500);
  const g = JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`));
  check("Generate writes the form, a seed and chord overrides for the 16 chords", !!g.form && g.form.type === "AABA" && g.form.seed === g.seed && Object.keys(g.over.chord).length > 0 && Object.keys(g.over.chord).every((i) => +i < 16), g.form);
  check("the seed field shows the new seed", +(await ev("document.querySelector('#compose-dock .cd-head input').value")) === g.seed, null);
  check("what it wrote is the model's generate (deterministic)", await ev(`(function(){ var b = ${gBefore}; return JSON.stringify(FsCompose.generate(b, { form: "AABA", contrast: ${g.form && g.form.contrast}, seed: ${g.seed}, nchords: 16 })) === JSON.stringify(__fa.routePatch(${R}).comp); })()`), null);
  check("the zones and sections stay", JSON.stringify(g.over.zone) === JSON.stringify(JSON.parse(gBefore).over.zone) && JSON.stringify(g.over.section) === JSON.stringify(JSON.parse(gBefore).over.section), null);
  check("the form row shows the letters over their chords", (await ev("[].map.call(document.querySelectorAll('#compose-dock .cd-form .cd-band'), function (b) { return b.textContent; }).join('|')")) === "A|A|B|A",
    await ev("[].map.call(document.querySelectorAll('#compose-dock .cd-form .cd-band'), function (b) { return b.textContent; })"));
  check("Generate keeps focus on its button", await ev("document.activeElement === document.getElementById('cd-generate')"), await ev("document.activeElement && document.activeElement.outerHTML.slice(0, 80)"));
  const gspill2 = await spillOf();
  check("generated, with the form row and the panel: one screen", gspill2.length === 0, gspill2.slice(0, 5));
  await shot("6-generate");
  await ev("document.body.focus(), document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(300);
  check("one Ctrl+Z takes the whole generation back", (await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)) === gBefore && !(await ev("!!document.querySelector('#compose-dock .cd-form')")), null);
  await ev("(function(){ var i = document.querySelector('#compose-dock .cd-head input'); i.value = 7; i.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400);
  check("the panel open, a typed seed is that seed's piece", await ev(`(function(){ var c = __fa.routePatch(${R}).comp; return c.seed === 7 && !!c.form && c.form.seed === 7 && JSON.stringify(c) === JSON.stringify(FsCompose.generate(${gBefore}, { form: "AABA", contrast: c.form.contrast, seed: 7, nchords: 16 })); })()`), null);
  await ev("document.body.focus(), document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(300);
  await ev("document.querySelector('#compose-dock .cd-gen').click(), 0"); await sleep(200);
  /* a zone on the route (made here, so the test never depends on the data): picked on the map, it is the dock's selection */
  const zid = await ev(`(function(){ var f = __fa.features().filter(function (g) { return g.properties.id === ${R}; })[0], c = f.geometry.coordinates[Math.floor(f.geometry.coordinates.length / 2)];
    return __fa.addPoint(c[0], c[1], "Compose test zone"); })()`);
  await ev(`fsListen.select(${JSON.stringify(zid)}), 0`); await sleep(500);
  check("a zone picked on the map selects it in the dock", await ev("/Zone · Compose test zone/.test(document.getElementById('cd-insp-title').textContent) && [].some.call(document.querySelectorAll('.cd-band.zone.on'), function (b) { return b.textContent === 'Compose test zone'; })"),
    await ev("[document.getElementById('cd-insp-title') && document.getElementById('cd-insp-title').textContent, [].map.call(document.querySelectorAll('.cd-band.zone'), function (b) { return b.textContent + (b.classList.contains('on') ? '*' : ''); })]"));
  check("the zone's ring is lit on the map", await ev("(function(){ var d = __fa.map.getSource('compsel').serialize().data; return d.features.length === 1 && d.features[0].geometry.type === 'Polygon'; })()"), null);
  check("a zone's inspector has when and blend", await ev("!!document.querySelector('#cd-insp .cd-row2[data-k=\"when\"]') && !!document.querySelector('#cd-insp .cd-row2[data-k=\"blend\"]')"), null);
  check("blend offers next bar, over 2 bars, over 4 bars (no 'at once': 0 and 1 are the same)", (await ev("[].map.call(document.querySelectorAll('#cd-insp .cd-row2[data-k=\"blend\"] button'), function (b) { return b.textContent; }).join('|')")) === "next bar|over 2 bars|over 4 bars", null);
  await ev("(function(){ var m = document.querySelector('#cd-insp .cd-row2[data-k=\"style\"] select'); m.value = 'euclid'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400);
  check("the zone's cells are tinted and outlined", await ev("document.querySelectorAll('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-cell.src-zone.lit').length > 0"), null);
  check("the zone's subline stays", await ev("(function(){ var p = document.querySelector('#cd-insp .cd-isub'); return !!p && p.offsetHeight > 0 && /voice 1/.test(p.textContent); })()"), await ev("(document.querySelector('#cd-insp .cd-isub') || {}).textContent"));
  const zspill = await spillOf();
  check("a zone with euclid fits one screen", zspill.length === 0, zspill.slice(0, 5));
  await shot("3-zone");
  /* a point's own panel: Compose is there, disabled, saying why */
  await ev("FsComposeDock.close(), 0");
  await ev("document.querySelector('#f-rhythm').click(), 0"); await sleep(800);
  check("a point's panel: Compose disabled, saying why", await ev("(function(){ var b = document.getElementById('rp-compose'), h = document.getElementById('rp-compose-hint'); return !!b && !b.hidden && b.disabled && !!h && h.offsetHeight > 0 && /Compose works on a route — select a route, then open Compose from its patch\./.test(h.textContent); })()"), null);
  await shot("5-point-panel");
  await ev("document.getElementById('rp-close').click(), 0"); await sleep(300);
  /* Esc: back to the route's patch panel, its Compose button focused */
  await ev(`fsListen.select(${R}), 0`); await sleep(400);
  await ev("__fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0"); await sleep(500);
  await ev("document.querySelector('#cd-lanes .cd-cell').focus(), 0");
  await key("Escape", "Escape");
  check("Esc closes the dock, back on the patch's Compose", await ev("!FsComposeDock.isOpen() && !document.getElementById('patchpanel').hidden && document.activeElement === document.getElementById('pp-compose')"), await ev("document.activeElement && document.activeElement.id"));
  /* the dock closed: its keys do nothing */
  await ev("document.getElementById('pp-close').click(), document.body.focus(), 0");
  await ev("fsListen.select(null), 0"); await sleep(300);     /* nothing selected: the page's own Delete has nothing to remove */
  const before = await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`), pp = await ev("window.__pp");
  const untouched = await ev("['Delete', ' ', 'ArrowRight', 'Enter'].every(function (k) { return document.body.dispatchEvent(new KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true })); }) && document.body.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true, cancelable: true }))");
  check("with the dock closed its keys do nothing", untouched && before === (await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)) && pp === (await ev("window.__pp")), null);
  /* 1024x700 with the Generate panel open and a generated piece: one screen */
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1024, height: 700, deviceScaleFactor: 1, mobile: false }); await sleep(600);
  await ev(`fsListen.select(${R}), __fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0`); await sleep(800);
  await ev("document.querySelector('#compose-dock .cd-gen').click(), 0"); await sleep(300);
  await ev("document.getElementById('cd-generate').click(), 0"); await sleep(500);
  const sspill = await spillOf();
  check("1024x700: the Generate panel and the form row fit one screen", sspill.length === 0 && !!(await ev("!!document.querySelector('#compose-dock .cd-form')")), sspill.slice(0, 5));
  await shot("7-generate-1024");
  await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), FsComposeDock.close(), 0"); await sleep(300);
  /* desk only: a phone's patch panel has no Compose */
  await s.send("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 2, mobile: true }); await sleep(600);
  await ev(`fsListen.select(${R}), __fa.openPatchPanel(), 0`); await sleep(600);
  check("phones: no Compose", await ev("!document.getElementById('pp-compose')"), null);
  check("no page errors", errors.length === 0, errors.slice(0, 3));
} finally { ch.kill(); }
console.log(failed ? failed + " FAILED" : "all passed"); process.exit(failed ? 1 : 0);
