# Sample harmony 3b — a sample per role, saved with the route Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A setter's per-role setups (synth, controls, an uploaded sample per sampler role) are saved with a route — setters only, apart from the live patch — and load back in the lab.

**Architecture:** A new table `public.lab_route_roles` (RLS: setters) holds each route's role setups as JSON; samples go to the private `recordings` bucket under `lab/<route id>/…` (outside the anon-read prefix). The lab gains the site's supabase-js client (shared session), Upload per sampler role, Save route, and load-on-choose.

**Tech Stack:** Supabase (Postgres, RLS, Storage) via `npm run db:apply`; node:test DB tests (`tests/*.test.mjs`, `tests/clients.mjs`, `.env.local`); the lab (`lab.html`, `web/lab.js`), Puppeteer (`core/tests/web_lab.mjs`).

**Spec:** `docs/superpowers/specs/2026-10-04-samples-3b-role-samples-design.md` (S1–S4).

## Global Constraints

- Nothing live changes: the route's own row is never written by the lab; live engines never read `lab_route_roles`.
- Setters only: table and `lab/…` objects unreadable and unwritable by anon; RLS through `public.is_setter()`.
- No deletes (soft everywhere): a replaced sample stays unreferenced in the bucket; a table row is upserted, never deleted by the lab.
- The migration is applied to the live database only with Kerem's OK at that step.
- `roles` JSON = `{ voice|sect|v3: { synth, gain, harm, index, sampler{…}, sample{ path, name, analysis } } }`.
- Not logged in → the panel works with dropped files; Save / Upload say "Log in as a setter on the site to save".

## Review Focus

1. **Two setters saving the same route** — last write wins on the whole `roles` (acceptable for a lab); never a partial mix. Task 2 test (two upserts → the second whole).
2. **A route deleted (soft) after its roles were saved** — the row stays; the lab only lists live routes. Task 1 (FK `on delete cascade` only for hard deletes, which never happen).
3. **A large upload** (a 3-minute WAV) — the bucket's existing size handling; the analysis keeps 30 s as now. Task 2.
4. **The session expiring mid-session** — Save fails with the server's message, nothing half-written. Task 2.
5. **Loading a route whose sample object is missing** — that role silent, the panel says which. Task 2 test.

---

### Task 1: The table, its policies, and the Storage prefix

**Files:** Create `supabase/migrations/0019_lab_route_roles.sql`, `tests/lab-roles.test.mjs`.

- [ ] **Step 1: Failing test** `tests/lab-roles.test.mjs` (pattern of `tests/authority.test.mjs`): a probe setter (service creates the user + `setters` row) and an unpublished test route (`features`, kind route, id `…0000c1`) and a *published* test route (`…0000c2`); then:
  - the setter upserts `lab_route_roles { route_id: c1, roles: {v3:{synth:"s-freeze"}} }` and reads it back equal;
  - a second upsert replaces `roles` whole;
  - anon `select` on `lab_route_roles` → no rows (or a permission error); anon `insert` → error;
  - the setter uploads `lab/<c2>/v3-1.wav` and downloads it; anon download of that object → error (even though c2 is published);
  - the route rows c1/c2 unchanged (updated_at and properties as inserted);
  - after: delete the test rows / objects / user with the service client.
  Run `npm test -- --test-name-pattern "lab roles"` → FAIL (relation does not exist).
- [ ] **Step 2: Migration** `0019_lab_route_roles.sql`:

```sql
-- Sample harmony 3b: a route's role setups made in the lab (synth, controls, the sample of each sampler role), kept apart
-- from the route's own patch until the switch-over - the live site and apps never read this table, and the lab never
-- writes the route row (no read-modify-write of properties). Setters only, as everything a setter makes.
create table if not exists public.lab_route_roles (
  route_id   uuid primary key references public.features (id) on delete cascade,
  roles      jsonb not null default '{}'::jsonb,
  updated_by uuid references auth.users (id) on delete set null,
  updated_at timestamptz not null default now()
);
alter table public.lab_route_roles enable row level security;
drop policy if exists lab_route_roles_setter on public.lab_route_roles;
create policy lab_route_roles_setter on public.lab_route_roles
  for all to authenticated using (public.is_setter()) with check (public.is_setter());
drop trigger if exists lab_route_roles_touch on public.lab_route_roles;
create trigger lab_route_roles_touch before update on public.lab_route_roles
  for each row execute function public.touch_updated_at();
-- Samples: recordings/lab/<route id>/<role>-<ms>.<ext>. The anon read policy (0015) opens paths whose first segment is a
-- published feature id; "lab" never is, so these stay setters-only under the existing setter policies (0012).
```
- [ ] **Step 3: Ask Kerem**, then `npm run db:apply supabase/migrations/0019_lab_route_roles.sql`.
- [ ] **Step 4:** Run the test → PASS; `npm test` all green. Commit.

---

### Task 2: The lab saves and loads role setups and samples

**Files:** `lab.html` (supabase-js script, Upload / Save controls, a login line), `web/lab.js`, `core/tests/web_lab.mjs`.

- [ ] **Step 1: Failing browser checks** (`web_lab.mjs`, a new block; the node side creates the probe setter and an unpublished test route via the service client and removes them after):
  - not signed in: Save shows "Log in as a setter on the site to save";
  - `fsLab.signIn(email, password)` (test hook over the client's `signInWithPassword`); with the test route selected and Third voice = Freeze: Upload a generated WAV for v3 → the role shows its name and "unpitched"; Save → the table row (read by the node service client) has `roles.v3.synth == "s-freeze"`, `roles.v3.sample.path` starting `lab/<route id>/v3-`, and the analysis' `f0`;
  - reload the page, sign in, choose the route: the v3 menu reads Freeze, its controls the saved values, its sample name shown; Play route → sound (level > −40 dBFS);
  - the test route's own row unchanged;
  - a saved path pointing at a missing object → that role says "sample missing", the route still plays.
  Run → FAIL.
- [ ] **Step 2: Implement:** `lab.html` loads `https://unpkg.com/@supabase/supabase-js@2/dist/umd/supabase.js`; `lab.js` makes `sb = supabase.createClient(SUPA, ANON, { auth: { persistSession: true, autoRefreshToken: true } })` (the site's session); per sampler role an `<input type=file>` Upload → decode → `fs_analyse` → `sb.storage.from("recordings").upload("lab/" + route.id + "/" + role + "-" + Date.now() + ext, file)` → the role's `sample = { path, name, analysis }` (analysis kept as the JSON `fs_analyse` returned) and the decoded buffer kept for the engine; **Save route** → `sb.from("lab_route_roles").upsert({ route_id, roles, updated_by })`; on choosing a route → select its row; for each role restore synth / controls, and for a sample `sb.storage.from("recordings").download(path)` → decode → send (role + analysis) to the engine; errors per role shown on the panel. Not signed in → the panel's previous behaviour (the loaded file) and the message on Save / Upload.
- [ ] **Step 3:** Run → PASS; all suites. Commit. Final review; then a local check with Kerem's login on the live lab after merge (his OK).

## Self-review notes

- Spec coverage: S1 role holds sample (T2); S2 one sample (T2); S4 apart from the live patch (T1 table, T2 never writes the route row — checked); Storage prefix (T1 anon check); round trip, sound, failure, safety (T1/T2 tests).
- The migration step stops for Kerem's OK (a live database change).
