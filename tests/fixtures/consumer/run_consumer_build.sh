#!/usr/bin/env bash
# Builds the consumer fixture against the esprite tree under test and checks
# the produced runner boots the consumer target. Fails loudly on any step.
set -euo pipefail
ESPRITE_SOURCE_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cmake -S "$(dirname "$0")" -B "$WORK/build" \
      -DESPRITE_SOURCE_DIR="$ESPRITE_SOURCE_DIR" >/dev/null
cmake --build "$WORK/build" --target consumer-runner >/dev/null
# The runner defaults to native single-target (ESPRITE_RUNNER suppresses the
# embedded qemu boards), so no env var is needed here: a fresh subprocess must
# list only the consumer target.
OUT="$("$WORK/build/consumer-runner" list-targets --json)"
if ! echo "$OUT" | grep -q '"consumer"'; then
    echo "FAIL: consumer target missing"; exit 1
fi
if echo "$OUT" | grep -q 'qemu_esp32c3'; then
    echo "FAIL: runner did not default to native single-target"; exit 1
fi
# A --target-less command must resolve to the single onboarded target and render.
"$WORK/build/consumer-runner" screenshot "$WORK/shot.png" >/dev/null
if [ ! -s "$WORK/shot.png" ]; then
    echo "FAIL: screenshot without --target did not render"; exit 1
fi
# The daemon path shares the default and the default target: a boot with no
# explicit target must use --target and succeed.
printf '{"cmd":"boot"}\n{"cmd":"quit"}\n' \
    | "$WORK/build/consumer-runner" run --target consumer > "$WORK/run.out"
if ! grep -q '"ok":true' "$WORK/run.out"; then
    echo "FAIL: daemon boot did not succeed"; cat "$WORK/run.out"; exit 1
fi
echo "consumer runner OK (native single-target default + screenshot + daemon)"
