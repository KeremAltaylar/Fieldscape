/* The web app on the shared core (?core), in headless Chrome against a local server: presses Sound,
   walks the Koşuyolu route and back through __fa.walkTo, and reads what the engine reports
   (__fa.core) and what actually leaves it (an analyser after the output gain).

     python -m http.server 8765    (repo root, in another shell)
     node core/tests/web_core.mjs [seconds]                                          */
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const seconds = +(process.argv[2] || 60);
const dir = mkdtempSync(join(tmpdir(), "fs-webcore-"));
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9235", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + dir, "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9235))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => {
    if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 300));
    if (m.method === "Runtime.consoleAPICalled" && /core|error/i.test(JSON.stringify(m.params.args))) console.log("console:", m.params.args.map((a) => a.value ?? a.description).join(" ").slice(0, 300));
  });
  await s.send("Runtime.enable");
  await s.send("Page.navigate", { url: process.env.FS_URL || "http://localhost:8765/?core" });
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && document.body.classList.contains('world'))")); i++) await sleep(300);
  await sleep(3000);                                      /* the published features */
  await ev("document.getElementById('patch-play').click()");
  for (let i = 0; i < 50 && !(await ev("!!(window.__fa && __fa.core)")); i++) {
    await ev("__fa.walkTo(29.038879, 41.00771)"); await sleep(300);
  }
  const lon0 = 29.038879, lat0 = 41.00771, lon1 = 29.0412, lat1 = 41.0102;
  const rows = [];
  for (let k = 0; k <= seconds; k++) {
    const u = k / seconds < 0.5 ? (2 * k) / seconds : 2 - (2 * k) / seconds;
    await ev(`__fa.walkTo(${lon0 + (lon1 - lon0) * u}, ${lat0 + (lat1 - lat0) * u})`);
    await sleep(1000);
    if (k % 5 === 0) {
      const st = await ev("JSON.stringify({ c: __fa.core, lvl: __fa.coreLevel ? __fa.coreLevel() : null })");
      rows.push(st); const j = JSON.parse(st); console.log(`t=${k}s level ${j.lvl === null ? "-" : j.lvl.toFixed(1)} dB · route ${j.c && j.c.route} · ${j.c ? j.c.rows.map((r) => r.name + (r.loaded ? "" : " (loading)")).join(", ") : ""} · rhythm ${j.c ? j.c.rhythms.join(", ") : ""}`);
    }
  }
  /* W1: the engine's live reads reach the page, and the cells draw them */
  await ev(`__fa.walkTo(${lon0}, ${lat0})`); await sleep(2500);
  const L = JSON.parse(await ev("JSON.stringify(window.__fa.coreLive ? { n: __fa.coreLive.n, clock: __fa.coreLive.clock, chord: __fa.coreLive.chord } : null)"));
  console.log("live:", JSON.stringify(L));
  if (!L || L.n < 1 || !(L.clock > 0) || !L.chord || L.chord.count !== 16) { console.error("FAIL live data"); process.exitCode = 1; }
  else {
    const c1 = await ev("__fa.coreLive.clock"); await sleep(1000); const c2 = await ev("__fa.coreLive.clock");
    if (!(c2 - c1 > 0.8 && c2 - c1 < 1.2)) { console.error("FAIL live clock advances", c1, c2); process.exitCode = 1; }
    const same = await ev("(function () { var m = liveCellsForTest(); return !!m && m.length === __fa.coreLive.n && m[0].v === __fa.coreLive.morphs[4]; })()");
    if (!same) { console.error("FAIL cells read the engine"); process.exitCode = 1; }
    await ev("__fa.walkTo(29.09, 41.05)"); await sleep(2500);            /* ~6 km from any route */
    const none = await ev("__fa.coreLive.n");
    if (none !== -1) { console.error("FAIL no route: n", none); process.exitCode = 1; }
  }
  console.log("errors:", errors.length ? errors : "none");
  if (errors.length) process.exitCode = 1;
  s.close();
} finally { process.kill(ch.pid); await sleep(500); try { rmSync(dir, { recursive: true, force: true }); } catch {} }
