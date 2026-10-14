/* Test 15 in the browser (docs/superpowers/specs/2026-10-14-test15-design.md): each point's EQ and envelope, the stretch's
   Sing, the grain envelope on the graph - every control writes the point and reaches the engine, and each panel still fits
   one screen. Headless Chrome against a local server, signed in through the listener bridge's gate; published points,
   edited only on this device (nothing is saved to the server).
     node tools/serve.mjs 8765    (repo root, in another shell)
     node core/tests/web_t15.mjs          FS_SHOTS=<dir> for screenshots */
import { spawn } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const SHOTS = process.env.FS_SHOTS || "";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9293", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-t15-")), "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9293))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 400)); });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const shot = async (name) => { if (!SHOTS) return; const r = await s.send("Page.captureScreenshot", { format: "png" }); writeFileSync(join(SHOTS, name + ".png"), Buffer.from(r.data, "base64")); };
  const sent = async (id) => JSON.parse((await ev(`__fa.coreSentFeature(${JSON.stringify(id)}) || 'null'`)) || "null");
  const key = (sel, k, times = 1) => ev(`(function(){ var e = document.querySelector(${JSON.stringify(sel)}); if (!e) return false; e.focus();
    for (var i = 0; i < ${times}; i++) e.dispatchEvent(new KeyboardEvent("keydown", { key: ${JSON.stringify(k)}, bubbles: true })); return true; })()`);
  /* one screen (C-8): nothing in the panel's body scrolls or spills */
  const fits = () => ev(`(function(){ var b = document.getElementById('rp-body'); if (!b) return null;
    var over = b.scrollHeight - b.clientHeight, r = b.getBoundingClientRect(), spill = 0;
    [].forEach.call(b.querySelectorAll('.pprow, .ppeq, canvas'), function (x) { var q = x.getBoundingClientRect(); if (q.width && q.right > r.right + 1) spill++; });
    return { over: over, spill: spill, doc: document.documentElement.scrollHeight - innerHeight }; })()`);

  for (const [vp, w, h, mobile] of [["desk", 1440, 900, false], ["phone", 390, 844, true]]) {
    await s.send("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor: mobile ? 2 : 1, mobile });
    await s.send("Emulation.setTouchEmulationEnabled", { enabled: mobile });
    await ev("try { localStorage.clear(); } catch (e) {} 0");   /* the last viewport's edits are on this device: start clean */
    await s.send("Page.navigate", { url: URL });
    for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
    await sleep(2500);
    await ev("fsListen._gate(true), 0"); await sleep(600);
    for (let i = 0; i < 60 && !(await ev("__fa.features().some(function (f) { return f.properties.audio_mode === 'grains'; })")); i++) await sleep(500);
    const p = (n) => vp + ": " + n;
    await ev("fsListen.sound()"); for (let i = 0; i < 40 && !(await ev("!!(__fa.coreSentFeature && __fa.core)")); i++) await sleep(250);   /* the engine, so edits can be seen reaching it */
    const kinds = JSON.parse(await ev(`JSON.stringify((function(){ var F = __fa.features().filter(function (f) { return f.geometry.type === "Point"; });
      var pick = function (fn) { var f = F.filter(fn)[0]; return f ? { id: f.properties.id, c: f.geometry.coordinates } : null; };
      return { stretch: pick(function (f) { return f.properties.has_audio && (!f.properties.audio_mode || f.properties.audio_mode === "soundscape"); }),
               hits: pick(function (f) { return f.properties.audio_mode === "hits" && f.properties.hits; }),
               grains: pick(function (f) { return f.properties.audio_mode === "grains" && f.properties.has_audio; }) }; })())`));
    const open = async (id) => { await ev(`fsListen.select(${JSON.stringify(id)}), 0`); await sleep(900); await ev("document.querySelector('#ls-card #f-rhythm').click(), 0"); await sleep(1200); };
    const tab = async (name) => { if (!mobile) return; await ev(`(function(){ var b = [].filter.call(document.querySelectorAll('#rp-body .pptabs button'), function (x) { return x.textContent === ${JSON.stringify(name)}; })[0]; if (b) b.click(); return !!b; })()`); await sleep(300); };

    /* ---- Stretch ---- */
    const st = kinds.stretch.id;
    await open(st);
    await tab("Point");
    check(p("stretch: the Point column has an EQ and an envelope button"), await ev("!!document.getElementById('pp-pt-eqbtn') && !!document.getElementById('pp-pt-envbtn')"), null);
    await ev("document.getElementById('pp-pt-eqbtn').click(), 0"); await sleep(300);
    check(p("stretch: the EQ opens with its five bands"), (await ev("document.querySelectorAll('#pp-eq-pop .eq-band').length")) === 5, null);
    await shot(vp + "-stretch-eq");
    await key("#pp-eq-pop .eq-band[data-band='2']", "ArrowDown", 12); await sleep(400);
    const se = await sent(st);
    check(p("stretch: an EQ edit is saved in sound.eq and reaches the engine"), !!se && Array.isArray(se.properties.sound.eq) && se.properties.sound.eq[2][1] === -6, se && se.properties.sound.eq);
    await ev("document.getElementById('pp-pt-envbtn').click(), 0"); await sleep(300);
    check(p("stretch: the envelope opens, none set (the chord glides)"), /glides/.test(await ev("(document.querySelector('#pp-env-pop .ppeq-read') || {}).textContent || ''")), await ev("(document.querySelector('#pp-env-pop') || {}).textContent"));
    await shot(vp + "-stretch-env");
    await key("#pp-env-pop .eq-band[data-env='0']", "ArrowRight", 2); await sleep(400);
    const sv = await sent(st);
    check(p("stretch: an envelope edit is saved in sound.env and reaches the engine"), !!sv && Array.isArray(sv.properties.sound.env) && sv.properties.sound.env.length === 4 && sv.properties.sound.env[0] > 0.5, sv && sv.properties.sound.env);
    await ev("document.querySelector('#pp-env-pop .ppeq-top button').click(), 0"); await sleep(400);
    check(p("stretch: None takes the envelope away"), !((await sent(st)).properties.sound.env), (await sent(st)).properties.sound.env);
    await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })), 0"); await sleep(200);
    await tab("Space");
    const sing = await ev(`(function(){ var s = document.querySelector('#rp-body input[data-k="sing"]'), c = document.querySelector('#rp-body input[data-k="contour"]'); if (!s || !c) return null;
      s.value = '0.6'; s.dispatchEvent(new Event('input', { bubbles: true })); c.value = '0.8'; c.dispatchEvent(new Event('input', { bubbles: true })); return [s.value, c.value]; })()`);
    await sleep(500);
    const ss = await sent(st);
    check(p("stretch: Sing and body · shape are in the panel, saved in the shape and sent"), !!sing && ss.properties.sound.shape.sing === 0.6 && ss.properties.sound.shape.contour === 0.8, [sing, ss && ss.properties.sound.shape]);
    const f1 = await fits();
    check(p("stretch: the panel fits one screen with Sing"), f1 && f1.over <= 1 && f1.spill === 0, f1);
    await shot(vp + "-stretch-space");
    if (mobile) for (const tn of ["Point", "Time", "Harmony"]) { await tab(tn); const ft = await fits(); check(p("stretch: tab " + tn + " fits"), ft && ft.over <= 1 && ft.spill === 0, ft); }

    /* ---- Rhythm (hits) ---- */
    if (kinds.hits) {
      const hp = kinds.hits.id;
      await open(hp); await tab("Point");
      check(p("rhythm: EQ and envelope buttons"), await ev("!!document.getElementById('pp-pt-eqbtn') && !!document.getElementById('pp-pt-envbtn')"), null);
      await ev("document.getElementById('pp-pt-envbtn').click(), 0"); await sleep(300);
      check(p("rhythm: none set - each hit plays out"), /plays out/.test(await ev("(document.querySelector('#pp-env-pop .ppeq-read') || {}).textContent || ''")), null);
      await key("#pp-env-pop .eq-band[data-env='1']", "ArrowDown", 4); await sleep(400);
      const hv = await sent(hp);
      check(p("rhythm: an envelope edit is saved in rhythm.env and reaches the engine"), !!hv && Array.isArray(hv.properties.rhythm.env) && hv.properties.rhythm.env[2] < 0.5, hv && hv.properties.rhythm.env);
      await shot(vp + "-rhythm-env");
      await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })), 0"); await sleep(200);
      await ev("document.getElementById('pp-pt-eqbtn').click(), 0"); await sleep(300);
      await key("#pp-eq-pop .eq-band[data-band='4']", "ArrowDown", 6); await sleep(400);
      check(p("rhythm: an EQ edit is saved in rhythm.eq"), ((await sent(hp)).properties.rhythm.eq || [])[4] && (await sent(hp)).properties.rhythm.eq[4][1] === -3, (await sent(hp)).properties.rhythm.eq);
      await ev("document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })), 0"); await sleep(200);
      const fr = await fits(); check(p("rhythm: the panel fits one screen"), fr && fr.over <= 1 && fr.spill === 0, fr);
    } else check(p("rhythm: a published hits point exists"), false, kinds);

    /* ---- Grains ---- */
    const gp = kinds.grains.id;
    await open(gp); await tab("Point");
    check(p("grains: an EQ button, no envelope button (the grain's own is in its column)"), await ev("!!document.getElementById('pp-pt-eqbtn') && !document.getElementById('pp-pt-envbtn')"), null);
    await tab("Envelope");
    check(p("grains: the grain envelope is the graph, its four sliders gone"),
      await ev("!!document.getElementById('pp-grain-env') && !document.querySelector('#rp-body input[data-k=\"attack\"]') && !document.querySelector('#rp-body .grainenv')"), null);
    const a0 = (await sent(gp)).properties.rhythm.grains.attack;
    await key("#pp-grain-env .eq-band[data-env='0']", "ArrowRight", 3); await sleep(400);
    const a1 = (await sent(gp)).properties.rhythm.grains.attack;
    check(p("grains: dragging the attack moves rhythm.grains.attack (a fraction) and reaches the engine"), a1 > a0 && a1 <= 1, [a0, a1]);
    check(p("grains: follow chord says unpitched grains follow by their own tone"), /strongest tone/.test(await ev("[].map.call(document.querySelectorAll('#rp-body .pprow'), function (r) { return r.title || ''; }).join(' ')")), null);
    await shot(vp + "-grains-env");
    const fg = await fits(); check(p("grains: the panel fits one screen"), fg && fg.over <= 1 && fg.spill === 0, fg);

    /* ---- the sound: Sing keeps a stretch point's level (A-18), soloed so only it is heard ---- */
    if (!mobile) {
      await open(st);
      await ev(`(function(){ var r = [].filter.call(document.querySelectorAll('#rp-body .pprow'), function (x) { var sp = x.querySelector('span'); return sp && sp.textContent.trim() === 'level'; })[0];
        var i = r.querySelector('input'); i.value = '0.9'; i.dispatchEvent(new Event('input', { bubbles: true })); return 1; })()`);
      const setSing = (v) => ev(`(function(){ var s = document.querySelector('#rp-body input[data-k="sing"]'); s.value = '${v}'; s.dispatchEvent(new Event('input', { bubbles: true })); return s.value; })()`);
      await setSing(0); await sleep(300);
      await ev(`fsListen.listen(${JSON.stringify(st)}), 0`);
      const lvl = async (secs) => { let e = 0, n = 0; for (let i = 0; i < secs * 4; i++) { await ev(`__fa.walkTo(${kinds.stretch.c[0]}, ${kinds.stretch.c[1]})`); await sleep(250); e += Math.pow(10, (await ev("__fa.coreLevel ? __fa.coreLevel() : -120")) / 10); n++; } return 10 * Math.log10(e / n + 1e-30); };
      await lvl(6);
      const l0 = await lvl(6);
      await setSing(1); await sleep(300); await lvl(4);
      const l1 = await lvl(6);
      check("desk: sound - a stretch point sings at its own level (sing 0 " + l0.toFixed(1) + " dB, sing 1 " + l1.toFixed(1) + " dB)", l0 > -60 && l1 > -60 && Math.abs(l1 - l0) < 3, [l0, l1]);
      await ev("fsListen.listen(null), 0");
    }
    await ev("fsListen.sound()"); await sleep(500);
  }
  check("no page errors", errors.length === 0, errors.slice(0, 3));
} finally { ch.kill(); }
console.log(failed ? failed + " FAILED" : "all passed");
process.exit(failed ? 1 : 0);
