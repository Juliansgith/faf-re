#!/usr/bin/env bash
# GPU check for the native Linux client: run it from a Linux desktop (a live USB is enough) on the
# machine to test. It reports the system and the Vulkan device, plays a real FAF game (replay
# 27796180, 8 players on SCMP_009) flat out and checks every hashed beat against the Windows build,
# then plays it at normal speed for a while with the render profiler on, and packs the logs into
# one archive to send back.
#
#   gpu-check.sh --game <dir with faf, libdxcompiler.so, libdxil.so>
#                --fa <Forged Alliance install>  --faf <FAF data folder: bin/init_faf.lua, gamedata/>
#                [--out <dir>] [--perf-seconds N] [--replay <file.scfareplay>]
#
# From a Windows install, mounted read-only on the live system: --fa is the Steam or GOG
# "Supreme Commander Forged Alliance" folder, --faf is FAF's C:\ProgramData\FAForever.
# Needs: bash, python3, curl (only to download the replay), zstd or python's zlib, a display.
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
game="" fa="" faf="" out="$PWD/faf-gpu-check-$(date +%Y%m%d-%H%M%S)" perf_seconds=150 replay=""
while [ $# -gt 0 ]; do
  case "$1" in
    --game) game="$2"; shift 2 ;;
    --fa) fa="$2"; shift 2 ;;
    --faf) faf="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --perf-seconds) perf_seconds="$2"; shift 2 ;;
    --replay) replay="$2"; shift 2 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
fail() { echo "gpu-check: $*" >&2; exit 2; }
[ -x "$game/faf" ] || fail "--game must be the folder with the faf binary (got '$game')"
[ -f "$game/libdxcompiler.so" ] || fail "libdxcompiler.so is missing next to faf"
[ -d "$fa/gamedata" ] || fail "--fa must be the Forged Alliance install (no gamedata/ in '$fa')"
[ -f "$faf/bin/init_faf.lua" ] && [ -d "$faf/gamedata" ] || fail "--faf must hold bin/init_faf.lua and gamedata/ (got '$faf')"
mkdir -p "$out" || fail "can't create $out"
out="$(cd "$out" && pwd)"
summary="$out/summary.txt"
say() { echo "$*" | tee -a "$summary"; }

# --- The system ---------------------------------------------------------------------------------
{
  echo "== system"; uname -a
  grep -E '^(PRETTY_NAME)=' /etc/os-release 2>/dev/null
  ldd --version 2>/dev/null | head -1
  echo "== GPUs"; lspci 2>/dev/null | grep -iE 'vga|3d|display' || echo "(lspci not available)"
  echo "== Vulkan ICDs"; ls /usr/share/vulkan/icd.d /etc/vulkan/icd.d 2>/dev/null
  command -v vulkaninfo >/dev/null && vulkaninfo --summary 2>/dev/null | sed -n '1,60p'
  echo "== session"; echo "XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-} DISPLAY=${DISPLAY:-}"
} > "$out/system.txt" 2>&1
say "system: $(grep PRETTY_NAME "$out/system.txt" | cut -d= -f2 | tr -d '"'), $(ldd --version 2>/dev/null | head -1 | grep -o '[0-9.]*$' | sed 's/^/glibc /')"

# --- The game's data folder, as FAF lays it out ---------------------------------------------------
data="$out/data"
mkdir -p "$data/bin" "$data/vault"
cp "$faf/bin/init_faf.lua" "$data/bin/"
ln -sfn "$(cd "$faf/gamedata" && pwd)" "$data/gamedata"
{
  echo "fa_path = \"$(cd "$fa" && pwd)\""
  echo "custom_vault_path = \"$data/vault\""
  [ -f "$faf/fa_path.lua" ] && grep -vE '^(fa_path|custom_vault_path) *=' "$faf/fa_path.lua" | tr -d '\r'
} > "$data/fa_path.lua"
export XDG_DATA_HOME="$out/xdg-data" XDG_CACHE_HOME="$out/xdg-cache" # keeps the test's prefs and caches apart

# --- The replay ---------------------------------------------------------------------------------
if [ -z "$replay" ]; then
  replay="$out/27796180.scfareplay"
  if [ ! -f "$replay" ]; then
    echo "downloading FAF replay 27796180 (public, replay.faforever.com)"
    curl -fsSL -o "$out/27796180.fafreplay" "https://replay.faforever.com/27796180" || fail "replay download failed"
    python3 - "$out/27796180.fafreplay" "$replay" <<'PY' || fail "could not decode the .fafreplay"
import base64, json, shutil, subprocess, sys, zlib
raw = open(sys.argv[1], "rb").read()
newline = raw.index(b"\n")
header, body = json.loads(raw[:newline]), raw[newline + 1:]
if header.get("compression") == "zstd" or body[:4] == b"\x28\xb5\x2f\xfd":
    if shutil.which("zstd") is None:
        sys.exit("the replay is zstd-compressed: install zstd")
    data = subprocess.run(["zstd", "-dc"], input=body, capture_output=True, check=True).stdout
