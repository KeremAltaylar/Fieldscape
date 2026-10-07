# SDD ledger — plan: docs/superpowers/specs/2026-10-07-samples-6-octave-eq-pluck-design.md (short spec, no separate plan; Kerem chose: draggable-curve EQ, every voice)
Pluck: measured Bell Plucked -47.6/-54.8 dB, String/Tube Plucked -7..-11 dB vs String Bowed (first 300 ms) -> within 4.3 dB ("6 resonator levels" RED -> GREEN)
Ruling: the 2a resonator hash guard updated (Plucked output changes by design) - cost if wrong: none, it guards refactors
Ruling: String/Tube Plucked get a fixed +8 dB, the Bell a per-note make-up (only its modes are narrow) - cost if wrong: a String/Tube pluck a few dB off on unusual recordings
Octave: "6 octave" RED (220 Hz kept at +1) -> GREEN; applied after the fold
EQ: "6 eq" unit RED (stub) -> GREEN; "6 route eq" RED without wiring (+0.0 dB) -> GREEN (-11.9 dB); flat EQ bit-identical
Ruling: the route EQ check uses a low shelf at 16 kHz (-12 dB on everything) - a 1 kHz shelf on this route's low-heavy sound moved it 1 dB, which proves nothing either way - cost if wrong: none
Page: unit (overlay/session/eqResponse) RED 3 -> GREEN 10/10; site 6 checks RED -> GREEN
Ruling: the site lab test's draft route copies the first published route with its three voices on at default levels, and the Freeze check reads the voice's own meter - the live route changed today (Third voice off on one, 0.15 under a First voice at 1.0 on the other) and the whole-mix check failed on main too - cost if wrong: the test no longer covers a loud-route mix
Ruling: the EQ graph weighs 3.5 rows in the panel's column balancing - cost if wrong: a column a little uneven
Final review: fresh Opus reviewer on 0bef523..4935899
Final: fixed C1 a re-voice applied Octave again (held notes up an octave per lab tweak) - Voice keeps the note as asked - "6 octave and re-voice" RED (0.0018 vs 0.0295) -> GREEN (0.0182)
Final: fixed I2 Bell Plucked note start ~108 us on the audio thread - burst only, the ringing in closed form, the burst read in place - harness median 108.3 -> 19.2 us; "6 bell plucked note start" < 25 us
Final: fixed I3 Retune's Octave was inside Tune's power (Tune 0.5: +1 octave = 440 not 622 Hz) - "6 retune octave" RED on the pre-fix engine (622 Hz 1e-9, 440 Hz 0.037) -> GREEN (622 0.037)
Final: fixed (re-graded Important from Minor) a drag was not heard until the pointer paused - lab sync throttled at 60 ms, the public path untouched - "mid-drag, the engine already has the band moving" RED -> GREEN
Final: fixed (re-graded Important from Minor) EQ dots were 31 px touch targets on a phone - 22-unit hit circle (49 px) - phone check RED (31) -> GREEN
Final: minor (deferred): a high-Q band (Q 8, hand-written JSON only; the UI never sets Q) can click when it settles into bypass
Final: minor (deferred): five bands stack to +60 dB (three bells on one spot +36) - the master limiter holds the output, the role's delay and reverb take the boost
Final: minor (deferred): double-tap does not reset one band on iOS Safari (Flat resets all); aria-valuenow is the gain only; a drag's first move jumps the dot's frequency to the pointer
Final: minor (deferred): the core EQ "no step" bound (4.2x the dry step) would not catch a moderate click
Final: Ruling: declined-to-judge - rebuilding public core.wasm would carry the EQ/octave/pluck code (neutral by default); core.wasm stays unbuilt per D1 - cost if wrong: none until step 6
Final: Ruling: declined-to-judge - re-voicing a Bell Plucked note re-plucks it (3c.1 design) - cost if wrong: a re-strike while turning a knob
