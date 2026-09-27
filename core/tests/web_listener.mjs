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
    /* the chord is -1 until the first bar lands (3.3 s at 72 bpm), later when the page is busy */
    for (let i = 0; i < 40 && !/chord \d+ of 16/.test(await ev("document.querySelector('#ls-walk').textContent")); i++) await sleep(250);
    await sleep(600);
    const walkText = await ev("document.querySelector('#ls-walk').textContent");
    check(p("route line with the chord"), /Route .*chord \d+ of 16/.test(walkText), walkText);
    await ev("window.__lsBtn = document.querySelector('#ls-sound')"); await sleep(1500);
    check(p("buttons survive live updates (a tap is not lost)"), await ev("document.querySelector('#ls-sound') === window.__lsBtn"), false);
    check(p("Sound turned to Stop"), /Stop/.test(await ev("document.querySelector('#ls-sound').textContent")), null);
    check(p("cells shown for the route"), await ev("!document.querySelector('#ls-cells').hidden"), false);
    check(p("fits while hearing"), await ev("document.documentElement.scrollHeight - innerHeight === 0"), await ev("document.documentElement.scrollHeight - innerHeight"));
    await shot(vp + "-hearing");
    await ev("document.querySelector('#ls-cells-btn').click()"); await sleep(300);
    check(p("Cells button hides them"), await ev("document.querySelector('#ls-cells').hidden && document.querySelector('#ls-cells-btn').getAttribute('aria-pressed') === 'false'"), false);
    /* a listener's own Cells choice survives Sound off and on */
    await ev("document.querySelector('#ls-sound').click()"); await sleep(1500);
    await ev("document.querySelector('#ls-sound').click()");
    for (let i = 0; i < 30 && !(await ev("!!(window.__fa.coreLive && __fa.coreLive.n > 0 && fsListen.state().live)")); i++) { await ev("__fa.walkTo(29.038879, 41.00771)"); await sleep(300); }
    await sleep(1200);
    check(p("Cells stay off across Sound off and on"), await ev("document.querySelector('#ls-cells').hidden && document.querySelector('#ls-cells-btn').getAttribute('aria-pressed') === 'false'"), await ev("document.querySelector('#ls-cells-btn') && document.querySelector('#ls-cells-btn').getAttribute('aria-pressed')"));
    await ev("document.querySelector('#ls-cells-btn').click()"); await sleep(300);
    await ev("document.querySelector('#ls-grip').click()"); await sleep(400);
    check(p("grip folds to the bar"), await ev("!document.querySelector('#ls-bar').hidden && document.querySelector('#ls-walk').hidden"), false);
    check(p("fits folded"), await ev("document.documentElement.scrollHeight - innerHeight === 0"), await ev("document.documentElement.scrollHeight - innerHeight"));
    await shot(vp + "-folded");
    await ev("document.querySelector('#ls-grip').click()"); await sleep(300);
    await ev("__fa.walkTo(29.09, 41.05)"); await sleep(2500);
    const far = await ev("document.querySelector('#ls-walk').textContent");
    check(p("far from routes: no route line"), !/Route /.test(far), far);
    /* prefetch: approaching a point fetches its recording before you reach it, so the first
       entrance does not wait on a download (Kerem, 2026-09-27: "smooth is better") */
    if (vp === "phone") {
      const near = JSON.parse(await ev(`JSON.stringify((function(){
        var f = __fa.features().filter(function(f){ return f.geometry.type === "Point" && f.properties.has_audio && f.properties.audio_mode !== "hits"; })[0];
        var r = (f.properties.sound && f.properties.sound.radius) || 140, c = f.geometry.coordinates;
        return { id: f.properties.id, lon: c[0], lat: c[1] - (r + 80) / 111320 }; })())`));
      await ev(`__fa.walkTo(${near.lon}, ${near.lat})`);
      for (let i = 0; i < 40 && !(await ev(`(__fa.prefetched || []).indexOf(${JSON.stringify(near.id)}) >= 0`)); i++) await sleep(300);
      check(p("approaching a point fetches its recording first"), await ev(`(__fa.prefetched || []).indexOf(${JSON.stringify(near.id)}) >= 0`), await ev("JSON.stringify(__fa.prefetched)"));
      check(p("…without playing it before you arrive"), !(await ev(`(fsListen.state().core.rows || []).some(function(r){ return r.id === ${JSON.stringify(near.id)}; })`)), null);
    }

    /* ---- W2/W3: everything a listener does, with real mouse events ---- */
    const click = async (x, y) => {
      await s.send("Input.dispatchMouseEvent", { type: "mousePressed", x, y, button: "left", clickCount: 1 });
      await s.send("Input.dispatchMouseEvent", { type: "mouseReleased", x, y, button: "left", clickCount: 1 });
    };
    const clickEl = async (sel) => {
      const r = JSON.parse(await ev(`JSON.stringify((function(){var e=document.querySelector(${JSON.stringify(sel)});if(!e)return null;var b=e.getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2};})())`));
      if (!r) return false; await click(r.x, r.y); await sleep(500); return true;
    };
    const text = (sel) => ev(`(document.querySelector(${JSON.stringify(sel)})||{}).textContent||""`);
    await ev("__fa.walkTo(29.038879, 41.00771)"); await sleep(1500);

    /* Places: the routes, and a route opens its card */
    check(p("place picker opens Places"), await clickEl("#ls-place") && /Routes/.test(await text("#ls-sheet")), await text("#ls-sheet"));
    check(p("Places lists the Koşuyolu route"), /Koşuyolu Parkı/.test(await text("#ls-sheet .ls-routes")), await text("#ls-sheet"));
    await clickEl("#ls-sheet .ls-routes button"); await sleep(1200);
    check(p("a route opens its card with 16 chords"), await ev("document.querySelectorAll('#ls-card .ls-chord').length") === 16, await text("#ls-card"));
    const linkOk = await ev("(function(){ var parts = location.pathname.split('/').filter(Boolean); var rid = parts[parts.length - 1], f = __fa.features().filter(function (x) { return x.properties.id === rid; })[0]; return !!f && parts[parts.length - 2] === f.properties.place; })()");
    check(p("a route's address names the park it is in (a shared link opens it)"), linkOk, await ev("location.pathname"));
    check(p("route card: Show whole route"), /Show whole route/.test(await text("#ls-card")), await text("#ls-card"));
    check(p("fits with a route card"), await ev("document.documentElement.scrollHeight - innerHeight === 0"), await ev("document.documentElement.scrollHeight - innerHeight"));
    await shot(vp + "-route-card");
    await clickEl("#ls-card .ls-close"); await sleep(500);
    check(p("closing a card returns to the walk"), await ev("!document.querySelector('#ls-card')"), await text("#ls-panel"));
    if (await ev("!document.querySelector('#ls-bar').hidden")) { await clickEl("#ls-grip"); }

    /* Layers: base map, zones */
    check(p("Layers opens"), await clickEl("#ls-layers") && /Satellite/.test(await text("#ls-sheet")), await text("#ls-sheet"));
    await shot(vp + "-layers");
    await clickEl("#ls-sheet [data-ls-base='topo']");
    check(p("Topo switches the base map"), await ev("fsListen.layers().base") === "topo", await ev("fsListen.layers()"));
    await clickEl("#ls-sheet [data-ls-layer='zones']");
    check(p("zones switch off"), await ev("fsListen.layers().zones") === false, await ev("fsListen.layers()"));
    await clickEl("#ls-sheet [data-ls-layer='zones']"); await clickEl("#ls-sheet [data-ls-base='sat']");
    await clickEl("#ls-sheet .ls-close");

    /* Account: the setter's door, and offline (the old sidebar's Download map, for listeners too) */
    check(p("Account opens with setter sign-in"), await clickEl("#ls-account") && /Setter sign-in/.test(await text("#ls-sheet")), await text("#ls-sheet"));
    check(p("Account offers the park underfoot for offline"), /Download Koşuyolu Parkı/.test(await text("#ls-sheet .ls-offline")), await text("#ls-sheet"));
    await clickEl("#ls-sheet .ls-offline"); await sleep(3000);
    check(p("offline sizes it first (tap again to confirm)"), /Confirm .* tap again/i.test(await text("#ls-sheet .ls-offline")), await text("#ls-sheet .ls-offline"));
    await clickEl("#ls-sheet .ls-close");

    /* a point on the map opens its card; Listen solos it */
    await ev("fsListen.frame(fsListen.state().rows[0].id)"); await sleep(1200);
    const pt = JSON.parse(await ev("JSON.stringify((function(){var id=fsListen.state().rows[0].id,q=fsListen.point(id),c=__fa.map.project([q.lon,q.lat]),r=__fa.map.getContainer().getBoundingClientRect();return {id:id,name:q.name,x:c.x+r.x,y:c.y+r.y};})())"));
    await click(pt.x, pt.y); await sleep(800);
    check(p("clicking a point opens its card"), (await text("#ls-card .ls-cardtitle")) === pt.name, await text("#ls-card"));
    await shot(vp + "-point-card");
    check(p("point card has Listen"), await clickEl("#ls-card .ls-listen"), null);
    await sleep(1500);
    check(p("Listen solos the point"), await ev("fsListen.listening()") === pt.id && /Stop listening/.test(await text("#ls-card .ls-listen")), await ev("fsListen.listening()"));
    const soloLevel = await ev(`(function(){var s=fsListen.state();var r=(s.core.rows||[]).concat(s.core.beats||[]).filter(function(x){return x.id===${JSON.stringify(pt.id)}})[0];return r?r.level:null;})()`);
    check(p("the soloed point plays at full level"), soloLevel === 1, soloLevel);
    await clickEl("#ls-card .ls-listen");
    await clickEl("#ls-card .ls-play");
    for (let i = 0; i < 30 && !(await ev("!!(fsListen.playing() && !fsListen.playing().paused && fsListen.playing().pos > 0)")); i++) await sleep(300);
    check(p("the card plays the original recording"), await ev("!!(fsListen.playing() && fsListen.playing().pos > 0)"), await ev("JSON.stringify(fsListen.playing())"));
    check(p("the player shows its clock"), /\d:\d\d \/ \d+:\d\d/.test(await text("#ls-card .ls-pos")), await text("#ls-card .ls-pos"));
    await clickEl("#ls-card .ls-play");
    check(p("the play button pauses it"), await ev("!!(fsListen.playing() && fsListen.playing().paused)"), await ev("JSON.stringify(fsListen.playing())"));
    check(p("Stop listening lets go"), (await ev("fsListen.listening()")) === null, await ev("fsListen.listening()"));
    await clickEl("#ls-card .ls-close"); await sleep(400);
    if (await ev("!document.querySelector('#ls-bar').hidden")) { await clickEl("#ls-grip"); }
    /* soloed, the route is resting: the panel says who is playing, not the route */
    await ev(`fsListen.listen(${JSON.stringify(pt.id)})`); await sleep(1200);
    const soloWalk = await text("#ls-walk");
    check(p("while soloed the panel drops the resting route"), /Listening to .* alone/.test(soloWalk) && !/Route /.test(soloWalk), soloWalk);
    await clickEl("#ls-walk .ls-routeline .ls-btn"); await sleep(800);
    check(p("Everything lets go from the panel"), (await ev("fsListen.listening()")) === null, await ev("fsListen.listening()"));

    /* the walker: press and hold anywhere moves it (and opens no card); dragging its dot moves it */
    const w0 = await ev("JSON.stringify(fsListen.walker())");
    const box = JSON.parse(await ev("JSON.stringify(__fa.map.getContainer().getBoundingClientRect())"));
    /* a spot well away from the walker's dot: pressing the dot itself is a drag, not a hold */
    const d0 = JSON.parse(await ev("JSON.stringify((function(){var b=document.querySelector('.pacer').getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2};})())"));
    let hx = box.x + box.width * 0.72, hy = box.y + box.height * 0.3;
    if (Math.hypot(hx - d0.x, hy - d0.y) < 150) { hx = box.x + box.width * 0.3; hy = box.y + box.height * 0.25; }
    /* a phone is touched, a computer is clicked: each with its own events */
    const press = async (x, y) => mobile ? s.send("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x, y }] })
                                         : s.send("Input.dispatchMouseEvent", { type: "mousePressed", x, y, button: "left", clickCount: 1 });
    const moveTo = async (x, y) => mobile ? s.send("Input.dispatchTouchEvent", { type: "touchMove", touchPoints: [{ x, y }] })
                                          : s.send("Input.dispatchMouseEvent", { type: "mouseMoved", x, y, button: "left", buttons: 1 });
    const release = async (x, y) => mobile ? s.send("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] })
                                           : s.send("Input.dispatchMouseEvent", { type: "mouseReleased", x, y, button: "left", clickCount: 1 });
    await press(hx, hy); await sleep(800); await release(hx, hy);
    await sleep(600);
    const w1 = await ev("JSON.stringify(fsListen.walker())");
    check(p("press and hold moves the walker"), w1 !== w0, [w0, w1]);
    check(p("press and hold opens no card"), await ev("!document.querySelector('#ls-card')"), await text("#ls-card"));
    const dot = JSON.parse(await ev("JSON.stringify((function(){var b=document.querySelector('.pacer').getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2};})())"));
    await press(dot.x, dot.y);
    for (let k = 1; k <= 8; k++) { await moveTo(dot.x - 10 * k, dot.y + 5 * k); await sleep(30); }
    await release(dot.x - 80, dot.y + 40);
    await sleep(500);
    check(p("dragging the dot moves the walker"), (await ev("JSON.stringify(fsListen.walker())")) !== w1, null);
    await ev("fsListen.moveTo(29.03, 41.2)"); await sleep(1800);
    const inView = await ev("(function(){var p=__fa.map.project(fsListen.walker()),c=__fa.map.getContainer();return p.x>0&&p.y>0&&p.x<c.clientWidth&&p.y<c.clientHeight;})()");
    check(p("the map follows the walker off screen"), inView, null);
    check(p("no page errors"), errors.length === 0, errors);
    errors.length = 0;
  }
  /* a shared route link, as 404.html hands it on (?p=/<park>/<route>): open world, the route's card */
  const r = JSON.parse(await ev("JSON.stringify((function(){ var f = __fa.features().filter(function(x){ return x.properties.kind === 'route' && /Koşuyolu/.test(x.properties.name); })[0]; return { id: f.properties.id, place: f.properties.place, name: f.properties.name }; })())"));
  await s.send("Page.navigate", { url: URL.replace(/\/$/, "") + "/?p=/" + r.place + "/" + r.id });
  for (let i = 0; i < 60 && !(await ev("!!document.querySelector('#ls-card .ls-cardtitle')")); i++) await sleep(300);
  check("a shared route link opens its card", (await ev("(document.querySelector('#ls-card .ls-cardtitle')||{}).textContent")) === r.name, await ev("location.href"));
  check("…in open world", await ev("document.body.classList.contains('world') && document.body.classList.contains('listener')"), null);
  s.close();
} finally { process.kill(ch.pid); await sleep(500); try { rmSync(dir, { recursive: true, force: true }); } catch {} }
console.log(failed ? failed + " FAILED" : "all passed");
process.exitCode = failed ? 1 : 0;
