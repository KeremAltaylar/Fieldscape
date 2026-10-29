#!/bin/sh
# Builds core/tests/run.py's three test programs on the Mac mini and copies them back, from the repo root:
#   sh tools/mac-tests.sh && FS_PREBUILT=1 python core/tests/run.py
# Smart App Control blocks emsdk's wasm-ld.exe on this PC (2026-10-29); same compiler and flags as run.py's em++ build.
set -e
MAC=appdevcla@192.168.1.105 KEY=~/.ssh/fieldscape_mac
tar czf - core | ssh -i $KEY $MAC 'rm -rf ~/fsw/core && mkdir -p ~/fsw/build/stretch-tests && cd ~/fsw && tar xzf - &&
  export PATH=~/pybin:$PATH && . ~/emsdk/emsdk_env.sh >/dev/null 2>&1 &&
  em++ -std=c++17 -O1 -DFS_TEST_O1 -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 core/core.cpp core/mix.cpp core/place.cpp core/webm.cpp core/sections.cpp core/resample.cpp core/analysis.cpp core/bench.cpp core/composer.cpp core/piece.cpp core/engine.cpp core/devices/*.cpp core/test.cpp -o build/stretch-tests/core_test.js &&
  em++ -std=c++17 -O1 -DFS_TEST_O1 -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 core/core.cpp core/mix.cpp core/place.cpp core/webm.cpp core/sections.cpp core/resample.cpp core/analysis.cpp core/bench.cpp core/composer.cpp core/piece.cpp core/engine.cpp core/devices/*.cpp core/tests/composer_test.cpp -o build/stretch-tests/composer_test.js &&
  em++ -std=c++17 -O1 -DFS_TEST_O1 -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 core/core.cpp core/mix.cpp core/place.cpp core/webm.cpp core/sections.cpp core/resample.cpp core/bench.cpp core/composer.cpp core/piece.cpp core/devices/test_devices.cpp core/devices/play.cpp core/tests/stretch_test.cpp -o build/stretch-tests/stretch_test.js'
mkdir -p build/stretch-tests
for n in core_test composer_test stretch_test; do
  scp -q -i $KEY $MAC:fsw/build/stretch-tests/$n.js $MAC:fsw/build/stretch-tests/$n.wasm build/stretch-tests/
done
echo '{"type": "commonjs"}' > build/stretch-tests/package.json
ls build/stretch-tests/*.wasm
