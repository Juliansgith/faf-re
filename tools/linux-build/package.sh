#!/usr/bin/env bash
# Engine addition (faf-neural-ai): the downloadable Linux client, faf-linux-x86_64-<version>.tar.gz.
#
#   FAF_LINUX_IMAGE=release FAF_LINUX_CMAKE_ARGS="-DFAF_ULTRA=ON -DFAF_LINUX_CLIENT=ON" \
#     tools/linux-build/build.sh faf_client                        # build/linux-x64-release/
#   tools/linux-build/package.sh [version]                         # build/package/
#
# Packs the release build (tools/linux-build/Dockerfile.release, glibc 2.34 and up) with the launcher
# (tools/linux-build/package/faf-launch), the engine's own Lua (data/fafengine) and the licence notices
# of the third-party code in it, in the release image. Microsoft's DXC libraries are built on glibc
# 2.38; they get libfaf-glibc-compat.so (glibc-compat/) and weak GLIBC_2.35+ requirements
# (elf_weak_versions.py), and the step fails when any file still needs a glibc newer than 2.34.
#
#   version               default: $FAF_LINUX_VERSION, else <UTC date>-g<commit>
#   FAF_LINUX_OUT         the build's programs (default build/linux-x64-release)
#   FAF_LINUX_VOLUME      the build's volume, for the dependencies' licences (default faf-linux-release)
#   FAF_LINUX_PACKAGE_DIR where the archive goes (default build/package)
#
# Inside the container (FAF_LINUX_PACKAGE_INNER=1) the same script does the packing.
set -euo pipefail

glibc_max=2.34
name_prefix=faf-linux-x86_64

