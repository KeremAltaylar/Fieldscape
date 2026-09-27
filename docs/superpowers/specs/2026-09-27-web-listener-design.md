# The website's listener, in the apps' views — design

2026-09-27 · branch `web-listener` (from `native-core` at tag `v4.0-the-apps`)

## What Kerem asked for

- The website gets the same listener views as the iOS and Android apps (the approved mock,
  `design/app/gen_mock.py`, and the screens built in Tests 5–9).
- Listeners are in open world only: no route walk, no place mode (Kerem, 2026-09-27: "yes drop
  them"). Place mode survives only as the setter's drawing tool.
- On a computer the panel floats over the map's left edge (Kerem chose it over a bottom panel and
  a fixed sidebar).
- The setter stays on the website only (the apps stay listener-only). The setter's own redesign
  is the next piece of work (C), not this one.

## Approach

One page, two surfaces. Signed out, the whole current chrome is hidden and a new listener shell
is shown: top bar, panel, sheets, cards, cells. The map, the published data and the core engine
stay shared. Signed in, the setter keeps today's interface until C. Splitting the listener into
its own page is left for C, when it becomes cheap.

## 1 · Layout

**Phone (≤ 860 px):** the app exactly. Map full screen; top bar floating over it (place picker
left; Layers and Account round buttons right); the walk panel across the foot of the map,
hugging its content, folding to the slim bar with its grip; the cells just above the panel on
the left, 136 px, riding with it.

**Computer (> 860 px):** the same top bar; the panel a floating card ~380 px wide under the place
picker, the map behind it; it hugs its content and stops above the cells (240 px, bottom-left),
so nothing scrolls except a long Places list (C-8). The grip folds it here too.

**Both:** tokens only (the site's `:root` is already Tokens.md); Cormorant Garamond, Newsreader,
Courier Prime; targets ≥ 44 px (M-4). Tap/click a point or route opens its card; press-and-hold,
or dragging the walker's dot, moves you.

## 2 · Engine additions (core/engine.cpp, web/core.wasm)

The website drives the walk through `fs_engine`, which does not yet reach what the views need:

- `fs_engine_state` also carries the playing route's index and the chord (`{step, count, label}`).
- `fs_engine_morphs(e, out, max, &clock, &root, &shown)` — the piece's `fs_piece_morphs`, called
  by the worklet ~30 times a second while the cells are shown and posted to the page. **This fixes
  a live defect:** since the core became the default (2026-09-26) the website's cells run on the
  wall clock (`morphClock` falls back when the Tone bed is off), so they do not show what is heard.
- `fs_engine_solo(e, id)` — Listen: a stretch point or a rhythm point alone at its full level from
  any distance, everything else resting (the apps' rule), "" to let go.
- `fs_engine_route_info(e, i, out, size)` — the route card's patch (`fs_piece_route_info`).
- The card's "play the recording as it was made" needs no engine work on the web: an
  `HTMLAudioElement` on the original file, the engine's output faded down while it plays.
- `web/build.sh` exports the new calls; `core/test.cpp` checks each.

## 3 · The views (from the apps)

| View | Holds |
| --- | --- |
| Top bar | place picker (opens Places), Layers, Account |
| Walk panel | place + GPS fix chip (±m / HOLDING / BY HAND), Sound/Stop, route line with chord + rhythm points + **Cells**, the two nearest points with level bars, the five states (hearing, loading, nothing in range, weak GPS, location off), "Use my location" |
| Folded bar | place (or "Listening to …"), chord, Sound/Stop |
| Places | search, parks with what they hold, published routes |
| Layers | Map / Topo / Satellite / Virtual, park boundary, Fit all, zones, sections |
| Account | the test/build line, and a quiet "Setter sign-in" that opens today's setter surface |
| Point card | kind chip, distance, photos, waveform of the recording, where and when, **Listen**, play original; rhythm points list their four originals |
| Route card | the 16 chords in root colours, "you are at", key, tempo, sections, Show whole route |
| Map | chord segments beside each route (playing chord thick), zones and sections filled as today |
| Cells | the website's `cellsDraw`, fed by `fs_engine_morphs` |

## 4 · What listeners lose

The sidebar (Selected / Archive / Storage / Setter), the Map/Topo/Satellite/Virtual/Off/Frame/Fit
buttons (moved into Layers), the Sound/Cells/GPS bar and along/zones readout, lat/lon, Locate
(becomes "Use my location"), the phone's Details drawer, route walk and place mode. The Tone
engine stays in the page only for the setter until C (`?core=0` is a setter fallback now).

## 5 · Testing

- `python core/tests/run.py` (core checks, including the new engine calls) and `npm test`.
- `npm run check` on index.html.
- A CDP run at 390×844 (touch) and 1440×900: `scrollHeight − innerHeight === 0` **and** a
  screenshot of each view (V-2); contrast measured on every text/surface pair (C-7).
- The engine's level measured through a walk: Listen solos, letting go returns (as Test 7).
- After deploy: fetch the live `index.html` and `web/core.wasm` and check they are the new ones
  (V-5), then a live walk in headless Chrome.

## Stages

Each stage is checked locally, shown to Kerem, and deployed to `main` only on his OK.

- **W1** engine additions + the cells from the engine (the live fix) + the shell: top bar,
  panel (phone bottom / computer floating), walk panel, fold; old chrome hidden for listeners.
- **W2** Places, Layers, Account sheets.
- **W3** point and route cards (Listen, originals, photos), chord segments, walker drag / hold.
- **W4** route walk and place mode removed from the listener path; leftovers cleaned.
