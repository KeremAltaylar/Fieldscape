# Fieldscape's own stretch shaping — design

2026-09-27 · branch `setter-redesign`

## What Kerem asked for

- Replace PaulXStretch's modules (Harmonics, Tonal vs noise, Frequency/Pitch shift, Spread, Filter,
  Compressor, Ratios, Binaural; and warp/morph/field) with Fieldscape's own. None of them does
  anything on the core engine today (only stretch, window, freeze, onset are set).
- Wanted: **pitch & harmony**, **movement & space**, a **region**. Not wanted: tone colour,
  tonal-vs-noise.
- Harmony follows **the route's chord**.
- Noise recordings (wind, leaves) must gain pitch and harmony: a **tune** stage puts the sound onto
  the chord's notes.
- **"Keep the stretching like this, the stretching is great"**: the stretch itself is untouched.
  With every new control at zero the output is bit-identical to today's.

## The controls (per stretch point, `properties.sound.shape`)

| Group | Control | Range, default | Effect |
|---|---|---|---|
| Time | start, end | 0–1, 0 / 1 | the part of the recording that is stretched, looped |
| Pitch & harmony | transpose | −12…+12 st, 0 | the whole spectrum moved by 2^(st/12) |
| | tune | 0–1, 0 | pulled onto the chord's notes and their overtones (resonator comb) |
| | focus | 0–1, 0.5 | the comb's width: narrow = pure tones, wide = breathy |
| | partials | 1–24, 8 | overtones per note |
| | layers | 0–4, 0 | copies at the chord's intervals above the recording |
| | harmony | 0–1, 0.5 | dry ↔ layers |
| | glide | 0.5–10 s, 2 | how slowly the chord's pitches move to the next chord |
| Movement & space | drift | 0–1, 0 | the read position wanders (up to ±2 s), smoothly |
| | blur | 0–1, 0 | each frame's spectrum smeared over time |
| | width | 0–1, 1 | stereo (the engine's existing parameter, now exposed) |
| Output | grit | as now | |

## Where it happens

A new stage in the core's stretch device (core/devices/stretch.cpp) between the magnitudes and the
random phases, on magnitudes only: transpose → layers → tune → blur, then the frame's energy is
restored to what it was (A-18: a timbre control is not a level control). Skipped entirely when all
are at zero (A-8: bit-identical). Chord pitches come from the piece (the chord playing on the route
in reach; the last one heard off a route; Dm9 before any), glide in log-frequency per frame.
Tune's comb is a table built when the chord or focus/partials change, not per frame.

## Phone

The Stretch panel's tabs become Point · Time · Harmony · Space.

## Tests (core/test.cpp, measured)

1. All zero: bit-identical output to the device before the stage existed (same seed).
2. Transpose +12 on a 440 Hz sine: the peak at 880 Hz (±1 bin).
3. Tune 1 on pink noise with chord D-F-A: ≥ 70 % of the energy within ±30 cents of D/F/A partials.
4. A chord change with glide 2 s: the comb's peaks move, and no sample-to-sample jump > 0.1.
5. Region 0–0.5 on a recording that is 300 Hz then 900 Hz: 900 Hz absent from the output.
6. Tune/layers/blur at 1: RMS within ±1 dB of the dry stretch (A-18).
7. Worst process() time stays within the existing budget check.
