/* Sample harmony 6a, release day: the lab setups (lab_route_roles) carried into the routes and points themselves
   (docs/superpowers/specs/2026-10-09-samples-6a-website-switch-over-design.md, section 3).
     node --env-file=.env.local tools/carry-over-6a.mjs                 a dry run: every change printed, nothing written
     node --env-file=.env.local tools/carry-over-6a.mjs --run           the changes written
     ... --only <id,id>                                                 only these rows (the tests use probe rows)
   A route's sampler voice: its lab sample copied to <route id>/<role>-<ms>.wav, its analysis (from the lab row) written
   beside it as .json, and synth / sampler / eq / sample into properties.patch.<role>; a digital voice with an EQ gets eq only.
   A point: follow / resonate / body into properties.sound.shape (stretch) or rhythm.follow (grains, hits); following, its
   recording's track (first 60 s, analysed by the page served at FS_URL, read through a signed link: fuzzed points too)
   written beside it as track-<ms>.json. lab_route_roles is left as it is. Run twice, the second run changes nothing. */
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createClient } from "@supabase/supabase-js";
import { targets, session } from "./cdp.mjs";

const RUN = process.argv.includes("--run");
const oi = process.argv.indexOf("--only"), ONLY = oi > 0 ? new Set(process.argv[oi + 1].split(",")) : null;
const URL = process.env.FS_URL || "http://localhost:8765/";
const db = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_SERVICE_KEY, { auth: { persistSession: false } });
const store = db.storage.from("recordings");
const ROLES = ["voice", "sect", "v3"], HIT_SLOTS = ["low", "mid", "high", "rand"];
const isSampler = (s) => typeof s === "string" && s.startsWith("s-");
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let changes = 0;
const say = (id, what) => { changes++; console.log((RUN ? "" : "[dry] ") + id.slice(0, 8) + " " + what); };

/* the page that reads a recording's pitch - started on the first track needed */
let page = null, chrome = null;
async function analyser() {
  if (page) return page;
  chrome = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe",
    ["--headless=new", "--remote-debugging-port=9278", "--user-data-dir=" + mkdtempSync(join(tmpdir(), "fs-carry-")), "about:blank"]);
  let t; for (let i = 0; i < 50 && !t; i++) { try { t = (await targets(9278))[0]; } catch { await sleep(200); } }
  const s = session(t); await s.ready; await s.send("Runtime.enable"); await s.send("Page.enable");
  const ev = async (e) => { const r = await s.send("Runtime.evaluate", { expression: e, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 300)); return r.result.value; };
  await s.send("Page.navigate", { url: URL });
  for (let i = 0; i < 100 && !(await ev("!!(window.__fa && __fa.trackForUrl)")); i++) await sleep(300);
  page = { ev };
  return page;
}
async function trackOf(path) {
  const { data, error } = await store.createSignedUrl(path, 600);
  if (error) throw new Error("signed link for " + path + ": " + error.message);
  const p = await analyser();
  return p.ev(`__fa.trackForUrl(${JSON.stringify(data.signedUrl)})`);
}
async function upload(path, body, type) {
  const { error } = await store.upload(path, body, { contentType: type, upsert: false });
  if (error) throw new Error("upload " + path + ": " + error.message);
}

try {
  const rows = ((await db.from("lab_route_roles").select("route_id, roles")).data || []).filter((r) => !ONLY || ONLY.has(r.route_id));
  for (const row of rows) {
    const { data: f } = await db.from("features").select("id, kind, properties").eq("id", row.route_id).maybeSingle();
    if (!f) { console.log(row.route_id.slice(0, 8) + " skipped: no such feature"); continue; }
    const props = JSON.parse(JSON.stringify(f.properties)), before = JSON.stringify(props);
    if (f.kind === "route") {
      const patch = props.patch = props.patch || {};
      for (const r of ROLES) {
        const rr = row.roles[r]; if (!rr) continue;
        const q = patch[r] = patch[r] || {};
        if (Array.isArray(rr.eq) && !same(q.eq, rr.eq)) { say(f.id, `${r}: eq`); q.eq = rr.eq; }
        if (!isSampler(rr.synth)) continue;
        if (q.synth !== rr.synth) { say(f.id, `${r}: synth ${q.synth} -> ${rr.synth}`); if (q.synth && !isSampler(q.synth)) q.digital = q.synth; q.synth = rr.synth; }
        if (rr.sampler && !same(q.sampler, rr.sampler)) { say(f.id, `${r}: sampler ${JSON.stringify(rr.sampler)}`); q.sampler = rr.sampler; }
        const sm = rr.sample;
        if (sm && sm.path && !(q.sample && q.sample.path && q.sample.path.startsWith(f.id + "/"))) {
          const ms = Date.now(), wav = `${f.id}/${r}-${ms}.wav`, json = `${f.id}/${r}-${ms}.json`;
          say(f.id, `${r}: sample ${sm.path} -> ${wav} (+ its analysis)`);
          if (RUN) {
            const { error } = await store.copy(sm.path, wav);
            if (error) throw new Error("copy " + sm.path + ": " + error.message);
            await upload(json, new Blob([JSON.stringify(sm.analysis || null)], { type: "application/json" }), "application/json");
          }
          q.sample = { path: wav, name: sm.name || "", analysis_path: json, f0: (sm.analysis && sm.analysis.f0) || 0 };
        }
      }
    } else if (f.kind === "point" && row.roles.point) {
      const pt = row.roles.point, mode = props.audio_mode || "";
      const fol = pt.follow > 0 ? pt.follow : 0, res = pt.resonate > 0 ? pt.resonate : 0;
      if (mode === "hits" || mode === "grains") {
        props.rhythm = props.rhythm || {};
        if (fol && props.rhythm.follow !== fol) { say(f.id, `rhythm.follow ${fol}`); props.rhythm.follow = fol; }
      } else {
        props.sound = props.sound || {}; const sh = props.sound.shape = props.sound.shape || {};
        if (fol && sh.follow !== fol) { say(f.id, `follow ${fol}`); sh.follow = fol; }
        if (res && sh.resonate !== res) { say(f.id, `resonate ${res}`); sh.resonate = res; }
        if (res && (pt.body || 0) !== (sh.body || 0)) { say(f.id, `body ${pt.body || 0}`); sh.body = pt.body || 0; }
      }
      if (fol) {   /* each recording without a track gets one */
        const recs = mode === "hits"
          ? HIT_SLOTS.filter((k) => props.hits && props.hits[k] && props.hits[k].storage_path && !props.hits[k].track_path).map((k) => ({ slot: k, path: props.hits[k].storage_path }))
          : props.has_audio && props.storage_path && !(props.sound && props.sound.track_path) ? [{ slot: null, path: props.storage_path }] : [];
        for (const rc of recs) {
          const tpath = `${f.id}/track-${rc.slot ? rc.slot + "-" : ""}${Date.now()}.json`;
          say(f.id, `track of ${rc.path} -> ${tpath}`);
          if (RUN) await upload(tpath, new Blob([JSON.stringify(await trackOf(rc.path))], { type: "application/json" }), "application/json");
          if (rc.slot) props.hits[rc.slot].track_path = tpath; else (props.sound = props.sound || {}).track_path = tpath;
        }
      }
    }
    if (RUN && JSON.stringify(props) !== before) {
      const { error } = await db.from("features").update({ properties: props }).eq("id", f.id);   /* features_touch bumps updated_at */
      if (error) throw new Error("update " + f.id + ": " + error.message);
    }
  }
} finally {
  if (chrome) chrome.kill();
}
console.log(`${changes} changes${RUN ? "" : " (dry run: nothing written)"}`);
