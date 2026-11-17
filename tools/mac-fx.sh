#!/bin/sh
# Builds and runs core/tests/fx_test.cpp (the modulation-FX engine's checks) on the Mac mini, from the repo root:
#   sh tools/mac-fx.sh        -> prints "fx ok" last
# Smart App Control blocks the linker on this PC; native clang there, no emscripten needed.
set -e
MAC=appdevcla@192.168.1.105 KEY=~/.ssh/fieldscape_mac
tar czf - core | ssh -i $KEY $MAC 'mkdir -p ~/fsw && rm -rf ~/fsw/core && cd ~/fsw && tar xzf - &&
  c++ -std=c++17 -O2 -Wall core/tests/fx_test.cpp -o /tmp/fx_test && /tmp/fx_test'
