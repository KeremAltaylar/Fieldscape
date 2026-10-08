/* Repro (2026-10-08, Kerem): walking Koşuyolu Parkı in lab mode, the sound stops after ~2 minutes. Reads only - Kerem's
   lab row is never written (no control is touched). node --env-file=.env.local scratch/repro-kos.mjs [minutes] [lab 1|0] */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createClient } from "@supabase/supabase-js";
import { targets, session } from "../tools/cdp.mjs";

const URL = process.env.FS_URL || "http://localhost:8765/";
const MIN = +(process.argv[2] || 4), LAB = (process.argv[3] || "1") === "1";
const db = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_SERVICE_KEY, { auth: { persistSession: false } });
const EMAIL = "probe-repro-kos@fieldarc.test", PASSWORD = "probe-" + "r".repeat(16);
const KOS = "5cbac33a-77c9-45e0-b699-d797eeca8c7a";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
{ const { data } = await db.auth.admin.listUsers({ perPage: 1000 }); const old = data && data.users.find((u) => u.email === EMAIL);
  if (old) { await db.from("setters").delete().eq("id", old.id); await db.auth.admin.deleteUser(old.id); } }
const { data: u } = await db.auth.admin.createUser({ email: EMAIL, password: PASSWORD, email_confirm: true });
const userId = u.user.id;
await db.from("setters").insert({ id: userId, name: "probe-repro" });
const route = (await db.from("features").select("geometry").eq("id", KOS).single()).data;
const POINTS = ((await db.from("features").select("id, geometry, properties").eq("kind", "point").is("deleted_at", null).filter("properties->>published", "eq", "true")).data || []).filter((p) => p.properties.has_audio);
const TOUR = process.argv[4] === "points";
const ch = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
  ["--headless=new", "--remote-debugging-port=9276", "--autoplay-policy=no-user-gesture-required", "--window-size=1440,900", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-repro-")), "about:blank"]);
try {
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9276))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready;
  const logs = [];
  s.on((m) => {
    if (m.method === "Runtime.exceptionThrown") logs.push("EXC " + JSON.stringify(m.params.exceptionDetails).slice(0, 400));
    if (m.method === "Runtime.consoleAPICalled" && /error|warn/.test(m.params.type)) logs.push(m.params.type + " " + m.params.args.map((a) => a.value || a.description).join(" ").slice(0, 300));
  });
  await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => (await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true })).result.value;
  const until = async (expr, ms) => { for (let i = 0; i < ms / 200; i++) { if (await ev(expr)) return true; await sleep(200); } return false; };
  await s.send("Page.navigate", { url: URL }); await until("!!(window.__fa && window.fsListen && __fa.features)", 30000); await sleep(1500);
  console.log("sign in", await ev(`__fa.sb.auth.signInWithPassword({ email: ${JSON.stringify(EMAIL)}, password: ${JSON.stringify(PASSWORD)} }).then(function (r) { return r.error ? r.error.message : "ok"; })`));
  await sleep(1500);
  await ev(`fsListen.select('${KOS}'), 0`); await sleep(900);
  await ev("(function(){ var b = document.querySelector('#ls-card #f-patch') || document.querySelector('#f-patch'); b && b.click(); return 0; })()");
  await until("!document.getElementById('patchpanel').hidden", 5000);
  if (LAB) { await ev("document.querySelector('#pp-lab').click(), 0"); await until(`__fa.labRolesFor === '${KOS}'`, 8000); }
  console.log("lab", await ev("__fa.labMode"), "roles for", await ev("__fa.labRolesFor"));
  await ev("(function(){ var b = document.querySelector('#rp-close, #pp-close'); return 0; })()");
  const c = route.geometry.coordinates;
  await ev("fsListen.sound()");
  const t0 = Date.now(); let k = 0, dir = 1;
  /* the walker moved along the route, a point every 0.4 s, back and forth, while the level is logged every 10 s */
  while (Date.now() - t0 < MIN * 60000) {
    if (TOUR) {   /* 15 s at each point with a recording, then 15 s on the route, round and round */
      const slot = Math.floor((Date.now() - t0) / 15000) % (POINTS.length + 1), at = slot < POINTS.length ? POINTS[slot].geometry.coordinates : c[Math.floor(c.length / 2)];
      await ev(`__fa.walkTo(${at[0]}, ${at[1]})`);
    } else {
      await ev(`__fa.walkTo(${c[k][0]}, ${c[k][1]})`);
      k += dir; if (k >= c.length - 1 || k <= 0) dir = -dir;
    }
    await sleep(400);
    if ((Date.now() - t0) % 10000 < 420) {
      const st = await ev("JSON.stringify({ lvl: Math.round(__fa.coreLevel ? __fa.coreLevel() : -999), wasm: __fa.coreWasm, nodes: __fa.coreNodes, on: !!(window.__fa.core && __fa.core.on), route: __fa.core && __fa.core.route_id && __fa.core.route_id.slice(0, 8), tracks: __fa.labTracks, analyses: __fa.labAnalyses, heap: Math.round((performance.memory || {}).usedJSHeapSize / 1e6), ctx: (function(){ try { return __fa.core.ctx.state; } catch (e) { return 'n/a'; } })(), err: window.__fa.coreError || null })");
      console.log(Math.round((Date.now() - t0) / 1000) + "s", st);
      while (logs.length) console.log("   ", logs.shift());
    }
  }
  while (logs.length) console.log("   ", logs.shift());
} finally {
  ch.kill();
  await db.from("audit").delete().eq("setter_id", userId);
  await db.from("setters").delete().eq("id", userId);
  await db.auth.admin.deleteUser(userId);
}
