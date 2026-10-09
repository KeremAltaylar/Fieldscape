/* Sample harmony 6a in the browser: the site's own patch panel - a setter sets a route voice to a sampler with its
   controls and sample, saved in the route's own patch (the sample and its analysis JSON in the route's folder), and every
   listener's engine plays it (docs/superpowers/specs/2026-10-09-samples-6a-website-switch-over-design.md). Grown from
   web_site_lab.mjs (3c-7). A probe setter and an unpublished test route are made with the service key and removed after.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node --env-file=.env.local core/tests/web_site_samplers.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createClient } from "@supabase/supabase-js";
import { targets, session } from "../../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const db = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_SERVICE_KEY, { auth: { persistSession: false } });
const EMAIL = "probe-site-lab@fieldarc.test", PASSWORD = "probe-" + "w".repeat(16);
const DRAFT = "00000000-0000-4000-8000-00000000003d";      /* an id no other test file uses */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0, userId = null;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };

/* a leftover probe user from a crashed run would make createUser fail: remove it first */
{ const { data } = await db.auth.admin.listUsers({ perPage: 1000 }); const old = data && data.users.find((u) => u.email === EMAIL);
  if (old) { await db.from("setters").delete().eq("id", old.id); await db.auth.admin.deleteUser(old.id); } }
const { data: u } = await db.auth.admin.createUser({ email: EMAIL, password: PASSWORD, email_confirm: true });
userId = u.user.id;
await db.from("setters").insert({ id: userId, name: "probe-site-lab" });
const pubRow = (await db.from("features").select("id, place, geometry, properties").eq("kind", "route").is("deleted_at", null).filter("properties->>published", "eq", "true").limit(1)).data[0];
const PUB = pubRow.id;
/* a published route with no sampler voice in its patch (6a: a route carried over has them) */
const PUB2 = ((await db.from("features").select("id, properties").eq("kind", "route").is("deleted_at", null).filter("properties->>published", "eq", "true")).data || [])
  .filter((r) => !/"s-[a-z]+"/.test(JSON.stringify(r.properties.patch || {}))).map((r) => r.id)[0];
await db.from("features").delete().eq("id", DRAFT);
/* the site reads only published features from the server: a setter's draft is on their own device (the page's local
   store), with the same id as its row - which lab_route_roles needs */
/* the published route's patch with its three voices on at their default levels: the Freeze checks hear the Third voice
   against the others, and a setter may have changed them on the live route (2026-10-07: Third voice off on one route,
   0.15 under a First voice at 1.0 on the other) */
const pp0 = pubRow.properties.patch, lv = (k, g) => Object.assign({}, pp0[k], { on: true, gain: g });
const draftPatch = Object.assign({}, pp0, { voice: lv("voice", 0.45), sect: lv("sect", 0.8), v3: lv("v3", 0.55) });
const draftFeature = { type: "Feature", geometry: { type: "LineString", coordinates: pubRow.geometry.coordinates.map((c) => [c[0], c[1] + 0.01]) },
  properties: { id: DRAFT, kind: "route", place: pubRow.place, published: false, name: "lab site draft", patch: draftPatch } };
await db.from("features").insert({ id: DRAFT, place: pubRow.place, kind: "route", geometry: draftFeature.geometry, properties: draftFeature.properties });
const dbPatch0 = JSON.stringify((await db.from("features").select("properties").eq("id", DRAFT).single()).data.properties.patch);

const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9274", "--autoplay-policy=no-user-gesture-required", "--window-size=1440,900", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-sitelab-")), "about:blank"]);
/* 3c.1: bird-like recordings - 0.35 s calls every 2.5 s, energy only high (a 5 kHz whistle, or high-passed noise) */
const birdish = (kind, name) => `(function(){ var sr = 48000, n = sr * 12, b = new ArrayBuffer(44 + n * 2), v = new DataView(b), r = 11;
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
  var y1 = 0, y2 = 0;
  for (var i = 0; i < n; i++) {
    var t = i / sr, on = (t % 2.5) < 0.35 ? Math.sin(Math.PI * (t % 2.5) / 0.35) : 0, x;
    if (${JSON.stringify(kind)} === "tone") { x = on * 0.5 * Math.sin(2 * Math.PI * (5000 * t + 5 * Math.sin(2 * Math.PI * 30 * t))); }
    else { r ^= r << 13; r >>>= 0; r ^= r >>> 17; r ^= r << 5; r >>>= 0; var w = (r / 4294967296) * 2 - 1; var hp = w - y1; y1 = w; y2 = 0.6 * y2 + 0.4 * hp; x = on * 0.6 * (hp - y2); }
    v.setInt16(44 + i * 2, Math.max(-32767, Math.min(32767, Math.round(32767 * x))), true);
  }
  return new File([b], ${JSON.stringify(name)}, { type: "audio/wav" }); })()`;
