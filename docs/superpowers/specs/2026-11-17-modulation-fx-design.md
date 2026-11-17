# Modulation FX — a master FX for routes, a Loop point device, live LFOs, new markers (2026-11-17)

Kerem, 2026-11-17 (his words, lightly cleaned; binding):

1. "In the setter view, patch screen of setter routes: remove the rhythm effects section. Instead a good modulating
   master FX channel that takes all 3 route voices in and runs modulation FX chains parallel to the original sound,
   adjustable with a gain knob. No sound generation in it. Many fx parameters and a great range of modulation
   possibilities; add new modulating and transforming fx, find new ways of sound fx."
2. "A new point sound device: takes the recording as a whole and plays it in a loop (loop length = the recording's total
   length), modulated by the same fx design but a separate instance per point. Dry/wet with that fx. Good feedback delays
   or other transforming methods on top of the loop / in the feedback."
3. "The cells' LFOs have issues: update their mapping targets to the new voice system and the new master fx; a good
   visualization per LFO showing where we are on its wave right now, and any other indicator that proves it is working.
   This is about the LFO UI, not the cells visual."
4. "Better, more distinctive icons for the point kinds/devices on the map (web + Android + iOS)."

Design calls below that Kerem has not yet confirmed are listed under **Open questions**, each with the default this
spec assumes. Nothing in the dock or a panel is built before its mock is approved (C-13).

## What the code is today (found 2026-11-17, main `99f0459`)

