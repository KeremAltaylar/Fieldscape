# Sample harmony 7: Resonate inside the stretch (lab)

Kerem, 2026-10-08: *"I want a total super consonance and good harmony"*; the voices' sampler synths for a bed point, but
*"I want the stretch at the end since it's the best drone machine we can have; if we stretch then resonate I will lose the
cloudy ambient aura"*. Chosen: option 3 - the resonance inside the stretch, before its random phases make the cloud.
Lab mode only (D1: `web/core.wasm` untouched).

## Why inside the stretch is the same chain
Resonator, Harmonic filter and Formant are filters. Filtering a sound and then cutting it into bands equals cutting it into
bands and then shaping them - which is what the stretch's shaping stage does, before the phase randomisation (the cloud).
So "sampler synth, then stretch" is exact here, and a chord change reaches the next frames at once (no seconds-old material
in the wrong chord, which a live chain or a pre-render would play).

## What it does
Per frame, in the shaping stage, after transpose/follow and before layers and tune:
- **Excitation:** the frame's magnitudes smoothed over +-1/6 octave (the recording's colour at that moment) - so the chord
  rings wherever the recording has energy, pitched or not.
- **Response:** peaks at every chord note in every octave from 55 Hz up, with the body's overtones:
  - String (0): overtones k = 1..Partials, weight 1/k
  - Tube (1): odd overtones only, weight 1/k
  - Bell (2): 1, 2.76, 5.40, 8.93 x f, weights 1, 0.6, 0.35, 0.2
  Peak width from Focus (as Tune: pure at 0, breathy at 1). Built as a table over bins, rebuilt only when the chord, Focus,
  Partials or body change (spread over frames like the tune comb).
- **Mix:** x = (1 - Resonate) x + Resonate (excitation x response); the frame's energy then goes back to the stretch's own
  (the existing normalisation): level does not change with Resonate, only the colour.
- The chord glides as Tune's does (Glide). Resonate 0 = the stretch exactly as before, bit for bit.

## Controls (point, lab)
- `sound.shape.resonate` 0..1 (default 0), `sound.shape.body` 0..2 (default 0); Focus and Partials shared with Tune.
- In lab mode the point panel's header gains "resonate" and a body menu beside "follow the chord (lab)"; saved under the
  point's id in `lab_route_roles` as `roles.point.resonate` / `roles.point.body`, overlaid for the engine (as follow).

## Checks
- Resonate 0 (or absent) output is identical to before.
- White noise, a D minor chord (D F A), Partials 1: at Resonate 1 most of the energy sits within 25 cents of D, F or A
  pitch classes (>= 60%; noise alone ~12%); the level within 1 dB of Resonate 0.
- Bell puts energy at 2.76 x the chord notes (+557 cents of each pitch class) where String does not.
- A chord change (to E major) moves the energy to the new chord within the glide time.
- Site: the point panel shows resonate and body in lab mode; saved and overlaid; Resonate 0 = the point as saved.
