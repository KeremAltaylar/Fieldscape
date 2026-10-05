/* Sample harmony 3b: a route's role setups and samples made in the lab - setters only, apart from the route's own row
   (docs/superpowers/specs/2026-10-04-samples-3b-role-samples-design.md). */
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { createClient } from "@supabase/supabase-js";
import { anon, service } from "./clients.mjs";

const db = service();
const EMAIL = "probe-lab-setter@fieldarc.test";
const PASSWORD = "probe-" + "y".repeat(16);
const DRAFT = "00000000-0000-4000-8000-00000000003b";      /* an unpublished route (an id no other test file uses: c1/c2 are publish and fuzz) */
/* a published route: one of the real ones - a published fixture shows in public_features, which other test files read
   whole while they run in parallel (they failed with one present, measured) */
let PUB = null, OBJ = null;
let userId = null, rows0 = null;

before(async () => {
  const { data } = await db.auth.admin.createUser({ email: EMAIL, password: PASSWORD, email_confirm: true });
  userId = data.user.id;
  await db.from("setters").insert({ id: userId, name: "probe-lab" });
  await db.from("features").delete().eq("id", DRAFT);
  const line = { type: "LineString", coordinates: [[28.99, 41.18], [28.991, 41.181]] };
  await db.from("features").insert([
    { id: DRAFT, place: "LAB3B", kind: "route", geometry: line, properties: { published: false, name: "lab draft" } }
  ]);
  PUB = (await anon().from("public_features").select("id").eq("kind", "route").limit(1)).data[0].id;
  OBJ = `lab/${PUB}/probe-lab-roles.wav`;
  rows0 = (await db.from("features").select("id, properties, updated_at").in("id", [DRAFT, PUB]).order("id")).data;
});

after(async () => {
  await db.from("lab_route_roles").delete().eq("route_id", DRAFT);
  if (OBJ) { await db.storage.from("recordings").remove([OBJ]); }
  await db.from("features").delete().eq("id", DRAFT);
  await db.from("audit").delete().eq("setter_id", userId);
  await db.from("setters").delete().eq("id", userId);
  if (userId) { await db.auth.admin.deleteUser(userId); }
});

async function asSetter() {
  const c = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_ANON_KEY, { auth: { persistSession: false } });
  const { error } = await c.auth.signInWithPassword({ email: EMAIL, password: PASSWORD });
  assert.equal(error, null, error?.message);
  return c;
}

test("lab roles: a setter saves a route's role setups and reads them back; a second save replaces them whole", async () => {
  const s = await asSetter();
  const first = { v3: { synth: "s-freeze", gain: 0.5, sampler: { focus: 0.8 } }, voice: { synth: "fm" } };
  let r = await s.from("lab_route_roles").upsert({ route_id: DRAFT, roles: first, updated_by: userId });
  assert.equal(r.error, null, r.error?.message);
  r = await s.from("lab_route_roles").select("roles").eq("route_id", DRAFT).single();
  assert.deepEqual(r.data.roles, first);
  const second = { sect: { synth: "s-pulsar" } };
  r = await s.from("lab_route_roles").upsert({ route_id: DRAFT, roles: second, updated_by: userId });
  assert.equal(r.error, null, r.error?.message);
  r = await s.from("lab_route_roles").select("roles").eq("route_id", DRAFT).single();
  assert.deepEqual(r.data.roles, second);
});

test("lab roles: anon can neither read nor write the role setups", async () => {
  const { data, error } = await anon().from("lab_route_roles").select("*");
  assert.ok(error || (data ?? []).length === 0, "anon reached lab_route_roles");
  const w = await anon().from("lab_route_roles").insert({ route_id: DRAFT, roles: {} });
  assert.ok(w.error, "anon wrote lab_route_roles");
});

test("lab roles: a sample under lab/<published route id>/ stays setters-only", async () => {
  const s = await asSetter();
  const bytes = new Uint8Array(64).fill(7);
  const up = await s.storage.from("recordings").upload(OBJ, bytes, { upsert: true, contentType: "audio/wav" });
  assert.equal(up.error, null, up.error?.message);
  const mine = await s.storage.from("recordings").download(OBJ);
  assert.equal(mine.error, null, mine.error?.message);
  const theirs = await anon().storage.from("recordings").download(OBJ);
  assert.ok(theirs.error, "anon downloaded a lab sample");
});

test("lab roles: saving never touches the route's own row", async () => {
  const rows = (await db.from("features").select("id, properties, updated_at").in("id", [DRAFT, PUB]).order("id")).data;
  assert.deepEqual(rows, rows0);
});
