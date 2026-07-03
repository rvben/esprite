#!/usr/bin/env bash
# Build only the rgb_c3.bin display/input/net fixture (docker). Split out of
# build-fixtures.sh so CI can rebuild just this one on a cache miss without
# the arduino-cli toolchain the other fixtures need.
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$PWD/tests/fixtures/qemu" && mkdir -p "$out"

# Digest-pinned: a silent upstream rebuild of the release-v5.4 tag could
# change fixture bytes (and golden stability) underneath us. Bump with:
#   docker buildx imagetools inspect espressif/idf:release-v5.4
# then regenerate fixtures + goldens and eyeball them. The CI fixture cache
# keys on this script's hash, so a pin bump rebuilds the cache automatically.
IDF_IMAGE="espressif/idf:release-v5.4@sha256:6cd8af13969cacaacf3d88eed9282710fa41af7237a448a1626833ce56ff2669"

docker run --rm -v "$PWD/tools/qemu/rgb_demo":/proj_demo:ro \
  -v "$PWD/tools/qemu/esprite_qemu_agent":/proj_agent:ro -v "$out":/out \
  "$IDF_IMAGE" bash -ec '
  cp -r /proj_demo /tmp/rgb && cp -r /proj_agent /tmp/esprite_qemu_agent
  cd /tmp/rgb
  idf.py set-target esp32c3 build
  cd build
  esptool.py --chip esp32c3 merge_bin --fill-flash-size 4MB \
      -o /out/rgb_c3.bin @flash_args'
ls -la "$out"