if [ "${FAF_LINUX_PACKAGE_INNER:-0}" = "1" ]; then
  version="$1"
  deps=/work/src/dependencies
  stage_root=/tmp/stage
  name="$name_prefix-$version"
  stage="$stage_root/$name"
  rm -rf "$stage_root"
  mkdir -p "$stage/bin" "$stage/licenses"

  for f in faf libdxcompiler.so libdxil.so; do
    [ -f "/bin-in/$f" ] || { echo "package: /bin-in/$f is missing (build faf_client with FAF_LINUX_IMAGE=release)" >&2; exit 1; }
    install -m 0755 "/bin-in/$f" "$stage/bin/$f"
  done
  cp -r /src/data/fafengine "$stage/bin/fafengine"
  install -m 0755 /src/tools/linux-build/package/faf-launch "$stage/faf-launch"

  # The program itself must not need more than glibc 2.34.
  python3 /src/tools/linux-build/elf_weak_versions.py --max "$glibc_max" --report "$stage/bin/faf"

  # DXC: its few newer glibc functions come from the compat library when glibc lacks them.
  /usr/lib/llvm-18/bin/clang -O2 -fPIC -shared -fno-builtin -Wall -Werror -Wl,-z,defs -Wl,--as-needed \
    -Wl,-soname,libfaf-glibc-compat.so -o "$stage/bin/libfaf-glibc-compat.so" \
    /src/tools/linux-build/glibc-compat/faf_glibc_compat.c
  for lib in libdxcompiler.so libdxil.so; do
    patchelf --add-needed libfaf-glibc-compat.so "$stage/bin/$lib"
    # The compat library sits next to them; dlopen by path does not search there by itself.
    existing="$(patchelf --print-rpath "$stage/bin/$lib")"
    patchelf --set-rpath "\$ORIGIN${existing:+:$existing}" "$stage/bin/$lib"
  done
  python3 /src/tools/linux-build/elf_weak_versions.py --max "$glibc_max" --weaken \
    "$stage/bin/libdxcompiler.so" "$stage/bin/libdxil.so"
  python3 /src/tools/linux-build/elf_weak_versions.py --max "$glibc_max" --report \
    "$stage/bin/libdxcompiler.so" "$stage/bin/libdxil.so" "$stage/bin/libfaf-glibc-compat.so"
  # Every symbol bound to a weakened (newer) version must be one the compat library defines.
  provided="$(nm -D --defined-only "$stage/bin/libfaf-glibc-compat.so" | awk '{print $3}' | sort -u)"
  for lib in libdxcompiler.so libdxil.so; do
    needed="$(objdump -T "$stage/bin/$lib" | awk '/\*UND\*/ && /\(GLIBC_[0-9.]+\)/ {
        v = $0; sub(/.*\(GLIBC_/, "", v); sub(/\).*/, "", v); split(v, p, ".");
        if (p[1] > 2 || (p[1] == 2 && p[2] > 34)) print $NF }' | sort -u)"
    for symbol in $needed; do
      if ! grep -qx "$symbol" <<<"$provided"; then
        echo "package: $lib needs $symbol from a glibc newer than $glibc_max, and libfaf-glibc-compat.so lacks it" >&2
        exit 1
      fi
    done
    needed_list="$(tr '\n' ' ' <<<"$needed")"
    echo "$lib: $(wc -w <<<"$needed") symbols from glibc > $glibc_max, all in libfaf-glibc-compat.so: $needed_list"
  done

  # --- Licence notices ------------------------------------------------------------------------
  # licence <file name> <what> <candidate source files...>: the first that exists.
  notices="$stage/licenses/THIRD_PARTY_NOTICES.txt"
  licence() {
    local out="$1" what="$2"
    shift 2
    for candidate in "$@"; do
      if [ -f "$candidate" ]; then
        cp "$candidate" "$stage/licenses/$out"
        printf '%-28s %s\n' "$out" "$what" >> "$notices"
        return 0
      fi
    done
    echo "package: no licence file for $what (looked for: $*)" >&2
    exit 1
  }
  {
    echo "faf-re native Linux client $version: third-party code in this download"
    echo
    echo "bin/faf links these libraries statically; bin/libdxcompiler.so and bin/libdxil.so are Microsoft's"
    echo "DirectX Shader Compiler for Linux, unchanged except for the added libfaf-glibc-compat.so dependency"
    echo "and weak glibc version requirements (see the faf-re repository, tools/linux-build/package.sh)."
    echo "Each library's licence is in this folder:"
    echo
  } > "$notices"
  licence SDL3.txt "SDL 3 (zlib licence)" "$deps/SDL3/LICENSE.txt"
  licence FAudio.txt "FAudio (zlib licence)" "$deps/FAudio/LICENSE"
  licence FreeType.txt "FreeType (FreeType Project Licence)" "$deps/freetype/docs/FTL.TXT" "$deps/freetype/LICENSE.TXT"
  licence msdfgen.txt "msdfgen (MIT)" "$deps/msdfgen/LICENSE.txt" "$deps/msdfgen/LICENSE"
  licence DirectXTex.txt "DirectXTex (MIT)" "$deps/DirectXTex/LICENSE"
  licence DirectXMath.txt "DirectXMath (MIT)" "$deps/DirectXMath/LICENSE"
  licence DirectX-Headers.txt "DirectX-Headers (MIT)" "$deps/DirectX-Headers/LICENSE"
  licence DXC.txt "DirectX Shader Compiler (University of Illinois/NCSA, LLVM)" \
    "$deps/dxc-linux/LICENSE.TXT" "$deps/dxc-linux/LICENSE.txt" "$deps/dxc-linux/LICENSE-LLVM.txt"
  if [ -f "$deps/dxc-linux/LICENSE-MIT.txt" ]; then
    licence DXC-MIT.txt "DirectX Shader Compiler, MIT-licensed parts" "$deps/dxc-linux/LICENSE-MIT.txt"
  fi
  if [ -f "$deps/dxc-linux/LICENSE-MS.txt" ]; then
    licence DXC-MS.txt "DirectX Shader Compiler, Microsoft's terms" "$deps/dxc-linux/LICENSE-MS.txt"
  fi
  licence Vulkan-Headers.txt "Vulkan headers (Apache-2.0 or MIT)" "$deps/vulkan/LICENSE.md" "$deps/vulkan/LICENSE.txt"
  licence Vulkan-Headers-MIT.txt "Vulkan headers, used under MIT" "$deps/vulkan/LICENSES/MIT.txt"
  licence Boost.txt "Boost 1.34.1 (Boost Software Licence 1.0)" "$deps/boost_1_34_1/LICENSE_1_0.txt"
  licence WildMagic3License.pdf "Wild Magic 3.8 (Geometric Tools, Wild Magic 3 licence)" "$deps/WildMagic3p8/License/WildMagic3License.pdf"
  head -n 30 "$deps/zlib-1.2.3/zlib.h" > /tmp/zlib-notice.txt
  licence zlib.txt "zlib 1.2.3 (zlib licence)" /tmp/zlib-notice.txt
  licence zstd.txt "Zstandard (BSD-3-Clause)" "$deps/zstd/LICENSE"
  licence Monocypher.txt "Monocypher (BSD-2-Clause or CC0)" "$deps/monocypher/LICENCE.md" "$deps/monocypher/LICENSE"
  licence LuaPlus.txt "Lua 5.0 / LuaPlus headers (MIT)" /src/src/lua/luaplus/LICENSE
  licence libc++.txt "libc++ and libc++abi 18 (Apache-2.0 with LLVM exception)" \
    /usr/share/doc/libc++-18-dev/copyright /usr/share/doc/libc++1-18/copyright
  {
    echo
    echo "FreeType: portions of this software are copyright (c) The FreeType Project (www.freetype.org)."
    echo "All rights reserved."
  } >> "$notices"

  sed "s/@VERSION@/$version/" /src/tools/linux-build/package/README.txt > "$stage/README.txt"
  echo "$version" > "$stage/VERSION"

  # Modes as a Linux checkout would have them (a Windows bind mount shows everything as 0777).
  find "$stage" -type d -exec chmod 0755 {} +
  find "$stage" -type f -exec chmod 0644 {} +
  chmod 0755 "$stage/faf-launch" "$stage/bin/faf" "$stage"/bin/*.so
  # Reproducible-ish: fixed owner and order, files dated by the commit.
  mtime="${SOURCE_DATE_EPOCH:-$(date +%s)}"
  tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$mtime" -C "$stage_root" -czf "/pkg-out/$name.tar.gz" "$name"
  (cd /pkg-out && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
  echo "--- $name.tar.gz"
  tar -tzvf "/pkg-out/$name.tar.gz" | awk '{printf "%10s  %s\n", $3, $6}'
  exit 0
fi

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
version="${1:-${FAF_LINUX_VERSION:-}}"
if [ -z "$version" ]; then
  commit="$(git -C "$root" rev-parse --short=9 HEAD 2>/dev/null || echo unknown)"
  version="$(date -u +%Y%m%d)-g$commit"
fi
case "$version" in
  *[!A-Za-z0-9._+-]*) echo "package: version '$version' has characters a file name should not" >&2; exit 2 ;;
esac
epoch="$(git -C "$root" log -1 --format=%ct 2>/dev/null || date +%s)"
out="${FAF_LINUX_OUT:-$root/build/linux-x64-release}"
pkg="${FAF_LINUX_PACKAGE_DIR:-$root/build/package}"
image="faf-linux-build:22.04-release"
if ! docker image inspect "$image" >/dev/null 2>&1; then
  docker build -t "$image" -f "$here/Dockerfile.release" "$here"
fi
mkdir -p "$pkg"
# MSYS (Git Bash) would rewrite the container paths below into Windows ones.
MSYS_NO_PATHCONV=1 docker run --rm \
  -e FAF_LINUX_PACKAGE_INNER=1 -e SOURCE_DATE_EPOCH="$epoch" \
  -v "${FAF_LINUX_VOLUME:-faf-linux-release}:/work:ro" \
  -v "$root:/src:ro" \
  -v "$out:/bin-in:ro" \
  -v "$pkg:/pkg-out" \
  "$image" bash /src/tools/linux-build/package.sh "$version"
echo "Package: $pkg/$name_prefix-$version.tar.gz"
