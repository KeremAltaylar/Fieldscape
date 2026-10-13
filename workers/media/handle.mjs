/* Phase 1b (docs/superpowers/specs/2026-10-13-media-r2-design.md): Fieldscape's recordings from R2, with storage.objects'
   rules - anyone reads a published, non-sensitive feature's folder (public.public_features, not fuzzed: a sensitive
   point's WAV can carry its true coordinates in its bext/iXML chunks - migration 0016); a setter (public.is_setter())
   reads and writes everything; anything else is refused, and so is everything when Supabase cannot answer (fail closed).
   fetch and R2 (env.MEDIA) are injected so the rules are tested in node (tests/media-worker.test.mjs). */

function cors(req, env) {
  const o = req.headers.get("Origin"), ok = o && (env.ALLOW_ORIGINS || "").split(",").map((s) => s.trim()).includes(o);
  return ok ? { "Access-Control-Allow-Origin": o, Vary: "Origin" } : { Vary: "Origin" };
}
const answer = (req, env, status, body, headers = {}) => new Response(body, { status, headers: { ...cors(req, env), ...headers } });

/* the object path after /o/, decoded: no traversal, no backslash, not empty, not absurd */
function pathOf(url) {
  if (!url.pathname.startsWith("/o/")) return { route: false };
  let p;
  try { p = decodeURIComponent(url.pathname.slice(3)); } catch { return { route: true, bad: true }; }
  const bad = !p || p.length > 512 || p.startsWith("/") || p.includes("\\") || p.includes("\0") || p.split("/").some((s) => s === ".." || s === "." || s === "");
  return { route: true, bad, path: p };
}

/* Each request asks Supabase afresh (a few hundred bytes): unpublishing or a sensitive flag revokes on the very next request
   (migration 0016's promise), and a Supabase blip is never remembered (R2 final review I2: a cache kept grants up to 60 s) */
async function isPublic(env, deps, folder) {
  if (!/^[0-9a-f-]{36}$/i.test(folder)) return false;
  {
    const r = await deps.fetch(env.SUPABASE_URL + "/rest/v1/public_features?select=id,fuzzed:properties->fuzzed&id=eq." + folder,
      { headers: { apikey: env.SUPABASE_ANON_KEY, Authorization: "Bearer " + env.SUPABASE_ANON_KEY } });
    if (!r.ok) return false;
    const rows = await r.json();
    return Array.isArray(rows) && rows.length > 0 && rows[0].fuzzed !== true;
  }
}
async function isSetter(env, deps, token) {
  if (!token) return false;
  {
    const r = await deps.fetch(env.SUPABASE_URL + "/rest/v1/rpc/is_setter",
      { method: "POST", headers: { apikey: env.SUPABASE_ANON_KEY, Authorization: "Bearer " + token, "Content-Type": "application/json" }, body: "{}" });
    return r.ok && (await r.json()) === true;
  }
}

export async function handle(req, env, deps) {
  const url = new URL(req.url), p = pathOf(url);
  if (!p.route) return answer(req, env, 404, "not found");
  if (req.method === "OPTIONS") {
    return answer(req, env, 204, null, { "Access-Control-Allow-Methods": "GET, PUT, OPTIONS",
      "Access-Control-Allow-Headers": "authorization, content-type, x-upsert", "Access-Control-Max-Age": "86400" });
  }
  if (p.bad) return answer(req, env, 400, "bad path");
  if (req.method !== "GET" && req.method !== "PUT") return answer(req, env, 405, "method not allowed");
  const token = (req.headers.get("Authorization") || "").replace(/^Bearer\s+/i, "");
  let pub = false, setter = false;
  try {
    if (req.method === "GET") pub = await isPublic(env, deps, p.path.split("/")[0]);
    if (!pub) setter = await isSetter(env, deps, token);
  } catch { return answer(req, env, 403, "refused"); }   /* Supabase unreachable: fail closed */
  if (!pub && !setter) return answer(req, env, 403, "refused");

  if (req.method === "GET") {
    const o = await env.MEDIA.get(p.path);
    if (!o) return answer(req, env, 404, "not found");
    return answer(req, env, 200, o.body, { "Content-Type": (o.httpMetadata && o.httpMetadata.contentType) || "application/octet-stream",
      "Cache-Control": pub ? "public, max-age=3600" : "private, no-store" });
  }
  if (req.headers.get("x-upsert") === "false" && await env.MEDIA.head(p.path)) return answer(req, env, 409, "exists");
  await env.MEDIA.put(p.path, req.body, { httpMetadata: { contentType: req.headers.get("content-type") || "application/octet-stream" } });
  return answer(req, env, 200, JSON.stringify({ path: p.path }), { "Content-Type": "application/json" });
}
