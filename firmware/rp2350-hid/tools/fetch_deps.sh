#!/usr/bin/env bash
# Fetch pinned Pico SDK (+TinyUSB submodule), Pico-PIO-USB and picotool into
# ${RKMOON_FW_DEPS:-<repo>/build/rp2350-hid-deps}, verifying exact commits.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(cd "$here/../.." && pwd)"
# shellcheck disable=SC1091
source "$here/deps.lock"
deps="${RKMOON_FW_DEPS:-$repo/build/rp2350-hid-deps}"
mkdir -p "$deps"

fetch() { # dir url tag commit
  local dir="$1" url="$2" tag="$3" commit="$4"
  if [ ! -d "$deps/$dir/.git" ]; then
    git -c advice.detachedHead=false clone -q --branch "$tag" --depth 1 "$url" "$deps/$dir"
  fi
  local got
  got="$(git -C "$deps/$dir" rev-parse HEAD)"
  if [ "$got" != "$commit" ]; then
    echo "error: $dir is at $got, expected $commit ($tag)" >&2
    exit 1
  fi
}

fetch pico-sdk "$PICO_SDK_URL" "$PICO_SDK_TAG" "$PICO_SDK_COMMIT"
git -C "$deps/pico-sdk" submodule update --init --depth 1 lib/tinyusb >/dev/null
tusb="$(git -C "$deps/pico-sdk/lib/tinyusb" rev-parse HEAD)"
[ "$tusb" = "$TINYUSB_COMMIT" ] || { echo "error: tinyusb at $tusb, expected $TINYUSB_COMMIT" >&2; exit 1; }
fetch Pico-PIO-USB "$PICO_PIO_USB_URL" "$PICO_PIO_USB_TAG" "$PICO_PIO_USB_COMMIT"
fetch picotool "$PICOTOOL_URL" "$PICOTOOL_TAG" "$PICOTOOL_COMMIT"
echo "deps ok: $deps"
