# RKMoon RP2350 HID bridge build image (arm-none-eabi cross compiler + host gcc for unit tests)
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      cmake ninja-build make gcc g++ python3 git ca-certificates \
      gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
      pkg-config libusb-1.0-0-dev \
 && python3 -c 'from pathlib import Path; import shutil; [(shutil.rmtree(p) if p.is_dir() else p.unlink()) for p in Path("/var/lib/apt/lists").iterdir()]'
WORKDIR /work