const noise = (sec, name) => `(function(){ var sr = 48000, n = sr * ${sec}, b = new ArrayBuffer(44 + n * 2), v = new DataView(b), r = 7;
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
  for (var i = 0; i < n; i++) { r ^= r << 13; r >>>= 0; r ^= r >>> 17; r ^= r << 5; r >>>= 0; v.setInt16(44 + i * 2, Math.round(9000 * ((r / 4294967296) * 2 - 1)), true); }
  return new File([b], ${JSON.stringify(name)}, { type: "audio/wav" }); })()`;
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9274))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [], reqs = [];
  s.on((m) => {
    if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 300));
    if (m.method === "Network.requestWillBeSent") reqs.push(m.params.request.url);
    if (m.method === "Fetch.requestPaused") s.send("Fetch.failRequest", { requestId: m.params.requestId, errorReason: "Failed" }).catch(() => {});
  });
  await s.send("Runtime.enable"); await s.send("Page.enable"); await s.send("Network.enable");
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const until = async (expr, ms) => { for (let i = 0; i < ms / 200; i++) { if (await ev(expr)) return true; await sleep(200); } return false; };
  const open = async () => { await s.send("Page.navigate", { url: URL }); await until("!!(window.__fa && window.fsListen && __fa.features)", 30000); await sleep(1500); };
  const openPanel = async (id) => {
    await ev(`fsListen.select(${JSON.stringify(id)}), 0`); await sleep(900);
    await ev("(function(){ var b = document.querySelector('#ls-card #f-patch') || document.querySelector('#f-patch'); b && b.click(); return 0; })()");
    await until("!document.getElementById('patchpanel').hidden", 5000);
  };
  const rowsKey = "[].map.call(document.querySelectorAll('#pp-body .pprow'), function (r) { var i = r.querySelector('input,select,button'); return ((r.querySelector('span') || {}).textContent || '') + '|' + (i && (i.dataset.k || i.tagName)); }).join(';')";
  const livePatch = (id) => ev(`JSON.stringify((__fa.features().filter(function (f) { return f.properties.id === ${JSON.stringify(id)}; })[0] || { properties: {} }).properties.patch)`);
  const setSel = (k, v) => ev(`(function(){ var s = document.querySelector('#pp-body select[data-k="${k}"]'); s.value = '${v}'; s.dispatchEvent(new Event('change')); return 1; })()`);
  const setRange = (k, v) => ev(`(function(){ var s = document.querySelector('#pp-body input[data-k="${k}"]'); s.value = '${v}'; s.dispatchEvent(new Event('input')); return 1; })()`);
  /* 6a: a voice's EQ opens from its button (the graph floats over the column) */
  const eqOpen = (r) => ev(`(function(){ var p = document.getElementById('pp-eq-pop'); if (p && p.dataset.role === '${r}') return 1; var b = document.getElementById('pp-${r}-eqbtn'); if (!b) return 0; b.click(); return document.getElementById('pp-eq-pop') ? 1 : 0; })()`);

  /* signed out: no switch, nothing asked of the lab */
  await open();
  check("signed out: the lab table is never asked", !reqs.some((r) => /lab_route_roles/.test(r)), reqs.filter((r) => /lab/.test(r)));

  /* the setter's draft on this device */
  await ev(`(function(){ var k = 'fieldarc.v1', fc = JSON.parse(localStorage.getItem(k) || '{"type":"FeatureCollection","features":[]}'); fc.features = fc.features.filter(function (f) { return f.properties.id !== '${DRAFT}'; }); fc.features.push(${JSON.stringify(draftFeature)}); localStorage.setItem(k, JSON.stringify(fc)); return 1; })()`);
  await open();

  /* signed in, lab mode off: the panel as before */
  const si = await ev(`__fa.sb.auth.signInWithPassword({ email: ${JSON.stringify(EMAIL)}, password: ${JSON.stringify(PASSWORD)} }).then(function (r) { return r.error ? r.error.message : "ok"; })`);
  const has = await until(`__fa.features().some(function (f) { return f.properties.id === '${DRAFT}'; })`, 15000);
  check("signed in as a setter: the draft route is on the map", si === "ok" && has, si);
  await openPanel(DRAFT);
  check("6a: no lab switch in the panel header", await ev("!document.querySelector('#pp-lab')"), null);

  /* lab mode on: Digital and Sampler groups; Freeze shows its rows, hides harm/index */
  await until(`__fa.rolesFor === '${DRAFT}'`, 8000);
  check("signed in: each role's instrument menu has Digital and Sampler groups",
    await ev("['voice','sect','v3'].every(function (r) { var s = document.querySelector('#pp-body select[data-k=\"' + r + '.synth\"]'); return s && s.querySelector(\"optgroup[label='Digital'] option[value='fm']\") && s.querySelectorAll(\"optgroup[label='Sampler'] option\").length === 8; })"), null);
  await setSel("v3.synth", "s-freeze"); await sleep(400);
  check("Freeze on the Third voice: Focus, Colour, Tune and a Sample row; no harm/index row",
    await ev("['v3.focus','v3.colour','v3.tune'].every(function (k) { return !!document.querySelector('#pp-body [data-k=\"' + k + '\"]'); }) && !!document.querySelector('#pp-v3-file') && !document.querySelector('#pp-body [data-k=\"v3.harm\"]') && !document.querySelector('#pp-body [data-k=\"v3.index\"]')"), null);

  /* 3c.1: the sample block - inside its column, a button, a warning when a role is silent, a waveform */
  const inCol = (sel) => ev(`(function(){ var e = document.querySelector('${sel}'); if (!e) return false; var c = e.closest('.ppcol').getBoundingClientRect(), b = e.getBoundingClientRect(); return b.width > 0 && b.left >= c.left - 1 && b.right <= c.right + 1; })()`);
  check("a sampler role's sample block: an Upload button, its name and warning inside its column", (await ev("!!document.querySelector('#pp-v3-upload')")) && (await inCol("#pp-v3-warn")), null);
  check("no sample yet: the warning says the role is silent", /no sample.*silent/i.test(await ev("(document.querySelector('#pp-v3-warn') || {}).textContent || ''")), await ev("(document.querySelector('#pp-v3-warn') || {}).textContent"));
  check("Freeze's Colour row is labelled 'moment' and its readout is the time only", await ev("(function(){ var i = document.querySelector('#pp-body input[data-k=\"v3.colour\"]'), r = i && i.closest('.pprow'); return !!r && /^moment$/i.test(r.querySelector('span').textContent.trim()) && /^\\d+:\\d\\d$/.test(r.querySelector('i').textContent.trim()); })()"), await ev("(function(){ var i = document.querySelector('#pp-body input[data-k=\"v3.colour\"]'), r = i && i.closest('.pprow'); return r && [r.querySelector('span').textContent, r.querySelector('i').textContent]; })()"));

  /* upload, move Focus: autosaved to the lab row; the live patch unchanged byte for byte */
  await ev(`(function(){ var f = ${noise(3, "probe-site.wav")}, dt = new DataTransfer(); dt.items.add(f); var i = document.querySelector('#pp-v3-file'); i.files = dt.files; i.dispatchEvent(new Event('change')); return 1; })()`);
  await until("/uploaded|failed/.test((document.querySelector('#pp-v3-name') || {}).textContent || '')", 15000);
  await setRange("v3.focus", "0.77");
  await sleep(2500);
  await sleep(900);
  const lp = JSON.parse(await livePatch(DRAFT));
  check("6a: a sampler voice is saved in the route's own patch: synth, its controls, the sample's two files in the route's folder",
    lp.v3.synth === "s-freeze" && lp.v3.sampler.focus === 0.77 && lp.v3.sample.path.indexOf(DRAFT + "/v3-") === 0 && /\.json$/.test(lp.v3.sample.analysis_path) && !("analysis" in lp.v3.sample), lp.v3);
  const files = ((await db.storage.from("recordings").list(DRAFT)).data || []).map((o) => o.name);
  check("6a: the WAV and its analysis JSON are in Storage beside each other", files.some((n) => /^v3-\d+\.wav$/.test(n)) && files.some((n) => /^v3-\d+\.json$/.test(n)), files);
  check("uploaded: the name inside its column, no warning, the waveform drawn", /probe-site\.wav/.test(await ev("(document.querySelector('#pp-v3-name') || {}).textContent || ''")) && (await inCol("#pp-v3-name")) && !(await ev("(document.querySelector('#pp-v3-warn') || {}).textContent || ''")) && (await ev("(function(){ var c = document.querySelector('#pp-v3-wave'); if (!c || c.width < 100) return false; var d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data, n = 0; for (var i = 3; i < d.length; i += 4) if (d[i]) n++; return n > 200; })()")), [await ev("(document.querySelector('#pp-v3-name') || {}).textContent"), await ev("(document.querySelector('#pp-v3-warn') || {}).textContent")]);
  const fill = await ev("(function(){ var c = document.querySelector('#pp-v3-wave'), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data, best = 0; for (var x = 0; x < c.width; x++) { var n = 0; for (var y = 0; y < c.height; y++) if (d[(y * c.width + x) * 4 + 3]) n++; if (n < c.height) best = Math.max(best, n); } return best / c.height; })()");
  check("the waveform is scaled to the sample's own peak (its tallest bar near full height) (final review 3c.1)", fill > 0.8, fill);
  const col0 = await ev("+document.querySelector('#pp-body input[data-k=\"v3.colour\"]').value");
  await ev("(function(){ var c = document.querySelector('#pp-v3-wave'), b = c.getBoundingClientRect(); ['pointerdown','pointerup'].forEach(function (t) { c.dispatchEvent(new PointerEvent(t, { clientX: b.left + b.width * 0.8, clientY: b.top + b.height / 2, bubbles: true })); }); return 1; })()");
  await sleep(300);
  const col1 = await ev("+document.querySelector('#pp-body input[data-k=\"v3.colour\"]').value");
  check("a click on Freeze's waveform moves its Moment (Colour)", col1 > 0.6 && col1 !== col0, [col0, col1]);
  const mk0 = await ev("+document.querySelector('#pp-v3-wave').dataset.moment");
  await setRange("v3.colour", "0.2"); await sleep(300);
  const mk1 = await ev("+document.querySelector('#pp-v3-wave').dataset.moment");
  check("the Moment marker follows the slider (final review 3c.1)", mk1 < mk0 - 10, [mk0, mk1]);
  await ev("(function(){ var c = document.querySelector('#pp-v3-wave'), b = c.getBoundingClientRect(), y = b.top + b.height / 2; c.dispatchEvent(new PointerEvent('pointerdown', { clientX: b.left + b.width * 0.3, clientY: y, buttons: 1, pointerId: 1, bubbles: true })); c.dispatchEvent(new PointerEvent('pointermove', { clientX: b.left + b.width * 0.75, clientY: y, buttons: 1, pointerId: 1, bubbles: true })); c.dispatchEvent(new PointerEvent('pointerup', { clientX: b.left + b.width * 0.75, clientY: y, pointerId: 1, bubbles: true })); return 1; })()");
  await sleep(300);
  const col2 = await ev("+document.querySelector('#pp-body input[data-k=\"v3.colour\"]').value");
  check("dragging on Freeze's waveform moves its Moment (final review 3c.1)", col2 > 0.6, col2);
  /* final review 3d: a Method saved for Harmonic/Formant does not become Retune's Model (Comb 2 would play Granular) */
  await setSel("v3.synth", "s-harmonic"); await sleep(300); await setSel("v3.method", "2"); await sleep(300);
  await setSel("v3.synth", "s-retune"); await sleep(400);
  check("switching to Retune starts at One-shot, whatever Method the role had", (await ev("(document.querySelector('#pp-body select[data-k=\"v3.method\"]') || {}).value")) === "0", await ev("(document.querySelector('#pp-body select[data-k=\"v3.method\"]') || {}).value"));
  check("3d: Retune shows a model menu (One-shot, Looped, Granular) and a position control",
    await ev("(function(){ var m = document.querySelector('#pp-body select[data-k=\"v3.method\"]'), p = document.querySelector('#pp-body input[data-k=\"v3.focus\"]'); return !!m && [].map.call(m.options, function (o) { return o.textContent; }).join(',') === 'One-shot,Looped,Granular' && !!p && /^position$/i.test(p.closest('.pprow').querySelector('span').textContent.trim()) && /^\\d+:\\d\\d$/.test(p.closest('.pprow').querySelector('i').textContent.trim()); })()"),
    await ev("(function(){ var m = document.querySelector('#pp-body select[data-k=\"v3.method\"]'), p = document.querySelector('#pp-body input[data-k=\"v3.focus\"]'); return [m && [].map.call(m.options, function (o) { return o.textContent; }).join(','), p && p.closest('.pprow').textContent]; })()"));
  await setSel("v3.synth", "s-fm"); await sleep(400);
  check("4: Sample FM shows model, ratio (x) and depth, and warns on an unpitched sample",
    await ev("(function(){ var r = function (k) { var i = document.querySelector('#pp-body [data-k=\"v3.' + k + '\"]'); return i && i.closest('.pprow'); }; return !!r('method') && /^ratio$/i.test(r('focus').querySelector('span').textContent.trim()) && /^x\\d/.test(r('focus').querySelector('i').textContent.trim()) && /^depth$/i.test(r('colour').querySelector('span').textContent.trim()) && /needs a pitched sample/i.test((document.querySelector('#pp-v3-warn') || {}).textContent || ''); })()"),
    await ev("(function(){ var r = function (k) { var i = document.querySelector('#pp-body [data-k=\"v3.' + k + '\"]'); return i && i.closest('.pprow') && i.closest('.pprow').textContent; }; return [r('method'), r('focus'), r('colour'), (document.querySelector('#pp-v3-warn') || {}).textContent]; })()"));
  await setSel("v3.synth", "s-retune"); await sleep(400);
  check("Retune with an unpitched sample warns", /Retune needs a pitched sample/i.test(await ev("(document.querySelector('#pp-v3-warn') || {}).textContent || ''")), await ev("(document.querySelector('#pp-v3-warn') || {}).textContent"));
  await setSel("v3.synth", "s-freeze"); await sleep(400);
  await setRange("v3.focus", "0.77"); await sleep(2500);
  const shot = async (name) => { if (!process.env.SHOTS) return; const r = await s.send("Page.captureScreenshot", { format: "png" }); (await import("node:fs")).writeFileSync(process.env.SHOTS + "/" + name + ".png", Buffer.from(r.data, "base64")); };
  /* phone width: the block fits its column (Review Focus 5) */
  await s.send("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 2, mobile: true }); await sleep(500);
  await open();                                                   /* a phone opens the page at its own width */
  await until(`__fa.features().some(function (f) { return f.properties.id === '${DRAFT}'; })`, 15000);
  await openPanel(DRAFT); await until(`__fa.rolesFor === '${DRAFT}'`, 8000); await sleep(1500);
  await ev("(function(){ var t = document.querySelector(\"#pp-tabs [data-tab='voices']\"); t && t.click(); var v = document.querySelector(\"#pp-sub [data-sub='v3']\"); v && v.click(); return 1; })()"); await sleep(500);
  const phone = await ev("(function(){ var b = document.querySelector('.ppsample[data-role=v3]'), c = b && b.closest('.ppcol'), s = document.querySelector('#pp-body select[data-k=\"v3.synth\"]'); return b && c && s ? { block: Math.round(b.getBoundingClientRect().width), col: Math.round(c.getBoundingClientRect().width), sel: Math.round(s.getBoundingClientRect().width), vw: innerWidth } : null; })()");
  check("phone width: the sample block fits its column, spans it, and the instrument menu stays usable", (await inCol("#pp-v3-name")) && (await inCol("#pp-v3-wave")) && (await ev("document.documentElement.scrollWidth <= innerWidth + 1")) && !!phone && phone.col > phone.vw * 0.8 && phone.block >= phone.col * 0.95 && phone.sel > 120, phone);
  /* 6: each voice's EQ graph on a phone, in its own tab */
  const phoneEq = [];
  for (const [sub, r] of [["v1", "voice"], ["v2", "sect"], ["v3", "v3"]]) {
    await ev(`(function(){ var t = document.querySelector("#pp-tabs [data-tab='voices']"); t && t.click(); var v = document.querySelector("#pp-sub [data-sub='${sub}']"); v && v.click(); return 1; })()`); await sleep(500);
    await eqOpen(r); await sleep(200);
    await ev(`document.querySelector('#pp-${r}-eq').scrollIntoView({ block: 'center' }), 0`); await sleep(300);
    const w = await ev(`(function(){ var e = document.querySelector('#pp-${r}-eq svg'), c = e && e.closest('.ppcol'); return e && c ? [Math.round(e.getBoundingClientRect().width), Math.round(c.getBoundingClientRect().width)] : null; })()`);
    const hit = await ev(`Math.round(document.querySelector('#pp-${r}-eq .eq-hit').getBoundingClientRect().width)`);   /* final review 6: a 44 px touch target */
    if (!(await inCol(`#pp-${r}-eq svg`)) || !w || w[0] < w[1] * 0.9 || hit < 44) phoneEq.push([r, w, hit]);
    if (r === "v3") await shot("6-eq-phone");
  }
  check("6: on a phone each voice's EQ graph spans its column, its dots are 44 px touch targets, the page does not scroll sideways",
    phoneEq.length === 0 && (await ev("document.documentElement.scrollWidth <= innerWidth + 1")), phoneEq);
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await sleep(1000);

  /* reload: the voice back, from the route's patch */
  await open();
  await until(`__fa.features().some(function (f) { return f.properties.id === '${DRAFT}'; })`, 15000);
  await ev(`fsListen.select('${DRAFT}'), 0`); await sleep(900);
  const drawn = await ev("(function(){ var b = document.querySelector('#ls-card #f-patch') || document.querySelector('#f-patch'); b.click(); return document.querySelectorAll('#pp-body .ppcols').length; })()");
  check("opening the panel draws it once (final review 3c I6)", drawn === 1, drawn);
  await until("!document.getElementById('patchpanel').hidden", 5000);
  await until(`__fa.rolesFor === '${DRAFT}'`, 8000);
  const back = await until("(document.querySelector('#pp-body select[data-k=\"v3.synth\"]') || {}).value === 's-freeze' && +(document.querySelector('#pp-body input[data-k=\"v3.focus\"]') || {}).value === 0.77 && /probe-site\\.wav/.test((document.querySelector('#pp-v3-name') || {}).textContent || '')", 10000);
  await ev("fsListen.sound()");
  const ov = await until(`(__fa.coreSentPatch && __fa.coreSentPatch('${DRAFT}') || '').indexOf('s-freeze') >= 0`, 15000);
  check("after a reload, the engine plays the saved sampler voice (final review 3c I3)", ov, await ev(`__fa.coreWasm + ' ' + (__fa.coreSentPatch && (__fa.coreSentPatch('${DRAFT}') || '').slice(0, 80))`));
  await ev("fsListen.sound()"); await sleep(1500);
  check("reloaded: the Third voice Freeze again with its Focus and sample", back, await ev("[(document.querySelector('#pp-body select[data-k=\"v3.synth\"]') || {}).value, (document.querySelector('#pp-v3-name') || {}).textContent]"));

  /* back to digital: written to the live patch as today */
  await setSel("v3.synth", "pluck"); await sleep(2500);
  const page2 = JSON.parse(await livePatch(DRAFT));
  check("6a: a digital choice reaches the patch; the sampler's sample stays in it (Review Focus 5)", page2.v3.synth === "pluck" && !!(page2.v3.sample && page2.v3.sample.path), page2.v3);

  const hmax = await ev("(document.querySelector('#pp-body input[data-k=\"v3.harm\"]') || {}).max");
  await setRange("v3.harm", hmax); await sleep(300);
  const h0 = JSON.parse(await livePatch(DRAFT)).v3.harm;
  await setSel("v3.synth", "s-freeze"); await sleep(400); await setSel("v3.synth", "pluck"); await sleep(400);
  const h1 = JSON.parse(await livePatch(DRAFT)).v3.harm;
  check("a sampler tried and the same digital synth chosen again: its live timbres kept (final review 3c I2)", h1 === h0 && String(h0) === String(+hmax), [hmax, h0, h1]);


  /* the site's sound: core.wasm, the voice's sample heard */
  await setSel("v3.synth", "s-freeze"); await sleep(2500);
  const mid = await ev(`(function(){ var c = __fa.features().filter(function (f) { return f.properties.id === '${DRAFT}'; })[0].geometry.coordinates; return c[Math.floor(c.length / 2)]; })()`);
  await ev("fsListen.sound()");
  for (let i = 0; i < 40 && !(await ev(`!!(window.__fa.core && __fa.core.route_id === '${DRAFT}')`)); i++) { await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await sleep(400); }
  check("6a: the site's engine is core.wasm and the walker is on the draft route", (await ev("__fa.coreWasm")) === "web/core.wasm" && (await ev("__fa.core && __fa.core.route_id")) === DRAFT, [await ev("__fa.coreWasm"), await ev("__fa.core && __fa.core.route_id")]);
  const lvl = async () => { let e = 0; for (let i = 0; i < 16; i++) { await sleep(250); await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); e += Math.pow(10, (await ev("__fa.coreLevel ? __fa.coreLevel() : -120")) / 10); } return 10 * Math.log10(e / 16); };
  /* the Freeze role's own meter (3c.1 F4), not the whole mix: on a route whose other voices are loud (2026-10-07:
     Koşuyolu Parkı, all three on) the Third voice sits ~5 dB under the rest and the mix moves by less than its noise */
  const v3db = async () => { let e = 0; for (let i = 0; i < 12; i++) { await sleep(250); await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); e += Math.pow(10, (+(await ev("(document.querySelector('#pp-v3-meter') || { dataset: {} }).dataset.db || -120"))) / 10); } return 10 * Math.log10(e / 12); };
  await sleep(3000); const withRole = await v3db();
  await setRange("v3.gain", "0"); await sleep(4000); const muted = await v3db();
  await setRange("v3.gain", "0.55");
  check("walking the route, the Freeze role with its sample sounds (its meter above -45 dB; muting it drops it >= 20 dB)", withRole > -45 && withRole > muted + 20, [withRole, muted]);
  /* 6c (Kerem 2026-10-08: "the digital synthesizers a volume meter as well"): a digital voice's meter, live */
  const dmeter = await ev("(function(){ var r = ['voice','sect'].filter(function (k) { var s = document.querySelector('#pp-body select[data-k=\"' + k + '.synth\"]'); return s && !/^s-/.test(s.value); }); return r.map(function (k) { var m = document.querySelector('#pp-' + k + '-meter'); return [k, m ? +m.dataset.db : null]; }); })()");
  let dnotes = ""; for (let i = 0; i < 20 && !dnotes; i++) { dnotes = await ev("['voice','sect'].map(function (k) { var n = document.querySelector('#pp-' + k + '-notes'); return n ? n.textContent : ''; }).join('')"); if (!dnotes) await sleep(250); }
  check("6c: a digital voice names its sounding notes too", /[A-G]/.test(dnotes), dnotes);
  check("6c: a digital voice has the live meter too, moving while it sounds", dmeter.length > 0 && dmeter.every((x) => x[1] !== null) && dmeter.some((x) => x[1] > -60), dmeter);
  const sends0 = await ev("__fa.roleSends");
  for (let i = 0; i < 20; i++) { await setRange("v3.focus", (0.30 + i * 0.01).toFixed(2)); await sleep(30); }
  await sleep(350);
  check("a sampler control reaches the engine at patch-edit speed, before any save (final review 3c I7)", (await ev(`__fa.coreSentPatch('${DRAFT}') || ''`)).indexOf('"focus":0.49') >= 0, await ev(`(__fa.coreSentPatch('${DRAFT}') || '').slice(0, 200)`));
  await sleep(3000);
  check("moving a control and its autosave send no sample to the engine again (final review 3c C1)", (await ev("__fa.roleSends")) === sends0, [sends0, await ev("__fa.roleSends")]);
  /* 6 (Kerem 2026-10-07): an EQ graph on every voice, digital or sampler; octave on a sampler voice */
  const sentP = async () => JSON.parse((await ev(`__fa.coreSentPatch('${DRAFT}') || 'null'`)) || "null");
  const eqAll = []; for (const r of ["voice", "sect", "v3"]) { await eqOpen(r); eqAll.push(await ev(`!!document.querySelector('#pp-${r}-eq svg')`)); }
  check("6a: every voice has an EQ, behind a button in its instrument row - its graph opens over the column", eqAll.every(Boolean), eqAll);
  check("6a: Escape closes the EQ, and only the EQ (the route stays selected, its panel open)", await ev(`(function(){ document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })); return !document.getElementById('pp-eq-pop') && !document.getElementById('patchpanel').hidden; })()`), null);
  await eqOpen("sect");
  const curve0 = await ev("(document.querySelector('#pp-sect-eq path.eq-curve') || {}).getAttribute ? document.querySelector('#pp-sect-eq path.eq-curve').getAttribute('d') : ''");
  await ev("(function(){ var d = document.querySelector('#pp-sect-eq [data-band=\"2\"]'); d.focus(); for (var i = 0; i < 4; i++) d.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowUp', bubbles: true })); return 1; })()");
  await sleep(400);
  let sp = await sentP();
  check("6: arrow keys on a digital voice's EQ band raise it (+2 dB) and the engine gets it at once", !!(sp && sp.sect && sp.sect.eq && sp.sect.eq[2][1] === 2 && sp.sect.synth !== undefined && !/^s-/.test(sp.sect.synth)), sp && sp.sect);
  check("6: the curve is redrawn", curve0 !== "" && curve0 !== (await ev("document.querySelector('#pp-sect-eq path.eq-curve').getAttribute('d')")), curve0.slice(0, 40));
  /* a drag: a band's dot pulled up 20 px reads a positive gain */
  await eqOpen("v3");
  await ev(`(function(){ var d = document.querySelector('#pp-v3-eq [data-band="4"]'), r = d.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2;
    var o = function (t, yy) { return new PointerEvent(t, { pointerId: 1, clientX: x, clientY: yy, bubbles: true, isPrimary: true, button: 0, buttons: 1 }); };
    d.dispatchEvent(o('pointerdown', y)); d.dispatchEvent(o('pointermove', y - 10)); d.dispatchEvent(o('pointermove', y - 20)); d.dispatchEvent(o('pointerup', y - 20)); return 1; })()`);
  await sleep(400); sp = await sentP();
  check("6: dragging a sampler voice's EQ dot up raises that band", !!(sp && sp.v3 && sp.v3.eq && sp.v3.eq[4][1] > 1), sp && sp.v3 && sp.v3.eq);
  /* final review 6 I6: the sound follows a drag while it moves - moves every 30 ms, the engine has the band before the pointer lets go */
  await eqOpen("voice");
  await ev(`(function(){ var d = document.querySelector('#pp-voice-eq [data-band="1"]'), r = d.getBoundingClientRect(); window.__dragY = r.top + r.height / 2; window.__dragX = r.left + r.width / 2;
    d.dispatchEvent(new PointerEvent('pointerdown', { pointerId: 2, clientX: __dragX, clientY: __dragY, bubbles: true, isPrimary: true, button: 0, buttons: 1 })); return 1; })()`);
  for (let i = 1; i <= 12; i++) { await ev(`document.querySelector('#pp-voice-eq [data-band="1"]').dispatchEvent(new PointerEvent('pointermove', { pointerId: 2, clientX: __dragX, clientY: __dragY - ${i * 2}, bubbles: true, buttons: 1 })), 0`); await sleep(30); }
  sp = await sentP();
  const mid6 = sp && sp.voice && sp.voice.eq ? sp.voice.eq[1][1] : null;
  await ev("document.querySelector('#pp-voice-eq [data-band=\"1\"]').dispatchEvent(new PointerEvent('pointerup', { pointerId: 2, bubbles: true })), 0");
  check("6: mid-drag, the engine already has the band moving (final review 6 I6)", mid6 > 0, sp && sp.voice && sp.voice.eq);
  await setRange("v3.octave", "1"); await sleep(400); sp = await sentP();
  check("6: a sampler voice's octave reaches the engine", !!(sp && sp.v3 && sp.v3.sampler && sp.v3.sampler.octave === 1), sp && sp.v3 && sp.v3.sampler);
  await sleep(2500);
  const p6 = JSON.parse(await livePatch(DRAFT));
  check("6a: EQ and octave are saved in the route's patch", !!(p6.sect.eq && p6.sect.eq[2][1] === 2 && p6.v3.sampler.octave === 1 && p6.v3.eq[4][1] > 1), [p6.sect.eq, p6.v3.sampler]);
  await eqOpen("sect");
  await ev("(function(){ var d = document.querySelector('#pp-sect-eq [data-band=\"2\"]'); d.dispatchEvent(new MouseEvent('dblclick', { bubbles: true })); return 1; })()");
  await sleep(400); sp = await sentP();
  check("6: double-click puts a band back to 0 dB", !!(sp && sp.sect && sp.sect.eq && sp.sect.eq[2][1] === 0), sp && sp.sect && sp.sect.eq);
  await setRange("v3.octave", "0");
  /* 6: the graphs sit inside their columns, on a desk and on a phone; screenshots with SHOTS=dir */
  const eqIn = async () => { for (const r of ["voice", "sect", "v3"]) { await eqOpen(r); if (!(await inCol(`#pp-${r}-eq svg`))) return r; } return null; };
  await eqOpen("sect");
  await ev("document.querySelector('#pp-sect-eq').scrollIntoView({ block: 'center' }), 0"); await sleep(300);
  check("6: on a desk every EQ graph is inside its column", (await eqIn()) === null, await eqIn());
  await shot("6-eq-desk");

  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await sleep(800);
  /* 3c.1: bird-like samples on every role - every non-pitch sampler sounds (the role alone, gain 0.8) */
  const R = ["voice", "sect", "v3"];
  for (const [role, kind, name] of [["voice", "noise", "probe-titmouse.wav"], ["sect", "tone", "probe-blackbird.wav"], ["v3", "tone", "probe-blackbird.wav"]]) {
    await setSel(role + ".synth", "s-resonator"); await sleep(500);
    await ev(`(function(){ var f = ${birdish(kind, name)}, dt = new DataTransfer(); dt.items.add(f); var i = document.querySelector('#pp-${role}-file'); i.files = dt.files; i.dispatchEvent(new Event('change')); return 1; })()`);
    await until(`/uploaded|failed/.test((document.querySelector('#pp-${role}-name') || {}).textContent || '')`, 20000);
  }
  await sleep(2500);
  const quiet = {}, heard = {};
  for (const role of R) {
    for (const o of R) await setRange(o + ".gain", o === role ? "0.8" : "0");
    for (const syn of ["s-resonator", "s-harmonic", "s-formant", "s-pulsar", "s-freeze"]) {
      await setSel(role + ".synth", syn); await sleep(4000);
      const l = await lvl(); heard[role + " " + syn] = +l.toFixed(1); if (!(l > -45)) quiet[role + " " + syn] = +l.toFixed(1);
    }
  }
  console.log("bird-like levels", JSON.stringify(heard));
  check("bird-like samples (sparse calls, high band): every sampler sounds on every role (role alone, above -45 dB)", Object.keys(quiet).length === 0, quiet);
  for (const o of R) await setRange(o + ".gain", "0.55");
  await setSel("v3.synth", "s-freeze"); await sleep(3000);
  check("the live row: a sounding sampler role shows its notes and a meter above -60 dB",
    /[A-G]#?-?\d/.test(await ev("(document.querySelector('#pp-v3-notes') || {}).textContent || ''")) && (await ev("+((document.querySelector('#pp-v3-meter') || { dataset: {} }).dataset.db)")) > -60,
    [await ev("(document.querySelector('#pp-v3-notes') || {}).textContent"), await ev("(document.querySelector('#pp-v3-meter') || { dataset: {} }).dataset.db")]);
  await setRange("v3.gain", "0"); await sleep(2500);
  check("... muted, its meter falls", (await ev("+((document.querySelector('#pp-v3-meter') || { dataset: {} }).dataset.db)")) < -70, await ev("(document.querySelector('#pp-v3-meter') || { dataset: {} }).dataset.db"));
  await setRange("v3.gain", "0.55"); await sleep(1500);

  await ev("fsListen.sound()"); await sleep(1500);
  check("Stop clears the live notes and meters (final review 3c.1)", !(await ev("/[A-G]/.test((document.querySelector('#pp-v3-notes') || {}).textContent || '')")) && (await ev("+((document.querySelector('#pp-v3-meter') || { dataset: {} }).dataset.db)")) <= -120, [await ev("(document.querySelector('#pp-v3-notes') || {}).textContent"), await ev("(document.querySelector('#pp-v3-meter') || { dataset: {} }).dataset.db")]);
  if (!PUB2) { console.log("SKIP every published route has a sampler voice: the no-sampler-route checks need one without"); } else {
  await ev("fsListen.sound()"); await sleep(1500);    /* on: the walker moves between routes */
  /* a route without sampler keys: the engine is given its own patch */
  const pm = await ev(`(function(){ var c = __fa.features().filter(function (f) { return f.properties.id === '${PUB2}'; })[0].geometry.coordinates; return c[Math.floor(c.length / 2)]; })()`);
  for (let i = 0; i < 40 && !(await ev(`!!(window.__fa.core && __fa.core.route_id === '${PUB2}')`)); i++) { await ev(`__fa.walkTo(${pm[0]}, ${pm[1]})`); await sleep(400); }
  check("6a: on a route without sampler keys, the engine gets its own patch as it is", await ev(`__fa.coreSentPatch('${PUB2}') === JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === '${PUB2}'; })[0].properties.patch)`), await ev(`__fa.coreSentPatch && __fa.coreSentPatch('${PUB2}') && __fa.coreSentPatch('${PUB2}').slice(0, 120)`));
  check("walked onto another route: the draft's live notes are not left on screen", !(await ev("/[A-G]#?-?\\d/.test((document.querySelector('#pp-v3-notes') || {}).textContent || '')")), await ev("(document.querySelector('#pp-v3-notes') || {}).textContent"));
  check("... and the walker is on it (its own samples, none: nothing from the draft carried over)", (await ev("__fa.core && __fa.core.route_id")) === PUB2 && (await ev("__fa.labSentFor")) === PUB2, [await ev("__fa.core && __fa.core.route_id"), await ev("__fa.labSentFor")]);
  }
  await ev("fsListen.sound()"); await sleep(1500);    /* off */

  /* Review Focus 5: a voice set back to digital asks for no sample */
  await openPanel(DRAFT); await until(`__fa.rolesFor === '${DRAFT}'`, 8000);
  await ev("fsListen.sound()");
  for (let i = 0; i < 40 && !(await ev(`!!(window.__fa.core && __fa.core.route_id === '${DRAFT}')`)); i++) { await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await sleep(400); }
  await setSel("v3.synth", "pluck"); await sleep(1500);
  const rs5 = await ev("__fa.roleSends");
  for (let i = 0; i < 8; i++) { await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await sleep(500); }
  check("6a: a voice back on a digital synth: no sample sent, the engine's patch has the digital synth (Review Focus 5)", (await ev("__fa.roleSends")) === rs5 && (await sentP()).v3.synth === "pluck", [rs5, await ev("__fa.roleSends"), (await sentP()).v3.synth]);

  /* Review Focus 3: a sample that does not download - the voice is silent, the walk plays on */
  await setSel("v3.synth", "s-freeze"); await sleep(1500);
  const good = JSON.parse(await livePatch(DRAFT)).v3.sample;
  await ev(`__fa.patchEdit('${DRAFT}', function (p) { p.v3.sample = { path: '${DRAFT}/v3-missing.wav', name: 'gone.wav', analysis_path: '${DRAFT}/v3-missing.json', f0: 0 }; }), 0`);
  await ev("__fa.reloadSession(), 0"); await sleep(4000);
  const lv3 = []; for (let i = 0; i < 8; i++) { await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await sleep(500); lv3.push(await ev("(__fa.coreRoles() || [])[16]")); }
  check("6a: a sample that will not download leaves its voice silent and the walk playing (Review Focus 3)", Math.max(...lv3) < -60 && (await ev("__fa.coreLevel()")) > -60, [lv3, await ev("__fa.coreLevel()")]);
  await ev(`(function(){ var g = ${'${JSON.stringify(good)}'}; __fa.patchEdit('${DRAFT}', function (p) { p.v3.sample = g; }); return 1; })()`);
  await ev("fsListen.sound()"); await sleep(1500);    /* off */

  /* the listener: the draft published (as Publish writes it), a signed-out page with nothing on the device */
  const props = JSON.parse(await ev(`JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === '${DRAFT}'; })[0].properties)`));
  props.published = true;
  const pub = await db.from("features").update({ properties: props }).eq("id", DRAFT);
  check("the draft published with its patch", !pub.error, pub.error);
  await ev("__fa.sb.auth.signOut({ scope: 'local' }).then(function () { localStorage.clear(); return 1; })"); await sleep(500);
  await open();
  await until(`__fa.features().some(function (f) { return f.properties.id === '${DRAFT}'; })`, 20000);
  await ev("fsListen.sound()");
  for (let i = 0; i < 60 && !((await ev("__fa.roleSends || 0")) > 0 && (await ev("(__fa.coreRoles() || [])[16]")) > -60); i++) { await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await sleep(500); }
  check("6a: a signed-out listener's engine gets the route's sampler sample and plays it (Third voice above -60 dB)", (await ev("__fa.roleSends")) > 0 && (await ev("(__fa.coreRoles() || [])[16]")) > -60, [await ev("__fa.roleSends"), await ev("JSON.stringify(__fa.coreRoles())")]);
  check("6a: the listener's engine is the public core.wasm", (await ev("__fa.coreWasm")) === "web/core.wasm", await ev("__fa.coreWasm"));
  await ev("fsListen.sound()"); await sleep(1000);
  check("no page errors", errors.length === 0, errors);
} finally {
  ch.kill();
  const { data: objs } = await db.storage.from("recordings").list(`lab/${DRAFT}`);
  if (objs && objs.length) await db.storage.from("recordings").remove(objs.map((o) => `lab/${DRAFT}/${o.name}`));
  const { data: own } = await db.storage.from("recordings").list(DRAFT);
  if (own && own.length) await db.storage.from("recordings").remove(own.map((o) => `${DRAFT}/${o.name}`));
  await db.from("lab_route_roles").delete().eq("route_id", DRAFT);
  await db.from("features").delete().eq("id", DRAFT);
  await db.from("audit").delete().eq("setter_id", userId);
  await db.from("setters").delete().eq("id", userId);
  if (userId) await db.auth.admin.deleteUser(userId);
}
console.log(failed ? `${failed} FAILED` : "all passed");
process.exit(failed ? 1 : 0);
