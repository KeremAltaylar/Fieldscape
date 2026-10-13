# Sample harmony 6b: the apps get today's sound — design

Kerem, 2026-10-13: "yeah" to the 6b design (after 6a's "website first, then apps"). Parent: the 6a spec
(2026-10-09-samples-6a-website-switch-over-design.md, §2 "what listeners hear"), and phase 1b (recordings on R2).

## Goal

The iOS and Android apps sound like the website: route sampler voices, points that follow the chord, and everything the
shared core already does (resonate, Low end, envelopes, EQ, octave). Recordings come from the media Worker (R2).
The apps stay listeners: editing stays on the website.

## What changes

1. **Core: one shared preparation of a voice's sample** — `fs_piece_role_sample(piece, role, channels, frames, pcm, analysis_json)`:
   the website's `FsRoles.prepare` in C++ (the first 30 s; 50 ms blocks, a block sounds at the loudest block's −40 dB, silent
   runs under 0.25 s kept, sounding runs joined with 5 ms raised-cosine fades; the pitch track's frames in silent blocks
   dropped), then the role's source and analysis as `fs_piece_role_source` / `fs_piece_role_analysis` take them.
   `fs_role_compact` exposes the splice for the parity test: the same samples as the website's `compactData`, frame for frame.
2. **Recordings from the Worker** — both apps fetch `https://fieldscape-media.keremaltaylar.workers.dev/o/<path>`
   (anonymous: the apps play published points), cached as today.
3. **Route sampler voices** — when the route underfoot changes (`fs_piece_route`), for each voice whose patch synth is a
   sampler with `sample.path`: the WAV and `sample.analysis_path` are fetched (cached), decoded at the engine's rate, and
   handed to `fs_piece_role_sample`. A role that had a sample and has none on the new route gets silence first (as the web's
   `routeSamples`), so the last route's sample never plays on this one.
4. **Points that follow** — a stretch point's `sound.track_path`: the JSON's track → `fs_stretch_track` (follow from
   `sound.shape.follow`); a grains point's `sound.track_path` and each hit slot's `track_path` → `fs_piece_rhythm_track`.
5. **Build numbers** — Test 14 on both.

## Not in 6b

Editing in the apps; deleting the Supabase copies (after Kerem's phone test, by his OK).

## Tests

- Core: `fs_piece_role_sample` (a sample with a silence: compacted length and track as expected; silence clears a role).
- Parity (npm): `fs_role_compact` (wasm) against `FsRoles.compactData` on recordings with silences: identical lengths,
  samples within 1e-6, identical kept blocks.
- Android: the emulator walked onto a route with a sampler voice and to a following point — the voice and point sound (the
  app's own stats/log), no Supabase storage URL in the app's traffic.
- iOS: simulator over SSH, the same walk; then Kerem's iPhone through Xcode.
