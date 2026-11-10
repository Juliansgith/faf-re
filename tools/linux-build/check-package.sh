#!/usr/bin/env bash
# Engine addition (faf-neural-ai): checks that the downloadable Linux client loads on the oldest
# distributions it is for, in their plain container images.
#
#   tools/linux-build/check-package.sh <faf-linux-x86_64-*.tar.gz> [image...]
#
# Default images: ubuntu:22.04 (glibc 2.35), debian:12 (2.36), almalinux:9 (2.34, as RHEL 9). In
# each, `ldd -r` must resolve every library and symbol of bin/faf and of the DXC libraries (a weak
# version glibc lacks is fine: libfaf-glibc-compat.so has those functions), and faf-launch must run.
set -euo pipefail

tarball="$1"
shift
images=("$@")
if [ ${#images[@]} -eq 0 ]; then
  images=(ubuntu:22.04 debian:12 almalinux:9)
fi
[ -f "$tarball" ] || { echo "check-package: no such file: $tarball" >&2; exit 2; }

tar_dir="$(cd "$(dirname "$tarball")" && pwd)"
tar_name="$(basename "$tarball")"

failed=0
for image in "${images[@]}"; do
  echo "--- $image"
  # MSYS (Git Bash) would rewrite the container paths into Windows ones.
  # Unpacked in the container, so the modes are the archive's whatever the host's file system keeps.
  if ! MSYS_NO_PATHCONV=1 docker run --rm -v "$tar_dir:/p:ro" "$image" sh -c '
      set -e
      mkdir -p /tmp/unpack && tar -xzf "/p/$1" -C /tmp/unpack
      dir=$(ls -d /tmp/unpack/faf-linux-x86_64-*)
      [ -x "$dir/bin/faf" ] || { echo "bin/faf missing or not executable"; exit 1; }
      ldd --version | head -n 1
      for f in "$dir/bin/faf" "$dir/bin/libdxil.so" "$dir/bin/libdxcompiler.so"; do
        problems=$(ldd -r "$f" 2>&1 | grep -v "weak version" | grep -E "not found|undefined symbol" || true)
        if [ -n "$problems" ]; then
          echo "$problems"
          exit 1
        fi
        echo "$(basename "$f"): every library and symbol resolves"
      done
      "$dir/faf-launch" --help > /dev/null
      echo "faf-launch: runs"' sh "$tar_name"; then
    echo "check-package: FAILED on $image" >&2
    failed=1
  fi
done
exit "$failed"
