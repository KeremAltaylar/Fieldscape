/* Sample harmony 3c in the browser: the site's own patch panel in lab mode - a setter sets a route role to a sampler with
   its controls and sample, saved apart from the live patch, and the site's engine plays it
   (docs/superpowers/specs/2026-10-05-samples-3c-patch-screen-design.md). A probe setter and an unpublished test route are
   made with the service key and removed after.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node --env-file=.env.local core/tests/web_site_lab.mjs */
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
/* a published route with no lab setup (a setter may have saved one on any route, e.g. in 3b) */
const labbed = new Set(((await db.from("lab_route_roles").select("route_id")).data || []).map((r) => r.route_id));
const PUB2 = ((await db.from("features").select("id").eq("kind", "route").is("deleted_at", null).filter("properties->>published", "eq", "true")).data || []).map((r) => r.id).find((id) => !labbed.has(id));
await db.from("features").delete().eq("id", DRAFT);
/* 5: a published point with its own recording (the stretched bed), for the follow control; its lab row removed after */
const PT = ((await db.from("features").select("id, geometry, properties").eq("kind", "point").is("deleted_at", null)
  .filter("properties->>published", "eq", "true").filter("properties->>has_audio", "eq", "true")).data || [])
  .find((r) => !["hits", "grains"].includes(r.properties.audio_mode)) || null;
