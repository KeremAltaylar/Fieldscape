/* A point's sound panel (Stretch, Rhythm, Grains) in the new look (setter S3): knobs over the
   page's own sliders, one screen on a computer, the panel's own tabs on a phone. Headless Chrome against a
   local server, signed in through the listener bridge's gate; real mouse and touch.
     python -m http.server 8765    (repo root, in another shell)
     node core/tests/web_sound.mjs          FS_SHOTS=<dir> for screenshots */
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const SHOTS = process.env.FS_SHOTS || "";
const dir = mkdtempSync(join(tmpdir(), "fs-sound-"));
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9264", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + dir, "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9264))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 400)); });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const shot = async (name) => { if (!SHOTS) return; const r = await s.send("Page.captureScreenshot", { format: "png" }); writeFileSync(join(SHOTS, name + ".png"), Buffer.from(r.data, "base64")); };

  for (const [vp, w, h, mobile] of [["desk", 1440, 900, false], ["phone", 390, 844, true]]) {
    await s.send("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor: mobile ? 2 : 1, mobile });
    await s.send("Emulation.setTouchEmulationEnabled", { enabled: mobile });
    await s.send("Page.navigate", { url: URL });
    for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
    await sleep(3000);
    const p = (n) => vp + ": " + n;
    const box = async (sel) => JSON.parse(await ev(`JSON.stringify((function(){var e=document.querySelector(${JSON.stringify(sel)});if(!e)return null;e.scrollIntoView({block:"nearest"});var b=e.getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2,w:b.width,h:b.height};})())`));
    const down = (x, y) => mobile ? s.send("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x, y }] }) : s.send("Input.dispatchMouseEvent", { type: "mousePressed", x, y, button: "left", clickCount: 1 });
    const moveTo = (x, y) => mobile ? s.send("Input.dispatchTouchEvent", { type: "touchMove", touchPoints: [{ x, y }] }) : s.send("Input.dispatchMouseEvent", { type: "mouseMoved", x, y, button: "left", buttons: 1 });
    const up = (x, y) => mobile ? s.send("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] }) : s.send("Input.dispatchMouseEvent", { type: "mouseReleased", x, y, button: "left", clickCount: 1 });
    const tapAt = async (b) => { await down(b.x, b.y); await up(b.x, b.y); await sleep(500); };

    await ev("fsListen._gate(true), 0"); await sleep(600);
    const kinds = JSON.parse(await ev(`JSON.stringify((function(){ var F = __fa.features().filter(function (f) { return f.geometry.type === "Point"; });
      var pick = function (fn) { var f = F.filter(fn)[0]; return f ? f.properties.id : null; };
      return { stretch: pick(function (f) { return f.properties.has_audio && !f.properties.audio_mode; }) || pick(function (f) { return f.properties.has_audio && f.properties.audio_mode === "soundscape"; }),
               rhythm: pick(function (f) { return f.properties.audio_mode === "hits"; }), grains: pick(function (f) { return f.properties.audio_mode === "grains"; }) }; })())`));
    for (const kind of ["stretch", "rhythm", "grains"]) {
      const id = kinds[kind];
      if (!id) { check(p(kind + ": a published point of this kind exists"), false, kinds); continue; }
      await ev(`fsListen.select(${JSON.stringify(id)}), 0`); await sleep(900);
      await tapAt(await box("#ls-card #f-rhythm")); await sleep(1200);
      check(p(kind + ": the card opens its sound panel"), await ev("!document.getElementById('rhythmpanel').hidden"), null);
      const c = JSON.parse(await ev("JSON.stringify({ ranges: document.querySelectorAll('#rp-body input[type=range]').length, knobs: document.querySelectorAll('#rp-body .knob').length, bare: [].filter.call(document.querySelectorAll('#rp-body input[type=range]'), function (r) { return r.offsetWidth > 2; }).length })"));
      check(p(kind + ": every slider is a knob, none shown beside it"), c.ranges > 0 && c.knobs === c.ranges && c.bare === 0, c);
      /* a real drag on the first knob on screen moves its slider */
      const kb = JSON.parse(await ev("JSON.stringify((function(){ var k = [].filter.call(document.querySelectorAll('#rp-body .knob'), function (e) { return e.offsetParent; })[0]; if (!k) return null; var i = k.previousElementSibling && k.previousElementSibling.type === 'range' ? k.previousElementSibling : k.parentNode.previousElementSibling; var b = k.getBoundingClientRect(); window.__kin = k.closest('.pprow') ? k.closest('.pprow').querySelector('input[type=range]') : i; return { x: b.x + b.width / 2, y: b.y + b.height / 2, v: +window.__kin.value, max: +window.__kin.max }; })())"));
      if (kb) {
        const dirUp = kb.v < kb.max;
        await down(kb.x, kb.y);
        for (let i = 1; i <= 8; i++) { await moveTo(kb.x, kb.y + (dirUp ? -6 : 6) * i); await sleep(30); }
        await up(kb.x, kb.y); await sleep(500);
        const v1 = await ev("+window.__kin.value");
        check(p(kind + ": dragging a knob moves its slider"), v1 !== kb.v, [kb.v, v1]);
      }
      if (!mobile) {
        const fit = JSON.parse(await ev("JSON.stringify((function(){ var pp = document.getElementById('rhythmpanel'), b = document.getElementById('rp-body'); return { page: document.documentElement.scrollHeight - innerHeight, panel: pp.scrollHeight - pp.clientHeight, body: b.scrollHeight - b.clientHeight }; })())"));
        check(p(kind + ": the panel fits one screen"), fit.page === 0 && fit.panel <= 0 && fit.body <= 0, fit);
        await shot("desk-sound-" + kind);
      } else {
        const n = await ev("document.querySelectorAll('#rp-body .pptabs button').length");
        check(p(kind + ": the phone panel has tabs"), n > 1, n);
        for (let i = 0; i < n; i++) {
          await tapAt(await box(`#rp-body .pptabs button:nth-child(${i + 1})`)); await sleep(400);
          const f2 = JSON.parse(await ev("JSON.stringify((function(){ var pp = document.getElementById('rhythmpanel'), b = document.getElementById('rp-body'); return { tab: (document.querySelector('#rp-body .pptabs [aria-selected=true]')||{}).textContent, page: document.documentElement.scrollHeight - innerHeight, panel: pp.scrollHeight - pp.clientHeight, body: b.scrollHeight - b.clientHeight }; })())"));
          check(p(kind + ": " + f2.tab + " tab fits, no scrolling"), f2.page === 0 && f2.panel <= 0 && f2.body <= 0, f2);
          await shot("phone-sound-" + kind + "-" + i);
        }
      }
      await ev("document.getElementById('rp-close').click(), 0"); await sleep(400);
    }
    check(p("no page errors"), errors.length === 0, errors);
    errors.length = 0;
  }
  s.close();
} finally { process.kill(ch.pid); await sleep(500); try { rmSync(dir, { recursive: true, force: true }); } catch {} }
console.log(failed ? failed + " FAILED" : "all passed");
process.exitCode = failed ? 1 : 0;
