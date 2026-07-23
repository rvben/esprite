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
# The runner is native-only: suppress embedded qemu boards so the single
# consumer target resolves without --target. This is a real subprocess with a
# fresh registry, so it also verifies the gate (which the process-global,
# idempotent registry makes untestable in-process).
OUT="$(ESPRITE_REGISTER_QEMU_BUILTINS=0 "$WORK/build/consumer-runner" list-targets --json)"
if ! echo "$OUT" | grep -q '"consumer"'; then
    echo "FAIL: consumer target missing"; exit 1
fi
if echo "$OUT" | grep -q 'qemu_esp32c3'; then
    echo "FAIL: gate did not suppress qemu builtins"; exit 1
fi
# Exercise the daemon path too (it shares the gate helper and the default
# target): a boot with no explicit target must use --target and succeed.
printf '{"cmd":"boot"}\n{"cmd":"quit"}\n' \
    | ESPRITE_REGISTER_QEMU_BUILTINS=0 "$WORK/build/consumer-runner" run --target consumer > "$WORK/run.out"
if ! grep -q '"ok":true' "$WORK/run.out"; then
    echo "FAIL: daemon boot did not succeed"; cat "$WORK/run.out"; exit 1
fi
echo "consumer runner OK (gate + daemon verified)"
