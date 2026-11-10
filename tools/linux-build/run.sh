#!/usr/bin/env bash
# Engine addition (faf-neural-ai): runs a Linux program from tools/linux-build/build.sh in the pinned
# container, with the game data mounted, on a Windows desktop (Docker Desktop) or any Docker host.
#
#   tools/linux-build/run.sh <program> [args...]
#
# <program> is one of the built programs (headless, server, ...), or an absolute path in the container
# (e.g. /usr/bin/python3, to run a trainer next to the game).
#
# Mounts (read-only) and the container paths they get:
#   FAF_LINUX_FA     retail Forged Alliance install   -> /fa      (default: fa_path from FAF's fa_path.lua)
#   FAF_LINUX_FAF    the FAF client's data folder     -> /faf     (default: C:/ProgramData/FAForever)
#   FAF_LINUX_VAULT  maps / mods vault                -> /vault   (default: custom_vault_path)
#   FAF_LINUX_RUN    working folder, read-write       -> /run-dir (default: build/linux-run)
# /fafinit/bin/init_faf.lua is FAF's own init file next to a fa_path.lua naming those paths, so a
# program takes `/init /fafinit/bin/init_faf.lua` exactly as the Windows build takes FAF's.
# Extra docker options (e.g. --network host for the agent bridge): FAF_LINUX_DOCKER_ARGS.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
program="$1"
shift

faf="${FAF_LINUX_FAF:-C:/ProgramData/FAForever}"
read_path_var() { sed -n "s/^$1 *= *\"\\(.*\\)\"/\\1/p" "$faf/fa_path.lua" | head -1; }
fa="${FAF_LINUX_FA:-$(read_path_var fa_path)}"
vault="${FAF_LINUX_VAULT:-$(read_path_var custom_vault_path)}"
run="${FAF_LINUX_RUN:-$root/build/linux-run}"
mkdir -p "$run/fafinit/bin"

cp "$faf/bin/init_faf.lua" "$run/fafinit/bin/init_faf.lua"
{
  echo 'fa_path = "/fa"'
  echo 'custom_vault_path = "/vault"'
  grep -vE '^(fa_path|custom_vault_path) *=' "$faf/fa_path.lua" || true
} > "$run/fafinit/fa_path.lua"

# MSYS (Git Bash) would rewrite the container paths below into Windows ones.
MSYS_NO_PATHCONV=1 docker run --rm ${FAF_LINUX_DOCKER_ARGS:-} \
  -v "$fa:/fa:ro" -v "$vault:/vault:ro" \
  -v "$root/build/linux-x64:/bin-linux:ro" \
  -v "$run:/run-dir" \
  -v "$run/fafinit:/fafinit" -v "$faf/gamedata:/fafinit/gamedata:ro" \
  -w /run-dir \
  faf-linux-build:24.04 \
  bash -c 'case "$0" in /*) exec "$0" "$@" ;; *) exec "/bin-linux/$0" "$@" ;; esac' "$program" "$@"
