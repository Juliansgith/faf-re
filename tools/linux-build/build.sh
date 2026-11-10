#!/usr/bin/env bash
# Engine addition (faf-neural-ai): a parity Linux build from any host with Docker.
#
#   tools/linux-build/build.sh [cmake --build targets...]      (default: headless server training)
#
# Builds the image (tools/linux-build/Dockerfile, the pinned toolchain) when missing, copies the
# source tree into the `faf-linux-work` volume (a build straight off a Windows bind mount is several
# times slower) and builds the linux-clang-x64-release-plain preset there. The programs end up in
# build/linux-x64/ next to this checkout. FAF_LINUX_PRESET overrides the preset; FAF_LINUX_CMAKE_ARGS
# the extra cache settings (default -DFAF_ULTRA=ON, the exact-results speed codegen the Windows
# training build uses too).
#
# FAF_LINUX_IMAGE=release builds in the release image instead (tools/linux-build/Dockerfile.release:
# Ubuntu 22.04, LLVM's clang 18.1.8, glibc 2.34 and up at run time) with the
# linux-clang-x64-release-portable preset, into build/linux-x64-release/ by default: the downloadable
# client (tools/linux-build/package.sh). It has its own volume (FAF_LINUX_VOLUME, default
# faf-linux-release); the parity image's build tree is not reusable there. FAF_LINUX_OUT overrides
# the output folder.
#
# Inside the container (FAF_LINUX_BUILD_INNER=1) the same script does the copy and the build.
set -euo pipefail

image_kind="${FAF_LINUX_IMAGE:-parity}"
case "$image_kind" in
  parity) default_preset=linux-clang-x64-release-plain ;;
  release) default_preset=linux-clang-x64-release-portable ;;
  *) echo "FAF_LINUX_IMAGE=$image_kind: expected parity or release" >&2; exit 2 ;;
esac
preset="${FAF_LINUX_PRESET:-$default_preset}"
cmake_args="${FAF_LINUX_CMAKE_ARGS:--DFAF_ULTRA=ON}"
targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
  targets=(headless server training)
fi

if [ "${FAF_LINUX_BUILD_INNER:-0}" = "1" ]; then
  mkdir -p /work/src /work/out
  # The checkout's .git is a worktree link to a host path; the build only needs the files.
  # Dependencies the configure step downloads here (the Linux client's) are kept between builds.
  rsync -a --delete --filter 'P /dependencies/**' --exclude '/.git' --exclude '/build/' --exclude '/out/' /src/ /work/src/
  cd /work/src
  # shellcheck disable=SC2086 # cmake_args is a list of -D settings
  cmake --preset "$preset" -B /work/build $cmake_args >/work/configure.log 2>&1 \
    || { tail -40 /work/configure.log; exit 1; }
  cmake --build /work/build --target "${targets[@]}" -- -k 0
  rm -rf /work/out/*
  for target in "${targets[@]}"; do
    name="$target"
    if [ "$target" = "faf_client" ]; then
      name="faf" # its OUTPUT_NAME (cmake/FafLinux.cmake)
    fi
    found="$(find /work/build -maxdepth 3 -type f -name "$name" -perm -u+x | head -1)"
    if [ -n "$found" ]; then
      cp "$found" /work/out/
    fi
  done
  # The client (output name faf) loads DXC from its own folder (gpg/gal/effects/ShaderCompiler.cpp).
  if [ -f /work/out/faf ]; then
    cp /work/src/dependencies/dxc-linux/lib/libdxcompiler.so /work/src/dependencies/dxc-linux/lib/libdxil.so /work/out/
  fi
  cp -r /work/out/. /src-out/
  exit 0
fi

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
if [ "$image_kind" = "release" ]; then
  image="faf-linux-build:22.04-release"
  dockerfile="$here/Dockerfile.release"
  volume="${FAF_LINUX_VOLUME:-faf-linux-release}"
  out="${FAF_LINUX_OUT:-$root/build/linux-x64-release}"
else
  image="faf-linux-build:24.04"
  dockerfile="$here/Dockerfile"
  volume="${FAF_LINUX_VOLUME:-faf-linux-work}"
  out="${FAF_LINUX_OUT:-$root/build/linux-x64}"
fi
if ! docker image inspect "$image" >/dev/null 2>&1; then
  docker build -t "$image" -f "$dockerfile" "$here"
fi
mkdir -p "$out"
# MSYS (Git Bash) would rewrite the container paths below into Windows ones.
MSYS_NO_PATHCONV=1 docker run --rm \
  -e FAF_LINUX_BUILD_INNER=1 -e FAF_LINUX_PRESET="$preset" -e FAF_LINUX_CMAKE_ARGS="$cmake_args" \
  -v "$volume:/work" \
  -v "$root:/src:ro" \
  -v "$out:/src-out" \
  "$image" bash /src/tools/linux-build/build.sh "${targets[@]}"
echo "Linux programs: $out"