else:
    blob = base64.b64decode(body)
    data = zlib.decompress(blob[4:])
open(sys.argv[2], "wb").write(data)
PY
  fi
fi
# The Windows build's checksums for replay 27796180, whether downloaded or given with --replay.
reference=""
[ "$(basename "$replay")" = "27796180.scfareplay" ] && reference="$here/27796180.checksums"

# --- 1. Determinism: the whole game flat out, every hashed beat against the Windows build ----------
say "determinism: playing $(basename "$replay") flat out (several minutes)"
log1="$out/determinism.log"
ck="$out/checksums.txt"
"$game/faf" /init "$data/bin/init_faf.lua" /windowed 1600 900 /nomovie /replay "$replay" \
  /con wld_RunWithTheWind=1 /checksumlog "$ck" /log "$log1" > "$out/determinism.out" 2>&1 &
pid=$!
last_beat=$(grep -v '^#' "${reference:-/dev/null}" 2>/dev/null | tail -1 | cut -d' ' -f1)
# Until the reference's last hashed beat is logged, or the log stops growing for a minute (the
# replay is over; the score screen comes up a little before the last beats are written).
lines=0 still=0
for _ in $(seq 1 1800); do # at most 30 minutes
  kill -0 "$pid" 2>/dev/null || break
  if [ -n "$last_beat" ] && grep -q "^$last_beat " "$ck" 2>/dev/null; then break; fi
  now=$(wc -l < "$ck" 2>/dev/null || echo 0)
  if [ "$now" -gt 0 ] && [ "$now" -eq "$lines" ]; then still=$((still + 1)); else still=0; fi
  lines=$now
  [ "$still" -ge 60 ] && break
  sleep 1
done
sleep 2; kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
mismatch_lines=$(grep -c "mismatched" "$log1" 2>/dev/null || true)
device=$(grep -m1 "Vulkan: device created on" "$log1" | sed 's/.*device created on //')
say "vulkan device: ${device:-NONE (see determinism.log)}"
if [ -n "$reference" ]; then
  python3 - "$reference" "$ck" "$summary" <<'PY'
import sys
ref = {}
for line in open(sys.argv[1]):
    if line.strip() and not line.startswith("#"):
        beat, digest = line.split()
        ref[int(beat)] = digest
got = {}
for line in open(sys.argv[2]):
    parts = line.split()
    if len(parts) == 2:
        got[int(parts[0])] = parts[1]
common = [b for b in sorted(ref) if b in got]
bad = [b for b in common if got[b] != ref[b]]
verdict = "PASS" if common and not bad and len(common) == len(ref) else ("FAIL" if bad else "INCOMPLETE")
line = "determinism: %s - %d of %d hashed beats compared with the Windows build, %d differ%s" % (
    verdict, len(common), len(ref), len(bad), (" (first at beat %d)" % bad[0]) if bad else "")
print(line)
open(sys.argv[3], "a").write(line + "\n")
PY
fi
say "recorded-checksum mismatch lines in the game log: $mismatch_lines"

# --- 2. Performance: normal speed, render profiler on ---------------------------------------------
say "performance: playing at normal speed for ${perf_seconds}s with FAF_GPU_PROFILE"
log2="$out/performance.log"
FAF_GPU_PROFILE=300 "$game/faf" /init "$data/bin/init_faf.lua" /windowed 1600 900 /nomovie /replay "$replay" \
  /log "$log2" > "$out/performance.out" 2>&1 &
pid=$!
sleep "$perf_seconds"; kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
python3 - "$log2" "$summary" <<'PY'
import re, sys
rows = re.findall(r"Render profile over (\d+) frames: ([\d.]+) ms/frame wall, GPU ([\d.]+) ms/frame", open(sys.argv[1], errors="replace").read())
if rows:
    wall = [float(r[1]) for r in rows[1:] or rows]  # the first window includes loading
    gpu = [float(r[2]) for r in rows[1:] or rows]
    line = "performance: %d profile windows, %.2f ms/frame wall (%.0f fps), GPU %.2f ms/frame" % (
        len(rows), sum(wall) / len(wall), 1000.0 / (sum(wall) / len(wall)), sum(gpu) / len(gpu))
else:
    line = "performance: no render profile lines (did the game reach the replay? see performance.log)"
print(line)
open(sys.argv[2], "a").write(line + "\n")
PY

# --- The archive -----------------------------------------------------------------------------------
rm -f "$data/gamedata"
tar -czf "$out.tar.gz" -C "$(dirname "$out")" --exclude='*.scfareplay' --exclude='*.fafreplay' \
  --exclude='xdg-cache' "$(basename "$out")"
say "logs: $out.tar.gz"
