# 8a ledger (Kerem 2026-10-11: drift menus, point meters, grain marks, master meter; panel fit; pull to chord)
Morph fix: a second var MORPH_DESTS (plain names for the cells, commit 9edcc86) replaced the panel's {k,label} list - every destination menu blank, choices saved as "undefined". Renamed MORPH_DEST_KEYS; an unknown dest shows "choose a parameter…" / "no parameter". web_patch "every morph's destination menu shows the parameter it drives" RED -> GREEN. Only Koşuyolu morph 1 (tide) is affected in the data - Kerem re-picks it.
Ruling: pull to chord not added to First/Third voices - they already pick only chord tones (pull is 100% built in); only the Second voice walks its sector's mode - cost if wrong: Kerem asks again for something else
Ruling: panel fit with sampler voices - one progression row from 1400 px (was 1500), paired sampler menus, the sample name on its header line, a 28 px waveform, one-line column headings on >= 1000 px with the full text in title - measured 141 px over -> fits - cost if wrong: headings truncated (hover shows them)
Ruling: web_patch's arrow-key check steps down at the top of the range (the live route's tempo now sits at 140, its max) - cost if wrong: none
Ruling: the site test's draft route gets digital voices without sampler keys (the live route it copies has samplers now) - cost if wrong: none
Final review (fresh Opus): no Critical
Final: fixed I1 the point meter froze at its last level after Stop - cleared on Stop, live point reports ignored after it - site RED (-13 dB) -> GREEN
Final: fixed I2 stretch and grains meters measured different things (before vs after the walk's distance gain) - both the point's own sound at its own gain - core "heard in full / at a tenth" RED (-15.3 vs -35.3) -> GREEN (-15.3 both)
Final: fixed (re-graded Important from Minor 1) only 32 recent grains (marks vanished early on dense clouds) - 96 - core RED (32) -> GREEN (81)
Final: fixed (re-graded Important from Minor 2) every panel redraw re-posted the watch and reset the meter/marks - the same point is not re-watched - site RED (1 -> 3 posts) -> GREEN
Final: fixed (re-graded Important from Minor 3) the master meter's rAF ran for the whole page life - only while the patch panel is open - site RED (ticks kept rising) -> GREEN
Final: minor (deferred): cells label a morph with an unknown destination "pad filter" (the panel says choose a parameter)
Final: minor (deferred): on engine start the watch uses the current selection, not the open panel's point
Final: minor (deferred): header meters - aria-label on a span is not announced; at phone width the header may wrap (unmeasured)
Final: minor (deferred): paired menus may truncate long names in a five-column layout (unmeasured)
Final: minor (deferred): the meter math split stops an FMA contraction on arm64 - rhythm output may differ by ~1 ulp (inaudible)
Final: minor (deferred): fs_piece_point's header lacks the audio-thread note; the probe grains point sits inside the route midpoint's radius in the site test
