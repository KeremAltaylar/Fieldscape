# Sample harmony 6a: the website switch-over

Date: 2026-10-09. Parent: `2026-09-30-sample-harmony-design.md`, sub-project 6 ("move to the site and apps").

## Decisions (Kerem, 2026-10-08/09)
| # | Decision |
|---|---|
| K1 | Digital synths stay, beside the samplers (the parent's "no digital synths" is dropped). |
| K2 | Website first (6a), then the apps (6b). |
| K3 | Lab setups are carried over on release day: listeners hear Koşuyolu and the points as Kerem hears them in lab mode. |
| K4 | Storage A: settings in the route / point; each recording's pitch analysis in a small JSON file beside it in Storage. |
| K5 | Follow, resonate, body sit in a stretch point's **Pitch & harmony** section beside tune, focus, partials, layers and glide (glide drives follow and resonate too). |

Sections 1-4 were approved in chat ("Yeah lets go", "Lets go").

## 1. What setters see, and how it saves
- The **Samplers (lab)** switch is removed. A signed-in setter's patch panel always shows, per voice: the instrument menu
  in Digital / Sampler groups; for a sampler voice its controls (body/excite, method/mode, model, focus/position/ratio,
  colour, tune, octave), the sample block (upload, name, warning, waveform); for every voice the EQ graph and the live row
  (meter and notes). These are today's lab rows, unchanged in look.
- **Saving:** every one of these writes the route's own `properties.patch` through `commitPatch` (heard at once, local
  draft, reaches listeners on **Publish**) - exactly like tempo or chords. No `lab_route_roles` writes.
- **Patch format** (per role `voice` / `sect` / `v3`, all optional, absent = today's digital voice):
  - `synth`: a digital name or a sampler (`s-resonator`, `s-harmonic`, `s-formant`, `s-pulsar`, `s-freeze`, `s-retune`,
    `s-fm`, `s-am`)
  - `sampler`: `{ body, excite, method, mode, focus, colour, tune, octave }`
  - `eq`: `[[hz, db, q] x5]` (any voice, digital too)
  - `sample`: `{ path, name, analysis_path, f0 }` - `path` the WAV, `analysis_path` its analysis JSON, `f0` for the panel's
    warning without fetching the file
- **Uploading a sample** writes both files straight to Storage under the route's own folder:
  `recordings/<route id>/<role>-<ms>.wav` and `recordings/<route id>/<role>-<ms>.json` (the analysis, as `fs_analyse`
  returns it). Setters can write there; anon reads only once the route is published (policies 0012/0016, unchanged).
- **Stretch point panel:** `follow` (0-1), `resonate` (0-1), `body` (String / Tube / Bell) join **Pitch & harmony**, in
  `properties.sound.shape`. **Grains and rhythm points:** `follow` in their panels, in `properties.rhythm.follow`.
- **Point pitch tracks** (follow needs one; Resonate reads none): when a setter sets follow > 0 on a point whose recording
  has no track file, the page analyses the recording once (its first 60 s, as lab mode does) and uploads
  `recordings/<point id>/track-<ms>.json`, its path in `properties.sound.track_path` (stretch, grains) - and for rhythm
  hits, one file per hit slot's recording, its path beside that slot's `storage_path`. Listeners and apps never analyse.

## 2. What listeners hear
- The public engine `web/core.wasm` is rebuilt from the current core (today's lab engine: samplers, octave, EQ, follow,
  resonate, digital notes) and exports what the lab build exports.
- A listener page, for each sampler voice of a route in reach: downloads `sample.path` and `sample.analysis_path`, prepares
  the sample as lab mode does (silence cut, `FsRoles.prepare`), and sends both to the engine. For each following point:
  downloads its `track_path` and sends the track. Cached like recordings (IndexedDB).
- Routes and points with none of the new keys sound exactly as today (fingerprint-checked).
- Until 6b, the installed apps play a sampler voice as their default digital synth (the core's unknown-synth fallback) and
  ignore EQ / follow / resonate. Not silent.

## 3. Release day
1. Tag + full backup (`tools/backup.mjs`), as for every release.
2. Push the code: `index.html`, `web/core.wasm`, worklet, `web/lab-roles.js`, service-worker cache bump - together.
   Nothing sounds different yet (no route has the new keys).
3. Carry-over script (`tools/carry-over-6a.mjs`, service key, run once with Kerem's OK; dry run first prints every change):
   - each `lab_route_roles` route row: for each sampler role, copy the sample from `lab/<route>/...` to
     `<route id>/<role>-<ms>.wav`, write its analysis JSON beside it (from the row's saved analysis), and write
     synth / sampler / eq / sample into `properties.patch.<role>`; a digital role with an EQ gets `eq` only.
   - each point row: follow / resonate / body into `properties.sound.shape` (stretch) or `properties.rhythm.follow`
     (grains/hits); for follow > 0, analyse the recording in node (the lab wasm's `fs_analyse`) and upload its track file.
   - updates go through the `features` row (bumping `updated_at`) so pages and apps sync; `lab_route_roles` is left as is.
4. Verify live: a signed-out listener walks Koşuyolu in headless Chrome - sampler voices sounding (role meters), no page
   errors, the live `core.wasm` byte-identical to the tested build, the points' tracks loaded.

Code before data: the other order lets the old public engine play Koşuyolu's sampler voices as FM for the minutes between.

## 4. Testing and rollback
- The lab browser tests become tests of the real panel and the public page: a sampler voice set, saved into the patch,
  published, heard by a signed-out listener; a sample's files land in the route's folder; a point's follow writes its track
  file and a listener's engine gets the track from the file; Resonate and follow in Pitch & harmony; EQ and octave saved in
  the patch. Routes without the new keys: the core's note fingerprints and `web_core` walk unchanged.
- `index.html` keeps no `signInWithPassword` (tests sign in through `__fa.sb`).
- Fresh whole-branch review, one fix pass, as always.
- **Rollback:** revert the release merge (the old page and engine return) and restore the routes and points the
  carry-over touched from the backup. Copied files are harmless if left.

## Out of scope (6a)
- The apps (6b). The instrument library, unpitched colouring, per-point scale choice, per-hit analysis (parent plan items
  not built; Kerem has not asked for them).
- Removing `lab.html` / `lab_route_roles` (kept as a record and a bench).
