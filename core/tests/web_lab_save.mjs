/* Sample harmony 3b in the browser: a setter uploads a sample for a role, saves the route's role setups, and they load back
   (docs/superpowers/specs/2026-10-04-samples-3b-role-samples-design.md). A probe setter and an unpublished test route are made
   with the service key and removed after.
     node tools/serve.mjs 8765    (repo root, in another shell)
     node --env-file=.env.local core/tests/web_lab_save.mjs */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createClient } from "@supabase/supabase-js";
import { targets, session } from "../../tools/cdp.mjs";

const URL = (process.env.FS_URL || "http://localhost:8765/") + "lab.html";
const db = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_SERVICE_KEY, { auth: { persistSession: false } });
const EMAIL = "probe-lab-ui@fieldarc.test", PASSWORD = "probe-" + "z".repeat(16);
const DRAFT = "00000000-0000-4000-8000-00000000003c";      /* an id no other test file uses */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0, userId = null;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };

const { data: u } = await db.auth.admin.createUser({ email: EMAIL, password: PASSWORD, email_confirm: true });
userId = u.user.id;
await db.from("setters").insert({ id: userId, name: "probe-lab-ui" });
await db.from("features").delete().eq("id", DRAFT);
await db.from("features").insert({ id: DRAFT, place: "LAB3B", kind: "route", geometry: { type: "LineString", coordinates: [[28.99, 41.18], [28.991, 41.181]] },
  properties: { published: false, name: "lab ui draft" } });
const row0 = (await db.from("features").select("properties, updated_at").eq("id", DRAFT).single()).data;

