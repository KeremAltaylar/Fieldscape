/* 6b: a published probe route with a sampler voice (a 220 Hz tone, its analysis), for checking the apps on a simulator or
   emulator - no real route has one published yet.  node --env-file=.env.local tools/probe-route.mjs up|down
   up: a probe setter (random password) puts the tone and its analysis in R2 through the media Worker; the route is
   published 1.1 km north of Koşuyolu (prints where to walk). down: the route, the files and the setter removed. */
import { execFileSync } from "node:child_process";
import { createClient } from "@supabase/supabase-js";

const WORKER = "https://fieldscape-media.keremaltaylar.workers.dev", ID = "00000000-0000-4000-8000-0000000006b1";
const EMAIL = "probe-6b-route@fieldarc.test", PASSWORD = "probe-" + crypto.randomUUID();
const WAV = `${ID}/sect-probe.wav`, JSONP = `${ID}/sect-probe.json`;
/* a pitched stretch point beside it (the tone, its track): the published points' recordings are all judged unpitched */
const PT = "00000000-0000-4000-8000-0000000006b2", PWAV = `${PT}/take.wav`, PTRK = `${PT}/track-probe.json`;
const db = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_SERVICE_KEY, { auth: { persistSession: false } });

async function down() {
  await db.from("features").delete().in("id", [ID, PT]);
  const { data } = await db.auth.admin.listUsers({ perPage: 1000 }); const u = data && data.users.find((x) => x.email === EMAIL);
  if (u) { await db.from("setters").delete().eq("id", u.id); await db.auth.admin.deleteUser(u.id); }
  for (const k of [WAV, JSONP, PWAV, PTRK]) try { execFileSync("npx", ["--yes", "wrangler", "r2", "object", "delete", `fieldscape-media/${k}`, "--remote"], { cwd: "workers/media", stdio: "ignore", shell: true }); } catch { /* gone */ }
  console.log("probe route removed");
}
if (process.argv[2] === "down") { await down(); process.exit(0); }

await down();
const { data: u } = await db.auth.admin.createUser({ email: EMAIL, password: PASSWORD, email_confirm: true });
await db.from("setters").insert({ id: u.user.id, name: "probe-6b" });
const anon = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_ANON_KEY, { auth: { persistSession: false } });
const token = (await anon.auth.signInWithPassword({ email: EMAIL, password: PASSWORD })).data.session.access_token;
/* 30 s (a real sample's length - the preparation's cost is measured on it): a 220 Hz tone, 0.6 s of silence (cut by the
   preparation), the tone again */
const sr = 48000, n = sr * 30, b = Buffer.alloc(44 + n * 2);
b.write("RIFF", 0); b.writeUInt32LE(36 + n * 2, 4); b.write("WAVEfmt ", 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22);
b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write("data", 36); b.writeUInt32LE(n * 2, 40);
for (let i = 0; i < n; i++) { const t = i / sr; b.writeInt16LE(t > 1.7 && t < 2.3 ? 0 : Math.round(9000 * Math.sin(2 * Math.PI * 220 * t)), 44 + 2 * i); }
const track = []; for (let t = 0; t < 30; t += 0.02) track.push(t > 1.7 && t < 2.3 ? [0, 0] : [220, 0.95]);
const ana = { f0: 220, hop_s: 0.02, verdict: "pitched", track };
for (const [k, body, type] of [[WAV, b, "audio/wav"], [JSONP, JSON.stringify(ana), "application/json"], [PWAV, b, "audio/wav"], [PTRK, JSON.stringify(ana), "application/json"]]) {
  const r = await fetch(`${WORKER}/o/${k}`, { method: "PUT", body, headers: { Authorization: "Bearer " + token, "content-type": type } });
  if (r.status !== 200) throw new Error("put " + k + " " + r.status);
}
const ko = (await db.from("features").select("place, geometry, properties").eq("kind", "route").filter("properties->>name", "eq", "Koşuyolu Parkı").limit(1)).data[0];
const coords = ko.geometry.coordinates.map((c) => [c[0], c[1] + 0.01]);
const patch = Object.assign({}, ko.properties.patch, { sect: Object.assign({}, ko.properties.patch.sect, { on: true, gain: 0.8, synth: "s-retune", sampler: { method: 1, focus: 0.5, colour: 0.5, tune: 1, octave: 0 },
  sample: { path: WAV, name: "probe tone", analysis_path: JSONP, f0: 220 } }) });
await db.from("features").insert({ id: ID, place: ko.place, kind: "route", geometry: { type: "LineString", coordinates: coords },
  properties: { id: ID, kind: "route", place: ko.place, name: "probe 6b route", published: true, patch } });
const mid = coords[Math.floor(coords.length / 2)];
await db.from("features").insert({ id: PT, place: ko.place, kind: "point", geometry: { type: "Point", coordinates: [mid[0] + 0.0004, mid[1]] },
  properties: { id: PT, kind: "point", place: ko.place, name: "probe 6b stretch", published: true, has_audio: true, storage_path: PWAV,
    audio: { name: "tone.wav", type: "audio/wav", size: b.length }, sound: { radius: 140, gain: 0.9, zoneR: 25, stretch: 0.2, shape: { follow: 1 }, track_path: PTRK } } });
console.log(`probe route up: walk to ${mid[0].toFixed(6)} ${mid[1].toFixed(6)} (its Second voice is a Retune sampler on a 220 Hz tone); a pitched stretch point following the chord at ${(mid[0] + 0.0004).toFixed(6)} ${mid[1].toFixed(6)}`);
