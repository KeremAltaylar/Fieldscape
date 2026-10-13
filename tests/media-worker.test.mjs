/* Phase 1b: the recordings Worker keeps storage.objects' rules (migrations 0012, 0015, 0016) - anyone reads a published,
   non-sensitive feature's folder; a setter reads and writes everything; everything else is refused. Supabase, R2 and the
   cache are stand-ins here; tools/media-smoke.mjs runs the main cases against the deployed Worker. */
import { test } from "node:test";
import assert from "node:assert/strict";
import { handle } from "../workers/media/handle.mjs";

const PUB = "11111111-1111-4111-8111-111111111111", PRIV = "22222222-2222-4222-8222-222222222222";
const SENS = "33333333-3333-4333-8333-333333333333";   /* published but sensitive: the view lists it, fuzzed (migration 0016) */
const SETTER = "setter-token", WALKER = "walker-token";
function world({ supabaseDown = false, cache = null } = {}) {
  const state = { published: new Set([PUB]), down: supabaseDown };
  const objects = new Map([[PUB + "/take.wav", { body: "pub-audio", type: "audio/wav" }], [PRIV + "/take.wav", { body: "priv-audio", type: "audio/wav" }], [SENS + "/take.wav", { body: "sens-audio", type: "audio/wav" }]]);
  const calls = [];
  const env = {
    SUPABASE_URL: "https://x.supabase.co", SUPABASE_ANON_KEY: "anon", ALLOW_ORIGINS: "https://keremaltaylar.github.io,http://localhost:8765",
    MEDIA: {
      async get(k) { const o = objects.get(k); return o ? { body: o.body, httpMetadata: { contentType: o.type }, size: o.body.length } : null; },
      async head(k) { return objects.has(k) ? {} : null; },
      async put(k, body, opts) { objects.set(k, { body: await new Response(body).text(), type: opts && opts.httpMetadata && opts.httpMetadata.contentType }); },
    },
  };
  const deps = {
    async fetch(url, init) {
      calls.push(url);
      if (state.down) throw new Error("down");
      const auth = (init.headers.Authorization || init.headers.authorization || "");
      if (url.includes("/rest/v1/public_features")) {   /* the view: published and not sensitive */
        const id = new URL(url).searchParams.get("id").replace("eq.", "");
        return new Response(JSON.stringify(state.published.has(id) ? [{ id, fuzzed: null }] : id === SENS ? [{ id, fuzzed: true }] : []), { status: 200 });
      }
      if (url.includes("/rest/v1/rpc/is_setter")) return new Response(JSON.stringify(auth === "Bearer " + SETTER), { status: auth === "Bearer anon" ? 401 : 200 });
      return new Response("?", { status: 404 });
    },
    cache,
  };
  return { env, deps, objects, calls, state };
}
const req = (method, path, { token, body, headers = {} } = {}) => new Request("https://m.example/o/" + path, {
  method, body, headers: { ...(token ? { Authorization: "Bearer " + token } : {}), Origin: "https://keremaltaylar.github.io", ...headers } });

