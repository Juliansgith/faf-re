# Engine addition (faf-neural-ai): the pinned Linux build environment, as a container.
#
# cmake/toolchains/linux-clang-pins.cmake pins the compiler and C library the simulation's
# Windows/Linux parity is checked with (Ubuntu 24.04, clang 18.1.3, glibc 2.39). This image is that
# machine, so a Linux build made on a Windows desktop (Docker Desktop) or any other host is a parity
# build. tools/linux-build/build.sh drives it.
FROM ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      clang-18 lld-18 llvm-18 libc++-18-dev libc++abi-18-dev libunwind-18-dev libc6-dev \
      cmake ninja-build python3 binutils git rsync ca-certificates curl xz-utils \
    && rm -rf /var/lib/apt/lists/*
# The Linux client (SDL3, FAudio, Vulkan): the window-system and audio headers SDL compiles its
# backends against (it loads the libraries themselves at run time), and the Vulkan loader. The
# simulation never sees any of these, so they leave its parity alone.
RUN apt-get update && apt-get install -y --no-install-recommends \
      pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
      libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
      libegl-dev libgl-dev libdrm-dev libgbm-dev libpulse-dev libpipewire-0.3-dev libasound2-dev \
      libdbus-1-dev libudev-dev libvulkan-dev libvulkan1 mesa-vulkan-drivers xvfb xauth \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
