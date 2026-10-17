# Composition — total control over how a route's three voices play (2026-10-17)

Kerem: "I want alternative playing styles and musical sentences for 3 voices in routes ... I want to have total control
over these structures ... we can adjust also some zones and chords can differ ... I also want a good UI and UX for this."
Designed with him in a brainstorm on 2026-10-17; his choices are marked **(Kerem)**, the rest are design calls made with
his approval of each section. Mockups: [`2026-10-17-composition-mocks/`](2026-10-17-composition-mocks/) (open in a
browser; they are sketches for direction, not the final look).

## Intent

A setter writes a route's music the way one writes a short piece: how each of the three voices plays (A, styles), what
they play (B, a motif and its development into a sentence), and how they relate (C, canon, answers, shared lines) - and
where along the walk any of that changes: per chord step, per section, per zone. Listeners and the apps play the result.

Success: Kerem can compose a route at his computer, hear each change at once from a spot on the route, and walk it with
the same music on the website, Android and iOS.

Constraints:
- **Composing is desk only (Kerem: "I will compose on computer always").** Phones show a summary, never the editor.
- **Hearing while editing: audition from a spot (Kerem: a).** The walker placed on the route plays from there; every
  change is heard live. No transport/play-through.
- **Total control.** Nothing random unless allowed: every random choice has a *variation* amount (0 = exactly as set) and
  is seeded per route, so a walk repeats.
- **Existing routes keep their sound (Kerem: a).** Each route's defaults are today's behaviour written out as settings;
  nothing changes until he touches it. A template button applies a starting composition in one click.
- The rulebook applies: C-8 one screen, C-9 fixed grid, C-5 the five states, tokens only (C-1), A-2 nothing clicks.

## Scopes and precedence

Every setting has the **route** default and may be overridden per **section**, per **chord step** (1-16) and per
**zone**. The narrower wins: **zone > chord > section > route**. An override holds only what it changes; the rest
inherits.

- A style change takes effect at the next bar line; continuous settings glide over the override's **blend** time (at the
  next bar, or over N bars).
- A zone override has **when**: *while inside* or *on entering* (one phrase), and **blend**.
- Zone overrides are stored on the route, keyed by the point's id: a point near two routes can act differently on each.

## A. Styles (per voice)

Common to all: **style**, **role**, **rate** (a bar division), **register** (lowest and highest note), **length** (share
of the step), **accents** (per step), **variation**, and the style's own settings.

| Style | Own settings |
|---|---|
| **Free** (today) | today's voice logic exactly; chord pull, density as now |
| **Arpeggio** | pattern grid (rows = chord degrees, columns = steps), presets up / down / up-down / 1-5-8-10 / Alberti, span |
| **Strum** | direction, spread (ms), which tones, accent first/last |
| **Chorale** | voicing (close / open / drop-2), voice count, voice-leading strictness, re-strike rate |
| **Ostinato** | its cell on the pattern grid; refit by chord degree or by fixed transposition |
| **Pedal** | which note (root / 5th / a fixed note), re-strike rate, follows chords or not |
| **Walking bass** | approach (chromatic / scale / 5th), beats before the change, rhythm |
| **Tintinnabuli** | which voice it answers; nearest triad tone above / below / alternating |
| **Pointillist** | silence (min-max), note length (randomness is the style: seeded) |
| **Swells** | attack and release as shares of the chord, peak level |
| **Euclidean** | pulses / steps / rotation, the pitch sequence the pulses walk |

Stackable on any style: **ornaments** (grace / trill / mordent / slide; every Nth or a probability; speed) and
**glides** (time, which intervals, microtonal offset per degree).

**Roles:** melody · counter · accompaniment · bass · off. A melody plays the motif (B); the others play their style.

**Voice 1's bass** (voice 1 is a bass and a top line today) has its own setting: *hold the root* (today) / *walking* /
*pedal* / *off*. Voice 1's style shapes its top line.

