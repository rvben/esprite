#!/usr/bin/env bash
# Runtime libraries the prebuilt Espressif QEMU needs on a bare Debian/Ubuntu
# host. Derived, not guessed: extract the pinned linux-gnu release tarball
# (see fetch-qemu.sh for the tag) and run
#   docker run --rm --platform linux/amd64 -v <extracted>/qemu:/q:ro \
#     ubuntu:24.04 ldd /q/bin/qemu-system-riscv32
# The packages below resolve every "not found" line (verified against
# esp_develop_9.2.2_20260417 on ubuntu:24.04: libSDL2-2.0.so.0,
# libslirp.so.0, libpixman-1.so.0). Re-derive when the fetch-qemu.sh pin
# moves.
set -euo pipefail
sudo apt-get update -qq
sudo apt-get install -y -qq --no-install-recommends \
  libsdl2-2.0-0 libslirp0 libpixman-1-0
