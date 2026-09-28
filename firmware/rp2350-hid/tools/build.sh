#!/usr/bin/env bash
# Build firmware (.elf/.uf2) and run host unit tests inside the pinned Docker
# image. Output: <repo>/build/rp2350-hid-{ON,OFF}/{rkmoon_rp2350_hid.elf,.uf2,SHA256SUMS}
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(cd "$here/../.." && pwd)"
image="rkmoon-rp2350-build:1"

"$here/tools/fetch_deps.sh"
docker build -q -t "$image" "$here/tools" >/dev/null

docker run --rm -e RKMOON_PIO_USB_CDC="${RKMOON_PIO_USB_CDC:-ON}" -v "$repo:/src" -w /src/firmware/rp2350-hid "$image" bash -euo pipefail -c '
  deps=/src/build/rp2350-hid-deps
  export PICO_SDK_PATH=$deps/pico-sdk
  # picotool (host tool used for UF2 generation), built once per deps dir
  if [ ! -x $deps/picotool-install/picotool/picotool ]; then
    cmake -S $deps/picotool -B /tmp/picotool-build -G Ninja -DPICOTOOL_NO_LIBUSB=1       -DCMAKE_INSTALL_PREFIX=$deps/picotool-install -DPICOTOOL_FLAT_INSTALL=1 >/dev/null
    cmake --build /tmp/picotool-build >/dev/null
    cmake --install /tmp/picotool-build >/dev/null
  fi
  out=/src/build/rp2350-hid-${RKMOON_PIO_USB_CDC}
  mkdir -p $out
  cmake -S . -B /tmp/fw -G Ninja -DCMAKE_BUILD_TYPE=Release -DRKMOON_PIO_USB_CDC=$RKMOON_PIO_USB_CDC -DPICO_BOARD=${PICO_BOARD:-waveshare_rp2350_usb_c}     -Dpicotool_DIR=$deps/picotool-install/picotool
  cmake --build /tmp/fw
  cp /tmp/fw/rkmoon_rp2350_hid.elf /tmp/fw/rkmoon_rp2350_hid.uf2 /tmp/fw/rkmoon_rp2350_hid.elf.map $out/
  arm-none-eabi-size $out/rkmoon_rp2350_hid.elf | tee $out/size.txt
  arm-none-eabi-gcc --version | head -1 > $out/toolchain.txt
  cmake --version | head -1 >> $out/toolchain.txt
  $deps/picotool-install/picotool/picotool info -a $out/rkmoon_rp2350_hid.uf2 > $out/picotool-info.txt || true
  make -C tests OUT=/tmp/tests CC=gcc test | tee $out/host-tests.txt
  gcc --version | head -1 >> $out/toolchain.txt
  (cd $out && sha256sum rkmoon_rp2350_hid.uf2 rkmoon_rp2350_hid.elf > SHA256SUMS)
  cat $out/SHA256SUMS
'
