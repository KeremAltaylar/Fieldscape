/* Holds the live media Worker to Supabase Storage, file by file: every published recording byte-identical,
   every unpublished one refused, Range served, a bad token / an upload without login / a traversal refused.
     node --env-file=.env.local tools/worker-check.mjs */
import crypto from "node:crypto";
import pg from "pg";
const W = "https://fieldscape-media.keremaltaylar.workers.dev";
const S = process.env.SUPABASE_URL, A = process.env.SUPABASE_ANON_KEY;
const db = new pg.Client({ connectionString: process.env.DATABASE_URL }); await db.connect();
const objs = (await db.query("select name from storage.objects where bucket_id='recordings' order by name")).rows.map((r) => r.name);
const pub = new Set((await (await fetch(S + "/rest/v1/public_features?select=id", { headers: { apikey: A, Authorization: "Bearer " + A } })).json()).map((r) => r.id));
await db.end();
const sha = (b) => crypto.createHash("sha256").update(Buffer.from(b)).digest("hex").slice(0, 16);
let same = 0, diff = 0, refusedOk = 0, refusedBad = 0;
for (const name of objs) {
  const isPub = pub.has(name.split("/")[0]);
  const w = await fetch(W + "/r/" + name);
  if (isPub) {
    const s = await fetch(S + "/storage/v1/object/authenticated/recordings/" + name, { headers: { apikey: A, Authorization: "Bearer " + A } });
    const [wb, sb] = [await w.arrayBuffer(), await s.arrayBuffer()];
    (w.status === 200 && s.status === 200 && sha(wb) === sha(sb)) ? same++ : (diff++, console.log("DIFF", name, w.status, s.status));
  } else {
    await w.arrayBuffer();
    (w.status === 403 || w.status === 404) ? refusedOk++ : (refusedBad++, console.log("SERVED UNPUBLISHED", name, w.status));
  }
}
console.log(`published: ${same} identical, ${diff} different · unpublished: ${refusedOk} refused, ${refusedBad} served`);
const one = objs.find((n) => pub.has(n.split("/")[0]));
const r = await fetch(W + "/r/" + one, { headers: { Range: "bytes=100-199" } });
console.log("range:", r.status, r.headers.get("content-range"), (await r.arrayBuffer()).byteLength, "bytes");
const bad = await fetch(W + "/r/" + one, { headers: { Authorization: "Bearer not-a-real-token" } });
console.log("bad token:", bad.status);
const put = await fetch(W + "/r/" + one, { method: "PUT", body: "x" });
console.log("upload without login:", put.status);
const trav = await fetch(W + "/r/../etc/passwd");
console.log("path traversal:", trav.status);
