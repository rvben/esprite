#!/usr/bin/env bash
# Build only the rgb_c3.bin display/input/net fixture (docker). Split out of
# build-fixtures.sh so CI can rebuild just this one on a cache miss without
# the arduino-cli toolchain the other fixtures need.
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$PWD/tests/fixtures/qemu" && mkdir -p "$out"

docker run --rm -v "$PWD/tools/qemu/rgb_demo":/proj_demo:ro \
  -v "$PWD/tools/qemu/esprite_qemu_agent":/proj_agent:ro -v "$out":/out \
  espressif/idf:release-v5.4 bash -ec '
  cp -r /proj_demo /tmp/rgb && cp -r /proj_agent /tmp/esprite_qemu_agent
  cd /tmp/rgb
  idf.py set-target esp32c3 build
  cd build
  esptool.py --chip esp32c3 merge_bin --fill-flash-size 4MB \
      -o /out/rgb_c3.bin @flash_args'
ls -la "$out"
