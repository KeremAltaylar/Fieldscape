/* A setter's unpublished rhythm and grains points sound (Kerem, 2026-09-29: "Rhythm and grains have no
   sound at all"). Their recordings live only on this device, so the engine must ask for them by id.
   Each draft is placed far from every published feature, where the walk is otherwise silent, and
   the level that leaves the engine is measured. Nothing is published.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_drafts.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9265", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-drafts-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
/* 1.5 s of a decaying click train as a WAV file, built in the page */
const WAV = `(function(name){ var sr = 22050, n = sr * 1.5, b = new ArrayBuffer(44 + n * 2), v = new DataView(b);
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
  for (var i = 0; i < n; i++) v.setInt16(44 + i * 2, (Math.random() * 2 - 1) * 12000 * Math.exp(-(i % 4000) / 800), true);
  return new File([b], name, { type: "audio/wav" }); })`;
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9265))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
  await sleep(3000);
  await ev("fsListen._gate(true), 0"); await sleep(800);
  const level = () => ev("__fa.coreLevel ? __fa.coreLevel() : -120");
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

  /* grains: one recording, attached as a file picked in the page's own input */
  const G = [29.09, 41.05];
  const gid = await place(G[0], G[1]);
  check("a draft point placed", !!gid, gid);
  await ev(`(function(){ var dt = new DataTransfer(); dt.items.add(${WAV}("g.wav")); var inp = document.getElementById("rec-file"); inp.files = dt.files; inp.dispatchEvent(new Event("change")); })()`);
  await sleep(2500);
  await ev("document.querySelector(\".recmode [data-amode='grains']\").click(), 0"); await sleep(600);
  check("it is a grains point with its recording", await ev(`(function(){ var f = __fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(gid)}; })[0]; return !!f && f.properties.audio_mode === "grains" && !!f.properties.has_audio && !f.properties.storage_path; })()`), null);

  /* rhythm: one hit, in the low slot */
  const R = [29.16, 41.1];
  const rid = await place(R[0], R[1]);
  await ev("document.querySelector(\".recmode [data-amode='hits']\").click(), 0"); await sleep(600);
  await ev("[].filter.call(document.querySelectorAll('#hitgrid .hitrow'), function (r) { return /low/.test(r.textContent); })[0].querySelector('button').click(), 0");
  await ev(`(function(){ var dt = new DataTransfer(); dt.items.add(${WAV}("k.wav")); var inp = document.getElementById("hit-file"); inp.files = dt.files; inp.dispatchEvent(new Event("change")); })()`);
  await sleep(1500);
  check("it is a rhythm point with a hit", await ev(`(function(){ var f = __fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(rid)}; })[0]; return !!f && f.properties.audio_mode === "hits" && !!(f.properties.hits && f.properties.hits.low); })()`), null);

  await ev("document.querySelector(\"#ls-tools [data-mode='select']\").click(), 0"); await sleep(300);
  await ev("fsListen.select(null), 0"); await sleep(300);
  await ev("__fa.walkTo(29.2, 41.2)"); await sleep(300);
  await ev("document.querySelector('#ls-sound').click()"); await sleep(2000);
  await ev("__fa.walkTo(29.2, 41.2)"); await sleep(2500);
  const quiet = await level();
  check("far from everything the walk is silent", quiet < -80, quiet);
  const g = await walkHear(await at(gid));
  check("walked into, the draft grains point sounds", g > -50, g);
  await ev("__fa.walkTo(29.2, 41.2)"); await sleep(3000);
  const r = await walkHear(await at(rid));
  check("walked into, the draft rhythm point sounds", r > -50, r);
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