test("anyone reads a published feature's recording", async () => {
  const w = world(); const r = await handle(req("GET", PUB + "/take.wav"), w.env, w.deps);
  assert.equal(r.status, 200); assert.equal(await r.text(), "pub-audio"); assert.equal(r.headers.get("content-type"), "audio/wav");
  assert.match(r.headers.get("cache-control"), /public/); assert.equal(r.headers.get("access-control-allow-origin"), "https://keremaltaylar.github.io");
});
test("an unpublished (or sensitive) feature's recording is refused without a setter", async () => {
  const w = world();
  assert.equal((await handle(req("GET", PRIV + "/take.wav"), w.env, w.deps)).status, 403);
  assert.equal((await handle(req("GET", PRIV + "/take.wav", { token: WALKER }), w.env, w.deps)).status, 403);
});
test("a sensitive point's recording is never anon-readable (its WAV can carry the true coordinates - migration 0016)", async () => {
  const w = world();
  assert.equal((await handle(req("GET", SENS + "/take.wav"), w.env, w.deps)).status, 403);
  assert.equal((await handle(req("GET", SENS + "/take.wav", { token: SETTER }), w.env, w.deps)).status, 200);
});
test("a setter reads everything, not cached publicly", async () => {
  const w = world(); const r = await handle(req("GET", PRIV + "/take.wav", { token: SETTER }), w.env, w.deps);
  assert.equal(r.status, 200); assert.equal(await r.text(), "priv-audio"); assert.match(r.headers.get("cache-control"), /private|no-store/);
});
test("a missing file the caller may read is a 404", async () => {
  const w = world(); assert.equal((await handle(req("GET", PUB + "/none.wav"), w.env, w.deps)).status, 404);
});
test("a setter writes; a walker or anyone else may not", async () => {
  const w = world();
  const ok = await handle(req("PUT", PRIV + "/new.json", { token: SETTER, body: "{}", headers: { "content-type": "application/json" } }), w.env, w.deps);
  assert.equal(ok.status, 200); assert.equal(w.objects.get(PRIV + "/new.json").type, "application/json");
  assert.equal((await handle(req("PUT", PUB + "/x.wav", { token: WALKER, body: "x" }), w.env, w.deps)).status, 403);
  assert.equal((await handle(req("PUT", PUB + "/x.wav", { body: "x" }), w.env, w.deps)).status, 403);
});
test("x-upsert: false refuses an existing file (409), as Supabase's upsert:false", async () => {
  const w = world();
  assert.equal((await handle(req("PUT", PRIV + "/take.wav", { token: SETTER, body: "y", headers: { "x-upsert": "false" } }), w.env, w.deps)).status, 409);
  assert.equal(w.objects.get(PRIV + "/take.wav").body, "priv-audio");
  assert.equal((await handle(req("PUT", PRIV + "/take.wav", { token: SETTER, body: "y" }), w.env, w.deps)).status, 200);
});
test("bad paths are refused; a dot-segment walk is resolved by the URL first and meets the rules", async () => {
  const w = world();
  for (const p of ["", PUB + "/%2e%2e%2f" + PRIV + "%2ftake.wav", PUB + "/a%5cb", "a".repeat(600)])
    assert.equal((await handle(req("GET", p), w.env, w.deps)).status, 400, p);
  assert.equal((await handle(req("GET", PUB + "/../" + PRIV + "/take.wav"), w.env, w.deps)).status, 403);
});
test("a Supabase failure refuses (fail closed)", async () => {
  const w = world({ supabaseDown: true });
  assert.equal((await handle(req("GET", PUB + "/take.wav"), w.env, w.deps)).status, 403);
  assert.equal((await handle(req("PUT", PUB + "/x.wav", { token: SETTER, body: "x" }), w.env, w.deps)).status, 403);
});
test("CORS: the site's preflight is answered; another origin gets no allow header", async () => {
  const w = world();
  const pre = await handle(new Request("https://m.example/o/" + PUB + "/take.wav", { method: "OPTIONS", headers: { Origin: "http://localhost:8765", "Access-Control-Request-Method": "PUT" } }), w.env, w.deps);
  assert.equal(pre.status, 204); assert.equal(pre.headers.get("access-control-allow-origin"), "http://localhost:8765");
  assert.match(pre.headers.get("access-control-allow-headers"), /authorization/i); assert.match(pre.headers.get("access-control-allow-headers"), /x-upsert/i);
  const evil = await handle(new Request("https://m.example/o/" + PUB + "/take.wav", { headers: { Origin: "https://evil.example" } }), w.env, w.deps);
  assert.equal(evil.headers.get("access-control-allow-origin"), null);
});
test("other methods and routes are refused", async () => {
  const w = world();
  assert.equal((await handle(new Request("https://m.example/o/" + PUB + "/take.wav", { method: "DELETE", headers: { Authorization: "Bearer " + SETTER } }), w.env, w.deps)).status, 405);
  assert.equal((await handle(new Request("https://m.example/other"), w.env, w.deps)).status, 404);
});

/* R2 final review I2: with a working cache (a custom domain), a grant must not outlive the feature's state - migration 0016:
   "unpublishing a feature revokes access to its audio on the very next request" - and a Supabase blip must not be remembered */
function memCache() { const m = new Map(); return { async match(k) { const r = m.get(k.url); return r ? r.clone() : undefined; }, async put(k, r) { m.set(k.url, r); } }; }
test("unpublishing revokes on the very next request, cache or not", async () => {
  const w = world({ cache: memCache() });
  assert.equal((await handle(req("GET", PUB + "/take.wav"), w.env, w.deps)).status, 200);
  w.state.published.delete(PUB);
  assert.equal((await handle(req("GET", PUB + "/take.wav"), w.env, w.deps)).status, 403);
});
test("a Supabase blip is not remembered", async () => {
  const w = world({ cache: memCache() });
  w.state.down = true; assert.equal((await handle(req("GET", PUB + "/take.wav"), w.env, w.deps)).status, 403);
  w.state.down = false; assert.equal((await handle(req("GET", PUB + "/take.wav"), w.env, w.deps)).status, 200);
});
