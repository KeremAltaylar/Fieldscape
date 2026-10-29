#!/bin/sh
# Builds web/core.wasm and web/core-lab.wasm on the Mac mini, from the repo root:  sh tools/mac-wasm.sh
# Smart App Control started blocking emsdk's wasm-ld.exe on this PC (2026-10-29), so the wasm is linked
# there (emsdk 6.0.10 in ~/emsdk, python 3.12 in ~/pybin) and copied back. Same compiler, same flags.
set -e
MAC=appdevcla@192.168.1.105 KEY=~/.ssh/fieldscape_mac
tar czf - core web/build.sh web/build-lab.sh | ssh -i $KEY $MAC 'rm -rf ~/fsw/core && mkdir -p ~/fsw/web && cd ~/fsw && tar xzf - &&
  export PATH=~/pybin:$PATH && . ~/emsdk/emsdk_env.sh >/dev/null 2>&1 && sh web/build.sh && sh web/build-lab.sh && cmp web/core.wasm web/core-lab.wasm'
scp -q -i $KEY $MAC:fsw/web/core.wasm web/core.wasm
scp -q -i $KEY $MAC:fsw/web/core-lab.wasm web/core-lab.wasm
cmp web/core.wasm web/core-lab.wasm && ls -l web/core.wasm
