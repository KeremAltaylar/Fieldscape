/* The cards' mute and solo, and the Plays choice (routes / points / both), measured at the engine's output
   (Kerem, 2026-10-29: "Solo and mute doesnt work properly" - they reached only the old audio graph).
   Two draft points far from everything published; nothing is published.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_mixer.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9266", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-mixer-")), "about:blank"]);
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
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9266))[0]; } catch { await sleep(200); } }
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

  const attach = async (id, mode) => {
    await ev(`(function(){ var dt = new DataTransfer(); dt.items.add(${WAV}("p.wav")); var inp = document.getElementById("rec-file"); inp.files = dt.files; inp.dispatchEvent(new Event("change")); })()`);
    await sleep(2500);
    if (mode) { await ev(`document.querySelector(".recmode [data-amode='${mode}']").click(), 0`); await sleep(600); }
  };
  /* the level heard standing on a point, averaged over ~2 s (energy) */
  const hear = async ([lon, lat]) => {
    let e = 0; for (let i = 0; i < 8; i++) { await ev(`__fa.walkTo(${lon + i * 1e-7}, ${lat})`); await sleep(250); e += Math.pow(10, (await level()) / 10); }
    return 10 * Math.log10(e / 8 + 1e-30);
  };
  const press = async (sel) => { await ev(`document.querySelector(${JSON.stringify(sel)}).click(), 0`); await sleep(1200); };
  const plays = async (m) => { await ev(`fsListen.setPlayback(${m}), 0`); await sleep(1200); };

  /* a stretch point and a grains point, far from everything published */
  const A = [29.09, 41.05], G = [29.16, 41.1];
  const sid = await place(A[0], A[1]); await attach(sid, null);
  const gid = await place(G[0], G[1]); await attach(gid, "grains");
  check("two draft points placed", !!sid && !!gid, [sid, gid]);
  await ev("document.querySelector(\"#ls-tools [data-mode='select']\").click(), 0"); await sleep(300);
  await ev("fsListen.select(null), 0"); await sleep(300);
  await ev("__fa.walkTo(29.2, 41.2)"); await sleep(300);
  await ev("document.querySelector('#ls-sound').click()"); await sleep(2000);
  const ps = await at(sid), pg = await at(gid);
  await walkHear(ps);
  const s0 = await hear(ps);
  check("the stretch point sounds", s0 > -50, s0);

  /* mute and solo on its card (Kerem, 2026-10-29: "Solo and mute doesnt work properly") */
  await ev(`fsListen.select(${JSON.stringify(sid)}), 0`); await sleep(800);
  await press("#f-mute"); await sleep(3500);   /* the room's tail rings out over ~3 s, as any release does */
  const s1 = await hear(ps);
  check("muted on its card, it is silent", s1 < -80, s1);
  await press("#f-mute");
  const s2 = await hear(ps);
  check("unmuted, it is back", s2 > s0 - 6, [s0, s2]);
  await press("#f-solo");
  const s3 = await hear(ps);
  check("soloed, it still sounds", s3 > s0 - 6, [s0, s3]);
  await ev(`__fa.walkTo(${pg[0]}, ${pg[1]})`); await sleep(4500);   /* the stretch point's tail, left behind, rings out */
  const g1 = await hear(pg);
  check("with another point soloed, the grains point is silent", g1 < -80, g1);
  await ev(`fsListen.select(${JSON.stringify(sid)}), 0`); await sleep(800);
  await press("#f-solo");
  await ev("fsListen.select(null), 0"); await sleep(300);
  const g2 = await walkHear(pg);
  check("the solo let go, the grains point sounds", g2 > -50, g2);

  /* what plays: routes only rests every point, points only plays them */
  await ev("document.querySelector('#ls-layers').click(), 0"); await sleep(500);
  check("Layers offers Both / Routes / Points", await ev("document.querySelectorAll('[data-ls-playback]').length") === 3, null);
  await ev("document.querySelector(\"[data-ls-playback='1']\").click(), 0"); await sleep(1200);
  const g3 = await hear(pg);
  check("routes only: the point is silent", g3 < -80, g3);
  check("the choice is kept on this device", await ev("localStorage.getItem('fs.playback')") === "1", null);
  await plays(2);
  const g4 = await hear(pg);
  check("points only: the point sounds", g4 > -50, g4);
  await plays(0);
} finally { ch.kill(); }
process.exit(failed ? 1 : 0);
