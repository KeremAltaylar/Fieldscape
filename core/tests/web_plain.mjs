/* Recording, the fifth point device (Kerem, 2026-11-03: "a plain soundscape recording ... nothing fancy"): a draft point set to
   Recording plays its file as it was made, its panel is the point's column and the waveform, and switching back to Stretch
   leaves no trace. Nothing is published.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_plain.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9269", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-plain-")), "about:blank"]);
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
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9269))[0]; } catch { await sleep(200); } }
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
  const P = [29.09, 41.05];
  const id = await place(P[0], P[1]);
  await ev(`(function(){ var dt = new DataTransfer(); dt.items.add(${WAV}("r.wav")); var inp = document.getElementById("rec-file"); inp.files = dt.files; inp.dispatchEvent(new Event("change")); })()`);
  await sleep(2500);
  check("the card offers Recording", await ev("!!document.querySelector(\".recmode [data-amode='plain']\")"), null);
  await ev("document.querySelector(\".recmode [data-amode='plain']\").click(), 0"); await sleep(600);
  const prop = async () => JSON.parse(await ev(`JSON.stringify((function(){ var p = __fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(id)}; })[0].properties; return { mode: p.audio_mode, plain: p.sound && p.sound.shape ? p.sound.shape.plain : null }; })())`));
  const p1 = await prop();
  check("set to Recording, the point is plain (sound.shape.plain)", p1.mode === "plain" && p1.plain === 1, p1);
  await ev("document.querySelector('#ls-card #f-rhythm').click(), 0"); await sleep(1500);
  const ui = JSON.parse(await ev(`JSON.stringify({ title: document.getElementById("rp-title").textContent, wave: !!document.querySelector("#rp-body .plainwave"),
    eq: [].some.call(document.querySelectorAll("#rp-body .pprow span"), function (x) { return x.textContent === "eq"; }),
    gainMax: (function(){ var r = [].filter.call(document.querySelectorAll("#rp-body label.pprow"), function (x) { return /^level/.test(x.textContent); })[0]; return r ? +r.querySelector("input").max : null; })(),
    scroll: document.documentElement.scrollHeight - innerHeight })`));
  check("its panel: Recording, the waveform, eq, level to 4x, one screen", /Recording/.test(ui.title) && ui.wave && ui.eq && ui.gainMax === 4 && ui.scroll === 0, ui);
  { const sh = await s.send("Page.captureScreenshot", { format: "png" }); (await import("node:fs")).writeFileSync("build/plain-desk.png", Buffer.from(sh.data, "base64")); }
  await ev("document.getElementById('rp-close').click(), 0"); await sleep(300);
  await ev("document.querySelector(\"#ls-tools [data-mode='select']\").click(), 0"); await sleep(300);
  await ev("fsListen.select(null), 0"); await sleep(300);
  await ev("__fa.walkTo(29.2, 41.2)"); await sleep(300);
  await ev("document.querySelector('#ls-sound').click()"); await sleep(2500);
  const Q = await at(id);
  let best = -120; for (let i = 0; i < 30 && best < -45; i++) { await ev(`__fa.walkTo(${Q[0] + i * 1e-7}, ${Q[1]})`); await sleep(400); best = Math.max(best, await level()); }
  check("walked into, it sounds", best > -50, best);
  await ev(`fsListen.select(${JSON.stringify(id)}), 0`); await sleep(800);
  await ev("document.querySelector(\".recmode [data-amode='soundscape']\").click(), 0"); await sleep(600);
  const p2 = await prop();
  check("switched back to Stretch, it is a stretch again", p2.mode === "soundscape" && (p2.plain === undefined || p2.plain === null), p2);
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
