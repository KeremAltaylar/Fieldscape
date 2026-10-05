# Sample harmony 3c — samplers on the real patch screen, in lab mode

Parent: `docs/superpowers/specs/2026-09-30-sample-harmony-design.md` (D1: the live site and apps do not change until Kerem
approves). Builds on 3b (`2026-10-04-samples-3b-role-samples-design.md`): role setups and samples saved per route in
`lab_route_roles` and `recordings/lab/<route id>/`.

## Decisions (Kerem, 2026-10-05)

| # | Question | Decision |
|---|---|---|
| P1 | Where | The site's own patch panel (`index.html`, `#pp-body`), in a setter-only **lab mode**. Not a lab copy of the panel; not the switch-over. |
| P2 | Split | 3c is the screen. The new pitch models (looped, granular) and Position as Retune's Timbre 1 are **3d**. |
| P3 | Listening | A **Preview** Play/Stop in the panel running the shared engine. The map walk's sound is unchanged. |

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
4. **Preview.** A Play / Stop button in the panel header (lab mode only). It plays the route through the shared engine
   (`web/core-lab.wasm`, `fieldscape-core` worklet, `piece` device, through the lab's `fs-limiter`), walking along at a
   fixed pace (one pass in 3 minutes), every role as set, each sampler role on its own sample. The engine and worklets
   load on the first Play only. Controls moved while it plays are heard within ~0.1 s.

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

- Signed out, or lab mode off: no request to `lab_route_roles`, no wasm fetched, no DOM change.
- The live patch's bytes for a role set to a sampler are unchanged by anything sampler-related (checked byte for byte).
- RLS and grants as 3b (setters only); nothing new in the database.

## Failure

- Not a setter: no switch. Session expired: autosave says "Not saved: …" in the panel and keeps trying on the next change.
- A missing sample: that role silent, with the message. Preview with no engine (wasm failed): the button says why.

## Tests

| Kind | Check |
|---|---|
| Public | signed out: no switch, no `lab_route_roles` request, no wasm fetched, panel DOM identical to lab mode off |
| Off | lab mode off: panel rows identical to before this change (snapshot of row keys and order) |
| Sections | lab mode on: each role's menu has Digital and Sampler groups; choosing Freeze shows Focus/Colour/Tune/Sample, hides harm/index |
| Live patch | after choosing a sampler, moving its controls and uploading: the route's `properties.patch` unchanged byte for byte |
| Save | upload a sample, move Focus; reload: the role is Freeze again with its Focus and sample (row checked in the DB) |
| Back to digital | choosing a digital synth writes it to the live patch, as today |
| Preview | Play: the engine plays (above -40 dBFS) with the sampler role sounding; Stop: silence |
| Lab | lab.html still passes `web_lab` and `web_lab_save` on the shared module |
| Regression | every existing suite |

## Not in scope

The new pitch models and Position (3d); the map walk's sound and the apps (sub-project 6); several samples per role;
publishing anything.
