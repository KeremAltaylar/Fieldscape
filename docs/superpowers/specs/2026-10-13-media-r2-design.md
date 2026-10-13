# Recordings on Cloudflare R2 (phase 1b) — design

Kerem, 2026-10-12/13: Supabase's Free plan restricted the project once (cached egress over 5 GB, 2026-10-10) and was at
37 % a day after the reset; "is there another option ... so we don't need a subscription". Approved design: recordings
move to the private R2 bucket `fieldscape-media` (created 2026-09-24, weur), served by a Cloudflare Worker that keeps
today's access rules. Supabase keeps the database and sign-in (2–6 % of their limits).

## Goals

- Listening downloads leave Supabase: R2 egress is free; the Worker's free tier (100 000 requests a day) is the ceiling.
- The same access rules as `storage.objects` today (migrations 0012, 0015, 0016):
  - anyone reads files under a **published, non-sensitive** feature's folder (`public.public_features`);
  - a **setter** (`public.is_setter()`) reads and writes everything.
- No change to paths: an R2 key is the Supabase object path (`<feature id>/…`).
- Nothing breaks during the move: the site falls back to Supabase for a file R2 does not have; the installed test apps
  keep reading Supabase until the 6b rebuild, so files stay in Supabase and new uploads are written to both.

## Non-goals

- Deleting the Supabase copies (after the 6b apps ship, by Kerem's OK).
- Range requests, signed URLs, image transforms (nothing uses them).
- Orphan cleanup when a feature is deleted (not done today either).

## The Worker (`workers/media/`)

`https://fieldscape-media.<account>.workers.dev`, R2 binding `MEDIA` → `fieldscape-media`; vars `SUPABASE_URL`,
`SUPABASE_ANON_KEY` (public by design, as in index.html); `ALLOW_ORIGINS` (the site, localhost:8765).

| Request | Rule | Answer |
|---|---|---|
| `GET /o/<path>` | folder (first path segment) is in `public_features` | 200 + body, `Cache-Control: public, max-age=3600` |
| | else `Authorization: Bearer <user JWT>` and `rpc/is_setter` is true | 200 + body, `Cache-Control: private, no-store` |
| | else | 403 (404 if the object is missing and the caller may read it) |
| `PUT /o/<path>` | setter only; `x-upsert: false` refuses an existing key (409), as Supabase's `upsert:false` | 200 `{ path }` |
| `OPTIONS` | CORS preflight for the allowed origins (authorization, content-type, x-upsert) | 204 |

- Checks call Supabase REST with the anon key (public check) or the user's token (setter check); answers are cached in
  the Worker's Cache API: public per folder 60 s, setter per token 300 s. A Supabase failure denies (fail closed).
- Path hygiene: decoded, no `..`, no leading `/`, ≤ 512 chars; content type stored with the object and returned.
- The logic is one module (`handle(request, env, deps)`) with fetch/R2/cache injected, unit-tested in node; the
  deployed Worker gets a live smoke test with probe files only.

## Clients

- **Site:** one `media` store in index.html with Supabase's bucket shape — `download(path) → {data: Blob, error}`,
  `upload(path, blob, {upsert, contentType}) → {data, error}` — so call sites change from
  `sb.storage.from("recordings")` to `media`. Download: Worker first; 404/network error → Supabase (during the move).
  Upload: Worker (R2), then Supabase best-effort (the old apps), the R2 result is the answer. lab-roles.js takes the
  store through its options (`o.store`), falling back to Supabase's bucket if absent.
- **Apps:** `Walk.swift` / `Walk.kt` fetch `<worker>/o/<path>` with the same Bearer header — in the 6b rebuild.
- **Tools:** `tools/migrate-r2.mjs` puts every file of the newest local backup into R2 (skipping identical keys);
  `backup.mjs` keeps reading Supabase until the copies are deleted.

## Migration

1. `tools/backup.mjs` (downloads only files newer than the 2026-10-11 backup — a few MB).
2. `tools/migrate-r2.mjs` → R2 (76 files, 192 MB, no Supabase egress); then a check that every Supabase object has an
   R2 twin of the same size (listing only, no download).
3. Deploy the Worker; then the site switch (merge on Kerem's OK); verify live: recordings load from the Worker
   (network panel / `__fa` counter), Supabase cached egress flat over the following day.

## Tests

- Worker unit tests (npm test): public read, private read refused anonymously, setter read, setter write, upsert:false
  409, non-setter write 403, sensitive feature refused, bad paths 400, CORS, fail-closed on a Supabase error.
- Live smoke (tools, probe feature ids): the same five main cases against the deployed Worker.
- Site: the existing local suites; one website run at the end (through the Worker, not Supabase).

## Costs if wrong

- Worker down → the site falls back to Supabase (the old egress) — no silence.
- A too-loose rule would expose an unpublished or sensitive recording: covered by the refusal tests (anon on an
  unpublished folder, anon on a sensitive one, non-setter token).
