# Modulation FX ledger

## Part 1 Task 7: the engine's cost (2026-11-17)

`sh tools/mac-tests.sh && node build/stretch-tests/fx_bench.js` (wasm -O1 built on the Mac, Node v24.19.0 on this PC,
128-frame blocks, 48 kHz, 20 s, the first second skipped). Three runs, nothing else of ours running (Chrome and
WebView2 were open).

| case | budget | run 1 | run 2 | run 3 |
|---|---|---|---|---|
| idle (nothing on, silence in) | <= 0.05 % | 0.021 % | 0.021 % | 0.022 % |
| defaults, pink in (ret 0: the host skips the engine) | <= 0.05 % | 0.021 % | 0.022 % | 0.022 % |
| typical: phase comb cloud echo space at 1, 2 slots | <= 3.0 % | 2.267 % | 2.275 % | 2.261 % |
| typical + a slot on shift.mix (Shift runs silent) | <= 3.0 % | 2.566 % | 2.564 % | 2.562 % |
| all nine at 1, 8 slots, costly settings | <= 8.0 % | 4.846 % | 4.847 % | 4.839 % |
| p99.9 block, typical / modmix / all | <= 0.35 ms | 0.156 / 0.155 / 0.302 | 0.153 / 0.167 / 0.267 | 0.150 / 0.157 / 0.291 |
| max block, typical / modmix / all | (not a budget) | 0.219 / 0.206 / 0.354 | 0.208 / 0.235 / 0.298 | 0.168 / 0.214 / 3.117 |
| first block after mix leaves 0, worst module | <= 1.5 ms | echo 0.053 | echo 0.053 | echo 0.053 |
| memory per Engine | - | 2,692,984 B | same | same |

Before the optimisation (same bench, e58e9d0's engine): idle 0.050 %, typical 4.01 %, typical+modmix 4.54 %,
all 6.83 % with p99.9 0.372 ms (over). Commit 6e8634e brought it under; `sh tools/mac-fx.sh` unchanged but
for one click ratio (space.damp sine 0.85 -> 0.84).
