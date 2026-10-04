# Sample harmony 3b — a sample per role, saved with the route — design

Date: 2026-10-04 · Branch: `samples-3b` (from main `baf5194`, where 3a is live) · Status: **for Kerem's review**
Parent spec: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (D1–D13; sessions through 3a passed).
Previous: `docs/superpowers/specs/2026-10-04-instruments-3a-routes-play-samplers-design.md` (I1–I4).

## Goal

A setter sets each route role (Voice, Sections, Third voice) to a digital or a sampler synth with its controls — as in the
lab's Route panel now — and, for a sampler role, **uploads a sample for that role**. The role setups and samples are saved with
the route, so opening the route later, on any device where the setter is logged in, plays exactly what was saved.

Kerem, 2026-10-04: *"as in the old patch design, setters will establish these in the route's patch screen"*; *"setter will for
example select voice, sections and third voice, then if it's sample based he will also upload a sample"*.

## Decisions (2026-10-04 session)

| # | Question | Decision |
|---|---|---|
| S1 | What a sampler sound is made of | The **route's role** holds everything: synth, controls, and its own uploaded sample — **no separate library** (the earlier "instrument" idea folds into the role). |
| S2 | Samples per role | **One** to start (several across a range, for the pitch sampler, later if one stretches too far). |
| S3 | Re-using a sample on another route | Later, if the same file keeps being uploaded twice. |
| S4 | Live safety (D1) | Role setups are **not** written into the live route patch: the live site and the current apps would read an unknown synth name and fall back to FM, changing published routes. They are kept apart until the switch-over (sub-project 6). |

## Where it is saved

- **Role setups** go in a new table, `public.lab_route_roles` (`route_id` uuid primary key → `features.id`, `roles` jsonb,
  `updated_by`, `updated_at`), setters only (RLS by `public.is_setter()`, as every policy here), never read by the live engines,
  never in `public_features`. A separate table rather than a field inside the route's `properties`: no read-modify-write of a
  route row, so a setter's edit of the route can never be overwritten by the lab, nor the lab's by the setter.
- **`roles`** is `{ voice | sect | v3: { synth, gain, harm, index, sampler: { body, excite, method, mode, focus, colour, tune },
  sample: { path, name, analysis } } }` — the same fields the route patch uses (3a), plus the sample.
- **Samples** go to the private `recordings` bucket under **`lab/<route id>/<role>-<unix ms>.<ext>`** — the existing anon read
  policy opens paths whose first segment is a *published feature id*; `lab` never is, so lab samples stay setters-only. No
  delete (soft deletes, as everywhere): a replaced sample stays in the bucket, unreferenced.
- **The migration** (`supabase/migrations/0019_lab_route_roles.sql`) only adds; it is applied with `npm run db:apply` **with
  Kerem's OK** at that step.

## In the lab (the real `lab.html`, live or local — lab2 on claude.ai cannot log in)

- The lab uses the site's own login (supabase-js, the same browser session). Not logged in as a setter → the Route panel works
  as now with dropped files, and Save / Upload say "Log in as a setter on the site to save".
- A sampler role shows **Upload sample**: the file is decoded and analysed in the browser (`fs_analyse`, as now), uploaded,
  and shown by name with its pitch (or "unpitched"). Uploading again replaces the role's sample.
- **Save route** writes the three role setups for the selected route.
- **Choosing a route** loads its saved setups: each role's synth and controls, and each sample — downloaded, decoded, sent to
  the engine with its saved analysis (no re-analysis).
- A role's sample missing or failing to load: that role is silent and the panel says which, never a crash.

## How we know it works

| Area | Check |
|---|---|
| Round trip | save, reload the page, choose the route: the same synths, controls and sample names; the analysis identical |
| Sound | a reloaded route plays the same notes as before saving (the engine's note log), sampler roles sounding |
| Safety | saving touches only `lab_route_roles` and `lab/…` objects; the route's own row unchanged (byte for byte); anon cannot read `lab_route_roles` or a `lab/…` object (RLS checked as anon); the live engine's reading of the route unchanged |
| Failure | not logged in → Save says so; a missing sample → that role silent with a message |
| Regression | every existing suite |

The database and Storage checks run against the real project as anon and as a test setter (the `tests/*.test.mjs` pattern,
`.env.local`), cleaning up their own rows.

## Not in scope

The real route patch screen and the live engines playing samplers, and the switch-over (3c / sub-project 6); several samples per
role (S2); a picker of samples from other routes (S3); publishing anything.