const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9269", "--autoplay-policy=no-user-gesture-required", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-labsave-")), "about:blank"]);
const noise = (sec, name) => `(function(){ var sr = 48000, n = sr * ${sec}, b = new ArrayBuffer(44 + n * 2), v = new DataView(b), r = 7;
  function s(o, t) { for (var i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); }
  s(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n * 2, true);
  for (var i = 0; i < n; i++) { r ^= r << 13; r >>>= 0; r ^= r >>> 17; r ^= r << 5; r >>>= 0; v.setInt16(44 + i * 2, Math.round(9000 * ((r / 4294967296) * 2 - 1)), true); }
  return new File([b], ${JSON.stringify(name)}, { type: "audio/wav" }); })()`;
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9269))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const errors = [];
  s.on((m) => { if (m.method === "Runtime.exceptionThrown") errors.push(JSON.stringify(m.params.exceptionDetails).slice(0, 300)); });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const open = async () => { await s.send("Page.navigate", { url: URL }); for (let i = 0; i < 60 && !(await ev("!!window.fsLab")); i++) await sleep(250); await ev("fsLab.ready"); };
  const until = async (expr, ms) => { for (let i = 0; i < ms / 200; i++) { if (await ev(expr)) return true; await sleep(200); } return false; };
  const msg = () => ev("(document.querySelector('#lab-rt-msg') || {}).textContent || ''");
  await open();

  /* not signed in: Save explains */
  await ev("document.querySelector('#lab-rt-save') && document.querySelector('#lab-rt-save').click(), 0");
  await sleep(300);
  check("not signed in: Save asks for a setter login", /Log in as a setter/i.test(await msg()), await msg());

  /* signed in: the setter's own routes, unpublished ones too */
  const si = await ev(`fsLab.signIn(${JSON.stringify(EMAIL)}, ${JSON.stringify(PASSWORD)}).then(function (e) { return e || "ok"; })`);
  const listed = await until(`!!document.querySelector("#lab-route option[value='${DRAFT}']")`, 8000);
  check("signed in as a setter, the lab lists unpublished routes too", si === "ok" && listed, si);
  const shownFor = await until("fsLab.rolesFor === document.querySelector('#lab-route').value", 5000);
  check("signed in: the route already shown has its saved roles loaded (final review I1)", shownFor, await ev("[fsLab.rolesFor, document.querySelector('#lab-route').value]"));

  /* a sample for the Third voice: uploaded, analysed, shown */
  await ev(`(function(){ var s = document.querySelector('#lab-route'); s.value = '${DRAFT}'; s.dispatchEvent(new Event('change')); return 1; })()`);
  await sleep(500);
  await ev("(function(){ var s = document.querySelector('#lab-rt-v3'); s.value = 's-freeze'; s.dispatchEvent(new Event('change')); return 1; })()");
  await ev(`(function(){ var f = ${noise(40, "probe-wind.wav")}, dt = new DataTransfer(); dt.items.add(f); var i = document.querySelector('#lab-rt-v3-file'); i.files = dt.files; i.dispatchEvent(new Event('change')); return 1; })()`);
  const shown = await until("/probe-wind\\.wav/.test((document.querySelector('#lab-rt-v3-sample') || {}).textContent || '') && /unpitched|Hz/.test(document.querySelector('#lab-rt-v3-sample').textContent)", 15000);
  check("a sampler role's Upload: the sample analysed, uploaded and shown", shown, await ev("(document.querySelector('#lab-rt-v3-sample') || {}).textContent"));
  await ev("(function(){ var s = document.querySelector('#lab-rt-v3-focus'); s.value = '0.83'; s.dispatchEvent(new Event('input')); return 1; })()");
  await ev("(function(){ var s = document.querySelector('#lab-rt-sect'); s.value = 's-freeze'; s.dispatchEvent(new Event('change')); return 1; })()");
  await ev(`(function(){ var f = ${noise(3, '<img src=x onerror="window.__xss=1">.wav')}, dt = new DataTransfer(); dt.items.add(f); var i = document.querySelector('#lab-rt-sect-file'); i.files = dt.files; i.dispatchEvent(new Event('change')); return 1; })()`);
  await until("/uploaded|failed/.test((document.querySelector('#lab-rt-sect-sample') || {}).textContent || '')", 15000);
  await ev("(function(){ var s = document.querySelector('#lab-rt-sect'); s.value = 's-pulsar'; s.dispatchEvent(new Event('change')); return 1; })()");
  await sleep(600);
  check("a sample's name is shown as text, never run as markup (final review I6)",
    !(await ev("!!window.__xss")) && /<img/.test(await ev("(document.querySelector('#lab-rt-sect-sample') || {}).textContent || ''")), await ev("[!!window.__xss, (document.querySelector('#lab-rt-sect-sample') || {}).innerHTML]"));

  /* Save: the row holds the role, its controls and its sample */
  await ev("document.querySelector('#lab-rt-save').click(), 0");
  await until("/Saved/.test((document.querySelector('#lab-rt-msg') || {}).textContent)", 10000);
  const saved = (await db.from("lab_route_roles").select("roles").eq("route_id", DRAFT).single()).data;
  const v3 = saved && saved.roles && saved.roles.v3;
  check("Save: the role setups are in lab_route_roles (synth, controls, the sample under lab/<route>/)",
    v3 && v3.synth === "s-freeze" && v3.sampler && v3.sampler.focus === 0.83 && v3.sample && v3.sample.path.indexOf(`lab/${DRAFT}/v3-`) === 0 && v3.sample.analysis && "f0" in v3.sample.analysis, saved);
  const obj = v3 && v3.sample ? await db.storage.from("recordings").download(v3.sample.path) : null;
  const bytes = obj && obj.data ? obj.data.size : -1;
  check("a 40 s file: only the 30 s the engine uses is uploaded, as a WAV (final review I4)", bytes > 0 && bytes <= 44 + 30 * 48000 * 2 + 64 && v3.sample.path.endsWith(".wav"), bytes);

  /* reload, choose the route: everything back, and it plays */
  await open();
  await ev(`fsLab.signIn(${JSON.stringify(EMAIL)}, ${JSON.stringify(PASSWORD)})`);
  await until(`!!document.querySelector("#lab-route option[value='${DRAFT}']")`, 8000);
  await ev(`(function(){ var s = document.querySelector('#lab-route'); s.value = '${DRAFT}'; s.dispatchEvent(new Event('change')); document.querySelector('#lab-rt-save').click(); return 1; })()`);
  await sleep(3000);
  const kept = (await db.from("lab_route_roles").select("roles").eq("route_id", DRAFT).single()).data;
  check("Save pressed before the route's roles have loaded: nothing overwritten (final review I2)", JSON.stringify(kept) === JSON.stringify(saved), kept);
  const back = await until("document.querySelector('#lab-rt-v3').value === 's-freeze' && /probe-wind\\.wav/.test((document.querySelector('#lab-rt-v3-sample') || {}).textContent || '') && document.querySelector('#lab-rt-v3-focus') && +document.querySelector('#lab-rt-v3-focus').value === 0.83", 15000);
  check("reloaded and chosen: the Third voice is Freeze again, its Focus and its sample back", back, await ev("[document.querySelector('#lab-rt-v3').value, (document.querySelector('#lab-rt-v3-sample') || {}).textContent]"));
  await ev("document.querySelector('#lab-rt-play').click(), 0");
  let lv = -120; for (let i = 0; i < 20; i++) { await sleep(200); lv = Math.max(lv, await ev("fsLab.level()")); }
  await ev("document.querySelector('#lab-rt-stop').click(), 0");
  check("... and the saved route plays (above -40 dBFS)", lv > -40, lv);

  /* the route's own row never written */
  const row1 = (await db.from("features").select("properties, updated_at").eq("id", DRAFT).single()).data;
  check("the route's own row is untouched", JSON.stringify(row1) === JSON.stringify(row0), [row0, row1]);

  /* an upload that fails: Save refuses, the saved sample stays */
  await s.send("Fetch.enable", { patterns: [{ urlPattern: "*/storage/v1/object/*", requestStage: "Request" }] });
  s.on((m) => { if (m.method === "Fetch.requestPaused") {
    const p = m.params; s.send(p.request.method === "GET" ? "Fetch.continueRequest" : "Fetch.failRequest", p.request.method === "GET" ? { requestId: p.requestId } : { requestId: p.requestId, errorReason: "Failed" }).catch(() => {}); } });
  await ev(`(function(){ var f = ${noise(3, "probe-other.wav")}, dt = new DataTransfer(); dt.items.add(f); var i = document.querySelector('#lab-rt-v3-file'); i.files = dt.files; i.dispatchEvent(new Event('change')); return 1; })()`);
  await until("/failed/.test((document.querySelector('#lab-rt-v3-sample') || {}).textContent || '')", 15000);
  await ev("document.querySelector('#lab-rt-save').click(), 0");
  await sleep(2500);
  await s.send("Fetch.disable");
  const after = (await db.from("lab_route_roles").select("roles").eq("route_id", DRAFT).single()).data;
  check("a failed upload: Save refuses and the saved sample stays (final review I3)",
    after && after.roles.v3.sample && after.roles.v3.sample.path === v3.sample.path && !/^Saved/.test(await msg()), [await msg(), after && after.roles.v3.sample]);

  /* a saved sample that has gone, after another route's sample played: that role says so and is silent */
  const missing = JSON.parse(JSON.stringify(saved.roles)); missing.v3.sample.path = `lab/${DRAFT}/gone.wav`;
  missing.voice = { synth: "s-freeze" }; missing.sect = { synth: "s-freeze" };   /* no sample, no recording loaded: silent too */
  await db.from("lab_route_roles").update({ roles: missing }).eq("route_id", DRAFT);
  const other = await ev(`[].slice.call(document.querySelectorAll('#lab-route option')).map(function (o) { return o.value; }).filter(function (v) { return v !== '${DRAFT}'; })[0]`);
  await ev(`(function(){ var s = document.querySelector('#lab-route'); s.value = '${other}'; s.dispatchEvent(new Event('change')); s.value = '${DRAFT}'; s.dispatchEvent(new Event('change')); return 1; })()`);
  const gone = await until("/missing/i.test((document.querySelector('#lab-rt-v3-sample') || {}).textContent || '')", 10000);
  check("a saved sample that is gone: the role says it is missing", gone, await ev("(document.querySelector('#lab-rt-v3-sample') || {}).textContent"));
  await until("fsLab.level() < -90", 30000);   /* the last play's release tails (up to 10 s) gone first */
  await ev("document.querySelector('#lab-rt-play').click(), 0");
  /* the effects' buffers from the last play flush and fade on a new play (a stop/play behaviour, measured: about 3 dB a
     second); a role still holding the earlier sample keeps it steady (near -21 dB) */
  const win = async () => { let l = -120; for (let i = 0; i < 8; i++) { await sleep(250); l = Math.max(l, await ev("fsLab.level()")); } return l; };
  await sleep(4000); const early = await win(); await sleep(4000); const late = await win();
  await ev("document.querySelector('#lab-rt-stop').click(), 0");
  check("... and that role is silent, not the sample played before: fading, not held (final review I5)", late < -60 || late < early - 6, [early, late]);
  check("no page errors", errors.length === 0, errors);
} finally {
  ch.kill();
  const { data: objs } = await db.storage.from("recordings").list(`lab/${DRAFT}`);
  if (objs && objs.length) await db.storage.from("recordings").remove(objs.map((o) => `lab/${DRAFT}/${o.name}`));
  await db.from("lab_route_roles").delete().eq("route_id", DRAFT);
  await db.from("features").delete().eq("id", DRAFT);
  await db.from("audit").delete().eq("setter_id", userId);
  await db.from("setters").delete().eq("id", userId);
  if (userId) await db.auth.admin.deleteUser(userId);
}
console.log(failed ? `${failed} FAILED` : "all passed");
process.exit(failed ? 1 : 0);
