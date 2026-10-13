/* Phase 1b: the deployed recordings Worker against its rules, with probe users, probe features and tiny probe files
   (no real recording is read):  node --env-file=.env.local tools/media-smoke.mjs
   A published point's file: anyone reads it. An unpublished one's, or a sensitive one's: refused to anyone but a setter.
   A walker (signed in, not a setter) may not write; a setter writes; x-upsert false refuses an existing file; the site's
   preflight is answered. Everything it made is removed after. */
import { execFileSync } from "node:child_process";
import { createClient } from "@supabase/supabase-js";

const WORKER = process.env.FS_MEDIA || "https://fieldscape-media.keremaltaylar.workers.dev";
const db = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_SERVICE_KEY, { auth: { persistSession: false } });
const PUB = "00000000-0000-4000-8000-0000000001b1", PRIV = "00000000-0000-4000-8000-0000000001b2", SENS = "00000000-0000-4000-8000-0000000001b3";
const users = [["probe-media-setter@fieldarc.test", true], ["probe-media-walker@fieldarc.test", false]], PASSWORD = "probe-" + "m".repeat(16);
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };

async function cleanup() {
  await db.from("features").delete().in("id", [PUB, PRIV, SENS]);
  const { data } = await db.auth.admin.listUsers({ perPage: 1000 });
  for (const [email] of users) { const u = data && data.users.find((x) => x.email === email); if (u) { await db.from("setters").delete().eq("id", u.id); await db.auth.admin.deleteUser(u.id); } }
  for (const id of [PUB, PRIV, SENS]) {
    try { execFileSync("npx", ["--yes", "wrangler", "r2", "object", "delete", `fieldscape-media/${id}/probe.txt`, "--remote"], { cwd: "workers/media", stdio: "ignore", shell: true }); } catch { /* not there */ }
  }
}

await cleanup();
const tokens = {};
for (const [email, setter] of users) {
  const { data: u } = await db.auth.admin.createUser({ email, password: PASSWORD, email_confirm: true });
  if (setter) await db.from("setters").insert({ id: u.user.id, name: "probe-media" });
  const anon = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_ANON_KEY, { auth: { persistSession: false } });
  const { data: s } = await anon.auth.signInWithPassword({ email, password: PASSWORD });
  tokens[setter ? "setter" : "walker"] = s.session.access_token;
}
const place = (await db.from("features").select("place").eq("kind", "route").limit(1)).data[0].place;
for (const [id, published, sensitive] of [[PUB, true, false], [PRIV, false, false], [SENS, true, true]])
  await db.from("features").insert({ id, place, kind: "point", geometry: { type: "Point", coordinates: [29.04, 41.0] },
    properties: { id, kind: "point", place, name: "probe media", published, ...(sensitive ? { sensitive: true, fuzz_m: 200 } : {}) } });

const call = (method, id, { who, body, headers = {} } = {}) => fetch(`${WORKER}/o/${id}/probe.txt`, { method, body,
  headers: { ...(who ? { Authorization: "Bearer " + tokens[who] } : {}), Origin: "https://keremaltaylar.github.io", ...headers } });
try {
  for (const id of [PUB, PRIV, SENS]) { const r = await call("PUT", id, { who: "setter", body: "probe " + id, headers: { "content-type": "text/plain" } }); check("a setter writes " + id.slice(-3), r.status === 200, r.status); }
  let r = await call("GET", PUB); check("anyone reads a published point's file", r.status === 200 && (await r.text()) === "probe " + PUB && r.headers.get("access-control-allow-origin") === "https://keremaltaylar.github.io", r.status);
  r = await call("GET", PRIV); check("an unpublished point's file is refused to anyone", r.status === 403, r.status);
  r = await call("GET", SENS); check("a sensitive point's file is refused to anyone (its WAV may carry the true place)", r.status === 403, r.status);
  r = await call("GET", PRIV, { who: "walker" }); check("...and to a walker who is not a setter", r.status === 403, r.status);
  r = await call("GET", PRIV, { who: "setter" }); check("a setter reads an unpublished point's file", r.status === 200 && (await r.text()) === "probe " + PRIV, r.status);
  r = await call("PUT", PUB, { who: "walker", body: "x" }); check("a walker may not write", r.status === 403, r.status);
  r = await call("PUT", PUB, { body: "x" }); check("nobody signed out may write", r.status === 403, r.status);
  r = await call("PUT", PUB, { who: "setter", body: "x", headers: { "x-upsert": "false" } }); check("x-upsert false refuses an existing file", r.status === 409, r.status);
  r = await fetch(`${WORKER}/o/${PUB}/probe.txt`, { method: "OPTIONS", headers: { Origin: "http://localhost:8765", "Access-Control-Request-Method": "PUT", "Access-Control-Request-Headers": "authorization,x-upsert" } });
  check("the site's preflight is answered", r.status === 204 && r.headers.get("access-control-allow-origin") === "http://localhost:8765", r.status);
} finally { await cleanup(); }
console.log(failed ? failed + " FAILED" : "all passed");
process.exit(failed ? 1 : 0);
