# Sample harmony 3c.1 — samplers that sound with real recordings, and a panel that shows them

Parent: `docs/superpowers/specs/2026-10-05-samples-3c-patch-screen-design.md` (lab mode on the site's patch panel).
D1 holds: the public keeps `web/core.wasm` and the live patch; everything here is lab mode only.

## What Kerem found (2026-10-06, live site, Koşuyolu Parkı)

1. The uploaded sample's name is in the wrong place.
2. Visualisations wanted for each voice, and for the sample.
3. Issues with sampler synth choices and sample uploads.
4. The Second and Third voice not working.
5. Only some synths working on the first voice.

## What was measured

- **Wiring is sound.** With broadband noise as every role's sample, all five non-pitch samplers sound on all three roles in
  the site engine (-14 to -34 dB, as loud as or louder than FM).
- **Panel** (screenshot): the sample's name sits in the row's narrow readout slot and spills into the next column (the
  Voice's name was drawn under the Second voice); the browser's file button reads "No file chosen" even with a saved
  sample; the Colour readout ("Grain 100 %") overflows into the next label; a sampler role with no sample says nothing
  loud enough - Kerem's Third voice is Formant with no sample saved, so it is silent.
- **Sound with bird recordings** (Tufted Titmouse: unpitched, centroid ~6 kHz; Red-winged Blackbird: 5162 Hz):
  - Freeze silent on every role: its Moment (Colour 0.5) lands in a gap between calls.
  - Retune silent on the Titmouse: no pitch to retune from.
  - Pulsar near-silent on the Titmouse: its grains land in silence.
  - The Second voice near-silent with the Blackbird for every filter sampler: it plays lower notes, where the
    recording has no energy to resonate.

## Decisions (Kerem, 2026-10-06)

| # | Question | Decision |
|---|---|---|
| F1 | Silence in a recording | **Skip it.** The samplers read only the sounding parts. |
| F2 | Notes outside the sample's range | **Fold into the sample.** Each note moves by whole octaves into the range where the recording has energy; its pitch class (so the harmony) is kept. Automatic, every sampler role. |
| F3 | Retune on an unpitched sample | **Warn in the panel**: "Retune needs a pitched sample - this one is unpitched". |
| F4 | Visualisations | **All three** for each sampler role: the sample's waveform, the role's live notes, a level meter. |

## Design

### F1 — skip silence (`web/lab-roles.js`)

`FsRoles.compact(ctx, audioBuffer, analysis) -> { buf, analysis }`, applied wherever a role's sample becomes the engine's
(the session's decode and upload, the site's per-path decode, the lab):
- 50 ms blocks; a block is sounding when its energy is at least the loudest block's -40 dB (the analyser's own silence
  rule, the one `fs_piece_role_source` uses for its level).
- The sounding blocks are joined in order, each join a 10 ms equal-power crossfade (no click, A-2). A gap shorter than
  0.25 s is kept (the breath inside a call stays).
- The analysis track (`hop_s` 0.02) keeps the hops whose time falls in a kept block, so Retune's pitch track stays
  aligned; `frames` is the new length.
- Nothing sounding (all silence): the buffer as it was.
- The stored sample and the stored analysis are unchanged (upload as now): compaction runs at decode, so it is the same
  every time and can change later without re-uploading.
- ponytail: in JS for the web; the apps (sub-project 6) need it in the core or their own code.

### F2 — fold into the sample (`core/samplers.hpp`, `core/piece.cpp`)

- When a role's recording arrives (`fs_piece_role_source`), its average power spectrum is measured (2048-point Hann frames,
  up to 48, DC removed), kept with the recording.
- A note's score in an octave: its first 8 overtones, each the strongest bin within a quarter tone of n·f, weighted 1/n.
  In `sampler::Resonator::start`, a note keeps its octave when its score is within 30 dB of the best octave's (±6 octaves);
  otherwise it moves to the nearest octave (up or down) that is. Pitch class kept, so it stays in the chord. Every sampler.
- So a broadband recording (wind, rain, noise) leaves every note as played; a narrow 5 kHz call moves a note to the nearest
  octave whose overtones meet the call (156 Hz → 625 Hz, its 8th overtone at 5 kHz); a pitched recording keeps notes whose
  overtones meet its partials.
- History (2026-10-06, measured): a fold toward f0/centroid pushed broadband recordings to 8–16 kHz (thin, quiet); an
  octave-band rule kept notes in a band with energy whose overtones still missed a narrow call (a G#7 Freeze note at −73 dB).
  Both replaced. Retune's trim stays at its 3c value (its notes keep their octave on a pitched recording).

### F3 and the panel (`index.html`)

- **The sample block** of a sampler role, below its controls, the full width of its column:
  - an **Upload sample** button (the file input hidden behind it);
  - one line: the name · pitch or "unpitched" · state, ellipsised, the full name in its title;
  - a warning line (warn colour) when it applies: "no sample - this role is silent" / "Retune needs a pitched sample -
    this one is unpitched" / "sample missing …";
  - the **waveform** (canvas, 40 px): the sample's peaks, sounding parts lit and skipped silence dim; for Freeze its
    Moment marked, and dragging on the strip moves the Moment (Colour). The Moment is placed on the sounding parts (F1).
- **Colour row**: the label is the colour's name ("grain", "moment", "partial", "overtones", "brightness"), the readout
  its value only ("100 %", "0:12", "9") — nothing overflows.
- **Live notes and level** (while sound plays, lab mode): under each sampler role's instrument row, the notes it is
  playing now (note names, lit while they sound) and a small level meter (-60 … 0 dB) of that role's output.

### Engine data for the live view (`core/piece.cpp`, `core/engine.cpp`, `web/core-worklet.js`)

- `int fs_engine_roles(fs_engine *e, float *out, int max_notes)`: for each of the three roles, its output level (RMS of
  the last ~0.1 s of that role's layer, dB) and up to `max_notes` sounding note frequencies (sampler voices active and not
  releasing; 0 = none). Returns the number of floats written. `core-lab.wasm` only (exported in `build-lab.sh`).
- The engine worklet adds `roles` to its `live` message (~30 Hz) when the export exists.

## Safety

- No change to `web/core.wasm`, the live patch, or the stored samples. Lab mode off or signed out: as now.
- lab.html uses the same compaction (so the lab and the site sound alike).

## Tests

| Kind | Check |
|---|---|
| Unit (Node) | `FsRoles.compact`: a buffer of calls with 2 s silences becomes the calls joined (length, no join step above the crossfade's), the track kept aligned, an all-silent buffer unchanged |
| Core | `fold_c`: a 110 Hz note on a sampler with `fold_c` 5000 Hz sounds at 110·2^k inside [3536, 7071] Hz (its loudest partial), pitch class kept; `fold_c` 0 unchanged |
| Core | `fs_engine_roles`: a sounding sampler role reports a level above -60 dB and its note; a silent one -inf/none |
| Browser (site) | the Titmouse-like and Blackbird-like test samples (sparse calls, high band; generated) on all three roles: every non-pitch sampler sounds (role alone, gain 0.8: above -45 dB), Freeze included |
| Browser (site) | the sample block: name inside its column (its box within the column's box), Upload button, the warning for no sample and for Retune + unpitched; the waveform drawn; Freeze's Moment drag changes Colour |
| Browser (site) | live notes and meter: while sound plays a sampler role shows note names and a meter above its floor; muted, the meter falls |
| Regression | every existing suite |

## Not in scope

The looped and granular pitch models and Position (3d); B (consonance from the recording's overtones); visualisations for
digital roles; the apps.
