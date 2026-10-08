# SDD ledger — samples-7 (spec docs/superpowers/specs/2026-10-08-samples-7-stretch-resonate-design.md; Kerem chose option 3, lab)
Core: "7 resonate" RED (13% on D F A at resonate 1) -> GREEN (94%, level +0.0 dB, chord change 95%/0%, off identical)
Ruling: the Bell check compares against String's fundamentals (Partials 1); a test bug (an unwrapped 1457-cent target) had read D's energy as a match - fixed in the measure - cost if wrong: none
Ruling: Resonate shares Focus and Partials with Tune (one "how pure / how many overtones" for the bed) - cost if wrong: Tune and Resonate cannot be set apart
Page: site "7:" checks RED -> GREEN; labPointSave keeps one timer per point (fixes the 5 deferred minor: B's slider cancelled A's save)
Ruling: the site test restores the test point's own lab row after (it reset Kerem's "Stretch" point's follow in earlier runs; the old value is not recoverable - Kerem told)
Final review: fresh Opus reviewer on 210e95a..62f3fcb (Resonate 0 verified bit-identical to the base at both windows)
Final: fixed C1 the chord table rebuilt in one callback while gliding (5.5 ms at defaults / 2 s window, 31 ms at Partials 24 / Focus 1) - painted bin by bin with a cursor, weight per bin, the bell shape looked up - "7 resonate gliding" RED on the old stretch (99.9% 2.72 ms) -> GREEN (0.75 / 1.39 ms, budget 2.00)
Final: fixed I2 Resonate after the layers threw them away at 1 - Resonate is its own stage before the layers - "7 resonate then layers" RED -> GREEN
Final: fixed I3 no performance check for Resonate - the gliding worst-process rows above
Final: fixed minor 4 (an unused width in res_peaks) in passing (the code it sat in was rewritten)
Final: minor (deferred): comb_key/res_key overflow signed long long (pre-existing in comb_key; wraps in practice)
Final: Ruling: Resonate rings the last chord (Dm9 before any) off a route, as Tune does; follow alone is gated on a chord - cost if wrong: a bed off every route rings Dm9 until a route is heard
Final: minor (deferred): a hits/grains point keeps an overlaid sound.shape.resonate if one was saved before its mode changed (the stretch alone reads it); the resonate slider has no aria-valuetext (as follow)