## B. Sentences (per route)

- **Motif:** 3-8 notes on a piano roll: degree, octave, start, length, accent. Written in **chord degrees** (root, 3, 5,
  7, 9 - it fits every chord) **(Kerem: A)**, with a switch per motif to **scale degrees** of the route's key for a fixed
  tune. Generate, then edit.
- **Development chain:** an ordered list of steps, each covering a span of chords: repeat · sequence ↑↓N · invert ·
  retrograde · augment · diminish · fragment (first N notes) · cadence (full / half / deceptive, strength, what the
  other voices do). Steps drag to reorder; each shows its span on the chord strip.
- **Form** is the chain's letters over the 16 chords (AABA, sentence, period) - one chain per melody voice.

## C. Relations (per route, each with its span of chords; overridable)

- **Canon:** leader → follower, delay (beats), transposition (chord degrees), transform (none / inverted / retrograde).
- **Call & response:** caller, answerer, answer source (the motif's second half / chain step N / mirror), gap, trigger
  (each phrase / entering a zone).
- **Hocket:** voices sharing one line; split pattern (alternate / every N / an edited mask).
- **Heterophony:** a voice doubling another's line; timing offset (ms), ornament amount.
- **Motion rule:** per pair, contrary / parallel 3rds / parallel 6ths / free, with a strength (max = strict).
- **Unison:** when voices meet (cadences / chord changes / never), octave relationship.
- **Shared density:** total notes per bar, a weight per voice.

## Saved data

In the route's own patch, a block `comp` (saved and published with the route, as the sampler voices are):

```
comp: {
  seed: 1,
  voice: Voice, sect: Voice, v3: Voice,          // the route's defaults (First, Second, Third voice)
  bass:  { mode: "hold" | "walk" | "pedal" | "off", ... },
  motif: { basis: "chord" | "scale", bars: 1, notes: [{ deg, oct, start, len, acc }] },
  chain: [{ op, arg, from, to }],                  // from/to: chord steps (0-15)
  relations: [{ type, a, b, from, to, ...params }],
  over: {
    chord:   { "4": Over, ... },
    section: { "2": Over, ... },
    zone:    { "<point id>": { when: "inside" | "enter", blend, ...Over } }
  }
}
Voice = { style, role, rate, lo, hi, len, acc: [...], variation, params: { ... } }
Over  = { voice?: partial Voice, sect?: ..., v3?: ..., bass?: ..., relations?: [...], chain?: ... }   // only what changed
```

A route with no `comp` (every route today) is read as today's defaults: voice 1 Free with its bass holding the root,
voice 2 the counter-line, voice 3 the held inner voice. When the setter first opens Compose, those defaults are written
out so he can see and adjust them; until he changes something the sound is identical.

## Engine

A `composer` module in the core (C++, beside `piece.cpp`), so the website, Android and iOS play the same music.

- Each bar it **resolves** the settings in force from the chord playing, the section underfoot and the zones the walker
  is inside (and the one just entered), merging route ← section ← chord ← zone.
- **Free** runs today's `voice_step` / `sector_step` / `third_step` unchanged. Any other style hands each step to that
  style's generator, which gets: the step, the chord's tones, key and scale, register, the voice's last notes, the motif
  cursor, and the route's seeded random source (seeded by route seed and bar). Notes go through the voices' existing
  synths, envelopes and EQ.
- A **melody** voice follows the chain: at each chord it plays that step's version of the motif, fitted by chord degree.
- **Relations** act on the notes the voices produce (canon replays them delayed and moved, call & response fills the
  leader's rests, hocket masks, heterophony doubles, motion rules weigh choices, shared density caps the bar).
- **Zones:** a new call tells the piece which zones the walker is inside and which was just entered (today only a zone's
  icon reaches the music). The web engine, Android's `jni.cpp` and iOS's `RouteSound.swift` pass it.
- **Cost:** no audio block over budget with every voice busy and all relations on, measured.

## Screens (desk)

Layout **C (Kerem)**: map on top, the composing dock below, inspector at the dock's right. Mocks: `layout.html`,
`lanes.html`, `inspector.html`, `motif.html`, `editors.html`.

- **Opening:** a **Compose** button on the route's patch panel. On phones: no button; the route card shows a one-line
  summary ("Arpeggio · Motif · Pedal").
- **The dock**, top to bottom: header (route, template menu, seed, undo/redo, grow, close) · the 16-chord strip in the
  chords' colours · band rows for sections and zones, placed where they cross the route **(Kerem: C)** · the three voice
  lanes and voice 1's bass row, each cell showing what plays, tinted by the band that changed it or outlined when a chord
  did · relation links drawn between lanes. "Show layers" per voice opens its stack (zone / chord / section / route).
- **Selection is linked:** a chord cell lights its stretch of the route on the map; a zone on the map lights the lanes
  it changes; a band in the dock selects its zone or section.
- **Inspector (Kerem: B):** every setting of the selection, inherited ones dimmed with their source (route / section /
  chord); moving one makes it this scope's own (lit, with × to reset). Zones add *when* and *blend*.
- **Editors** open in the inspector; **grow** raises the dock to full height (the map shrinks to a strip) for the motif
  roll, the chain, the pattern grid and relations. The page never scrolls (C-8).
- **Listen here** on a cell or band puts the walker on that stretch; every edit is heard at once.
- **Keyboard:** arrows move between cells, Enter opens, Delete clears a cell's overrides, Ctrl+Z / Ctrl+Shift+Z, Space
  plays or stops the sound.
- **Saving:** automatic, into the route's patch as panel edits are now; listeners get it when published.
- **States:** no route selected (Compose disabled, saying why) · a route with no progression (asks for chords first) ·
  a save failed (a note with retry) · hover and focus-visible on every control. Tokens only, the existing panels' look;
  a full mock is approved before building.

## Testing

- **Today's sound kept:** a route without `comp`, and one with the written-out defaults, play note for note what they do
  now (the piece's pinned note hashes).
- **Each style:** a core test renders a progression and checks the notes (Arpeggio 1-3-5 up over C maj7 gives C E G on
  time; Pedal holds across chord changes; Tintinnabuli answers are always the nearest triad tone; ...).
- **Overrides:** zone > chord > section > route with all four set at once; a style change lands on the bar line; a
  continuous setting glides over its blend time; no click at a change (measured).
- **Motif and chain:** each operation on a known motif; a cadence ends on the tonic; chord-degree fitting across chords.
- **Relations:** canon delay and transposition measured on the notes; hocket never doubles a note; shared density never
  exceeds its cap.
- **Repeatable:** same seed, same walk, same notes; another seed differs.
- **Cost:** worst audio block within budget, every voice busy and all relations on.
- **Browser:** every control writes the route and reaches the engine; the screen fits with no scroll (measured, and
  screenshots); keyboard and undo work.
- **Apps:** both build; the zone call works on the emulator and the simulator; a composed route plays the same notes as
  on the website (shared core).

## Build order (each step: its own plan, build, review, Kerem's test)

1. **Foundation:** `comp`, the resolver and blends, roles, voice 1's bass row, five styles (Arpeggio, Pedal, Strum,
   Euclidean, Swells), templates, the dock (strip, bands, lanes) and the inspector. Already usable: styles per chord,
   section and zone.
2. **Sentences:** motif roll, development chain, form, cadences.
3. **Relations:** canon, call & response, hocket, heterophony, motion rules, unison, shared density.
4. **Deeper styles:** Ostinato and Chorale editors, Walking bass, Tintinnabuli, Pointillist, ornaments and glides.

## Not in scope

Composing on a phone; a transport that plays the route through; control by time ("the third time you pass", "after 5
minutes") - asked, not chosen; motifs taken from recordings (raised earlier, not chosen for this work).
