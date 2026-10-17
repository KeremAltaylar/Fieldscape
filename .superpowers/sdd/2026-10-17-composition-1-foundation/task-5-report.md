# Task 5 report: zones reach the composition

Status: DONE (iOS unbuilt).

## Per platform
- Web engine (core/engine.cpp): fs_engine_step's zone loop collects the ids of spots with zone.inside and the id of one entered (ev == 1), then calls fs_piece_zones only when the inside set changed or a zone was entered. `std::string zones_sent` added to fs_engine. Spot already carried `id` here.
- Android (jni.cpp): `std::string id` added to Spot (set right after the push_back from the point id); same collect-and-send with a function-local `static std::string zones_sent`. Same thread as fs_piece_zone. `./gradlew assembleDebug`: BUILD SUCCESSFUL.
- iOS (RouteSound.swift): `id: String` added to Spot (from p["id"], "" if absent), `inside`/`entered` collected in the step loop, `zonesSent` on the class, same call as the brief. NOT BUILT here (needs the Mac); written by mirroring the existing fs_zone_step/fs_piece_zone calls.

## Tests
- Failing first: outside -30.7 dB, inside -30.7 dB (then -32.1 dB after the engine change, see deviation 2).
- Final: `C zone: outside the zone -28.9 dB, inside (voice 1 off) -53.4 dB` (24.5 dB drop). `python core/tests/run.py`: 20/20 passed. `npm test`: 504 pass.
- Earlier run.py runs aborted on core_test timing asserts (check 7/8) while gradle ran in parallel; the clean re-run passed.

## Deviations
1. Ruling applied: `"bass":{"mode":"off"}` added to the test route's comp.
2. The brief's test cannot pass as written: fs_zone_step has an 8 s cooldown measured from time 0 (fired_at_ms starts 0), so the first entry lands after 8 s, and the test measured 4-12 s. Test now runs 18 s and measures 12-18 s (comment in the test).
3. Test file has pre-existing odd bytes (backslash-CR inside some printf strings); I edited in binary mode to leave them untouched.

## Concerns
- Real walkers entering a zone in the first 8 s after launch are ignored by the cooldown (existing behaviour, affects plain-zone chimes too).
- Android `zones_sent` is a process-wide static; a route reload with a changed spot set just resends once.

## Fix round 1
The inside set now comes from geometry, not the chime cooldown. Each spot has `comp_in` (engine.cpp, jni.cpp Spot; `compIn` in RouteSound.swift): true when dist <= zoneR, stays true until dist > zoneR * FS_ZONE_MARGIN, no cooldown, updated before the far-spot `continue`. `entered` is a false->true flip of comp_in. fs_zone_step / `ev == 1` now only drive the icon chime. Android `zones_sent` moved to a Walker member (a reload re-sends). Test window reverted to the brief's 12 s run measured from 4 s.
Result: `C zone: outside the zone -30.7 dB, inside (voice 1 off) -300.0 dB` (the zone is entered at the first step, so voice 1 is silent for the whole window). run.py 20/20, npm test 504, assembleDebug BUILD SUCCESSFUL. iOS still unbuilt.