- **"Rhythm effects" is not on the route's voices.** `patch.rfx` (`cutoff, drive, delayDiv, delayWet, delayFb, revWet,
  revDecay`) drives `Piece::rdrive` + `Piece::rfx` in `core/piece.cpp`: the chain every **rhythm point** near the route
  (Rhythm, Grains, Melody: `Piece::rh[]`) passes through. UI: seven `rfx.*` rows of `PATCH_FIELDS` in `index.html`
  (section "Rhythm effects — the points, not the synths", column 4). Its defaults are not dry (echo 0.22, room 0.14,
  drive 0.15, 9 kHz), and `patchOf` writes them into every route, so **every published route with rhythm points has
  them in its sound today**.
- **Route voices:** three roles (`voice` = pad bass+top, `sect`, `v3`), each `Layer` (drive, warp, filter) → `Eq5` →
  its own `FxChain` (`fx`, `fx2`, `fx3`: delay into Freeverb) → summed in `Piece::render`, then × `synth_level` ×
  `route_solo`. Patch version is 17; the core rejects other versions (installed apps would play the default patch), so
  **the version must not be bumped** — new keys only.
- **"The cells' LFOs" are the morphs:** `patch.morph.list[]` (`shape` drift/breath/pulse/ramp/tide, `period` s, `depth`,
  `bias`, `phase`, `dest`), 16 destinations (`DEST_KEYS` in `core/piece.cpp`, mirrored by `MORPH_DESTS` /
  `MORPH_DEST_KEYS` in `index.html`). Missing: `sect.gain`, `v3.gain`, every effect parameter. The patch panel's
  per-morph canvas (`traceDraw`) draws from the page's copy of the list on `morphClock()`, shows nothing unless a walk
  is running (`morphList()` needs `pacer`), has no playhead (a dot at the right edge) and never shows what the target
  parameter is doing. Apps read the cells through `fs_piece_morphs` (7 doubles a cell — its layout must not change).
- **Point devices** (five): Stretch (`audio_mode` "soundscape"/absent), Rhythm ("hits"), Grains, Melody, Recording
  ("plain"). Recording = the stretch device in a mix slot with `sound.shape.plain = 1` (`core/devices/stretch.cpp`
  `original()`: the whole file, 20 ms crossfaded loop). `fs_point_sound(mix, slot, stretch, sound_json)` is the one
  seam all three platforms call for a point's own settings (web: `core/engine.cpp`; Android: `jni.cpp`; iOS:
  `App.swift` — the apps run their walk in Kotlin/Swift on `fs_mix` + `fs_piece`, not on `fs_engine`). 4 slots.
- **Markers:** the apps draw four badges (`mk-stretch`, `mk-rhythm`, `mk-grains`, `mk-silent`; Melody shares grains',
  Recording shares stretch's) in `Icons.kt` `Markers` and `MapView.swift`. The web map draws no device badge at all:
  it shows the point's nature icon (`ICONS`: tree, animal, flower, insect, fish, soil, water) or a dot.
- **Budgets:** on the web the worklet's message handler is the audio thread: a patch read ≤ 1 ms warm; nothing on the
  audio thread allocates, locks, sorts or simulates. wasm is built `-O1` on the Mac (`sh tools/mac-wasm.sh`).

## A. One engine, two places

`core/modfx.hpp`, namespace `mfx`, header-only, depends on `tone.hpp` and `json.hpp` only. **It makes no sound**: with
silence in, its output decays to exact 0. One `mfx::Engine` is the route's master FX (inside `Piece`); a pool of two
more serves the points (inside `fs_mix`). Every buffer is allocated in `Engine::init(sr)` (≈ 2.8 MB an instance: 4 s
echo, 2 s cloud, the rest small); `apply(cfg)` only sets smoothed targets.

### Signal path

```
in ─► Shift ─► Swarm ─► Phase ─► Comb ─► Vowel ─► Cloud ─► Tape ─► Echo ─► Space ─► out (wet only)
```

Serial, fixed order (C-9: same control, same place), **every module with its own `mix`** (A-8): at `mix` 0 a module is
skipped — not run, bit-exact pass-through — and its state is cleared before it next runs. When every `mix` is 0 the
engine's output is 0, so the return adds only processed sound. The engine is not run at all while its return (route)
or wet (point) is 0 and its tail has fallen under −80 dB.

- **Route master:** `in = Σ send[q] × (voice q after its own delay and room)`; `route out = dry + ret × wet`. Dry is
  never touched (parallel). The wet is added before `synth_level × route_solo`, so it fades with the route's leash and
  rests with the route. Zone one-shots ride voice 1's send (they already ride its chain). Rhythm points do not feed it.
- **Point:** `out = cos(wet·π/2) × dry + sin(wet·π/2) × engine(dry)` (equal power, as the point's "original · stretch"
  mix), before the slot's grit, proximity low-pass and gain — so the whole point, tail included, fades with distance.

### The modules (52 sound parameters)

All defaults are dry. Ranges are the stored units; `d` = a bar division (below).

| Module | What it is | Parameters (range, default) |
|---|---|---|
| **Shift** | frequency shifter (single side band from a Hilbert pair; not a pitch shifter: partials move by Hz, so harmonic sounds turn inharmonic) or ring modulator; feedback through a 30 ms delay makes endless rising/falling spirals (barber-pole) | `mix` 0–1 (0) · `hz` −2000…2000, cubic taper (0) · `mode` ssb/ring (ssb) · `fb` 0–0.9 (0) |
| **Swarm** | three modulated delay taps a side: chorus → ensemble → flanger; `zero` delays the module's dry by the same time so the taps cross it (through-zero flanging) | `mix` · `rate` d (1n) · `depth` 0–1 (0.5) · `delay` 0.1–30 ms (8) · `fb` −0.9…0.9 (0) · `spread` 0–1 (1) · `zero` off/on |
| **Phase** | phaser, 2–12 all-pass stages, the corner swept in octaves; right side 90° behind | `mix` · `stages` 2/4/6/8/12 (6) · `hz` 100–8000 (800) · `span` 0–4 oct (2) · `rate` d (2n) · `fb` −0.95…0.95 (0.3) |
| **Comb** | five tuned feedback combs: **on the chord the walker stands in** (root first, folded into 55–880 Hz, gliding 80 ms on a chord change) or free (`hz` × 1, 1.5, 2, 3, 4) | `mix` · `tune` chord/free (chord) · `hz` 40–2000 (110) · `decay` 0.05–20 s (2) · `damp` 500–16000 Hz (6000) · `detune` 0–50 cents L/R (8) |
| **Vowel** | three formant band-passes morphing a–e–i–o–u | `mix` · `pos` 0–4 (0) · `q` 2–20 (8) · `size` 0.5–2 (1) |
| **Cloud** | grains from the last 2 s: up to 8 Hann grains, re-pitched, scattered back in time, reversed by chance; `freeze` stops the buffer taking new sound (the grains keep reading it) | `mix` · `size` 20–500 ms (120) · `dens` 1–40 /s (12) · `pitch` −24…24 st (0) · `spray` 0–1 chance of ×2, ×0.5, ×1.5 (0) · `scatter` 0–1 (0.3) · `rev` 0–1 (0) · `freeze` off/on |
| **Tape** | wow (≈ 0.4 Hz + drift, to ±2 % pitch), flutter (7.3 Hz, to ±0.4 %), saturation at the signal's own scale (A-19), age filter | `mix` · `wow` 0–1 (0.3) · `flutter` 0–1 (0.2) · `drive` 0–1 (0.2) · `tone` 500–16000 Hz (9000) |
| **Echo** | the feedback delay, up to 4 s. **In its loop:** a tilt filter, a frequency shifter (each repeat moves by `shift` Hz), a wobbling delay time and a soft clip — every repeat is transformed again | `mix` · `time` d (8n.) · `fb` 0–0.98 (0.45) · `tone` −1…1: dark…thin (0) · `shift` −200…200 Hz (0) · `wobble` 0–1 (0) · `spread` 0–1 ping-pong (0.5) |
| **Space** | eight-line feedback delay network, lines modulated (as the room fix of 2026-10-20), decay to a minute | `mix` · `size` 0.2–1 (0.7) · `decay` 0.3–60 s (4) · `damp` 500–16000 Hz (7000) · `mod` 0–1 (1; 1 = the 2026-10-20 room fix swing; below ~0.75 held low notes can dip) |

Route only: `ret` 0–1 (0) and `send[3]` 0–1 (1, 1, 1). Point only: `on`, `wet` 0–1 (0).

Rules every module keeps:
- A-2: every parameter is smoothed (30 ms; delay times 100 ms as a tape glide); a `mix` leaving or reaching 0 and
  `freeze` cross-fade over 30 ms. Switches (`mode`, `stages`, `tune`, `zero`) cross-fade 30 ms.
- A-18: Comb, Vowel and Tape hold their level: one `LevelMatch` helper (output gain = slow in/out RMS ratio, 400 ms,
  ±12 dB) — measured within 3 dB across each control while the spectrum moves.
- Stability: Echo's loop is soft-clipped; no feedback gain reaches 1; a non-finite sample resets that module's state
  and outputs 0 for the block; delay lines flush denormals (`DelayLine::push` does).
- A-9: `rate` and `time` are divisions of the route's tempo, shown with their seconds ("1/8 · 0.33 s"). The division
  strings: `32m 16m 8m 4m 2m 1n 2n 4n 8n 16n 32n`, dotted `2n. 4n. 8n. 16n.`, triplets `4t 8t 16t`. A point takes
  the tempo of the route playing (72 bpm before any route). `hz`, wow and flutter are pitch/texture, not rhythm: Hz.

### Modulators (per instance)

- **4 LFOs:** `shape` sine / tri / up / down / square (5 ms slew) / steps (a seeded random value each period) / drift
  (smoothed random); `rate` d (default 1n, 2n, 1m… see the model); `phase` 0–1. Phase is derived from the piece's
  transport position, not accumulated per block, so tempo error is 0 and a tempo change does not jump the phase.
- **Envelope follower** on the engine's input: `attack` 1–200 ms (10), `release` 20–2000 ms (300), `sens` 0–4 (1).
- **Walk sources** (A-14: each smoothed by `smooth` 0.1–10 s, default 2; no quantisation, so no hysteresis needed;
  a lost fix holds the last value): `along` (0–1 along the route), `pace` (walking speed, 0–2 m/s → 0–1), `near`
  (a point: 1 at the point, 0 at its radius; the route master: 1 on the line, 0 at the leash).
- **`chord`:** a seeded random value that changes on each chord change (glided 200 ms) — harmony-driven steps.
- **Matrix:** 8 slots `{src, dst, amt −1…1}`. `src` ∈ lfo1–4, env, along, pace, near, chord. `dst` = any continuous
  sound parameter above (44 of them; not the switches), written `"phase.hz"`. `amt` is a fraction of the destination's
  range, on its own taper.
- **One writer per parameter (A-9):** value = (a morph's value if a morph aims at it — section D — else the stored
  value) + Σ matrix offsets, clamped to the range. Computed in one place (`Engine::resolve`), once a block.

Settings in all: 52 sound + 4 route (or 2 point) + 12 LFO + 3 envelope + 1 smooth + 24 matrix ≈ **96**.

### Storage

`patch.mfx` on a route, `sound.fx` on a point; the same object (`ret`/`send` only on a route, `on`/`wet` only on a
point). An absent object, an absent module or an absent key = that default = dry. Unknown keys are ignored.

```json
{ "ret": 0.6, "send": [1, 0.5, 1],
  "phase": { "mix": 1, "stages": 8, "hz": 900, "span": 3, "rate": "2m", "fb": 0.5 },
  "echo":  { "mix": 0.7, "time": "8n.", "fb": 0.7, "shift": 6, "tone": -0.3 },
  "lfo": [ { "shape": "drift", "rate": "4m", "phase": 0 } ],
  "env": { "attack": 10, "release": 300, "sens": 1 }, "smooth": 2,
  "mods": [ { "src": "lfo1", "dst": "echo.shift", "amt": 0.4 }, { "src": "pace", "dst": "phase.span", "amt": 0.5 } ] }
