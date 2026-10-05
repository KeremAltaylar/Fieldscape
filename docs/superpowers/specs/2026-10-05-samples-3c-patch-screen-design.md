# Sample harmony 3c — samplers on the real patch screen, in lab mode

Parent: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (D1: the live site and apps do not change until Kerem
approves). Builds on 3b (`2026-10-04-samples-3b-role-samples-design.md`): role setups and samples saved per route in
`lab_route_roles` and `recordings/lab/<route id>/`.

## Decisions (Kerem, 2026-10-05)

| # | Question | Decision |
|---|---|---|
| P1 | Where | The site's own patch panel (`index.html`, `#pp-body`), in a setter-only **lab mode**. Not a lab copy of the panel; not the switch-over. |
| P2 | Split | 3c is the screen. The new pitch models (looped, granular) and Position as Retune's Timbre 1 are **3d**. |
| P3 | Listening | **The whole site's sound, in lab mode.** The site already plays routes on the shared core (`web/core.wasm`, `fieldscape-engine`, built 2026-09-29, before the samplers). With lab mode on, the site's own engine loads `web/core-lab.wasm` (the same core with the samplers) and plays every route with its lab role setups and samples: map walk, simulation, bed, zones. Only the setter hears it. (An earlier "Preview button" choice was made on a wrong premise - that the site still played routes on Tone - and replaced.) |

## What the setter gets

1. **The switch.** Signed in as a setter (`setter.signedIn`), the patch panel header shows a toggle **"Samplers (lab)"**
   beside Reset / Close, remembered in `localStorage` (`fs-lab-mode`, wrapped in try/catch). Signed out it is not in the
   DOM. Off: the panel renders exactly as today (same rows, same order, same DOM).
2. **Role sections.** With lab mode on, the *instrument* row of Voice, Second voice and Third voice is a menu with two
   groups: **Digital** (`SYNTH_TYPES`, as today) and **Sampler** (Retune, Resonator, Harmonic filter, Formant, Pulsar,
   Freeze — `s-retune` … `s-freeze`).
3. **Sampler controls.** A role set to a sampler replaces its two timbre rows (`harm`, `index`) with the sampler's own:
   Focus (not Retune), Colour (named per synth as in the lab: Damping / Overtones / Vowel / Grain / Moment), Tune; Body and
   Excite for the Resonator; Method and Mode for Harmonic filter and Formant. Gain, drive, warp and the role's other rows
   stay and keep writing the live patch. A **Sample** row: Upload, and the sample's name · pitch (or "unpitched") ·
   state ("uploading…", "saved", "upload failed: …", "sample missing - this role is silent").
4. **The sound.** With lab mode on, the site's engine is `web/core-lab.wasm` instead of `web/core.wasm`. The engine is
   given each route with its lab role setups overlaid on the patch (the sampler synth and its controls; gain, drive, warp
   and the rest stay the live patch's), never saved. When the walker's route changes, the page sends that route's role
   samples to the engine (`role` and `analysis` messages, as the lab's); a sampler role without a sample plays silence.
   Turning lab mode on or off while sound plays restarts the engine on the other build. Controls moved in the panel are
   heard as patch edits are today (the debounced sync, ~0.12 s).

## Where things are saved

- **Digital** choices and every non-sampler row: into the live patch through `commitPatch`, exactly as today.
- **Sampler** choices, their controls and samples: into `lab_route_roles` (the 3b row format: `{voice, sect, v3}` each
  `{synth, gain, harm, index, sampler: {...}, sample: {path, name, analysis} | null}`), saved automatically 0.6 s after
  the last change. The live patch keeps that role's digital synth, so the public, the apps and the map walk play what
  they play now. The live patch never contains an `s-*` synth.
- Turning a role from Sampler back to Digital writes the digital synth to the live patch and the role's `synth` in
  `lab_route_roles` to that digital value (its sampler controls and sample kept for later).
- lab.html and the site read and write the same row; opening either shows the other's latest save.

## Shared code

`web/lab-roles.js` (new): everything 3b put in `lab.js` for role setups — load a route's roles (generation-guarded),
save (refused while loading, after a failed load, or with a sample that did not upload), the 30 s 16-bit WAV upload,
download and decode, missing-sample silence, escaped labels. It takes the Supabase client, the AudioContext and the
analyse function as arguments; it touches no DOM of its own except through callbacks. `lab.js` and `index.html` both use
it. The engine-side sending (`role`, `analysis`, `route`, `patch` messages; once per recording) moves with it.

## Safety

- Signed out, or lab mode off: no request to `lab_route_roles`, the engine is `web/core.wasm` as today, no DOM change.
- `web/core.wasm` (what the public runs) is not rebuilt in 3c. The core gains `fs_engine_role_source`,
  `fs_engine_role_analysis` and a `route_id` in the engine state; only `core-lab.wasm` is rebuilt with them.
- The live patch's bytes for a role set to a sampler are unchanged by anything sampler-related (checked byte for byte).
- RLS and grants as 3b (setters only); nothing new in the database.

## Failure

- Not a setter: no switch. Session expired: autosave says "Not saved: …" in the panel and keeps trying on the next change.
- A missing sample: that role silent, with the message. `core-lab.wasm` failing to load: lab mode says so and the
  site stays on `core.wasm`.

## Tests

| Kind | Check |
|---|---|
| Public | signed out: no switch, no `lab_route_roles` request, no wasm fetched, panel DOM identical to lab mode off |
| Off | lab mode off: panel rows identical to before this change (snapshot of row keys and order) |
| Sections | lab mode on: each role's menu has Digital and Sampler groups; choosing Freeze shows Focus/Colour/Tune/Sample, hides harm/index |
| Live patch | after choosing a sampler, moving its controls and uploading: the route's `properties.patch` unchanged byte for byte |
| Save | upload a sample, move Focus; reload: the role is Freeze again with its Focus and sample (row checked in the DB) |
| Back to digital | choosing a digital synth writes it to the live patch, as today |
| Sound | lab mode on: the engine is core-lab.wasm; walking the route with a Freeze role and its sample, that role sounds (muting its gain drops the level); lab mode off: core.wasm, as today |
| Core | `fs_engine_role_source` reaches the route's sampler role (a C++ test: silent without, sounding with); the state carries `route_id` |
| Lab | lab.html still passes `web_lab` and `web_lab_save` on the shared module |
| Regression | every existing suite |

## Not in scope

The new pitch models and Position (3d); the public's engine and the apps (sub-project 6); several samples per role;
publishing anything.
