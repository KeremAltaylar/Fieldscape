/* The route patch panel in the new look (docs/superpowers/plans/2026-09-27-setter-s2.md): knobs
   over the page's own sliders, one screen on a computer, tabs on a phone. Headless Chrome against a
   local server, signed in through the listener bridge's gate; real mouse and touch.
     python -m http.server 8765    (repo root, in another shell)
     node core/tests/web_patch.mjs          FS_SHOTS=<dir> for screenshots */
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const SHOTS = process.env.FS_SHOTS || "";
const dir = mkdtempSync(join(tmpdir(), "fs-patch-"));
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9263", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + dir, "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9263))[0]; } catch { await sleep(200); } }
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
    const route = await ev("__fa.features().filter(function(f){ return f.properties.kind === 'route' && /Koşuyolu/.test(f.properties.name); })[0].properties.id");
    await ev(`fsListen.select(${JSON.stringify(route)}), 0`); await sleep(900);
    await tapAt(await box("#ls-card #f-patch")); await sleep(900);
    check(p("Patch opens the route's panel"), await ev("!document.getElementById('patchpanel').hidden"), null);

    /* Task 1: knobs */
    const counts = JSON.parse(await ev("JSON.stringify({ ranges: document.querySelectorAll('#pp-body input[type=range]').length, knobs: document.querySelectorAll('#pp-body .knob').length })"));
    check(p("every slider is a knob"), counts.ranges > 20 && counts.knobs === counts.ranges, counts);
    if (mobile) { await tapAt(await box("#pp-tabs [data-tab='chords']")); await sleep(300); }
    const tempo0 = await ev(`fsListen.route(${JSON.stringify(route)}).tempo`);
    const k = await box("#pp-body .knob[data-k='tempo']");
    check(p("the Tempo knob is on screen"), !!k && k.w > 20, k);
    await down(k.x, k.y);
    for (let i = 1; i <= 8; i++) { await moveTo(k.x, k.y - 6 * i); await sleep(30); }
    await up(k.x, k.y - 48); await sleep(600);
    const tempo1 = await ev(`fsListen.route(${JSON.stringify(route)}).tempo`);
    check(p("dragging the knob up raises the tempo"), tempo1 > tempo0, [tempo0, tempo1]);
    check(p("…and its readout says so"), new RegExp(tempo1 + " bpm").test(await ev("(document.querySelector(\"#pp-body .knob[data-k='tempo']\").closest('.pprow')||{}).textContent")), await ev("(document.querySelector(\"#pp-body .knob[data-k='tempo']\").closest('.pprow')||{}).textContent"));
    await ev("document.querySelector(\"#pp-body .knob[data-k='tempo']\").focus(), 0");
    await s.send("Input.dispatchKeyEvent", { type: "keyDown", key: "ArrowUp", code: "ArrowUp", windowsVirtualKeyCode: 38 });
    await s.send("Input.dispatchKeyEvent", { type: "keyUp", key: "ArrowUp", code: "ArrowUp", windowsVirtualKeyCode: 38 }); await sleep(600);
    check(p("ArrowUp adds a step"), (await ev(`fsListen.route(${JSON.stringify(route)}).tempo`)) === tempo1 + 1, await ev(`fsListen.route(${JSON.stringify(route)}).tempo`));
    const k2 = await box("#pp-body .knob[data-k='tempo']");
    if (mobile) { await down(k2.x, k2.y); await up(k2.x, k2.y); await sleep(120); await down(k2.x, k2.y); await up(k2.x, k2.y); }
    else { await s.send("Input.dispatchMouseEvent", { type: "mousePressed", x: k2.x, y: k2.y, button: "left", clickCount: 1 }); await s.send("Input.dispatchMouseEvent", { type: "mouseReleased", x: k2.x, y: k2.y, button: "left", clickCount: 1 });
           await s.send("Input.dispatchMouseEvent", { type: "mousePressed", x: k2.x, y: k2.y, button: "left", clickCount: 2 }); await s.send("Input.dispatchMouseEvent", { type: "mouseReleased", x: k2.x, y: k2.y, button: "left", clickCount: 2 }); }
    await sleep(800);
    const def = await ev("+document.querySelector(\"#pp-body input[type=range][data-k='tempo'], #pp-body .knob[data-k='tempo']\").dataset.def");
    check(p("a double-click resets it to its default"), (await ev(`fsListen.route(${JSON.stringify(route)}).tempo`)) === def, [await ev(`fsListen.route(${JSON.stringify(route)}).tempo`), def]);
    check(p("the knob says what it is to a screen reader"), await ev("(function(){ var k = document.querySelector(\"#pp-body .knob[data-k='tempo']\"); return k.getAttribute('role') === 'slider' && /Tempo/i.test(k.getAttribute('aria-label')) && k.hasAttribute('aria-valuenow'); })()"), null);

    /* Task 2 / 3: the look, and fitting */
    if (!mobile) {
      const fit = JSON.parse(await ev("JSON.stringify((function(){ var pp = document.getElementById('patchpanel'), b = document.getElementById('pp-body'); return { page: document.documentElement.scrollHeight - innerHeight, panel: pp.scrollHeight - pp.clientHeight, body: b.scrollHeight - b.clientHeight }; })())"));
      check(p("the whole panel fits one screen"), fit.page === 0 && fit.panel <= 0 && fit.body <= 0, fit);
      await shot("desk-patch");
    } else {
      check(p("the phone panel has five tabs"), (await ev("[].map.call(document.querySelectorAll('#pp-tabs [data-tab]'), function (b) { return b.textContent; }).join(' ')")) === "Chords Voices Effects Morphs Rhythm", await ev("(document.querySelector('#pp-tabs')||{}).textContent"));
      for (const tab of ["chords", "voices", "effects", "morphs", "rhythm"]) {
        await tapAt(await box(`#pp-tabs [data-tab='${tab}']`)); await sleep(400);
        const subs = tab === "voices" ? JSON.parse(await ev("JSON.stringify([].map.call(document.querySelectorAll('#pp-sub [data-sub]'), function (b) { return b.dataset.sub; }))")) : [null];
        if (tab === "voices") { check(p("Voices splits into its three voices"), subs.length === 3, subs); }
        for (const sub of subs) {
          if (sub) { await tapAt(await box(`#pp-sub [data-sub='${sub}']`)); await sleep(400); }
          const f2 = JSON.parse(await ev("JSON.stringify((function(){ var pp = document.getElementById('patchpanel'), b = document.getElementById('pp-body'); return { page: document.documentElement.scrollHeight - innerHeight, panel: pp.scrollHeight - pp.clientHeight, body: b.scrollHeight - b.clientHeight, shown: [].filter.call(document.querySelectorAll('#pp-body h2'), function (h) { return h.offsetParent; }).map(function (h) { return h.textContent.split(' ')[0]; }) }; })())"));
          check(p(tab + (sub ? "/" + sub : "") + " fits the phone, no scrolling"), f2.page === 0 && f2.panel <= 0 && f2.body <= 0, f2);
          await shot("phone-patch-" + tab + (sub ? "-" + sub : ""));
          if (tab === "morphs") { check(p("Morphs shows the morphs, not Transport"), f2.shown.indexOf("Transport") < 0 && f2.shown.some(function (x) { return /Morphs/.test(x); }), f2.shown); }
        }
        const dup = await ev("[].filter.call(document.querySelectorAll('#pp-body input[type=range][data-knob]'), function (r) { return r.offsetWidth > 2; }).length");
        check(p(tab + ": no slider shows beside its knob"), dup === 0, dup);
      }
    }
    await ev("document.getElementById('pp-close').click(), 0"); await sleep(300);
    check(p("no page errors"), errors.length === 0, errors);
    errors.length = 0;
  }
  s.close();
} finally { process.kill(ch.pid); await sleep(500); try { rmSync(dir, { recursive: true, force: true }); } catch {} }
console.log(failed ? failed + " FAILED" : "all passed");
process.exitCode = failed ? 1 : 0;
