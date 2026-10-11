/* Sample harmony 6a: the carry-over (tools/carry-over-6a.mjs) on probe rows only - a published probe route whose lab row has a
   sampler voice and a digital voice with an EQ, and a published probe stretch point whose lab row follows and resonates. Kerem's
   rows are never touched (--only). Needs the page served: node tools/serve.mjs 8765
     node --env-file=.env.local core/tests/carry_over.mjs */
import { execFileSync } from "node:child_process";
import { createClient } from "@supabase/supabase-js";

const db = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_SERVICE_KEY, { auth: { persistSession: false } });
const RID = "00000000-0000-4000-8000-0000000006a1", PID = "00000000-0000-4000-8000-0000000006a2";
let failed = 0;
const check = (name, ok, got) => { console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (got " + JSON.stringify(got) + ")")); if (!ok) failed++; };
const wav = (hz, secs) => {
  const sr = 48000, n = sr * secs, b = Buffer.alloc(44 + n * 2);
  b.write("RIFF", 0); b.writeUInt32LE(36 + n * 2, 4); b.write("WAVEfmt ", 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22);
  b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write("data", 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(9000 * Math.sin(2 * Math.PI * hz * i / sr)), 44 + 2 * i);
  return b;
};
const files = async (dir) => ((await db.storage.from("recordings").list(dir)).data || []).map((o) => o.name);
const clean = async () => {
  for (const id of [RID, PID]) {
    await db.from("lab_route_roles").delete().eq("route_id", id);
    await db.from("features").delete().eq("id", id);
    for (const dir of [id, "lab/" + id]) { const f = await files(dir); if (f.length) await db.storage.from("recordings").remove(f.map((n) => dir + "/" + n)); }
  }
};
const run = (...a) => execFileSync(process.execPath, ["--env-file=.env.local", "tools/carry-over-6a.mjs", ...a], { encoding: "utf8", timeout: 240000 });

try {
  await clean();
  const pub = (await db.from("features").select("place, geometry, properties").eq("kind", "route").is("deleted_at", null).filter("properties->>published", "eq", "true").limit(1)).data[0];
  const geom = { type: "LineString", coordinates: pub.geometry.coordinates.map((c) => [c[0], c[1] + 0.02]) };
  await db.from("features").insert({ id: RID, place: pub.place, kind: "route", geometry: geom,
    properties: { id: RID, kind: "route", place: pub.place, name: "probe 6a carry route", published: true, patch: Object.assign({}, pub.properties.patch, { v3: Object.assign({}, pub.properties.patch.v3, { synth: "am" }) }) } });
  await db.storage.from("recordings").upload(`lab/${RID}/voice-1.wav`, wav(220, 2), { contentType: "audio/wav" });
  const eq = [[80, 2, 0.7], [250, 0, 1], [1000, -3, 1], [4000, 0, 1], [10000, 0, 0.7]];
  const analysis = { f0: 220, hop_s: 0.02, verdict: "pitched", track: [[220, 0.95, 0], [220, 0.95, 0]] };
  const rowBefore = { voice: { synth: "s-freeze", gain: 0.5, sampler: { focus: 0.6, colour: 0.4, octave: 1 }, eq, sample: { path: `lab/${RID}/voice-1.wav`, name: "tone.wav", analysis } },
    sect: { synth: "fm", eq }, v3: { synth: "am" } };
  await db.from("lab_route_roles").insert({ route_id: RID, roles: rowBefore });
  await db.storage.from("recordings").upload(`${PID}/take.wav`, wav(330, 3), { contentType: "audio/wav" });
  await db.from("features").insert({ id: PID, place: pub.place, kind: "point", geometry: { type: "Point", coordinates: geom.coordinates[0] },
    properties: { id: PID, kind: "point", place: pub.place, name: "probe 6a carry point", published: true, has_audio: true, storage_path: `${PID}/take.wav`,
      audio: { name: "tone.wav", type: "audio/wav" }, sound: { radius: 140, gain: 0.9, shape: { tune: 0.3 } } } });
  await db.from("lab_route_roles").insert({ route_id: PID, roles: { point: { follow: 1, resonate: 0.5, body: 2 } } });

  const dry = run("--only", `${RID},${PID}`);
  const untouched = (await db.from("features").select("properties").eq("id", RID).single()).data.properties.patch.voice.synth !== "s-freeze";
  check("a dry run lists the changes and writes nothing", /voice/.test(dry) && /follow/.test(dry) && untouched, dry.split("\n").slice(-4));
  const out = run("--run", "--only", `${RID},${PID}`);
  const r = (await db.from("features").select("properties").eq("id", RID).single()).data;
  const p = (await db.from("features").select("properties").eq("id", PID).single()).data;
  const rf = await files(RID), pf = await files(PID);
  check("route: the voice's sampler keys and EQ in its patch, the sample in the route's folder",
    r.properties.patch.voice.synth === "s-freeze" && r.properties.patch.voice.sampler.octave === 1 && r.properties.patch.voice.sample.path.startsWith(RID + "/voice-")
      && r.properties.patch.voice.sample.f0 === 220 && JSON.stringify(r.properties.patch.voice.eq) === JSON.stringify(eq), r.properties.patch.voice);
  check("route: a digital voice gets its EQ only", JSON.stringify(r.properties.patch.sect.eq) === JSON.stringify(eq) && r.properties.patch.sect.synth === pub.properties.patch.sect.synth, r.properties.patch.sect);
  check("route: the WAV copied and its analysis JSON written beside it", rf.some((n) => /^voice-\d+\.wav$/.test(n)) && rf.some((n) => /^voice-\d+\.json$/.test(n)), rf);
  const aj = JSON.parse(await (await db.storage.from("recordings").download(r.properties.patch.voice.sample.analysis_path)).data.text());
  check("route: the analysis file is the lab row's analysis", aj.f0 === 220 && aj.track.length === 2, aj);
  check("point: follow, resonate and body in its shape (its own tune kept), its track file made", p.properties.sound.shape.follow === 1 && p.properties.sound.shape.resonate === 0.5
    && p.properties.sound.shape.body === 2 && p.properties.sound.shape.tune === 0.3 && String(p.properties.sound.track_path).startsWith(PID + "/track-") && pf.some((n) => /^track-\d+\.json$/.test(n)), [p.properties.sound, pf]);
  const tj = JSON.parse(await (await db.storage.from("recordings").download(p.properties.sound.track_path)).data.text());
  check("point: the track file is the recording's pitch (330 Hz)", Math.abs(1200 * Math.log2(tj.f0 / 330)) < 20 && tj.track.length > 50, [tj.f0, tj.track && tj.track.length]);
  const second = run("--run", "--only", `${RID},${PID}`);
  check("a second run changes nothing", /\b0 changes\s*$/.test(second.trim()), second.split("\n").slice(-3));
  const rowAfter = (await db.from("lab_route_roles").select("roles").eq("route_id", RID).single()).data;
  const canon = (v) => JSON.stringify(v, (k, x) => x && typeof x === "object" && !Array.isArray(x) ? Object.keys(x).sort().reduce((o, q) => (o[q] = x[q], o), {}) : x);   /* jsonb reorders keys */
  check("the lab rows are left as they were", canon(rowAfter.roles) === canon(rowBefore), [canon(rowAfter.roles).slice(0, 120), canon(rowBefore).slice(0, 120)]);
  console.log(out.split("\n").slice(-2).join(" | "));
} finally {
  await clean();
}
console.log(failed ? `${failed} FAILED` : "all passed");
process.exit(failed ? 1 : 0);
