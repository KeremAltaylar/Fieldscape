# The website's setter, in the apps' views — design

2026-09-27 · branch `setter-redesign` (from `main` at 905521d, the live listener)

## What Kerem asked for

- The setter lives on the website only; the apps stay listener-only.
- The setter gets the same style as the listener (the approved mock, `design/app/gen_mock.py`,
  boards SetterDraw, SetterPoint, StretchDevice, RhythmDevice, PatchEditor, Archive, Account).
- Sound panels: **on a computer one full screen of controls** (rulebook C-8), **on a phone tabs**
  (M-2) — Kerem chose this over tabs everywhere and over a reskin only.
- Nothing old is removed before its replacement works, and each stage is tested with real mouse
  and touch before Kerem is asked to look (the lesson of the listener's W1).

## Design

1. **One frame.** Signed in, the listener shell (web/listener.js) gains the setter's tools: a mode
   bar (Select · Point · Route), a Publish bar (Publish N · what is pending), editable cards, and in
   Account who is signed in, Sign out, Recent activity. The old header, sidebar and patch dialog
   retire behind it; their logic stays in index.html and is driven through the bridge
   (window.fsListen, grown with setter calls).
2. **Setter point card:** Draft/Published chip; name, note; icon picker; Stretch · Rhythm · Grains;
   the recording (file, Replace, Upload); photos (add, remove); Mute · Solo; Delete; **Shape the
   sound** opens the point's device panel.
3. **Setter route card:** name, note, the 16 chords, **Patch** (the route's patch panel), Delete.
4. **Drawing:** Point mode — a tap places a point and opens its card. Route mode — taps add
   vertices; the panel reads "Drawing a route · N points" with Undo and Done.
5. **Sound panels** (route patch, Stretch, Rhythm), the same controls as today. Computer: one screen
   each, restyled; sliders become **knobs** (drag, fine drag with Shift / long-press, double-click
   resets, value readout, arrow keys; rulebook C-12). Phone: tabs — Patch: Chords · Voices ·
   Effects · Morphs · Rhythm; Stretch: Place · Stretch · Colour; Rhythm: Pattern · Hits · Effects.
   A knob drives the existing input (sets its value, fires its input event), so the sound logic
   does not change.
6. **Archive:** a sheet — search, sort, every feature with its status; a row opens its card.

## Stages

- **S1** frame, drawing, both setter cards, Archive, Publish; the old sidebar retires at its end.
- **S2** the knob control and the route patch panel (computer screen, phone tabs).
- **S3** the Stretch and Rhythm panels.
- **S4** Recent activity and a sweep for anything left behind.

Each stage: tested (core/tests/web_listener.mjs grows a setter half, run signed in via the gate),
screenshots at 390×844 and 1440×900, contrast measured, then live only on Kerem's OK.
