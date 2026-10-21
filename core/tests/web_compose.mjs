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
  const VK = { Tab: 9, " ": 32, Delete: 46, Escape: 27, z: 90, Enter: 13, ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40 };
  const key = async (k, code, mods = 0) => { await s.send("Input.dispatchKeyEvent", { type: "keyDown", key: k, code, windowsVirtualKeyCode: VK[k], modifiers: mods, text: k === " " ? " " : k === "Enter" ? "\r" : undefined });
    await s.send("Input.dispatchKeyEvent", { type: "keyUp", key: k, code, windowsVirtualKeyCode: VK[k], modifiers: mods }); await sleep(300); };
  const spillOf = () => ev(`(function(){ var out = [];
    [["#compose-dock", "#compose-dock > *"], [".cd-main", ".cd-main > *, .cd-cell, .cd-chord"], ["#cd-insp", "#cd-insp > *, #cd-insp .cd-row2 > *"], [".cd-chips", ".cd-chips > *"], [".cd-ed", ".cd-ed *"]].forEach(function (k) {
      var c = document.querySelector(k[0]); if (!c) { if (k[0].indexOf(".cd-") !== 0 || k[0] === ".cd-main") { out.push(k[0] + " missing"); } return; } var r = c.getBoundingClientRect();
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
  check("the foot's audio readout: 'audio ok', slow blocks or gaps heard", !!au && (au.text === "audio ok" ? au.n === 0 : /^\d+ slow blocks? in the last minute$|^\d+ gaps? heard/.test(au.text) && au.n > 0) && /slowest block/.test(au.title), au);
  check("the readout polls while the dock is open (__fa.dropouts)", !!au && au.polls >= 1, au);
  /* final review M5: "gaps heard" counts the last minute (stubbed device count; Date.now shifted past the minute), and starts again with a new engine */
  const gapText = async () => { await sleep(1200); return ev("document.getElementById('cd-audio').textContent"); };
  await ev("window.__fa.audioStats = { underruns: 5 }, 0"); const g0 = await gapText();
  await ev("window.__fa.audioStats = { underruns: 7 }, 0"); const g1 = await gapText();
  await ev("window.__off = 61000, window.__dn = Date.now, Date.now = function () { return window.__dn.call(Date) + window.__off; }, window.__fa.audioStats = { underruns: 7 }, 0"); const g2 = await gapText();
  await ev("window.__fa.audioStats = { underruns: 1 }, 0"); const g3 = await gapText();
  await ev("Date.now = window.__dn, delete window.__fa.audioStats, 0");
  check("gaps heard: the last minute's, not the page's; a new engine starts again", /^2 gaps heard/.test(g1) && !/gap/.test(g2) && !/gap/.test(g3), [g0, g1, g2, g3]);
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
  /* composition 2: the header's bars switch - develop by default, repeat writes comp.bars, one undo step, the header holds it */
  const headFit = "(function(){ var h = document.querySelector('#compose-dock .cd-head'), r = h.getBoundingClientRect(); return h.scrollWidth <= h.clientWidth + 1 && [].every.call(h.children, function (c) { var q = c.getBoundingClientRect(); return !q.width || (q.right <= r.right + 1 && q.left >= r.left - 1); }); })()";
  check("the header's bars switch reads develop and fits the header at 1440", await ev("(function(){ var b = document.querySelectorAll('#compose-dock .cd-bars button'); return b.length === 2 && b[0].querySelector('.cd-w').textContent === 'develop' && b[0].getAttribute('aria-pressed') === 'true'; })()") && await ev(headFit), null);
  await ev("document.querySelectorAll('#compose-dock .cd-bars button')[1].click(), 0"); await sleep(400);
  check("repeat writes comp.bars", await ev(`__fa.routePatch(${R}).comp.bars === 'repeat'`), null);
  await ev("document.body.focus(), document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(400);
  check("one Ctrl+Z takes it back to develop", await ev(`__fa.routePatch(${R}).comp.bars === 'develop' && document.querySelectorAll('#compose-dock .cd-bars button')[0].getAttribute('aria-pressed') === 'true'`), null);
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
  check("what it wrote is the model's generate (deterministic)", await ev(`(function(){ var b = ${gBefore}; return JSON.stringify(FsCompose.generate(b, { form: "AABA", contrast: ${g.form && g.form.contrast}, seed: ${g.seed}, nchords: 16, bpm: __fa.routePatch(${R}).tempo || 72 })) === JSON.stringify(__fa.routePatch(${R}).comp); })()`), null);
  check("the zones and sections stay", JSON.stringify(g.over.zone) === JSON.stringify(JSON.parse(gBefore).over.zone) && JSON.stringify(g.over.section) === JSON.stringify(JSON.parse(gBefore).over.section), null);
  check("the form · chain row: the letters' steps over their chords", (await ev("(function(){ var seen = {}, out = []; [].forEach.call(document.querySelectorAll('#compose-dock .cd-form .cd-step'), function (b) { if (!seen[b.dataset.span]) { seen[b.dataset.span] = 1; out.push(b.dataset.letter); } }); return out.join('|'); })()")) === "A|A|B|A",
    await ev("(function(){ var seen = {}, out = []; [].forEach.call(document.querySelectorAll('#compose-dock .cd-form .cd-step'), function (b) { if (!seen[b.dataset.span]) { seen[b.dataset.span] = 1; out.push(b.dataset.letter); } }); return out.join('|'); })()"));
  check("Generate keeps focus on its button", await ev("document.activeElement === document.getElementById('cd-generate')"), await ev("document.activeElement && document.activeElement.outerHTML.slice(0, 80)"));
  check("the relations row: none · some · rich, 'some' on; Generate wrote form.relations and the row shows its chips", await ev(`(function(){ var c = __fa.routePatch(${R}).comp; return [].map.call(document.querySelectorAll('#cd-insp .cd-row2[data-k="relations"] button'), function (b) { return b.textContent; }).join('|') === 'none|some|rich' && c.form.relations === 'some' && c.relations.length === 8 && document.querySelectorAll('#cd-relrow .cd-rchip').length === c.relations.length; })()`), await ev(`JSON.stringify((__fa.routePatch(${R}).comp.form || {}).relations)`));
  await shot("6a-generate-some-relations");  const gspill2 = await spillOf();
  check("generated, with the form row and the panel: one screen", gspill2.length === 0, gspill2.slice(0, 5));
  /* fix round 1: a generated chain's chips all show, and + step, grown at 1440; the melody's cells take the form's letters */
  await ev("document.querySelector('#compose-dock .cd-icon[aria-label=\"Grow the dock\"]').click(), 0"); await sleep(600);
  const gchips = await spillOf();
  check("grown at 1440, a generated chain: every chip and + step in view", gchips.length === 0 && (await ev(`document.querySelectorAll('#compose-dock .cd-chip').length === __fa.routePatch(${R}).comp.chain.length && document.getElementById('cd-addstep').getBoundingClientRect().width > 0`)), gchips.slice(0, 5));
  check("the B section's melody cells read B (its step's letter)", await ev("[].some.call(document.querySelectorAll('#cd-lanes .cd-cell.mel span'), function (x) { return /^B /.test(x.textContent); })"), await ev("[].map.call(document.querySelectorAll('#cd-lanes .cd-cell.mel span'), function (x) { return x.textContent; }).join('|')"));
  await shot("6b-generate-grown");
  await ev("document.querySelector('#compose-dock .cd-icon[aria-label=\"Show the map\"]').click(), 0"); await sleep(600);
  await shot("6-generate");
  await ev("document.body.focus(), document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(300);
  check("one Ctrl+Z takes the whole generation back", (await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)) === gBefore && !(await ev("!!document.querySelector('#compose-dock .cd-form')")), null);
  await ev("(function(){ var i = document.querySelector('#compose-dock .cd-head input'); i.value = 7; i.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400);
  check("the panel open, a typed seed is that seed's piece", await ev(`(function(){ var c = __fa.routePatch(${R}).comp; return c.seed === 7 && !!c.form && c.form.seed === 7 && JSON.stringify(c) === JSON.stringify(FsCompose.generate(${gBefore}, { form: "AABA", contrast: c.form.contrast, seed: 7, nchords: 16, bpm: __fa.routePatch(${R}).tempo || 72 })); })()`), null);
  /* fix round 1 #8: a hand edit keeps the form, marked edited; a template clears the form with its chords */
  await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-cell[data-i=\"2\"]').click(), 0"); await sleep(300);
  await ev("(function(){ var m = document.querySelector('#cd-insp .cd-row2[data-k=\"rate\"] select'); m.value = m.value === '4n' ? '2n' : '4n'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400);
  check("a hand edit after Generate: the form row says edited, dimmed", await ev(`(function(){ var r = document.querySelector('#compose-dock .cd-formrow'); return !!__fa.routePatch(${R}).comp.form.edited && !!r && r.classList.contains('edited') && r.querySelector('.cd-name').textContent === 'form · edited' && /generated · edited/.test(r.title) && getComputedStyle(r.querySelector('.cd-step em')).opacity === '0.5'; })()`), null);
  await shot("8-edited");
  await ev("(function(){ var m = document.getElementById('cd-template'); m.value = 'ambient'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400);
  check("a template after Generate clears the form and its chords, keeps zones and sections", await ev(`(function(){ var c = __fa.routePatch(${R}).comp, b = ${gBefore}; return !c.form && JSON.stringify(c.over.chord) === '{}' && JSON.stringify(c.over.zone) === JSON.stringify(b.over.zone) && JSON.stringify(c.over.section) === JSON.stringify(b.over.section) && !document.querySelector('#compose-dock .cd-form'); })()`), null);
  await ev("document.body.focus(), 0");
  for (let i = 0; i < 3; i++) { await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(250); }
  check("Ctrl+Z back through the template, the edit and the typed seed", (await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)) === gBefore, null);
  /* composition 2 (task-9-brief.md): the melody voice, the motif roll, the chain, Show layers; every edit one undo step */
  const snaps = [await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)];
  const snap = async () => snaps.push(await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`));
  const C = async () => JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`));
  const setIn = (k, v) => ev(`(function(){ var m = document.querySelector('#cd-insp .cd-row2[data-k="${k}"] select, #cd-insp .cd-row2[data-k="${k}"] input'); m.value = '${v}'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0`);
  const vcell = (i) => `document.querySelector('#cd-lanes .cd-lane[data-voice="voice"] .cd-cell[data-i="${i}"]')`;
  const mcell = (r, c) => `document.querySelector('#cd-motif .cd-mcell[data-row="${r}"][data-col="${c}"]')`;
  const words = () => ev(`[0, 2, 3, 4].map(function (i) { return document.querySelector('#cd-lanes .cd-lane[data-voice="voice"] .cd-cell[data-i="' + i + '"] span').textContent; }).join('|')`);
  const btn = (scope, text) => `[].filter.call(document.querySelectorAll('${scope}'), function (b) { return b.textContent === '${text}'; })[0]`;
  const active = () => ev("(function(){ var a = document.activeElement; return a ? (a.dataset.fk || a.id || a.className || a.tagName) : null; })()");
  const insp = () => ev("document.getElementById('cd-insp').textContent");
  const mouse = (type, x, y) => s.send("Input.dispatchMouseEvent", { type, x, y, button: "left", buttons: type === "mouseReleased" ? 0 : 1, clickCount: 1 });
  const dragEnd = async (i, cols) => {   /* a note's end, dragged by cols sixteenths */
    const nr = await ev(`(function(){ var r = document.querySelector('#cd-motif .cd-mnote[data-i="${i}"]').getBoundingClientRect(), g = document.querySelector('#cd-motif .cd-mgrid').getBoundingClientRect(), L = +document.querySelector('#cd-motif .cd-mgrid').dataset.cols; return [r.right - 2, r.top + r.height / 2, (g.width + 2) / L]; })()`);
    await mouse("mousePressed", nr[0], nr[1]); for (let k = 1; k <= 4; k++) { await mouse("mouseMoved", nr[0] + nr[2] * cols * k / 4, nr[1]); await sleep(30); }
    await mouse("mouseReleased", nr[0] + nr[2] * cols, nr[1]); await sleep(400);
  };
  await ev("FsComposeDock.close(), __fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0"); await sleep(800);
  check("the role field reads ROLE, its menu melody / on / off, with its helper line", await ev(`(function(){ var r = document.querySelector('#cd-insp .cd-row2[data-k="role"]'); return !!r && r.querySelector('.cd-k').textContent === 'role' && [].map.call(r.querySelectorAll('option'), function (o) { return o.textContent; }).join('|') === 'melody|on|off'; })()`) &&
    (await insp()).indexOf("“melody” plays the motif; “on” plays the style; “off” is silent.") >= 0, await insp());
  await setIn("role", "melody"); await sleep(400); await snap();
  let c2 = await C();
  check("voice 1 melody: the role and a starting motif in one edit", c2.voice.role === "melody" && c2.motif && c2.motif.notes.length === 4, c2.motif);
  check("the melody's cells read A", (await ev(`${vcell(0)}.querySelector('span').textContent`)) === "A", await ev(`${vcell(0)}.textContent`));
  check("no motif roll in the normal dock", !(await ev("!!document.getElementById('cd-motif')")), null);
  await ev("document.querySelector('#compose-dock .cd-icon[aria-label=\"Grow the dock\"]').click(), 0"); await sleep(600);
  check("grown: the motif roll is there", await ev("(function(){ var m = document.getElementById('cd-motif'); return !!m && m.offsetHeight > 60 && m.querySelectorAll('.cd-mnote').length === 4; })()"), null);
  let sp = await spillOf();
  check("grown with the motif: one screen at 1440x900", sp.length === 0, sp.slice(0, 5));
  check("the roll is one tab stop (the cursor's cell; notes are reached through it)", await ev("[].filter.call(document.querySelectorAll('#cd-motif .cd-mcell, #cd-motif .cd-mnote'), function (b) { return b.tabIndex >= 0; }).length === 1"), null);
  await ev(`${btn("#compose-dock .cd-edh button", "2 bars")}.click(), 0`); await sleep(400); await snap();
  await ev(`${mcell(4, 16)}.click(), 0`); await sleep(400); await snap();
  c2 = await C(); const ni = c2.motif.notes.findIndex((x) => x.start === 16 && x.deg === 4);
  check("2 bars, then an empty roll cell in bar 2 adds a note", c2.motif.bars === 2 && c2.motif.notes.length === 5 && ni >= 0 && c2.motif.notes[ni].len === 4, c2.motif);
  await dragEnd(ni, 2); await snap();
  check("dragging a note's end lengthens it", (await C()).motif.notes[ni].len === 6, (await C()).motif.notes[ni]);
  /* final review M6: a cancelled drag (the pointer lost) ends the drag too - the handle hears its own pointer events */
  await ev(`(function(){ var nb = document.querySelector('#cd-motif .cd-mnote[data-i="${ni}"]'), r = nb.getBoundingClientRect(), g = document.querySelector('#cd-motif .cd-mgrid').getBoundingClientRect(), pc = (g.width + 2) / 32;
    var f = function (t, x) { nb.dispatchEvent(new PointerEvent(t, { bubbles: true, clientX: x, clientY: r.top + 3, pointerId: 77, button: 0 })); };
    f("pointerdown", r.right - 2); f("pointermove", r.right - 2 + pc); f("pointercancel", r.right - 2 + pc); })(), 0`); await sleep(400); await snap();
  check("a cancelled drag still ends it, the length kept", (await C()).motif.notes[ni].len === 7, (await C()).motif.notes[ni]);
  await ev(`${mcell(3, 24)}.click(), 0`); await sleep(400); await snap();
  await dragEnd(ni, 6); await snap();
  check("dragged over the next note, its end stops at that note", (await C()).motif.notes[ni].len === 8, (await C()).motif.notes[ni]);
  await ev(`${btn("#compose-dock .cd-edh button", "1 bar")}.click(), 0`); await sleep(400); await snap();
  c2 = await C();
  check("2 bars back to 1: bar 2's notes go, the rest fit the bar", c2.motif.bars === 1 && c2.motif.notes.length === 4 && c2.motif.notes.every((x) => x.start < 16 && x.start + x.len <= 16), c2.motif);
  await ev(`${btn("#cd-basis button", "scale")}.click(), 0`); await sleep(400); await snap();
  c2 = await C();
  check("the basis switch: scale degrees, the roll 1..7", c2.motif.basis === "scale" && (await ev("document.querySelectorAll('#cd-motif .cd-mrk').length")) === 7, c2.motif.basis);
  check("the scale roll's guide: the chord's own tones lit, the mode named", await ev("document.querySelectorAll('#cd-motif .cd-mrk.tone').length >= 3 && /[A-G]#? (aeolian|ionian)/.test(document.querySelector('#compose-dock .cd-edh h3').textContent)"), await ev("document.querySelector('#compose-dock .cd-edh h3').textContent"));
  await ev("(function(){ var m = document.getElementById('cd-addstep'); m.value = 'seq'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400); await snap();
  check("+ step adds a chain step, the inspector on it", (await C()).chain.length === 1 && /Chain step 1/.test(await ev("document.getElementById('cd-insp-title').textContent")), await ev("document.getElementById('cd-insp-title').textContent"));
  await setIn("arg", 2); await sleep(300); await snap();
  await setIn("from", 2); await sleep(300); await snap();
  await setIn("to", 3); await sleep(300); await snap();
  c2 = await C();
  check("the chain holds sequence +2 over chords 3-4", c2.chain.length === 1 && c2.chain[0].op === "seq" && c2.chain[0].arg === 2 && c2.chain[0].from === 2 && c2.chain[0].to === 3, c2.chain);
  check("those cells read A↑2, the others A", (await words()) === "A|A↑2|A↑2|A", await words());
  check("the step's inspector: what it sets here, with a reset", await ev(`(function(){ var r = document.querySelector('#cd-insp .cd-row2[data-k="arg"] .cd-src'); return !!r && /here/.test(r.textContent) && !!r.querySelector('.cd-reset'); })()`), null);
  /* final review 2: Delete on a lane cell while a step is selected clears nothing and throws nothing */
  const e0 = errors.length, k0 = await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`);
  await ev(`${vcell(0)}.focus(), 0`); await key("Delete", "Delete");
  check("Delete on a cell with a chain step selected: no error, nothing cleared", errors.length === e0 && k0 === (await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)), errors.slice(e0));
  await ev("document.querySelector('#cd-chain .cd-step[data-i=\"0\"]').click(), 0"); await sleep(300);
  check("the chain row shows the step over its chords", await ev("(function(){ var b = document.querySelector('#cd-chain .cd-step[data-i=\"0\"]'), a = document.querySelectorAll('#cd-strip .cd-chord'); if (!b) return false; var r = b.getBoundingClientRect(); return Math.abs(r.left - a[2].getBoundingClientRect().left) < 3 && Math.abs(r.right - a[3].getBoundingClientRect().right) < 4; })()"), null);
  /* a cadence: the op writes its type and the others; the preview names its last note from the key's scale (full: the tonic) */
  await ev("(function(){ var m = document.getElementById('cd-addstep'); m.value = 'repeat'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400); await snap();
  await setIn("op", "cadence"); await sleep(300); await snap();
  c2 = await C();
  check("the op set to cadence writes cad full and others thin", c2.chain.length === 2 && c2.chain[1].op === "cadence" && c2.chain[1].cad === "full" && c2.chain[1].others === "thin" && c2.chain[1].from === 4, c2.chain);
  const fit = await ev("(document.querySelector('#compose-dock .cd-fit') || {}).textContent || ''");
  const lab = (i) => ev(`document.querySelectorAll('#cd-strip .cd-chord')[${i}].title.replace(/^\\d+ · /, '')`);
  const seg0 = fit.split("  ·  ")[0], L0 = await lab(0), N = seg0.slice(("over " + L0 + " ").length).split(" ");
  const wantFit = `over ${L0} ${N.join(" ")}  ·  over ${await lab(1)} ${N.join(" ")}  ·  cadence · full over ${await lab(4)} ${N.slice(0, 3).join(" ")} ${N[0]}`;
  check("the preview: the motif over chords 1, 2 and the cadence's, its last note the tonic", N.length === 4 && N[3] === N[0] + "′" && fit === wantFit, [fit, wantFit]);
  check("the cadence's inspector says what each type ends on, wrapped", await ev("[].some.call(document.querySelectorAll('#cd-insp .cd-isub'), function (p) { return /full ends on the tonic, half on the 5th, deceptive on the 6th/.test(p.textContent) && p.scrollWidth <= p.clientWidth; })"), null);
  sp = await spillOf();
  check("grown with a chain step selected: one screen", sp.length === 0, sp.slice(0, 5));
  await shot("10-motif");
  /* the chips: Alt+arrow moves one (a keyboard reorder), a drag moves it back; each keeps its chords */
  await ev("document.querySelector('#compose-dock .cd-chip[data-i=\"0\"]').focus(), 0");
  await key("ArrowRight", "ArrowRight", 1); await snap();
  c2 = await C();
  check("Alt+→ on a chip moves it, its chords kept, focus on it", c2.chain[0].op === "cadence" && c2.chain[1].op === "seq" && c2.chain[1].from === 2 && c2.chain[1].to === 3 && c2.chain[0].from === 4 && (await active()) === "chip:1", [c2.chain, await active()]);
  await ev(`(function(){ var a = document.querySelector('#compose-dock .cd-chip[data-i="1"]'), b = document.querySelector('#compose-dock .cd-chip[data-i="0"]'), dt = new DataTransfer();
    a.dispatchEvent(new DragEvent('dragstart', { dataTransfer: dt, bubbles: true })); b.dispatchEvent(new DragEvent('dragover', { dataTransfer: dt, bubbles: true, cancelable: true })); b.dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true })); })(), 0`); await sleep(400); await snap();
  c2 = await C();
  check("a chip dragged back: the order back, from and to kept", c2.chain[0].op === "seq" && c2.chain[0].from === 2 && c2.chain[0].to === 3 && c2.chain[1].op === "cadence" && c2.chain[1].from === 4 && c2.chain[1].to === 4, c2.chain);
  /* the accent bars: arrows step a note's accent; Enter does not jump it to 100 */
  await ev("document.querySelector('#compose-dock .cd-mab').focus(), 0");
  const a0 = (await C()).motif.notes.filter((x) => x.start === 0)[0].acc;
  await key("Enter", "Enter");
  check("Enter on an accent bar leaves the accent", (await C()).motif.notes.filter((x) => x.start === 0)[0].acc === a0, (await C()).motif.notes);
  await key("ArrowUp", "ArrowUp"); await snap();
  check("↑ on an accent bar: +10", (await C()).motif.notes.filter((x) => x.start === 0)[0].acc === (a0 === undefined ? 70 : a0) + 10 && /mab/.test(await active()), [(await C()).motif.notes, await active()]);
  /* the roll's keyboard: arrows move, Enter adds and removes a note, Delete removes the selected note; focus stays on the cursor */
  await ev(`${mcell(6, 13)}.focus(), 0`);
  await key("ArrowRight", "ArrowRight");
  check("an arrow moves in the roll", await ev("document.activeElement.dataset.row === '6' && document.activeElement.dataset.col === '14'"), await ev("document.activeElement && document.activeElement.outerHTML.slice(0, 90)"));
  const n0 = (await C()).motif.notes.length;
  await key("Enter", "Enter"); await snap();
  check("Enter adds a note there, focus kept", (await C()).motif.notes.length === n0 + 1 && (await C()).motif.notes.some((x) => x.deg === 6 && x.start === 14) && (await active()) === "mc:6:14", [(await C()).motif.notes, await active()]);
  check("the cursor shows over the note it is under", await ev(`(function(){ var c = ${mcell(6, 14)}, r = c.getBoundingClientRect(); return document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2) === c; })()`), null);
  await key("Enter", "Enter"); await snap();
  check("Enter again removes it, focus kept", (await C()).motif.notes.length === n0 && (await active()) === "mc:6:14", await active());
  await key("Enter", "Enter"); await snap();
  await key("Delete", "Delete"); await snap();
  check("Delete removes the selected note, focus kept", (await C()).motif.notes.length === n0 && !(await C()).motif.notes.some((x) => x.deg === 6 && x.start === 14) && (await active()) === "mc:6:14", [(await C()).motif.notes, await active()]);
  /* the sentence inspector's remove: the note goes, focus on the roll's cursor */
  await ev("document.querySelector('#cd-motif .cd-mnote[data-i=\"0\"]').click(), 0"); await sleep(300);
  check("a note's inspector: its own values here, the rest the motif's", await ev(`(function(){ var a = document.querySelector('#cd-insp .cd-row2[data-k="deg"] .cd-src'), b = document.querySelector('#cd-insp .cd-row2[data-k="oct"] .cd-src'); return !!a && !!b && /here/.test(a.textContent) && b.textContent === 'motif'; })()`), null);
  await ev(`${btn("#cd-insp .cd-ieb button", "remove")}.focus(), ${btn("#cd-insp .cd-ieb button", "remove")}.click(), 0`); await sleep(300); await snap();
  check("the inspector's remove: the note goes, focus on the roll", (await C()).motif.notes.length === n0 - 1 && /^mc:/.test(await active()), await active());
  await ev(`${btn("#compose-dock .cd-edh button", "clear")}.focus(), ${btn("#compose-dock .cd-edh button", "clear")}.click(), 0`); await sleep(400); await snap();
  check("cleared: Draw a motif, or generate one; focus on the roll", (await C()).motif.notes.length === 0 && /Draw a motif, or generate one/.test(await ev("(document.querySelector('#compose-dock .cd-mempty') || {}).textContent || ''")) && /^mc:/.test(await active()), await active());
  sp = await spillOf();
  check("the empty motif state fits", sp.length === 0, sp.slice(0, 5));
  await ev(`${btn("#compose-dock .cd-mempty button", "Use a starter")}.focus(), ${btn("#compose-dock .cd-mempty button", "Use a starter")}.click(), 0`); await sleep(400); await snap();
  check("Use a starter: the default motif, the basis kept, focus on the roll", (await C()).motif.notes.length === 4 && (await C()).motif.basis === "scale" && /^mc:/.test(await active()), [(await C()).motif, await active()]);
  /* eight notes at most (the core's): a ninth says so */
  for (const c of [1, 5, 9, 13]) { await ev(`${mcell(6, c)}.click(), 0`); await sleep(250); await snap(); }
  const k8 = await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`);
  await ev(`${mcell(5, 2)}.click(), 0`); await sleep(300);
  check("a ninth note: nothing added, the roll says why", (await C()).motif.notes.length === 8 && k8 === (await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)) && /8 notes at most/.test(await ev("(document.querySelector('#compose-dock .cd-mcap') || {}).textContent || ''")), null);
  /* final review M3: scale -> chord folds degrees 5..6 (rows the chord's roll lacks) into the new rows, their octave carried */
  const six = (await C()).motif.notes.filter((x) => x.deg === 6).length;
  await ev(`${btn("#cd-basis button", "chord")}.click(), 0`); await sleep(400); await snap();
  c2 = await C();
  check("scale -> chord: every degree 0..4, octaves within +-2, the sixth folded to 2nd tone +1 octave", six === 4 && c2.motif.basis === "chord" && c2.motif.notes.every((x) => x.deg >= 0 && x.deg < 5 && Math.abs(x.oct || 0) <= 2) && c2.motif.notes.filter((x) => x.deg === 1 && x.oct === 1).length === six, c2.motif.notes);
  await ev(`${btn("#cd-basis button", "scale")}.click(), 0`); await sleep(400); await snap();
  /* final review M7: augment on a two-bar motif says it plays bar 1 */
  await ev(`${btn("#compose-dock .cd-edh button", "2 bars")}.click(), 0`); await sleep(400); await snap();
  await ev("(function(){ var m = document.getElementById('cd-addstep'); m.value = 'augment'; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0"); await sleep(400); await snap();
  check("augment on a 2-bar motif: the chip says bar 1", /augment · bar 1/.test(await ev("[].map.call(document.querySelectorAll('#cd-chain .cd-step'), function (b) { return b.title; }).join('|')")), await ev("[].map.call(document.querySelectorAll('#cd-chain .cd-step'), function (b) { return b.title; }).join('|')"));
  await ev("document.querySelector('#cd-chain .cd-step[data-i=\"2\"]').focus(), 0"); await key("Delete", "Delete"); await sleep(300); await snap();
  await ev(`${btn("#compose-dock .cd-edh button", "1 bar")}.click(), 0`); await sleep(400); await snap();
  /* deleting the last chain step: focus on the step before it */
  await ev("document.querySelector('#cd-chain .cd-step[data-i=\"1\"]').focus(), 0"); await key("Delete", "Delete"); await snap();
  check("Delete on the last chain step: it goes, focus on the one before", (await C()).chain.length === 1 && (await active()) === "step:0", await active());
  await ev("document.querySelector('#cd-chain .cd-step[data-i=\"0\"]').click(), 0"); await sleep(300);
  await ev(`${btn("#cd-insp .cd-ih button", "remove")}.focus(), ${btn("#cd-insp .cd-ih button", "remove")}.click(), 0`); await sleep(300); await snap();
  check("the inspector's remove on the only step: focus on + step", (await C()).chain.length === 0 && (await active()) === "addstep", await active());
  await ev("document.querySelector('#compose-dock .cd-icon[aria-label=\"Show the map\"]').click(), 0"); await sleep(600);
  /* the melody's chord inspector: what it plays, and its layers in one line (frame 2) */
  await ev(`${vcell(0)}.click(), 0`); await sleep(300);
  check("a melody chord's inspector: plays here and the layers line", /plays here/i.test(await insp()) && /zone — · chord — · section — · route melody/.test(await insp()), await insp());
  /* Show layers on voice 1: zone, chord, section, route; the winner lit */
  await ev(`${vcell(1)}.click(), 0`); await sleep(300);
  await setIn("role", "off"); await sleep(300); await snap();
  await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-layers').click(), 0"); await sleep(400);
  check("Show layers: four sub-rows, zone chord section route", (await ev("[].map.call(document.querySelectorAll('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-layer'), function (r) { return r.dataset.k; }).join('|')")) === "zone|chord|section|route", null);
  check("the chord row lit where the chord says (chord 2 off), the route's elsewhere", await ev("(function(){ var L = function (k, i) { return document.querySelector('#cd-lanes .cd-layer[data-k=\"' + k + '\"] [data-i=\"' + i + '\"]'); }; return L('chord', 1).classList.contains('win') && /off/.test(L('chord', 1).textContent) && !L('route', 1).classList.contains('win') && L('route', 0).classList.contains('win') && /melody/.test(L('route', 0).textContent); })()"), null);
  sp = await spillOf();
  check("layers open: one screen at 1440x900", sp.length === 0, sp.slice(0, 5));
  await shot("11-layers");
  await ev("document.body.focus(), 0");
  let undoOk = true, undoAt = -1;
  for (let i = snaps.length - 2; i >= 0; i--) {
    await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(200);
    if ((await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)) !== snaps[i]) { undoOk = false; undoAt = i; break; }
  }
  check("Ctrl+Z backs out every sentence edit one step at a time (" + (snaps.length - 1) + " edits)", undoOk, undoAt);
  await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-layers').click(), 0"); await sleep(200);
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
  /* composition 3, the relations (design/compose/relations-mock.html): the relations row, links on the lanes, "+ relation", the inspector */
  await ev(`FsComposeDock.close(), 0`); await sleep(300);
  await ev(`fsListen.select(${R}), __fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0`); await sleep(800);
  const rels = async () => JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${R}).comp.relations || [])`));
  const addRel = async (label) => { await ev("document.getElementById('cd-addrel').click(), 0"); await sleep(200);
    await ev(`${btn("#cd-relmenu button b", label)}.click(), 0`); await sleep(350); };
  const chip = (i) => `document.querySelector('#cd-relrow .cd-rchip[data-i="${i}"]')`;
  const rmBtn = () => `${btn("#cd-insp .cd-ih button", "remove")}`;
  const TYPES = [["canon", "canon"], ["call", "call & response"], ["hocket", "hocket"], ["hetero", "heterophony"], ["motion", "motion rule"], ["unison", "unison"], ["density", "shared density"]];
  check("no relations yet: the row says so and offers + relation", await ev("(function(){ var r = document.getElementById('cd-relrow'); return !!r && !!document.getElementById('cd-addrel') && /relations/.test(r.textContent) && document.querySelectorAll('#cd-relrow .cd-rchip').length === 0; })()"), await ev("(document.getElementById('cd-relrow') || {}).textContent"));
  await ev("document.getElementById('cd-addrel').click(), 0"); await sleep(200);
  const menuTxt = await ev("[].map.call(document.querySelectorAll('#cd-relmenu button'), function (b) { return b.textContent; })");
  check("+ relation lists the seven types, a name and a line each", menuTxt.length === 7 && TYPES.every((t, i) => menuTxt[i].indexOf(t[1]) === 0 && menuTxt[i].length > t[1].length + 8), menuTxt);
  await ev("document.getElementById('cd-addrel').click(), 0"); await sleep(200);
  let allDefaults = true, linkOk = true, chipOk = true;
  for (let k = 0; k < 7; k++) {
    await addRel(TYPES[k][1]);
    const rs = await rels(), r = rs[k] || {};
    const want = await ev(`(function(){ var o = { type: '${TYPES[k][0]}', a: 0, b: 1, from: 0, to: 63 }; FsCompose.RELATIONS['${TYPES[k][0]}'].fields.forEach(function (f) { o[f.k] = f.def; }); return o; })()`);
    if (rs.length !== k + 1 || JSON.stringify(Object.keys(want).sort().map((x) => [x, r[x]])) !== JSON.stringify(Object.keys(want).sort().map((x) => [x, want[x]]))) { allDefaults = false; console.log("  defaults differ", TYPES[k][0], JSON.stringify(r), JSON.stringify(want)); }
    if (await ev(`document.querySelectorAll('#cd-relrow .cd-rchip').length`) !== k + 1) { chipOk = false; }
    const hasLink = await ev(`!!document.querySelector('#cd-rels .cd-link[data-i="${k}"]')`);
    if (hasLink !== (TYPES[k][0] !== "density")) { linkOk = false; }
  }
  check("each of the seven types, added from the menu: patch.comp.relations holds it with its defaults", allDefaults, await rels());
  check("each has its chip, and a link on the lanes (density: the chip only)", chipOk && linkOk, [chipOk, linkOk]);
  check("a new relation is selected: its inspector opens", await ev("/Shared density/.test(document.getElementById('cd-insp-title').textContent) && !!document.querySelector('#cd-insp .cd-row2[data-k=\"cap\"]')"), await ev("document.getElementById('cd-insp-title').textContent"));
  const sp7 = await spillOf();
  check("seven relations at 1440x900: one screen", sp7.length === 0, sp7.slice(0, 5));
  /* the first chip (canon): inspector, span, link geometry */
  await ev(`${chip(0)}.click(), 0`); await sleep(300);
  check("selecting a chip opens its inspector (canon: leader, follower, delay, move by, transform, from, to, zone)", await ev("/Canon/.test(document.getElementById('cd-insp-title').textContent) && ['a','b','delay','trans','transform','from','to','zone'].every(function (k) { return !!document.querySelector('#cd-insp .cd-row2[data-k=\"' + k + '\"]'); }) && document.querySelector('#cd-relrow .cd-rchip.on').dataset.i === '0'"), await ev("[].map.call(document.querySelectorAll('#cd-insp .cd-row2'), function (r) { return r.dataset.k; })"));
  await pick("from", "2"); await sleep(300); await pick("to", "5"); await sleep(300);
  const c0 = (await rels())[0];
  check("from / to write the span (chords 3-6 are 2..5)", c0.from === 2 && c0.to === 5, c0);
  const geo = await ev(`(function(){ var g = document.querySelector('#cd-rels .cd-link[data-i="0"]').getBoundingClientRect(), L = function (v, i) { return document.querySelector('#cd-lanes .cd-lane[data-voice="' + v + '"] .cd-cell[data-i="' + i + '"]').getBoundingClientRect(); };
    var a0 = L('voice', 2), a1 = L('voice', 5), b0 = L('sect', 2);
    return { left: g.left - a0.left, right: a1.right - g.right, top: g.top - a0.bottom, bottom: b0.top - g.bottom }; })()`);
  check("the canon's link runs from voice 1's cells to voice 2's over chords 3-6", Math.abs(geo.left) < 8 && geo.right > -2 && geo.right < 14 && geo.top > -6 && geo.bottom > -10, geo);
  await ev("document.querySelector('#cd-rels .cd-link[data-i=\"2\"]').dispatchEvent(new MouseEvent('click', { bubbles: true })), 0"); await sleep(300);
  check("selecting a link opens its inspector (hocket: split)", await ev("/Hocket/.test(document.getElementById('cd-insp-title').textContent) && !!document.querySelector('#cd-insp .cd-row2[data-k=\"split\"]') && document.querySelector('#cd-relrow .cd-rchip.on').dataset.i === '2'"), await ev("document.getElementById('cd-insp-title').textContent"));
  check("a hocket is a dashed outline around both lanes", await ev("(function(){ var p = document.querySelector('#cd-rels .cd-link[data-i=\"2\"] .vis'); return !!p && !!p.getAttribute('stroke-dasharray'); })()"), null);
  /* editing writes the field */
  await ev(`${btn('#cd-insp .cd-row2[data-k="split"] button', "every 2")}.click(), 0`); await sleep(300);
  check("hocket split: every 2 writes split 'every'", (await rels())[2].split === "every", (await rels())[2]);
  await ev(`${btn('#cd-insp .cd-row2[data-k="split"] button', "pattern")}.click(), 0`); await sleep(300);
  const mk0 = (await rels())[2];
  await ev("document.querySelector('#cd-insp .cd-xm[data-i=\"1\"]').click(), 0"); await sleep(300);
  check("hocket split: pattern shows 16 steps; a click writes the mask", mk0.split === "mask" && (await ev("document.querySelectorAll('#cd-insp .cd-xm').length")) === 16 && (await rels())[2].mask === "xxx.x.x.x.x.x.x.", (await rels())[2].mask);
  const setRange = (k, v) => ev(`(function(){ var m = document.querySelector('#cd-insp .cd-row2[data-k="${k}"] input'); m.value = ${v}; m.dispatchEvent(new Event('change', { bubbles: true })); })(), 0`);
  await ev(`${chip(0)}.click(), 0`); await sleep(300);
  await setRange("delay", 4); await sleep(300); await setRange("trans", 2); await sleep(300); await pick("transform", "invert"); await sleep(300);
  const ce = (await rels())[0];
  check("canon: delay, move by and transform write their fields", ce.delay === 4 && ce.trans === 2 && ce.transform === "invert", ce);
  check("the chip reads it (canon · v1 → v2 · ¼ bar · +2 · invert)", await ev(`/canon · v1 → v2 · ¼ bar · \\+2 · invert/.test(${chip(0)}.textContent)`), await ev(`${chip(0)}.textContent`));
  await pick("b", "2"); await sleep(300);
  const bOk = (await rels())[0].b === 2;
  await pick("a", "2"); await sleep(300);
  check("follower menu writes b; a leader the same voice is refused", bOk && (await rels())[0].a === 0 && (await ev("document.querySelector('#cd-insp .cd-row2[data-k=\"a\"] select').value")) === "0", (await rels())[0]);
  await ev(`${chip(6)}.click(), 0`); await sleep(300);
  await setRange("cap", 12); await sleep(300); await setRange("w0", 3); await sleep(300);
  const cd = (await rels())[6];
  check("density: cap and a voice's weight write their fields; the share line reads", cd.cap === 12 && JSON.stringify(cd.weight) === "[3,1,1]" && /v1 7/.test(await insp()), [cd, await insp()]);
  /* zone-limited: its band lights */
  await ev(`${chip(0)}.click(), 0`); await sleep(300);
  await pick("zone", zid); await sleep(300);
  check("a zone-limited relation: its zone written, its zone band lit, the chip names the zone", (await rels())[0].zone === zid && await ev(`[].some.call(document.querySelectorAll('.cd-band.zone.on'), function (b) { return b.textContent === 'Compose test zone'; }) && /Compose test zone/.test(${chip(0)}.textContent)`), (await rels())[0]);
  /* a voice that is off: the relation says so */
  await pick("zone", ""); await sleep(300);
  await ev(`document.querySelector('#cd-lanes .cd-lane[data-voice="v3"] .cd-cell[data-i="3"]').click(), 0`); await sleep(300);
  await setIn("role", "off"); await sleep(300);
  await ev(`${chip(0)}.click(), 0`); await sleep(300);
  check("a voice that is off on the relation's chords: the inspector says so", /voice 3 is off/.test(await insp()), await insp());
  await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(300);
  /* remove: one undo step */
  await ev(`${chip(0)}.click(), 0`); await sleep(300);
  const before7 = await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`);
  await ev(`${rmBtn()}.click(), 0`); await sleep(300);
  const after6 = await rels();
  await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(300);
  check("remove: the relation goes; one Ctrl+Z brings it back, exactly", after6.length === 6 && (await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`)) === before7, after6.length);
  /* down to three links: 1440x900 and its screenshot */
  for (let k = 0; k < 4; k++) { await ev(`(function(){ var c = document.querySelectorAll('#cd-relrow .cd-rchip'); c[c.length - 1].click(); })(), 0`); await sleep(200);
    await ev(`${rmBtn()}.click(), 0`); await sleep(250); }
  await ev(`${chip(0)}.click(), 0`); await sleep(300);
  check("three relations left, three chips, three links", (await rels()).length === 3 && (await ev("document.querySelectorAll('#cd-relrow .cd-rchip').length")) === 3 && (await ev("document.querySelectorAll('#cd-rels .cd-link').length")) === 3, (await rels()).length);
  check("the driven voice's cells read canon over the span and no further", await ev("(function(){ var w = function (i) { return document.querySelector('#cd-lanes .cd-lane[data-voice=\"v3\"] .cd-cell[data-i=\"' + i + '\"]'); }; return [2,3,4,5].every(function (i) { return w(i).classList.contains('relx') && /canon/i.test(w(i).textContent); }) && !w(1).classList.contains('relx') && !w(6).classList.contains('relx'); })()"), null);
  const sp3 = await spillOf();
  check("three links at 1440x900: no scroll, no spill", sp3.length === 0, sp3.slice(0, 5));
  check("1440: the foot's keys read in full", await ev("(function(){ var k = document.querySelector('#compose-dock .cd-keys'); return !!k && k.scrollWidth <= k.clientWidth + 1; })()"), await ev("(function(){ var k = document.querySelector('#compose-dock .cd-keys'); return [k.scrollWidth, k.clientWidth]; })()"));
  await shot("14-relations");
  /* the keyboard: Tab reaches the chips, Enter opens, Delete removes - only on chips and links */
  await ev("document.querySelector('#cd-strip .cd-chord').focus(), 0");
  let tabbed = false;
  for (let i = 0; i < 40 && !tabbed; i++) { await key("Tab", "Tab"); tabbed = await ev("!!document.activeElement.closest('.cd-rchip')"); }
  check("Tab reaches a relation chip", tabbed, await active());
  await ev("document.body.focus(), 0");
  const n3 = (await rels()).length;
  await ev("document.getElementById('cd-addrel').focus(), 0"); await key("Delete", "Delete");
  await ev("document.querySelector('#cd-insp select, #cd-insp input').focus(), 0"); await key("Delete", "Delete");
  check("Delete on + relation and on an inspector control removes nothing", (await rels()).length === n3, (await rels()).length);
  await ev(`${chip(1)}.focus(), 0`); await key("Enter", "Enter");
  check("Enter on a chip opens its inspector, focus goes into it", await ev("/Call/.test(document.getElementById('cd-insp-title').textContent) && !!document.activeElement.closest('#cd-insp')"), await ev("document.getElementById('cd-insp-title').textContent"));
  await ev("document.querySelector('#cd-rels .cd-link[data-i=\"0\"]').focus(), 0"); await key("Enter", "Enter");
  check("Enter on a link opens its inspector", await ev("/Canon/.test(document.getElementById('cd-insp-title').textContent)"), await ev("document.getElementById('cd-insp-title').textContent"));
  await ev("document.querySelector('#cd-rels .cd-link[data-i=\"0\"]').focus(), 0"); await key("Delete", "Delete");
  check("Delete on a link removes its relation, focus stays on the row", (await rels()).length === n3 - 1 && await ev("!!document.activeElement.closest('#cd-relrow, #cd-rels')"), await active());
  await ev("document.querySelector('#cd-relrow .cd-rchip').focus(), 0"); await key("Delete", "Delete");
  check("Delete on a chip removes its relation", (await rels()).length === n3 - 2, (await rels()).length);
  await ev(`FsComposeDock.close(), 0`);
  /* 1024x700 with three links, one selected */
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1024, height: 700, deviceScaleFactor: 1, mobile: false }); await sleep(600);
  await ev(`fsListen.select(${R}), __fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0`); await sleep(800);
  for (const l of ["canon", "hocket", "call & response"]) { await addRel(l); }
  check("1024x700: the relations are there with their links", (await rels()).length === 4 && (await ev("document.querySelectorAll('#cd-rels .cd-link').length")) === 4, (await rels()).length);
  const sp1024 = await spillOf();
  check("three links at 1024x700: no scroll, no spill", sp1024.length === 0, sp1024.slice(0, 5));
  await ev(`${chip(0)}.click(), 0`); await sleep(300);
  const sp1024b = await spillOf();
  check("1024x700 a relation selected: one screen", sp1024b.length === 0, sp1024b.slice(0, 5));
  await shot("15-relations-1024");
  await ev("document.getElementById('cd-addrel').click(), 0"); await sleep(200);
  check("the + relation menu stays inside the dock", await ev("(function(){ var m = document.getElementById('cd-relmenu').getBoundingClientRect(), d = document.querySelector('.cd-main').getBoundingClientRect(); return m.bottom <= d.bottom + 1 && m.right <= d.right + 1 && m.left >= d.left; })()"), await ev("(function(){ var m = document.getElementById('cd-relmenu').getBoundingClientRect(), d = document.querySelector('.cd-main').getBoundingClientRect(); return [m.bottom, d.bottom, m.right, d.right]; })()"));
  await key("Escape", "Escape");
  check("Esc closes the menu first, the dock stays", await ev("!document.getElementById('cd-relmenu') && FsComposeDock.isOpen()"), null);
  for (let k = 0; k < 4; k++) { await ev(`(function(){ var c = document.querySelectorAll('#cd-relrow .cd-rchip'); c[c.length - 1].click(); })(), 0`); await sleep(200); await ev(`${rmBtn()}.click(), 0`); await sleep(250); }
  check("all removed: the row says no relations yet", (await rels()).length === 0 && await ev("document.querySelectorAll('#cd-relrow .cd-rchip').length === 0"), null);
  /* final review: relations on Free voices, the default leader, a second density, grown row, arrows, the Generate note */
  const reopen = async (edit) => { await ev(`FsComposeDock.close(), 0`); await sleep(300);
    await ev(`__fa.patchEdit(${R}, function (p) { if (!window.__compSave) { window.__compSave = p.comp; } p.comp = FsCompose.defaults(); (${edit})(p.comp); }), 0`); await sleep(300);
    await ev(`fsListen.select(${R}), __fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0`); await sleep(800); };
  const undoKey = async () => { await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), 0"); await sleep(300); };
  await reopen("function (c) { c.sect.style = 'arpeggio'; }");
  await addRel("heterophony");
  const dl = (await rels())[0] || {};
  check("a new pair relation's leader is the first voice that plays something (voice 2 here), the follower another", dl.a === 1 && dl.b === 0, dl);
  await ev(`${rmBtn()}.click(), 0`); await sleep(300);
  await reopen("function (c) { }");
  await addRel("canon");
  check("a canon from a Free voice: the chip is dimmed", await ev(`${chip(0)}.classList.contains('dim')`), null);
  check("a canon from a Free voice: the driven voice's cells are not tagged", await ev("document.querySelectorAll('#cd-lanes .cd-cell.relx').length === 0"), null);
  check("a canon from a Free voice: the inspector says so, with a one-click fix", /voice 1 is Free, so there is nothing to follow/.test(await insp()) && await ev(`!!${btn("#cd-insp button", "give voice 1 a style")}`), await insp());
  await ev(`${btn("#cd-insp button", "give voice 1 a style")}.click(), 0`); await sleep(400);
  check("the fix gives the voice an arpeggio, the chip lights, the cells read canon", (await ev(`__fa.routePatch(${R}).comp.voice.style`)) === "arpeggio" && await ev(`!${chip(0)}.classList.contains('dim')`) && await ev("document.querySelectorAll('#cd-lanes .cd-lane[data-voice=\"sect\"] .cd-cell.relx').length > 0"), await ev(`__fa.routePatch(${R}).comp.voice.style`));
  await undoKey();
  check("one Ctrl+Z takes the style back (Free again, dimmed)", (await ev(`__fa.routePatch(${R}).comp.voice.style`)) === "free" && await ev(`${chip(0)}.classList.contains('dim')`), await ev(`__fa.routePatch(${R}).comp.voice.style`));
  /* a density-weighted voice that is Free: dimmed too, any member */
  await ev(`${rmBtn()}.click(), 0`); await sleep(300);
  await addRel("shared density");
  check("a shared density with Free voices: dimmed", await ev(`${chip(0)}.classList.contains('dim')`), null);
  await ev("document.getElementById('cd-addrel').click(), 0"); await sleep(200);
  check("a second shared density cannot be added: its menu item is disabled, saying why", await ev(`(function(){ var b = [].filter.call(document.querySelectorAll('#cd-relmenu button'), function (x) { return /shared density/.test(x.textContent); })[0]; return b.disabled && /already/.test(b.title); })()`), null);
  await ev("document.getElementById('cd-addrel').click(), 0"); await sleep(200);
  await ev(`${rmBtn()}.click(), 0`); await sleep(300);
  /* the grown dock keeps the relations row */
  await addRel("canon"); await addRel("hocket");
  for (const [w, h] of [[1440, 900], [1024, 700]]) {
    await s.send("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor: 1, mobile: false }); await sleep(700);
    for (const gr of [false, true]) {
      const isG = await ev("document.body.classList.contains('composing-grown')");
      if (isG !== gr) { await ev("document.querySelector('#compose-dock .cd-icon[aria-label=\"" + (gr ? "Grow the dock" : "Show the map") + "\"]').click(), 0"); await sleep(600); }
      const spx = await spillOf();
      check(`${w}x${h} ${gr ? "grown" : "normal"}: the relations row and both links are there, one screen`, spx.length === 0 && await ev("document.querySelectorAll('#cd-relrow .cd-rchip').length === 2 && document.querySelectorAll('#cd-rels .cd-link').length === 2"), spx.slice(0, 5));
      if (gr && w === 1440) { await shot("16-relations-grown"); }
    }
  }
  await ev("document.querySelector('#compose-dock .cd-icon[aria-label=\"Show the map\"]').click(), 0"); await sleep(600);
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await sleep(700);
  /* arrows move between chips, and between links */
  await ev(`${chip(0)}.focus(), 0`); await key("ArrowRight", "ArrowRight");
  const ar1 = await active(); await key("ArrowRight", "ArrowRight"); const ar2 = await active(); await key("ArrowLeft", "ArrowLeft"); const ar3 = await active();
  check("arrows move between relation chips (clamped at the ends)", ar1 === "rel:1" && ar2 === "rel:1" && ar3 === "rel:0", [ar1, ar2, ar3]);
  await ev("document.querySelector('#cd-rels .cd-link[data-i=\"0\"]').focus(), 0"); await key("ArrowRight", "ArrowRight");
  const al1 = await active(); await key("ArrowLeft", "ArrowLeft"); const al2 = await active();
  check("arrows move between links", al1 === "link:1" && al2 === "link:0", [al1, al2]);
  /* Generate says what it replaces */
  await ev("document.querySelector('#compose-dock .cd-gen').click(), 0"); await sleep(300);
  check("the Generate panel says it replaces the motif, chain and relations", await ev("[].some.call(document.querySelectorAll('#cd-insp .cd-isub'), function (p) { return /replaces the motif, chain and relations · Ctrl.Z undoes/.test(p.textContent); })"), await ev("[].map.call(document.querySelectorAll('#cd-insp .cd-isub'), function (p) { return p.textContent; })"));
  await ev(`FsComposeDock.close(), 0`); await sleep(300);
  await ev(`__fa.patchEdit(${R}, function (p) { p.comp = window.__compSave; window.__compSave = null; }), 0`); await sleep(300);
  await ev(`FsComposeDock.close(), 0`); await sleep(300);
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await sleep(600);
  await ev(`fsListen.select(${R}), __fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0`); await sleep(800);
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
  await ev("[].filter.call(document.querySelectorAll('#cd-insp .cd-row2[data-k=\"relations\"] button'), function (b) { return b.textContent === 'rich'; })[0].click(), 0"); await sleep(300);
  await ev("document.getElementById('cd-generate').click(), 0"); await sleep(500);
  const rspill = await spillOf();
  check("1024x700: the relations switch to rich writes form.relations, the row shows every chip, one screen", rspill.length === 0 && await ev(`(function(){ var c = __fa.routePatch(${R}).comp; return c.form.relations === 'rich' && c.relations.filter(function (r) { return r.type === 'density'; }).length === 1 && document.querySelectorAll('#cd-relrow .cd-rchip').length === c.relations.length; })()`), rspill.slice(0, 5));
  await shot("7b-generate-rich-1024");
  await ev("[].filter.call(document.querySelectorAll('#cd-insp .cd-row2[data-k=\"relations\"] button'), function (b) { return b.textContent === 'none'; })[0].click(), 0"); await sleep(300);
  await ev("document.getElementById('cd-generate').click(), 0"); await sleep(500);
  check("none writes no relations and the row has no chips", await ev(`(function(){ var c = __fa.routePatch(${R}).comp; return c.form.relations === 'none' && !(c.relations || []).length && document.querySelectorAll('#cd-relrow .cd-rchip').length === 0; })()`), null);  await shot("7-generate-1024");
  check("1024: the bars switch fits the header", await ev("(function(){ var h = document.querySelector('#compose-dock .cd-head'), r = h.getBoundingClientRect(); return !!h.querySelector('.cd-bars') && h.scrollWidth <= h.clientWidth + 1 && [].every.call(h.children, function (c) { var q = c.getBoundingClientRect(); return !q.width || (q.right <= r.right + 1 && q.left >= r.left - 1); }); })()"), await ev("(function(){ var h = document.querySelector('#compose-dock .cd-head'), r = h.getBoundingClientRect(); return [].map.call(h.children, function (c) { var q = c.getBoundingClientRect(); return [c.className, Math.round(q.left - r.left), Math.round(q.right - r.right), Math.round(q.width)]; }); })()"));
  check("Generate wrote bars: develop", await ev(`__fa.routePatch(${R}).comp.bars === 'develop'`), null);
  await ev("document.querySelector('#compose-dock .cd-icon[aria-label=\"Grow the dock\"]').click(), 0"); await sleep(600);
  await ev("(function(){ var b = document.querySelectorAll('#cd-chain .cd-step'); b[b.length - 1].click(); })(), 0"); await sleep(400);
  check("1024: a one-chord cadence step reads cad. in full", await ev("[].every.call(document.querySelectorAll('#cd-chain .cd-step.cad'), function (b) { return b.textContent === 'cad.' && b.scrollWidth <= b.clientWidth; })"), await ev("[].map.call(document.querySelectorAll('#cd-chain .cd-step.cad'), function (b) { return [b.textContent, b.scrollWidth, b.clientWidth]; })"));
  check("1024: the cadence's sub-line wraps", await ev("[].some.call(document.querySelectorAll('#cd-insp .cd-isub'), function (p) { return /deceptive on the 6th/.test(p.textContent) && p.scrollWidth <= p.clientWidth; })"), null);
  check("a cadence step's inspector: type, others, strength", await ev("!!document.querySelector('#cd-insp .cd-row2[data-k=\"cad\"]') && !!document.querySelector('#cd-insp .cd-row2[data-k=\"others\"]') && !!document.querySelector('#cd-insp .cd-row2[data-k=\"strength\"]')"), null);
  const gr1024 = await spillOf();
  check("1024x700 grown, a generated motif and chain: one screen", gr1024.length === 0 && !!(await ev("!!document.getElementById('cd-motif')")), gr1024.slice(0, 5));
  await shot("12-grown-1024");
  await ev("document.querySelector('#compose-dock .cd-icon[aria-label=\"Show the map\"]').click(), 0"); await sleep(600);
  await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-layers').click(), 0"); await sleep(400);
  const ly1024 = await spillOf();
  check("1024x700 layers open: one screen", ly1024.length === 0 && (await ev("document.querySelectorAll('#cd-lanes .cd-layer').length")) === 4, ly1024.slice(0, 5));
  await shot("13-layers-1024");
  await ev("document.querySelector('#cd-lanes .cd-lane[data-voice=\"voice\"] .cd-layers').click(), 0"); await sleep(200);
  await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), FsComposeDock.close(), 0"); await sleep(300);
  /* 24 chords: the form spans all 24 (the core reads 64); past 64 the panel says so */
  await ev(`__fa.patchEdit(${R}, function (p) { window.__prog24 = p.prog; p.prog = p.prog.concat(p.prog.slice(0, 8)); }), 0`); await sleep(300);
  await ev(`__fa.openPatchPanel(), document.getElementById('pp-compose').click(), 0`); await sleep(800);
  await ev("document.querySelector('#compose-dock .cd-gen').click(), 0"); await sleep(300);
  check("24 chords: the panel has no 'form over chords 1-16' note", await ev("![].some.call(document.querySelectorAll('#cd-insp .cd-isub'), function (p) { return /form over chords/.test(p.textContent); })"),
    await ev("[].map.call(document.querySelectorAll('#cd-insp .cd-isub'), function (p) { return p.textContent; })"));
  await ev("document.getElementById('cd-generate').click(), 0"); await sleep(500);
  const g24 = JSON.parse(await ev(`JSON.stringify(__fa.routePatch(${R}).comp)`));
  check("24 chords: overrides reach past chord 16, none past 24, the form row ends at chord 24", Object.keys(g24.over.chord).some((i) => +i >= 16) && Object.keys(g24.over.chord).every((i) => +i < 24) && await ev("(function(){ var b = document.querySelectorAll('#compose-dock .cd-form .cd-step'), c = document.querySelectorAll('#cd-strip .cd-chord')[23]; return new Set([].map.call(b, function (x) { return x.dataset.span; })).size === 4 && b[b.length - 1].getBoundingClientRect().right <= c.getBoundingClientRect().right + 2; })()"), Object.keys(g24.over.chord));
  const s24 = await spillOf();
  check("24 chords at 1024x700: one screen", s24.length === 0, s24.slice(0, 5));
  await shot("9-24-chords-1024");
  await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, bubbles: true })), FsComposeDock.close(), 0"); await sleep(300);
  await ev(`__fa.patchEdit(${R}, function (p) { p.prog = window.__prog24; }), 0`); await sleep(300);
  /* desk only: a phone's patch panel has no Compose */
  await s.send("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 2, mobile: true }); await sleep(600);
  await ev(`fsListen.select(${R}), __fa.openPatchPanel(), 0`); await sleep(600);
  check("phones: no Compose", await ev("!document.getElementById('pp-compose')"), null);
  check("no page errors", errors.length === 0, errors.slice(0, 3));
} finally { ch.kill(); }
console.log(failed ? failed + " FAILED" : "all passed"); process.exit(failed ? 1 : 0);
