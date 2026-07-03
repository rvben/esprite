#!/usr/bin/env bash
# Build only the lvgl_c3.bin real-LVGL-app image (docker). Same shape and pin
# as build-rgb-fixture.sh.
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$PWD/tests/fixtures/qemu" && mkdir -p "$out"

# Digest-pinned; bump procedure in build-rgb-fixture.sh.
IDF_IMAGE="espressif/idf:release-v5.4@sha256:6cd8af13969cacaacf3d88eed9282710fa41af7237a448a1626833ce56ff2669"

docker run --rm -v "$PWD/tools/qemu/lvgl_demo":/proj_demo:ro \
  -v "$PWD/tools/qemu/esprite_qemu_agent":/proj_agent:ro \
  -v "$PWD/tools/qemu/esp_lcd_touch_esprite":/proj_touch:ro -v "$out":/out \
  "$IDF_IMAGE" bash -ec '
  cp -r /proj_demo /tmp/lvgl_demo
  cp -r /proj_agent /tmp/esprite_qemu_agent
  cp -r /proj_touch /tmp/esp_lcd_touch_esprite
  cd /tmp/lvgl_demo
  idf.py set-target esp32c3 build
  cd build
  esptool.py --chip esp32c3 merge_bin --fill-flash-size 4MB \
      -o /out/lvgl_c3.bin @flash_args'
ls -la "$out"
