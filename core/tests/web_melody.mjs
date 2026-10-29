/* Melody, the fourth point device (Kerem, 2026-10-29): a draft point's recording cut into slices at its onsets, each heard at
   its pitch; the panel's line edited; the point heard on the walk; the panel on one screen, desk and phone. Nothing is published.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_melody.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9268", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-melody-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
/* 1.5 s of a decaying click train as a WAV file, built in the page */
const WAV0 = `(function(name){ var sr = 22050, n = sr * 1.5, b = new ArrayBuffer(44 + n * 2), v = new DataView(b);
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
  for (var i = 0; i < n; i++) v.setInt16(44 + i * 2, (Math.random() * 2 - 1) * 12000 * Math.exp(-(i % 4000) / 800), true);
  return new File([b], name, { type: "audio/wav" }); })`;
const WAV = `(function(name){ var sr = 22050, n = sr * 2, b = new ArrayBuffer(44 + n * 2), v = new DataView(b), hz = [220, 261.6, 329.6, 0, 220, 293.7, 196, 0];
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
  for (var i = 0; i < n; i++) { var k = Math.floor(i / (sr / 4)), t = (i % (sr / 4)) / sr, f = hz[k], e = Math.exp(-t * 9);
    var x = f ? Math.sin(2 * Math.PI * f * i / sr) : (Math.random() * 2 - 1); v.setInt16(44 + i * 2, x * e * 14000, true); }
  return new File([b], name, { type: "audio/wav" }); })`;
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9268))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  await s.send("Runtime.enable"); const logs = []; s.on((msg) => { if (msg.method === "Runtime.consoleAPICalled") logs.push(msg.params.args.map((a) => a.value || a.description).join(" ")); }); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
  await sleep(3000);
  await ev("fsListen._gate(true), 0"); await sleep(800);
  const place = async (lon, lat) => {
    await ev(`__fa.map.jumpTo({ center: [${lon}, ${lat}], zoom: 17 }), 0`); await sleep(1200);
    await ev("document.querySelector(\"#ls-tools [data-mode='point']\").click(), 0"); await sleep(400);
    const c = JSON.parse(await ev("JSON.stringify((function(){ var b = __fa.map.getContainer().getBoundingClientRect(), p = __fa.map.project(__fa.map.getCenter()); return { x: b.x + p.x, y: b.y + p.y }; })())"));
    await s.send("Input.dispatchMouseEvent", { type: "mousePressed", x: c.x, y: c.y, button: "left", clickCount: 1 });
    await s.send("Input.dispatchMouseEvent", { type: "mouseReleased", x: c.x, y: c.y, button: "left", clickCount: 1 });
    await sleep(1200);
    return ev("fsListen.selected()");
  };
  const at = async (id) => JSON.parse(await ev(`JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(id)}; })[0].geometry.coordinates)`));
  const walkHear = async ([lon, lat]) => {
    let best = -120;
    for (let i = 0; i < 30 && best < -50; i++) { await ev(`__fa.walkTo(${lon + i * 1e-7}, ${lat})`); await sleep(400); best = Math.max(best, await level()); }
    return best;
  };

  const level = () => ev("__fa.coreLevel ? __fa.coreLevel() : -120");
  const hear = async ([lon, lat]) => {
    let e = 0; for (let i = 0; i < 8; i++) { await ev(`__fa.walkTo(${lon + i * 1e-7}, ${lat})`); await sleep(250); e += Math.pow(10, (await level()) / 10); }
    return 10 * Math.log10(e / 8 + 1e-30);
  };
  /* Melody (Kerem, 2026-10-29): a draft point, a recording of eight notes 0.25 s apart, two of them noise */
  const P = [29.09, 41.05];
  const id = await place(P[0], P[1]);
  await ev(`(function(){ var dt = new DataTransfer(); dt.items.add(${WAV}("m.wav")); var inp = document.getElementById("rec-file"); inp.files = dt.files; inp.dispatchEvent(new Event("change")); })()`);
  await sleep(2500);
  check("the card offers Melody", await ev("!!document.querySelector(\".recmode [data-amode='melody']\")"), null);
  await ev("document.querySelector(\".recmode [data-amode='melody']\").click(), 0"); await sleep(600);
  await ev("document.querySelector('#ls-card #f-rhythm').click(), 0");
  let m = null;
  for (let i = 0; i < 30; i++) { await sleep(500); m = JSON.parse(await ev(`JSON.stringify((__fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(id)}; })[0].properties.rhythm || {}).melody || null)`)); if (m && m.slices && m.slices.length) break; }
  console.log("slices", JSON.stringify(m && m.slices));
  check("its slices are found at the onsets (8, 0.25 s apart)", m && m.slices.length === 8 && m.slices.every((s, i) => Math.abs(s[0] - i * 0.25) < 0.03), m && m.slices);
  const hz = m ? m.slices.map((s) => s[2]) : [];
  const near = (a, b) => a > 0 && Math.abs(1200 * Math.log2(a / b)) < 40;
  check("each pitched slice is heard at its note (A3 C4 E4 · A3 D4 G3)", near(hz[0], 220) && near(hz[1], 261.6) && near(hz[2], 329.6) && near(hz[4], 220) && near(hz[5], 293.7) && near(hz[6], 196), hz);
  check("the noise slices are unpitched", hz[3] === 0 && hz[7] === 0, hz);
  await sleep(800);
  const ui = JSON.parse(await ev(`JSON.stringify({ title: document.getElementById("rp-title").textContent, cells: document.querySelectorAll("#rp-body .melgrid .mc").length,
    lit: document.querySelectorAll("#rp-body .melgrid .mc.on").length, rows: document.querySelectorAll("#rp-body .melsl").length, scroll: document.documentElement.scrollHeight - innerHeight })`));
  check("the panel: Melody, 5 x 16 cells, a line already in it, 8 slices listed", /Melody/.test(ui.title) && ui.cells === 80 && ui.lit >= 6 && ui.rows === 8, ui);
  check("it fits one screen", ui.scroll === 0, ui);
  /* a cell placed, a lit one opened */
  await ev("document.querySelectorAll('#rp-body .melgrid .mc')[1 * 16 + 1].click(), 0"); await sleep(400);
  const st1 = JSON.parse(await ev(`JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(id)}; })[0].properties.rhythm.melody.steps[1])`));
  check("clicking a cell puts that step's note on that degree (the 7th)", st1[0] === 3, st1);
  await ev("document.querySelectorAll('#rp-body .melgrid .mc')[1 * 16 + 1].click(), 0"); await sleep(400);
  check("clicking a lit cell opens its slice / octave / accent", await ev("!document.querySelector('#rp-body .melpop').hidden && document.querySelectorAll('#rp-body .melpop .melseg').length === 3"), null);
  await ev("[].filter.call(document.querySelectorAll('#rp-body .melpop button'), function (b) { return b.textContent === '+1 oct'; })[0].click(), 0"); await sleep(300);
  check("the octave is set", await ev(`__fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(id)}; })[0].properties.rhythm.melody.steps[1][2]`) === 1, null);
  await ev("document.querySelector('#rp-close, #rhythmpanel .ppclose') && document.querySelector('#rp-close, #rhythmpanel .ppclose').click(), 0"); await sleep(300);
  await ev("document.querySelector(\"#ls-tools [data-mode='select']\").click(), 0"); await sleep(300);
  await ev("fsListen.select(null), 0"); await sleep(300);
  await ev("__fa.walkTo(29.2, 41.2)"); await sleep(300);
  await ev("document.querySelector('#ls-sound').click()"); await sleep(2500);
  const Q = await at(id);   /* where it was placed */
  let best = -120; for (let i = 0; i < 30 && best < -45; i++) { await ev(`__fa.walkTo(${Q[0] + i * 1e-7}, ${Q[1]})`); await sleep(400); best = Math.max(best, await level()); }
  const lv = await hear(Q);
  check("walked into, the melody point sounds", lv > -50, [best, lv]);
  /* a phone: the Line tab fits */
  await s.send("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 2, mobile: true }); await sleep(1200);
  await ev(`fsListen.select(${JSON.stringify(id)}), 0`); await sleep(900);
  await ev("document.querySelector('#ls-card #f-rhythm').click(), 0"); await sleep(1500);
  const ph = JSON.parse(await ev(`(function(){ var out = {}; ["Line", "Slices", "Note"].forEach(function (t) {
      var b = [].filter.call(document.querySelectorAll('#rp-body .pptabs button'), function (x) { return x.textContent === t; })[0]; if (b) b.click();
      var over = [].map.call(document.querySelectorAll('#rp-body *'), function (e) { return e.scrollHeight - e.clientHeight > 1 && getComputedStyle(e).overflowY !== 'visible' ? (e.className || e.tagName) + ':' + (e.scrollHeight - e.clientHeight) : null; }).filter(Boolean);
      var g = document.querySelector('#rp-body .melgrid'), gw = g && g.getBoundingClientRect();
      out[t] = { tab: !!b, over: over, page: document.documentElement.scrollHeight - innerHeight, wide: gw ? Math.round(gw.right) : null }; });
    return JSON.stringify(out); })()`));
  check("on a phone each tab fits without scrolling", Object.values(ph).every((x) => x.tab && !x.over.length && x.page === 0), ph);
  check("on a phone the line is inside the screen", ph.Line.wide !== null && ph.Line.wide <= 390, ph.Line);
  const shot = await s.send("Page.captureScreenshot", { format: "png" });
  (await import("node:fs")).writeFileSync(process.env.SHOT || "build/melody-phone.png", Buffer.from(shot.data, "base64"));
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await sleep(1200);
  await ev(`fsListen.select(${JSON.stringify(id)}), 0`); await sleep(900);
  await ev("document.querySelector('#ls-card #f-rhythm').click(), 0"); await sleep(1500);
  const shot2 = await s.send("Page.captureScreenshot", { format: "png" });
  (await import("node:fs")).writeFileSync(process.env.SHOT2 || "build/melody-desk.png", Buffer.from(shot2.data, "base64"));
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
