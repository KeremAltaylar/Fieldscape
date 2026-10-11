# SDD ledger — plan: docs/superpowers/plans/2026-10-09-samples-6a-website-switch-over.md
Pre-flight: T3 consumes T1 core.wasm exports + T2 fromPatch/toPatch/session options - match; T4 consumes T3 samplersOn/analyseMono - match; T5 consumes T2 key shape + T4 __fa.trackFor - match.
Task 1: complete (commits 00f1ed9..320d456, tests: node core/tests/exports.mjs → PASS core.wasm exports the sampler calls)
Task 2: Ruling: tests/lab-wasm.test.mjs 'the live site's engine file is not the lab's' (the D1 guard) inverted to 'is the lab's' - 6a ends D1 by Kerem's decision; the release gate (merge with his OK) is the protection now - cost if wrong: none until release
Task 2: complete (commits 320d456..37ac794, tests: npm test → [34mℹ duration_ms 11992.0659[39m)
Task 3: Ruling: the listener check publishes the draft with the service key (the same properties the page's Publish upserts), not the Publish button - the publish pipeline is unchanged and has its own tests - cost if wrong: a publish-only bug in carrying sampler keys goes unseen here
Task 3: Ruling: a voice moved to a sampler keeps its previous digital synth in patch.<role>.digital, so choosing it again keeps its timbres (3c I2 needed the live synth, which the patch now replaces) - unit test added - cost if wrong: one extra key per voice
Task 3: Ruling: the EQ and the live notes/meter add no rows on the desk: the EQ is a curve button in the instrument row's readout cell, its graph floating over the column (Escape / click away close it; Escape no longer also deselects the route), the notes and meter in the on/off row's readout cell - the real panel must fit one screen (C-8; measured 125 px over with the lab rows) - cost if wrong: the EQ is one click further than in lab mode
Task 3: Ruling: dropped lab-only checks (the switch, lab mode off/on, core-lab failing, the lab-table "Not saved" message on an expired session) - their code is gone; patch edits follow the page's own draft/publish rules - cost if wrong: none
Task 3: Ruling: removed __fa.labMode (now __fa.samplers); renamed labRolesFor/labRoleSends to rolesFor/roleSends as the plan says; added patchEdit/reloadSession/coreRoles test hooks - cost if wrong: none
Task 3: complete (commits 37ac794..9b2541e, tests: node core/tests/web_patch.mjs → all passed)
Task 4: Ruling: the browser test covers a stretch point; grains and hits get the same follow row and run the same ensureTrack/pointTrack code keyed by mode and slot, untested in the browser here - cost if wrong: a grains/hits-only track bug unseen until the final review or Kerem's test
Task 4: Ruling: the stretch panel's row builders (buildPxRow, buildPxSelect) now set data-k (the tests and the knobs find a control by it) - cost if wrong: none
Task 4: Ruling: the recording attach handler became attachRecording(f, file) so the re-attach path and its test hook are one code path - cost if wrong: none
Task 4: complete (commits 9b2541e..f9499d2, tests: npm test → [34mℹ duration_ms 11967.3101[39m)
Task 5: Ruling: the carry-over reads recordings through a signed link it makes with the service key (__fa.trackForUrl), not __fa.trackFor's anon download - fuzzed points are not anon-readable (0016) - cost if wrong: none
Task 5: complete (commits f9499d2..cfd263c, tests: node --env-file=.env.local core/tests/carry_over.mjs → all passed)
Final review: fresh Opus reviewer on 00f1ed9..cfd263c - no Critical
Final: fixed I1 a failed or in-flight sample upload deleted the saved sample from the patch (a silent voice on Publish) - toPatch keeps it while a sample has no path - unit "a sample still uploading (or failed) keeps the patch's saved sample" RED -> GREEN
Final: fixed I2 Reset to default was half undone by the next sampler/EQ touch (the session kept the old voices) - the session reloads from the fresh patch - site "after Reset to default, touching an EQ writes no old sampler back" RED -> GREEN
Final: fixed I3 a track made for a recording replaced meanwhile landed on the new recording - a token (audio.added_at / the hit object) checked before writing, the new one's made after - site "the track kept is the new recording's (660 Hz)" RED (440) -> GREEN
Final: fixed I4 every stretch drag retried an unreadable recording's track (a toast each time) - remembered per recording until it changes - site "tried once over four drags" RED -> GREEN
Final: fixed (re-graded Important from Minor 6) web/lab-roles.js missing from the offline shell (a walk started offline played sampler voices silent) - in SHELL_FILES - sw test RED -> GREEN
Final: Ruling: two restore lines in the site test stored a literal string instead of the saved sample - hidden until I2 was fixed (the stale session had written the good sample back); corrected - cost if wrong: none
Final: minor (deferred): samples uploaded to a published route's folder are anon-readable before Publish (and replaced ones are never removed); point track JSONs too
Final: minor (deferred): routeSamples/pointTrack do not check publication - a signed-out device holding a local unpublished sampler route requests its files (RLS refuses; the voice stays silent)
Final: minor (deferred): an upload finishing after the panel moved to another route is not written into its route (the file stays unreferenced, no message)
Final: minor (deferred): a panel re-render (a sample decode finishing just after opening) closes an open EQ popover
Final: minor (deferred): the EQ button inside the instrument row's <label> joins the select's accessible name (and is interactive content inside a label)
Final: minor (deferred): carry-over - no patch version guard (patchOf replaces a non-v17 patch); the Chrome temp profile is not removed and its path/port are fixed; ~30 s wait if FS_URL does not serve this branch; a partial failure leaves orphan copies
Final: minor (deferred): `digital` is not recorded when a voice had no synth before a sampler (falls back to the digital synth's defaults)
