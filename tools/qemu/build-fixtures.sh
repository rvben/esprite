#!/usr/bin/env bash
# Build QEMU fixture images. Requires docker (hello_world) and arduino-cli.
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$PWD/tests/fixtures/qemu" && mkdir -p "$out"

# 1) ESP-IDF hello_world for ESP32-C3, merged to one 4MB flash image.
docker run --rm -v "$out":/out espressif/idf:release-v5.4 bash -ec '
  cp -r "$IDF_PATH/examples/get-started/hello_world" /tmp/hw && cd /tmp/hw
  idf.py set-target esp32c3 build
  cd build
  esptool.py --chip esp32c3 merge_bin --fill-flash-size 4MB \
      -o /out/hello_c3.bin @flash_args'

# 2) Arduino tick sketch for ESP32. --export-binaries drops build/ next to
# the sketch; merge bootloader+partitions+boot_app0+app at standard offsets.
# boot_app0.bin ships with the esp32 core (not the sketch build); the OTA
# data selector at 0xe000 is required or the image will not boot.
# FlashMode=dio,FlashFreq=40 overrides the board's default QIO/80MHz: with
# those defaults the app image asks the bootloader to auto-enable QIO mode,
# which fails the QIE-bit-enable sequence on QEMU's simulated flash chip (a
# real-hardware GD/Winbond quirk QEMU faithfully reproduces, see
# espressif/esp-idf#1944) and then crashes with a fatal flash-init assert.
arduino-cli core install esp32:esp32 \
  --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli compile --fqbn esp32:esp32:esp32:FlashMode=dio,FlashFreq=40 \
  --export-binaries tools/qemu/arduino_tick
b=tools/qemu/arduino_tick/build/esp32.esp32.esp32
boot_app0=$(find "$(arduino-cli config get directories.data)/packages/esp32" \
            -name boot_app0.bin | head -1)
[ -n "$boot_app0" ] || { echo "boot_app0.bin not found in esp32 core" >&2; exit 1; }
# Prefer a standalone `esptool` on PATH: on hosts where esptool was installed
# as a pipx/uv tool rather than into the resolved python3's site-packages,
# `python3 -m esptool` fails with "No module named esptool".
if command -v esptool >/dev/null 2>&1; then
  esptool_cmd=(esptool)
else
  esptool_cmd=(python3 -m esptool)
fi
"${esptool_cmd[@]}" --chip esp32 merge_bin --fill-flash-size 4MB -o "$out/arduino_esp32.bin" \
  0x1000 "$b/arduino_tick.ino.bootloader.bin" \
  0x8000 "$b/arduino_tick.ino.partitions.bin" \
  0xe000 "$boot_app0" \
  0x10000 "$b/arduino_tick.ino.bin"
ls -la "$out"
