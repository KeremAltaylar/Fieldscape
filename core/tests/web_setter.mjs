/* The website's setter in the new frame (docs/superpowers/plans/2026-09-27-setter-s1.md), in
   headless Chrome against a local server, signed in through the listener bridge's gate (the UI of
   a signed-in setter; nothing is published). Real mouse on a computer, real touch on a phone.
     python -m http.server 8765    (repo root, in another shell)
     node core/tests/web_setter.mjs          FS_SHOTS=<dir> for screenshots
   Prints PASS/FAIL per check; exit code 1 on any FAIL. */
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const SHOTS = process.env.FS_SHOTS || "";
const dir = mkdtempSync(join(tmpdir(), "fs-setter-"));
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9262", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + dir, "about:blank"]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9262))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 400)); });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const shot = async (name) => { if (!SHOTS) return; const r = await s.send("Page.captureScreenshot", { format: "png" }); writeFileSync(join(SHOTS, name + ".png"), Buffer.from(r.data, "base64")); };
  const text = (sel) => ev(`(document.querySelector(${JSON.stringify(sel)})||{}).textContent||""`);
  const shown = (sel) => ev(`(function(){ var e = document.querySelector(${JSON.stringify(sel)}); return !!e && !!(e.offsetWidth || e.offsetHeight || e.getClientRects().length); })()`);
  const fits = () => ev("document.documentElement.scrollHeight - innerHeight");

  for (const [vp, w, h, mobile] of [["phone", 390, 844, true], ["desk", 1440, 900, false], ["laptop", 1366, 660, false], ["smallphone", 375, 667, true]]) {
    await s.send("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor: mobile ? 2 : 1, mobile });
    await s.send("Emulation.setTouchEmulationEnabled", { enabled: mobile });
    await s.send("Page.navigate", { url: URL });
    for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.walkTo && window.fsListen && document.body.classList.contains('world'))")); i++) await sleep(300);
    await sleep(3000);
    const p = (n) => vp + ": " + n;
    const tap = async (x, y) => {
      if (mobile) { await s.send("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x, y }] }); await s.send("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] }); }
      else { await s.send("Input.dispatchMouseEvent", { type: "mousePressed", x, y, button: "left", clickCount: 1 }); await s.send("Input.dispatchMouseEvent", { type: "mouseReleased", x, y, button: "left", clickCount: 1 }); }
    };
    const tapEl = async (sel) => {
      const r = JSON.parse(await ev(`JSON.stringify((function(){var e=document.querySelector(${JSON.stringify(sel)});if(!e)return null;e.scrollIntoView({block:"nearest"});var b=e.getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2};})())`));
      if (!r) return false; await tap(r.x, r.y); await sleep(500); return true;
    };
    /* an empty spot of map, clear of the panel, the top bar and any feature */
    const mapSpot = async (fx, fy) => JSON.parse(await ev(`JSON.stringify((function(){ var b = __fa.map.getContainer().getBoundingClientRect(); return { x: b.x + b.width * ${h < 700 && mobile ? 0.8 + (fx - 0.55) * 0.9 : fx}, y: b.y + b.height * ${fy} * ${h < 700 && mobile ? 0.6 : 1} }; })())`));
    await ev("__fa.map.jumpTo({ center: [29.02, 41.03], zoom: 15 }), 0"); await sleep(1500);

    /* Task 1: signed in, the frame stays and the setter's tools appear; the old chrome is gone */
    await ev("fsListen._gate(true), 0"); await sleep(800);
    check(p("signed in: the new frame stays"), await shown("#ls-top") && await shown("#ls-panel"), null);
    check(p("signed in: the old header and sidebar are not shown"), !(await shown("header")) && !(await shown("#panel")), null);
    check(p("the mode bar: Select · Point · Route"), /Select[\s\S]*Point[\s\S]*Route/.test(await text("#ls-tools")), await text("#ls-tools"));
    check(p("fits signed in"), (await fits()) === 0, await fits());
    /* the card's photo viewer is a dialog; it must not be trapped under the retired sidebar */
    const pv = await ev("(function(){ var d = document.getElementById('photo-view'); d.showModal(); var r = d.getBoundingClientRect(); d.close(); return r.width > 0 && r.height > 0; })()");
    check(p("a photo opened from the card shows"), pv, null);
    await tapEl("#ls-account");
    check(p("Undo delete shows exactly when there is something to undo"), await ev("(function(){ var u = document.querySelector('#ls-sheet #undo'); if (!u) return false; var n = +((u.textContent.match(/\\((\\d+)\\)/) || [0, 0])[1]); return !!u.offsetParent === n > 0; })()"), await text("#ls-sheet #undo"));
    check(p("Account's own buttons are 44 px targets"), await ev("[].every.call(document.querySelectorAll('#ls-sheet .ls-setacct button'), function (b) { return !b.offsetParent || b.getBoundingClientRect().height >= 43.5; })"), await ev("[].map.call(document.querySelectorAll('#ls-sheet .ls-setacct button'), function (b) { return Math.round(b.getBoundingClientRect().height); })"));
    await tapEl("#ls-sheet .ls-close");
    await shot(vp + "-setter");

    /* Task 2: drawing */
    await tapEl("#ls-tools [data-mode='point']");
    check(p("Point mode says what to do"), /Tap the map to place a point/.test(await text("#ls-panel")), await text("#ls-panel"));
    const n0 = await ev("__fa.features().length + (window.__fa.local ? 0 : 0)");
    const all0 = await ev("fsListen.setterCount()");
    let spot = await mapSpot(0.62, 0.3);
    await tap(spot.x, spot.y); await sleep(1200);
    check(p("a tap in Point mode places a point"), (await ev("fsListen.setterCount()")) === all0 + 1, [all0, await ev("fsListen.setterCount()")]);
    check(p("…and opens its card"), await ev("!!document.querySelector('#ls-card #card')"), await text("#ls-panel"));
    check(p("fits with the setter card"), (await fits()) === 0, await fits());
    await shot(vp + "-setter-point-card");

    /* Kerem, 2026-09-27: no scroll bar on the setter card - a switch (Point · Sound · Where), not a scrollbar */
    const noScroll = () => ev("(function(){ var h = document.getElementById('ls-holder'), p = document.getElementById('ls-panel'); return { holder: h.scrollHeight - h.clientHeight, panel: p.scrollHeight - p.clientHeight }; })()");
    check(p("the point card has Point · Photos · Sound · Where"), (await ev("[].map.call(document.querySelectorAll('#ls-cardtabs button'), function (b) { return b.textContent; }).join(' ')")) === "Point Photos Sound Where", await text("#ls-cardtabs"));
    for (const tab of ["main", "photos", "sound", "where"]) {
      await tapEl(`#ls-cardtabs [data-ct='${tab}']`); await sleep(300);
      const sc = await noScroll();
      check(p("point card, " + tab + ": no scroll bar"), sc.holder <= 0 && sc.panel <= 0, sc);
      check(p("point card, " + tab + ": Delete is always there"), await shown("#ls-card #f-delete"), null);
      await shot(vp + "-setter-card-" + tab);
    }
    /* Kerem, 2026-09-27: with a recording attached the card grew scroll bars again. Attach one (3 s of
       noise, as a file picked in the page's own input) and measure every tab again. */
    await ev(`(function(){ var sr = 22050, n = sr * 3, b = new ArrayBuffer(44 + n * 2), v = new DataView(b);
      function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
      s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
      v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
      for (var i = 0; i < n; i++) v.setInt16(44 + i * 2, (Math.random() * 2 - 1) * 8000, true);
      var dt = new DataTransfer(); dt.items.add(new File([b], "noise.wav", { type: "audio/wav" }));
      var inp = document.getElementById("rec-file"); inp.files = dt.files; inp.dispatchEvent(new Event("change")); })()`);
    await sleep(3000);
    check(p("a recording attached to the new point"), await ev(`!!(fsListen.point(fsListen.selected())||{}).has_audio || !!(__fa.features().filter(function(f){return f.properties.id===fsListen.selected();})[0]||{properties:{}}).properties.has_audio`), null);
    /* a card as a setter fills it: a note of several lines and two photos */
    await ev(`(function(){ var t = document.getElementById("f-note"); t.value = "Wind in the oaks above the stream.\\nA woodpecker, far off.\\nThe path bends here toward the water, and the sound opens.\\nBest at dusk."; t.dispatchEvent(new Event("input")); })()`);
    await ev(`(function(){ var dt = new DataTransfer(); for (var k = 0; k < 2; k++) { var c = document.createElement("canvas"); c.width = 400; c.height = 300; var g = c.getContext("2d"); g.fillStyle = k ? "#385" : "#835"; g.fillRect(0, 0, 400, 300);
      var bin = atob(c.toDataURL("image/png").split(",")[1]), u = new Uint8Array(bin.length); for (var i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); dt.items.add(new File([u], "p" + k + ".png", { type: "image/png" })); }
      var inp = document.getElementById("photo-file"); inp.files = dt.files; inp.dispatchEvent(new Event("change")); })()`);
    await sleep(2500);
    /* any box inside the card that scrolls - the card itself, a note, a photo strip, a meta line */
    const inner = () => ev(`JSON.stringify([].filter.call(document.querySelectorAll("#ls-holder, #ls-holder *, #ls-panel"), function (e) {
      if (!e.offsetParent && e.id !== "ls-panel") return false; var cs = getComputedStyle(e);
      var y = /(auto|scroll)/.test(cs.overflowY) && e.scrollHeight - e.clientHeight > 1, x = /(auto|scroll)/.test(cs.overflowX) && e.scrollWidth - e.clientWidth > 1;
      return (y || x) && !e.classList.contains("photos"); }).map(function (e) { return (e.id || e.tagName + "." + e.className) + " " + (e.scrollHeight - e.clientHeight) + "/" + (e.scrollWidth - e.clientWidth); }))`);
    for (const tab of ["main", "photos", "sound", "where"]) {
      await tapEl(`#ls-cardtabs [data-ct='${tab}']`); await sleep(300);
      const sc = await noScroll(), sb = JSON.parse(await inner());
      check(p("with a recording, point card, " + tab + ": no scroll bar"), sc.holder <= 0 && sc.panel <= 0, sc);
      /* Kerem, 2026-09-28 (a shorter window): whatever the height, no bar is drawn */
      const bar = await ev("(function(){ var h = document.getElementById('ls-holder'), p = document.getElementById('ls-panel'); var bw = function (e) { var c = getComputedStyle(e); return e.offsetWidth - e.clientWidth - parseFloat(c.borderLeftWidth) - parseFloat(c.borderRightWidth); }; return bw(h) + bw(p); })()");
      check(p("with a recording, point card, " + tab + ": no scroll bar drawn"), bar === 0, bar);
      check(p("with a recording, point card, " + tab + ": nothing inside it scrolls"), sb.length === 0, sb);
      await shot(vp + "-setter-card-audio-" + tab);
    }
    await tapEl("#ls-cardtabs [data-ct='main']");

    /* Kerem, 2026-09-27: the new point, unpublished, stretched - walked into with Sound on, it sat on
       "Preparing". Its recording is only on this device; the engine never asked for it. */
    {
      const nid = await ev("fsListen.selected()");
      const c = JSON.parse(await ev(`JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(nid)}; })[0].geometry.coordinates)`));
      await ev("document.getElementById('patch-play').click()");
      let row = null;
      for (let i = 0; i < 40; i++) {
        await ev(`fsListen.moveTo(${c[0]}, ${c[1]})`); await sleep(400);
        row = JSON.parse(await ev(`JSON.stringify(((window.__fa.core || {}).rows || []).filter(function (r) { return r.id === ${JSON.stringify(nid)}; })[0] || null)`));
        if (row && row.loaded) break;
      }
      check(p("walked into, the unpublished point's recording loads and sounds"), !!(row && row.loaded), row);
      await ev("document.getElementById('patch-play').click()"); await sleep(600);
    }

    /* S4: the whole card is reachable, and every one of its controls is a real target */
    const reach = JSON.parse(await ev(`JSON.stringify((function(){ var h = document.getElementById('ls-holder'); h.scrollTop = h.scrollHeight; var d = document.querySelector('#ls-card #f-delete').getBoundingClientRect(), hb = h.getBoundingClientRect();
      var small = [].filter.call(document.querySelectorAll('#ls-card #card button'), function (b) { var r = b.getBoundingClientRect(); return b.offsetParent && r.height < 43.5; }).map(function (b) { return b.id || b.textContent.trim(); });
      return { deleteInside: d.bottom <= hb.bottom + 0.5 && d.top >= hb.top - 0.5, small: small }; })())`));
    check(p("scrolled to its end, the card shows its last row whole"), reach.deleteInside, reach);
    check(p("every button on the card is at least 44 px tall"), reach.small.length === 0, reach.small);

    /* Task 3: the card edits the feature through the page's own inputs */
    const id = await ev("fsListen.selected()");
    await tapEl("#ls-card #f-name");
    await s.send("Input.insertText", { text: "Test pond" }); await sleep(600);
    check(p("typing a name renames the point"), (await ev(`(fsListen.point(${JSON.stringify(id)})||{}).name`)) === "Test pond", await ev(`(fsListen.point(${JSON.stringify(id)})||{}).name`));
    check(p("the card offers Stretch · Rhythm · Grains"), /Stretch[\s\S]*Rhythm[\s\S]*Grains/.test(await text("#ls-card .recmode")), await text("#ls-card"));
    await tapEl("#ls-card #f-delete"); await sleep(800);
    check(p("Delete removes it"), (await ev("fsListen.setterCount()")) === all0, await ev("fsListen.setterCount()"));
    check(p("…and closes its card"), !(await ev("!!document.querySelector('#ls-card')")), await text("#ls-panel"));

    await tapEl("#ls-tools [data-mode='route']");
    for (const [fx, fy] of [[0.55, 0.28], [0.65, 0.33], [0.72, 0.4]]) { spot = await mapSpot(fx, fy); await tap(spot.x, spot.y); await sleep(400); }
    check(p("Route mode counts the vertices"), /Drawing a route · 3 points/.test(await text("#ls-panel")), await text("#ls-panel"));
    await tapEl("#ls-draw-undo");
    check(p("Undo takes the last one back"), /2 points/.test(await text("#ls-panel")), await text("#ls-panel"));
    await shot(vp + "-setter-drawing");
    await tapEl("#ls-draw-done"); await sleep(1200);
    check(p("Done makes the route"), (await ev("fsListen.setterCount()")) === all0 + 1, await ev("fsListen.setterCount()"));
    const rsc = await ev("(function(){ var h = document.getElementById('ls-holder'), p = document.getElementById('ls-panel'); return { holder: h.scrollHeight - h.clientHeight, panel: p.scrollHeight - p.clientHeight }; })()");
    check(p("the route card fits with no scroll bar"), rsc.holder <= 0 && rsc.panel <= 0, rsc);
    await shot(vp + "-setter-route-card");
    check(p("…and opens its card with Patch"), await ev("!!document.querySelector('#ls-card #card') && !document.querySelector('#ls-card #f-patch').hidden"), await text("#ls-panel"));
    await tapEl("#ls-card #f-delete"); await sleep(800);

    await tapEl("#ls-tools [data-mode='route']");
    spot = await mapSpot(0.6, 0.3); await tap(spot.x, spot.y); await sleep(400);
    await tapEl("#ls-draw-done"); await sleep(600);
    check(p("Done with one vertex makes nothing"), (await ev("fsListen.setterCount()")) === all0, await ev("fsListen.setterCount()"));
    await tapEl("#ls-tools [data-mode='select']");

    /* Task 4: Archive, Publish, Account, Places for setters */
    check(p("setters get an Archive button"), await tapEl("#ls-archive") && /Archive/.test(await text("#ls-sheet")), await text("#ls-sheet"));
    check(p("the archive lists every feature"), (await ev("document.querySelectorAll('#ls-sheet #list button.row').length")) === all0, [await ev("document.querySelectorAll('#ls-sheet #list button.row').length"), all0]);
    check(p("fits with the archive"), (await fits()) === 0, await fits());
    await shot(vp + "-setter-archive");
    await tapEl("#ls-sheet #list button.row");
    check(p("an archive row opens its card"), await ev("!!document.querySelector('#ls-card #card')"), await text("#ls-panel"));
    await tapEl("#ls-card .ls-close"); await sleep(400);
    if (await ev("!document.querySelector('#ls-bar').hidden")) { await tapEl("#ls-grip"); }
    check(p("the page's status line (saves, import errors) shows under Publish"), await shown("#ls-pub #saved"), await text("#ls-pub"));
    check(p("the Publish bar is in the panel"), await shown("#ls-panel #publishbar") && /pending|Publish/.test(await text("#ls-panel #publishbar")), await text("#ls-panel"));
    await tapEl("#ls-account");
    check(p("Account shows who is signed in and Sign out"), await ev("!!document.querySelector('#ls-sheet #setter-out')"), await text("#ls-sheet"));
    check(p("Account keeps Export and Import"), /Export/.test(await text("#ls-sheet")) && /Import/.test(await text("#ls-sheet")), await text("#ls-sheet"));
    await tapEl("#ls-sheet .ls-close");
    await tapEl("#ls-place");
    check(p("Places for setters has no Open world switch (open world is the only mode)"), !(await ev("!!document.querySelector('#world-switch')")), await text("#ls-sheet"));
    await tapEl("#ls-sheet .ls-close");

    /* sign out with a card open: the listener's card, no setter controls left behind */
    const pid = await ev("__fa.features().filter(function(f){ return f.geometry.type === 'Point'; })[0].properties.id");
    await ev(`fsListen.select(${JSON.stringify(pid)}), 0`); await sleep(800);
    await ev("fsListen._gate(false), 0"); await sleep(900);
    check(p("signing out takes the setter card away"), !(await ev("!!document.querySelector('#ls #card')")) && !(await shown("#ls-tools")), await text("#ls-panel"));
    check(p("…and the page's own card goes home"), await ev("!!document.querySelector('#pane-info #card')"), null);
    check(p("no page errors"), errors.length === 0, errors);
    errors.length = 0;
  }
  s.close();
} finally { process.kill(ch.pid); await sleep(500); try { rmSync(dir, { recursive: true, force: true }); } catch {} }
console.log(failed ? failed + " FAILED" : "all passed");
process.exitCode = failed ? 1 : 0;