const ptHadRow = PT ? !!(await db.from("lab_route_roles").select("route_id").eq("route_id", PT.id).maybeSingle()).data : true;
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

  /* signed out: no switch, nothing asked of the lab */
  await open();
  check("signed out: no Samplers (lab) switch shown", !(await ev("(function(){ var b = document.querySelector('#pp-lab'); return !!(b && !b.hidden); })()")), null);
  check("signed out: nothing asked of lab_route_roles, core-lab.wasm never fetched", !reqs.some((r) => /lab_route_roles|core-lab\.wasm/.test(r)), reqs.filter((r) => /lab/.test(r)));

  /* the setter's draft on this device */
  await ev(`(function(){ var k = 'fieldarc.v1', fc = JSON.parse(localStorage.getItem(k) || '{"type":"FeatureCollection","features":[]}'); fc.features = fc.features.filter(function (f) { return f.properties.id !== '${DRAFT}'; }); fc.features.push(${JSON.stringify(draftFeature)}); localStorage.setItem(k, JSON.stringify(fc)); return 1; })()`);
  await open();

  /* signed in, lab mode off: the panel as before */
  const si = await ev(`__fa.sb.auth.signInWithPassword({ email: ${JSON.stringify(EMAIL)}, password: ${JSON.stringify(PASSWORD)} }).then(function (r) { return r.error ? r.error.message : "ok"; })`);
  const has = await until(`__fa.features().some(function (f) { return f.properties.id === '${DRAFT}'; })`, 15000);
  check("signed in as a setter: the draft route is on the map", si === "ok" && has, si);
  await openPanel(DRAFT);
  const rowsOff = await ev(rowsKey);
  check("signed in: the Samplers (lab) switch is in the panel header, off", await ev("(function(){ var b = document.querySelector('#pp-lab'); return !!b && !b.hidden && b.getAttribute('aria-pressed') === 'false'; })()"), null);
  check("lab mode off: no Sampler group in any instrument menu", await ev("!document.querySelector(\"#pp-body optgroup[label='Sampler']\")"), null);

  /* lab mode on: Digital and Sampler groups; Freeze shows its rows, hides harm/index */
  const page0 = await livePatch(DRAFT);
  await ev("document.querySelector('#pp-lab').click(), 0");
  await until(`__fa.labRolesFor === '${DRAFT}'`, 8000);
  check("lab mode on: each role's instrument menu has Digital and Sampler groups",
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
  const row = (await db.from("lab_route_roles").select("roles").eq("route_id", DRAFT).maybeSingle()).data;
  check("autosaved: the lab row has Freeze, Focus 0.77 and the sample", !!(row && row.roles.v3.synth === "s-freeze" && row.roles.v3.sampler.focus === 0.77 && row.roles.v3.sample && row.roles.v3.sample.path.indexOf(`lab/${DRAFT}/v3-`) === 0), row);
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
  await openPanel(DRAFT); await until(`__fa.labRolesFor === '${DRAFT}'`, 8000); await sleep(1500);
  await ev("(function(){ var t = document.querySelector(\"#pp-tabs [data-tab='voices']\"); t && t.click(); var v = document.querySelector(\"#pp-sub [data-sub='v3']\"); v && v.click(); return 1; })()"); await sleep(500);
  const phone = await ev("(function(){ var b = document.querySelector('.ppsample[data-role=v3]'), c = b && b.closest('.ppcol'), s = document.querySelector('#pp-body select[data-k=\"v3.synth\"]'); return b && c && s ? { block: Math.round(b.getBoundingClientRect().width), col: Math.round(c.getBoundingClientRect().width), sel: Math.round(s.getBoundingClientRect().width), vw: innerWidth } : null; })()");
  check("phone width: the sample block fits its column, spans it, and the instrument menu stays usable", (await inCol("#pp-v3-name")) && (await inCol("#pp-v3-wave")) && (await ev("document.documentElement.scrollWidth <= innerWidth + 1")) && !!phone && phone.col > phone.vw * 0.8 && phone.block >= phone.col * 0.95 && phone.sel > 120, phone);
  /* 6: each voice's EQ graph on a phone, in its own tab */
  const phoneEq = [];
  for (const [sub, r] of [["v1", "voice"], ["v2", "sect"], ["v3", "v3"]]) {
    await ev(`(function(){ var t = document.querySelector("#pp-tabs [data-tab='voices']"); t && t.click(); var v = document.querySelector("#pp-sub [data-sub='${sub}']"); v && v.click(); return 1; })()`); await sleep(500);
    await ev(`document.querySelector('#pp-${r}-eq').scrollIntoView({ block: 'center' }), 0`); await sleep(300);
    const w = await ev(`(function(){ var e = document.querySelector('#pp-${r}-eq svg'), c = e && e.closest('.ppcol'); return e && c ? [Math.round(e.getBoundingClientRect().width), Math.round(c.getBoundingClientRect().width)] : null; })()`);
    const hit = await ev(`Math.round(document.querySelector('#pp-${r}-eq .eq-hit').getBoundingClientRect().width)`);   /* final review 6: a 44 px touch target */
    if (!(await inCol(`#pp-${r}-eq svg`)) || !w || w[0] < w[1] * 0.9 || hit < 44) phoneEq.push([r, w, hit]);
    if (r === "v3") await shot("6-eq-phone");
  }
  check("6: on a phone each voice's EQ graph spans its column, its dots are 44 px touch targets, the page does not scroll sideways",
    phoneEq.length === 0 && (await ev("document.documentElement.scrollWidth <= innerWidth + 1")), phoneEq);
  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await sleep(1000);
  const page1 = await livePatch(DRAFT), dbPatch1 = JSON.stringify((await db.from("features").select("properties").eq("id", DRAFT).single()).data.properties.patch);
  check("the live patch is unchanged byte for byte, on the page and in the database", page1 === page0 && dbPatch1 === dbPatch0 && !/"s-/.test(page1), [JSON.parse(page0).v3, JSON.parse(page1).v3]);

  /* reload: lab mode remembered, the role back */
  await open();
  await until(`__fa.features().some(function (f) { return f.properties.id === '${DRAFT}'; })`, 15000);
  await ev(`fsListen.select('${DRAFT}'), 0`); await sleep(900);
  const drawn = await ev("(function(){ var b = document.querySelector('#ls-card #f-patch') || document.querySelector('#f-patch'); b.click(); return document.querySelectorAll('#pp-body .ppcols').length; })()");
  check("opening the panel in lab mode draws it once (final review 3c I6)", drawn === 1, drawn);
  await until("!document.getElementById('patchpanel').hidden", 5000);
  await until(`__fa.labRolesFor === '${DRAFT}'`, 8000);
  const back = await until("(document.querySelector('#pp-body select[data-k=\"v3.synth\"]') || {}).value === 's-freeze' && +(document.querySelector('#pp-body input[data-k=\"v3.focus\"]') || {}).value === 0.77 && /probe-site\\.wav/.test((document.querySelector('#pp-v3-name') || {}).textContent || '')", 10000);
  await ev("fsListen.sound()");
  const ov = await until(`(__fa.coreSentPatch && __fa.coreSentPatch('${DRAFT}') || '').indexOf('s-freeze') >= 0`, 15000);
  check("after a reload with lab mode remembered, the engine plays the saved sampler role, untouched (final review 3c I3)", ov, await ev(`__fa.coreWasm + ' ' + (__fa.coreSentPatch && (__fa.coreSentPatch('${DRAFT}') || '').slice(0, 80))`));
  await ev("fsListen.sound()"); await sleep(1500);
  check("reloaded: lab mode still on, the Third voice Freeze again with its Focus and sample", back, await ev("[(document.querySelector('#pp-body select[data-k=\"v3.synth\"]') || {}).value, (document.querySelector('#pp-v3-name') || {}).textContent]"));

  /* back to digital: written to the live patch as today */
  await setSel("v3.synth", "pluck"); await sleep(2500);
  const page2 = JSON.parse(await livePatch(DRAFT));
  const row2 = (await db.from("lab_route_roles").select("roles").eq("route_id", DRAFT).maybeSingle()).data;
  check("a digital choice in lab mode reaches the live patch, and the lab row follows (sample kept)", page2.v3.synth === "pluck" && !!(row2 && row2.roles.v3.synth === "pluck" && row2.roles.v3.sample), [page2.v3.synth, row2 && row2.roles.v3]);

  const hmax = await ev("(document.querySelector('#pp-body input[data-k=\"v3.harm\"]') || {}).max");
  await setRange("v3.harm", hmax); await sleep(300);
  const h0 = JSON.parse(await livePatch(DRAFT)).v3.harm;
  await setSel("v3.synth", "s-freeze"); await sleep(400); await setSel("v3.synth", "pluck"); await sleep(400);
  const h1 = JSON.parse(await livePatch(DRAFT)).v3.harm;
  check("a sampler tried and the same digital synth chosen again: its live timbres kept (final review 3c I2)", h1 === h0 && String(h0) === String(+hmax), [hmax, h0, h1]);

  /* lab mode off: the panel exactly as before */
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(600);
  const rowsOff2 = await ev(rowsKey);
  const noV3T = (r) => r.replace(/[^;]*\|v3\.(harm|index)/g, "");     /* the Third voice is Pluck now: its two timbres are named for it */
  check("lab mode off: no live rows, no sample canvases", await ev("!document.querySelector('[id$=\"-notes\"], .ppsample-wave')"), null);
  check("lab mode off again: the panel's rows are exactly those before", noV3T(rowsOff2) === noV3T(rowsOff), [rowsOff, rowsOff2]);

  /* the site's sound in lab mode: core-lab.wasm, the role's sample heard; lab mode off: core.wasm */
  await ev("document.querySelector('#pp-lab').click(), 0"); await until(`__fa.labRolesFor === '${DRAFT}'`, 8000);
  await setSel("v3.synth", "s-freeze"); await sleep(2500);
  const mid = await ev(`(function(){ var c = __fa.features().filter(function (f) { return f.properties.id === '${DRAFT}'; })[0].geometry.coordinates; return c[Math.floor(c.length / 2)]; })()`);
  await ev("fsListen.sound()");
  for (let i = 0; i < 40 && !(await ev(`!!(window.__fa.core && __fa.core.route_id === '${DRAFT}')`)); i++) { await ev(`__fa.walkTo(${mid[0]}, ${mid[1]})`); await sleep(400); }
  check("lab mode on: the site's engine is core-lab.wasm and the walker is on the draft route", (await ev("__fa.coreWasm")) === "web/core-lab.wasm" && (await ev("__fa.core && __fa.core.route_id")) === DRAFT, [await ev("__fa.coreWasm"), await ev("__fa.core && __fa.core.route_id")]);
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
  const sends0 = await ev("__fa.labRoleSends");
  for (let i = 0; i < 20; i++) { await setRange("v3.focus", (0.30 + i * 0.01).toFixed(2)); await sleep(30); }
  await sleep(350);
  check("a sampler control reaches the engine at patch-edit speed, before any save (final review 3c I7)", (await ev(`__fa.coreSentPatch('${DRAFT}') || ''`)).indexOf('"focus":0.49') >= 0, await ev(`(__fa.coreSentPatch('${DRAFT}') || '').slice(0, 200)`));
  await sleep(3000);
  check("moving a control and its autosave send no sample to the engine again (final review 3c C1)", (await ev("__fa.labRoleSends")) === sends0, [sends0, await ev("__fa.labRoleSends")]);
  /* 6 (Kerem 2026-10-07): an EQ graph on every voice, digital or sampler; octave on a sampler voice */
  const sentP = async () => JSON.parse((await ev(`__fa.coreSentPatch('${DRAFT}') || 'null'`)) || "null");
  check("6: every voice has an EQ graph in lab mode, the digital ones too", await ev("['voice','sect','v3'].every(function (r) { return !!document.querySelector('#pp-' + r + '-eq svg'); })"),
    await ev("['voice','sect','v3'].map(function (r) { return !!document.querySelector('#pp-' + r + '-eq svg'); })"));
  const curve0 = await ev("(document.querySelector('#pp-sect-eq path.eq-curve') || {}).getAttribute ? document.querySelector('#pp-sect-eq path.eq-curve').getAttribute('d') : ''");
  await ev("(function(){ var d = document.querySelector('#pp-sect-eq [data-band=\"2\"]'); d.focus(); for (var i = 0; i < 4; i++) d.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowUp', bubbles: true })); return 1; })()");
  await sleep(400);
  let sp = await sentP();
  check("6: arrow keys on a digital voice's EQ band raise it (+2 dB) and the engine gets it at once", !!(sp && sp.sect && sp.sect.eq && sp.sect.eq[2][1] === 2 && sp.sect.synth !== undefined && !/^s-/.test(sp.sect.synth)), sp && sp.sect);
  check("6: the curve is redrawn", curve0 !== "" && curve0 !== (await ev("document.querySelector('#pp-sect-eq path.eq-curve').getAttribute('d')")), curve0.slice(0, 40));
  /* a drag: a band's dot pulled up 20 px reads a positive gain */
  await ev(`(function(){ var d = document.querySelector('#pp-v3-eq [data-band="4"]'), r = d.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2;
    var o = function (t, yy) { return new PointerEvent(t, { pointerId: 1, clientX: x, clientY: yy, bubbles: true, isPrimary: true, button: 0, buttons: 1 }); };
    d.dispatchEvent(o('pointerdown', y)); d.dispatchEvent(o('pointermove', y - 10)); d.dispatchEvent(o('pointermove', y - 20)); d.dispatchEvent(o('pointerup', y - 20)); return 1; })()`);
  await sleep(400); sp = await sentP();
  check("6: dragging a sampler voice's EQ dot up raises that band", !!(sp && sp.v3 && sp.v3.eq && sp.v3.eq[4][1] > 1), sp && sp.v3 && sp.v3.eq);
  /* final review 6 I6: the sound follows a drag while it moves - moves every 30 ms, the engine has the band before the pointer lets go */
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
  const r6 = (await db.from("lab_route_roles").select("roles").eq("route_id", DRAFT).maybeSingle()).data;
  check("6: EQ and octave are saved with the route's lab setup", !!(r6 && r6.roles.sect.eq && r6.roles.sect.eq[2][1] === 2 && r6.roles.v3.sampler.octave === 1 && r6.roles.v3.eq[4][1] > 1), r6 && [r6.roles.sect.eq, r6.roles.v3.sampler]);
  await ev("(function(){ var d = document.querySelector('#pp-sect-eq [data-band=\"2\"]'); d.dispatchEvent(new MouseEvent('dblclick', { bubbles: true })); return 1; })()");
  await sleep(400); sp = await sentP();
  check("6: double-click puts a band back to 0 dB", !!(sp && sp.sect && sp.sect.eq && sp.sect.eq[2][1] === 0), sp && sp.sect && sp.sect.eq);
  await setRange("v3.octave", "0");
  /* 6: the graphs sit inside their columns, on a desk and on a phone; screenshots with SHOTS=dir */
  const eqIn = async () => { for (const r of ["voice", "sect", "v3"]) if (!(await inCol(`#pp-${r}-eq svg`))) return r; return null; };
  await ev("document.querySelector('#pp-sect-eq').scrollIntoView({ block: 'center' }), 0"); await sleep(300);
  check("6: on a desk every EQ graph is inside its column", (await eqIn()) === null, await eqIn());
  await shot("6-eq-desk");

  await s.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false }); await sleep(800);
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(5000);
  check("lab mode off while sound plays: the engine is core.wasm again, still sounding", (await ev("__fa.coreWasm")) === "web/core.wasm" && (await ev("__fa.coreLevel()")) > -60, [await ev("__fa.coreWasm"), await ev("__fa.coreLevel()")]);
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(5000);
  check("... and on again: core-lab.wasm, one engine", (await ev("__fa.coreWasm")) === "web/core-lab.wasm" && (await ev("__fa.coreNodes")) === 1, [await ev("__fa.coreWasm"), await ev("__fa.coreNodes")]);
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
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(500);
  await ev("(function(){ fsListen.sound(); document.querySelector('#pp-lab').click(); return 0; })()");
  await sleep(6000);
  check("lab mode turned while the engine loads: the build lab mode wants, one engine (final review 3c I4)", (await ev("__fa.coreWasm")) === "web/core-lab.wasm" && (await ev("__fa.coreNodes")) === 1, [await ev("__fa.coreWasm"), await ev("__fa.coreNodes")]);
  if (!PUB2) { console.log("SKIP every published route has a lab setup: the no-lab-row checks need one without"); } else {
  /* a route with no lab row: the engine is given its live patch (Review Focus 1, 5) */
  const pm = await ev(`(function(){ var c = __fa.features().filter(function (f) { return f.properties.id === '${PUB2}'; })[0].geometry.coordinates; return c[Math.floor(c.length / 2)]; })()`);
  for (let i = 0; i < 40 && !(await ev(`!!(window.__fa.core && __fa.core.route_id === '${PUB2}')`)); i++) { await ev(`__fa.walkTo(${pm[0]}, ${pm[1]})`); await sleep(400); }
  check("on a route with no lab row, the engine's patch is the live one", await ev(`__fa.coreSentPatch('${PUB2}') === JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === '${PUB2}'; })[0].properties.patch)`), await ev(`__fa.coreSentPatch && __fa.coreSentPatch('${PUB2}') && __fa.coreSentPatch('${PUB2}').slice(0, 120)`));
  check("walked onto another route: the draft's live notes are not left on screen", !(await ev("/[A-G]#?-?\\d/.test((document.querySelector('#pp-v3-notes') || {}).textContent || '')")), await ev("(document.querySelector('#pp-v3-notes') || {}).textContent"));
  check("... and the walker is on it (its own samples, none: nothing from the draft carried over)", (await ev("__fa.core && __fa.core.route_id")) === PUB2 && (await ev("__fa.labSentFor")) === PUB2, [await ev("__fa.core && __fa.core.route_id"), await ev("__fa.labSentFor")]);
  }
  await ev("fsListen.sound()"); await sleep(1500);    /* off */
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(500);
  await s.send("Network.setCacheDisabled", { cacheDisabled: true });       /* the wasm is cached (and the service worker */
  await s.send("Network.setBypassServiceWorker", { bypass: true });         /* answers from its own): the request must really go out */
  await s.send("Fetch.enable", { patterns: [{ urlPattern: "*core-lab.wasm*", requestStage: "Request" }] });
  await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(300);
  await ev("fsListen.sound()"); await sleep(6000);
  check("core-lab.wasm failing: the site stays on core.wasm and says so (final review 3c I5)", (await ev("__fa.coreWasm")) === "web/core.wasm" && (await ev("!!(window.core !== null)")) && /lab engine did not load/.test(await ev("(document.querySelector('#pp-lab-msg') || {}).textContent || ''")), [await ev("__fa.coreWasm"), await ev("(document.querySelector('#pp-lab-msg') || {}).textContent")]);
  await s.send("Fetch.disable"); await s.send("Network.setCacheDisabled", { cacheDisabled: false }); await s.send("Network.setBypassServiceWorker", { bypass: false });
  await ev("fsListen.sound()"); await sleep(1500);    /* off */

  /* 5: a point's follow (lab) - shown, saved under the point, overlaid for the engine; its recording's pitch track sent */
  if (!PT) { console.log("SKIP no published point with a recording"); } else {
    /* the step before failed core-lab.wasm on purpose: lab mode off and on clears that, then sound on the lab engine */
    await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(300); await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(500);
    await ev("fsListen.sound()"); await sleep(1500);
    await ev(`fsListen.select('${PT.id}'), 0`); await sleep(900);
    await ev("(function(){ var b = document.querySelector('#ls-card #f-rhythm') || document.querySelector('#f-rhythm'); b && b.click(); return 0; })()");
    await until("!!document.querySelector('#rp-lab-follow')", 6000);
    check("5: a point's panel in lab mode has follow the chord (lab)", await ev("!!document.querySelector('#rp-lab-follow')"), null);
    await ev("(function(){ var s = document.querySelector('#rp-lab-follow'); s.value = '0.8'; s.dispatchEvent(new Event('input')); return 1; })()");
    await sleep(2500);
    const prow = (await db.from("lab_route_roles").select("roles").eq("route_id", PT.id).maybeSingle()).data;
    const sent1 = await ev(`__fa.coreSentFeature ? __fa.coreSentFeature('${PT.id}') : null`);
    check("5: follow is saved under the point and overlaid for the engine (sound.shape.follow, rhythm.follow)",
      !!(prow && prow.roles.point && prow.roles.point.follow === 0.8) && !!sent1 && JSON.parse(sent1).properties.sound.shape.follow === 0.8 && JSON.parse(sent1).properties.rhythm.follow === 0.8,
      [prow && prow.roles, sent1 && JSON.parse(sent1).properties.sound]);
    check("5: the point's own row untouched (no follow in its saved properties)", !("follow" in (((PT.properties.sound || {}).shape) || {})), null);
    const pc = PT.geometry.coordinates;
    for (let i = 0; i < 40 && !((await ev("__fa.labTracks || 0")) > 0); i++) { await ev(`__fa.walkTo(${pc[0]}, ${pc[1]})`); await sleep(500); }
    check("5: walking to it, its recording's pitch track goes to the engine", (await ev("__fa.labTracks || 0")) > 0, await ev("__fa.labTracks"));
    /* final review 5 I3: a new engine (lab off and on) gets the track again, without analysing the recording again */
    const an0 = await ev("__fa.labAnalyses"), tk0 = await ev("__fa.labTracks");
    await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(4000);
    await ev("document.querySelector('#pp-lab').click(), 0"); await sleep(3000);
    for (let i = 0; i < 40 && !((await ev("__fa.labTracks")) > tk0); i++) { await ev(`__fa.walkTo(${pc[0]}, ${pc[1]})`); await sleep(500); }
    check("5: a new engine gets the track again from the cache, no second analysis (final review 5 I3)",
      (await ev("__fa.labTracks")) > tk0 && an0 > 0 && (await ev("__fa.labAnalyses")) <= (await ev("__fa.labTrackKeys()")),
      [an0, await ev("__fa.labAnalyses"), await ev("__fa.labTrackKeys()"), tk0, await ev("__fa.labTracks")]);
    check("5: no recording's analysis failed", !(await ev("__fa.labTrackErrors")), await ev("__fa.labTrackErrors"));
    await ev("(function(){ var s = document.querySelector('#rp-lab-follow'); s.value = '0'; s.dispatchEvent(new Event('input')); return 1; })()");
    await sleep(2500);
    check("5: follow 0 - the engine gets the point exactly as saved", await ev(`__fa.coreSentFeature('${PT.id}') === JSON.stringify(__fa.features().filter(function (f) { return f.properties.id === '${PT.id}'; })[0])`), await ev(`(__fa.coreSentFeature('${PT.id}') || '').slice(0, 80)`));
    await ev("(function(){ var b = document.querySelector('#rp-close'); b && b.click(); return 0; })()");
    await ev("fsListen.sound()"); await sleep(1500);     /* off */
  }

  /* an expired session: autosave says so and never writes the live patch (Review Focus 4) */
  if (!(await ev("__fa.labMode"))) { await ev("document.querySelector('#pp-lab').click(), 0"); }
  await openPanel(DRAFT); await until(`__fa.labRolesFor === '${DRAFT}'`, 8000);
  await ev("__fa.sb.auth.signOut({ scope: 'local' }).then(function () { return 1; })"); await sleep(500);
  await setSel("v3.synth", "s-pulsar"); await sleep(2500);
  const page3 = await livePatch(DRAFT);
  check("signed out mid-edit: the panel says Not saved, the live patch has no s-* synth", /Not saved|Log in/.test(await ev("(document.querySelector('#pp-lab-msg') || {}).textContent || ''")) && !/"s-/.test(page3), await ev("(document.querySelector('#pp-lab-msg') || {}).textContent"));
  check("no page errors", errors.length === 0, errors);
} finally {
  ch.kill();
  const { data: objs } = await db.storage.from("recordings").list(`lab/${DRAFT}`);
  if (objs && objs.length) await db.storage.from("recordings").remove(objs.map((o) => `lab/${DRAFT}/${o.name}`));
  await db.from("lab_route_roles").delete().eq("route_id", DRAFT);
  if (PT && !ptHadRow) await db.from("lab_route_roles").delete().eq("route_id", PT.id);
  await db.from("features").delete().eq("id", DRAFT);
  await db.from("audit").delete().eq("setter_id", userId);
  await db.from("setters").delete().eq("id", userId);
  if (userId) await db.auth.admin.deleteUser(userId);
}
console.log(failed ? `${failed} FAILED` : "all passed");
process.exit(failed ? 1 : 0);
