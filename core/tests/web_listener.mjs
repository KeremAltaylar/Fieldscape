/* The website's listener shell (docs/superpowers/plans/2026-09-27-web-listener-w1.md), in headless
   Chrome against a local server, at a phone and a computer viewport:
     python -m http.server 8765    (repo root, in another shell)
     node core/tests/web_listener.mjs
   Prints PASS/FAIL per check; exit code 1 on any FAIL. */
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const SHOTS = process.env.FS_SHOTS || "";           /* a directory: screenshots of each state */
const dir = mkdtempSync(join(tmpdir(), "fs-listener-"));
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9236", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + dir, "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9236))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 400)); });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const shot = async (name) => { if (!SHOTS) return; const r = await s.send("Page.captureScreenshot", { format: "png" }); writeFileSync(join(SHOTS, name + ".png"), Buffer.from(r.data, "base64")); };

  for (const [vp, w, h, mobile] of [["phone", 390, 844, true], ["desk", 1440, 900, false]]) {
    await s.send("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor: mobile ? 2 : 1, mobile });
    await s.send("Emulation.setTouchEmulationEnabled", { enabled: mobile });
    await s.send("Page.navigate", { url: URL });
    for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && document.body.classList.contains('world'))")); i++) await sleep(300);
    await sleep(3000);
    const p = (n) => vp + ": " + n;

    /* Task 3: the gate and the bridge */
    check(p("signed out is the listener"), await ev("document.body.classList.contains('listener')"), false);
    check(p("bridge reads the place"), await ev("typeof fsListen === 'object' && 'place' in fsListen.state()"), false);
    check(p("old header hidden"), await ev("getComputedStyle(document.querySelector('header')).display === 'none'"), false);
    await ev("fsListen._gate(true)"); await sleep(300);
    check(p("signed in: setter surface back"), await ev("!document.body.classList.contains('listener') && getComputedStyle(document.getElementById('panel')).display !== 'none'"), false);
    await ev("fsListen._gate(false)"); await sleep(300);
    check(p("signed out again: listener"), await ev("document.body.classList.contains('listener')"), false);

    /* Task 4: the shell */
    check(p("first screen before Sound"), await ev("!!document.querySelector('#ls-walk') && /Sound/.test(document.querySelector('#ls-sound').textContent)"), null);
    check(p("cells canvas in the shell"), await ev("!!document.querySelector('#ls-cells #cells')"), false);
    check(p("fits before Sound"), await ev("document.documentElement.scrollHeight - innerHeight === 0"), await ev("document.documentElement.scrollHeight - innerHeight"));
    await shot(vp + "-first");
    await ev("document.querySelector('#ls-sound').click()");
    for (let i = 0; i < 30 && !(await ev("!!(window.__fa.coreLive && __fa.coreLive.n > 0)")); i++) { await ev("__fa.walkTo(29.038879, 41.00771)"); await sleep(400); }
    await sleep(5000);                                    /* past the first bar (3.3 s at 72 bpm): the chord is -1 before it */
    const walkText = await ev("document.querySelector('#ls-walk').textContent");
    check(p("route line with the chord"), /Route .*chord \d+ of 16/.test(walkText), walkText);
    check(p("Sound turned to Stop"), /Stop/.test(await ev("document.querySelector('#ls-sound').textContent")), null);
    check(p("cells shown for the route"), await ev("!document.querySelector('#ls-cells').hidden"), false);
    check(p("fits while hearing"), await ev("document.documentElement.scrollHeight - innerHeight === 0"), await ev("document.documentElement.scrollHeight - innerHeight"));
    await shot(vp + "-hearing");
    await ev("document.querySelector('#ls-cells-btn').click()"); await sleep(300);
    check(p("Cells button hides them"), await ev("document.querySelector('#ls-cells').hidden && document.querySelector('#ls-cells-btn').getAttribute('aria-pressed') === 'false'"), false);
    await ev("document.querySelector('#ls-cells-btn').click()"); await sleep(300);
    await ev("document.querySelector('#ls-grip').click()"); await sleep(400);
    check(p("grip folds to the bar"), await ev("!document.querySelector('#ls-bar').hidden && document.querySelector('#ls-walk').hidden"), false);
    check(p("fits folded"), await ev("document.documentElement.scrollHeight - innerHeight === 0"), await ev("document.documentElement.scrollHeight - innerHeight"));
    await shot(vp + "-folded");
    await ev("document.querySelector('#ls-grip').click()"); await sleep(300);
    await ev("__fa.walkTo(29.09, 41.05)"); await sleep(2500);
    const far = await ev("document.querySelector('#ls-walk').textContent");
    check(p("far from routes: no route line"), !/Route /.test(far), far);
    check(p("no page errors"), errors.length === 0, errors);
    errors.length = 0;
  }
  s.close();
} finally { process.kill(ch.pid); await sleep(500); try { rmSync(dir, { recursive: true, force: true }); } catch {} }
console.log(failed ? failed + " FAILED" : "all passed");
process.exitCode = failed ? 1 : 0;