```

Installed apps and old cores ignore both keys: a route sounds without its master FX there, a Loop point plays as a
plain Recording (v7.4 apps) — nothing breaks, nothing needs an app update to keep working.

### Cost (estimates — to be measured in the plan's Part 1, Task 7; nothing here is measured yet)

Counted in arithmetic per stereo sample: Shift ≈ 90, Swarm ≈ 70, Phase ≈ 110 (12 stages), Comb ≈ 70, Vowel ≈ 60,
Cloud ≈ 130, Tape ≈ 50, Echo ≈ 110, Space ≈ 160; modulators ≈ 20. Against what this core already measures (a stretch
voice 2.15 % of realtime in Node; the FM stack 6.6 %): **a typical setting (three modules + Echo + Space) ≈ 1.5–2.5 %
of one desktop core, everything on ≈ 4–6 %; a phone 3–4× that.** Budgets the build must meet (wasm `-O1`, Node, this
PC, 128-frame blocks): typical ≤ 3 %, all on ≤ 8 %, p99.9 block ≤ 0.35 ms, idle (return 0) ≤ 0.05 %.

**Cap: the route master + 2 point instances.** `fs_mix` owns two engines; of the slots whose point has FX on, the two
with the highest gain get them; a third plays its loop dry (its wet ramps to 0 over 350 ms; it takes over when one of
the two leaves). Worst case 3 × all-on ≈ 24 % desktop — too much for a weak phone at 3–4×, so the Loop panel and the
master panel show the engine's own measured load, and the phone numbers are a release gate (Kerem's phone; unverified
until then).

## B. The route's master FX (Kerem 1)

- The "Rhythm effects" section leaves the patch screen. In its column: **Master FX** — the return knob, the three
  sends, a strip of the nine modules (lamp + mix each), and **Open**, which swaps the patch body for the FX editor
  (a toggle, not a scrollbar: C-8). The editor: the module strip across the top; the selected module's controls;
  the modulators (four LFO scopes with moving playheads, the envelope, the walk sources as live bars, the matrix of 8);
  in/out meters and the load. Every control: fine drag with shift, double-click resets, readout in real units
  (Hz, ms, s, st, cents, division · seconds). Desk only (≥ 1000 px wide), as composing is; a phone shows the summary.
- **What happens to rhythm effects on existing routes** (Open question 1; assumed default): the core keeps reading
  `patch.rfx` exactly as today, so nothing published changes its sound. New routes are written with it clean
  (`cutoff 20000, drive 0, delayWet 0, delayFb 0, revWet 0` — what the core's own tests call the clean bus). A route
  that still carries a non-clean `rfx` shows one line in the Master FX section — "This route's points still pass
  through the old rhythm effects (echo 22 %, room 14 %). Remove" — and Remove writes the clean values.

## C. The Loop device (Kerem 2)

A sixth device, **Loop** (`audio_mode: "loop"`): the recording whole, looped, through its own FX instance. Built on
Recording: the card's device switch writes `sound.shape.plain = 1` (as Recording does) and `sound.fx.on = true`; any
other device clears `plain` and sets `fx.on = false` (the settings stay stored, as every device's do).
`fs_point_sound` reads `sound.fx` and hands it to the slot, so the web and both apps get it through the call they
already make. Recording itself stays as Kerem asked on 2026-11-03 ("nothing fancy").

Its panel (the Recording panel's frame): the waveform with the loop's playhead; This point (carries, level, zone, EQ);
dry/wet; the same FX editor component as the master (no sends/return), with `near` = closeness to this point. The
empty state as Recording's ("No recording attached yet."). Listener cards and the apps say LOOP: "Its recording
looped through its own slow-moving effects - heard from wherever you are."

Slot rules: an FX engine is cleared when its slot is given to another point (after the slot's gain has reached 0),
so one point's tail never plays under the next. Solo/mute/Plays act on the slot, wet included.

## D. The LFOs (Kerem 3)

**Targets.** `DEST_KEYS` grows by appending (indexes 0–15 keep: the apps' cells and `MORPH_DEST_KEYS` are by index):
`sect.gain`, `v3.gain`; `fx.delayWet`, `fx.delayFb`, `fx.revWet` and the same for `fx2`, `fx3`; `mfx.ret`,
`mfx.send1..3`; and every continuous master parameter as `mfx.<module>.<param>` (44). A morph at a new destination
maps its 0–1 onto that parameter's whole range (its taper), averaged when several aim at one, exactly as today's.
The menu is grouped: Voice 1 · Voice 2 · Voice 3 · Master FX (by module); a voice's label follows its instrument as
today (`destLabel`). Old cores ignore destinations they do not know (`dest_of` → −1).

**The visual**, per morph row, drawn from the engine's own numbers (`fs_piece_mod`, ~30 Hz), not the page's copy:
- the wave of one period, drawn once, and **a playhead moving across it**; for the two shapes that never repeat
  (drift, tide) the wave scrolls under a fixed centre playhead instead, and the row says so;
- a level bar of the value now (0–1);
- **the target, live, in its own unit**: "pad filter · 2.4 kHz", "echo feedback · 0.62" — the proof it reaches the sound;
- states (C-5): sound off → the wave runs on a preview clock, dimmed, tagged "preview — press Sound"; this route is
  not the one playing → "walk onto this route to see it live"; morph off → flat and dim; no destination → "choose a
  parameter…"; morphs switched off → all dim with one note. `prefers-reduced-motion`: the playhead still moves (it is
  the information), nothing pulses.
The same scope component draws the FX editor's four LFOs (section B) and the Loop panel's.

## E. Markers (Kerem 4)

One drawing per device, the same on the web, Android and iOS, told apart by **silhouette as well as glyph** so they
read at a glance and zoomed out: Stretch, Rhythm, Grains, Melody, Recording, Loop, and a quiet point. The glyphs are
data — lists of lines, circles, polylines and cubic curves on a 26-unit grid — in `web/markers.js`, transcribed
number for number into `Icons.kt` and `MapView.swift`; a test compares the three tables. Starting proposals for the
mock: Stretch a long wave in a wide lozenge; Rhythm three beats in a rounded square; Grains scattered dots in a
circle; Melody three stepped notes in a diamond; Recording a dot in a ring (the record mark); Loop a turning arrow
around a short wave; quiet a hollow ring. Lamp colour while it sounds, dark when quiet, as today; contrast checked
mechanically against the map's three base styles (C-7). The web map gains the device badge (Open question 6: the
nature icon moves to the card and list; the mock shows both ways).

## What is kept

- A route without `mfx` (or with `ret` 0) renders **bit-identical** to main `99f0459` (`test_kept`, the step-2 repeat
  hash, `core/test.cpp` row 0). A point without `fx.on` is untouched.
- Patch version stays 17. `fs_piece_morphs`' 7-double layout stays. `rfx` stays in the core.
- The patch read stays ≤ 1 ms warm with a full `mfx`.

## Kerem's decisions (2026-11-17, binding: they override anything above that disagrees)

1. **Rhythm effects: removed.** The section leaves the patch screen and the core no longer applies `patch.rfx`
   (rhythm/grains/melody points play clean; old saved `rfx` values are ignored, new saves write the clean values).
   No legacy line, no Remove button. Kerem accepts that published routes with rhythm points lose that echo/room.
2. **One master FX bus** (one chain, one engine per route, which saves CPU); each of the three voices has its own
   **send amount** into it; a **return gain** knob sets the bus level beside the untouched dry.
3. **No walk/GPS modulation sources** (too heavy for now): no `along`, `pace`, `near`, no `walk()`, no
   `fs_piece_pace`, no `fs_mix_fx_walk`, no app `fxWalk`. Sources: lfo1-4, env, chord.
4. **Loop is a new sixth device**; Recording stays as it is.
5. **Rates in bar divisions** (shift, wow, flutter in Hz), and the patch window **shows them**: division · seconds
   and the LFO wave with its moving playhead.
6. **Icons:** just better, more distinctive point icons. The marker structure stays as it is; the mock shows candidates.
7. **The master taps each voice after its own delay** (and room).
8. **Editing is setter-only, in the desktop browser patch window.** No phone summary, no listener-view FX UI.
   (Listeners still hear the FX; only the editing is desk-only.)
9. **Point FX at once: more than two if the budget allows.** The cap is one constant, set from measurement
   (target 4; the phone CPU check at Test 24 decides), nearest win, the rest play dry.
10. **The master FX is fed only by the three route voices.** Rhythm/Grains/Melody points get no FX this round.
11. **LFOs:** every morph cell (six on a typical route) shows its wave with a moving playhead and its target's live value.

## Build order

Part 1 the engine and the route master (sections A, B) → Part 2 the Loop device (C) → Part 3 the LFOs (D) → Part 4 the markers (E; independent, may run
first). Each part: mock approved by Kerem before its UI is built, a test link, merge only on his OK, verify live,
tag, back up. Plan: `docs/superpowers/plans/2026-11-17-modulation-fx.md`.
