#!/usr/bin/env bash
# Fetch prebuilt Espressif QEMU (qemu-xtensa + qemu-riscv32) release tarballs
# for this host into .qemu/. No ESP-IDF install, no gh auth: plain curl
# against a PINNED release tag so the tested emulator version never moves
# silently (bump the tag deliberately, re-run the determinism checks).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p .qemu && cd .qemu

TAG="esp-develop-9.2.2-20260417"   # the release this backend is tested against
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)  pat="aarch64-apple-darwin" ;;
  Linux-x86_64)  pat="x86_64-linux-gnu" ;;
  *) echo "unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

# Confirmed against the real release (2026-07-02): assets are named
# qemu-<arch>-softmmu-esp_develop_<ver>_<date>-<platform>.tar.xz, e.g.
# qemu-riscv32-softmmu-esp_develop_9.2.2_20260417-aarch64-apple-darwin.tar.xz
assets=$(curl -fsSL "https://api.github.com/repos/espressif/qemu/releases/tags/$TAG" \
         | python3 -c 'import json,sys; [print(a["browser_download_url"]) for a in json.load(sys.stdin)["assets"]]')
for arch in xtensa riscv32; do
  url=$(printf '%s\n' "$assets" | grep "$arch" | grep "$pat" | head -1)
  [ -n "$url" ] || { echo "no $arch asset matching $pat in $TAG" >&2; exit 1; }
  curl -fsSL "$url" -o "$arch.tar.xz"
  mkdir -p "$arch" && tar -xf "$arch.tar.xz" -C "$arch" --strip-components=1
done

r32=$(find riscv32 -name 'qemu-system-riscv32' -type f | head -1)
xt=$(find xtensa -name 'qemu-system-xtensa' -type f | head -1)
[ -n "$r32" ] || { echo "qemu-system-riscv32 binary not found after extract" >&2; exit 1; }
[ -n "$xt" ] || { echo "qemu-system-xtensa binary not found after extract" >&2; exit 1; }
printf 'export ESPRITE_QEMU_RISCV32=%s\nexport ESPRITE_QEMU_XTENSA=%s\nexport ESPRITE_QEMU_VERSION=%s\n' \
  "$PWD/$r32" "$PWD/$xt" "$TAG" > env.sh
"$PWD/$r32" --version && "$PWD/$xt" --version
echo "wrote .qemu/env.sh (ESPRITE_QEMU_RISCV32, ESPRITE_QEMU_XTENSA, ESPRITE_QEMU_VERSION=$TAG)"
