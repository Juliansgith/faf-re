# Native Linux client

The game runs natively on Linux x86-64. The window, input and audio go through SDL 3, the
renderer is Vulkan, sound is XACT through FAudio, and fonts come from FreeType. The simulation is
the same code as on Windows and gives the same results, bit for bit (see [Determinism](#determinism)).

## What works

- Single player: the front end (profiles, options, the FAF game lobby), skirmish against the AI,
  save and load, the score screen.
- Replays: the Replay menu, `/replay <file>.scfareplay`, the replay timeline, speed up and down.
- Input: mouse (clicks, drag-select, wheel zoom), keyboard (hotkeys, arrow-key pan, chat and text
  fields), windowed and full screen (`/fullscreen` or the video options: the window covers the
  desktop at its own mode, borderless; no display mode is changed).
- Sound and music.
- Preferences, saved games and replays are written and read back (see [Files](#files)).
- Online play over the lockstep transport, with Windows players in the same game (see
  [Online play](#online-play)). `/connectserver` (the server-authoritative mode) is in too.

Not there yet:

- **The in-engine peer links** (`/nativenet`, `src/netlink`: ICE on libjuice and libdatachannel)
  are left out of the Linux build; FAF games go through FAF's ICE adapter, as the shipped game's do.
- **Movies.** They need FFmpeg, which the Linux build does not have yet. Every movie is skipped,
  and the main menu's background stays black.
- The map editor and the Windows tool windows (log window, Lua debugger, emitter editor).

The client has been tested in a container (Xvfb with Mesa's software Vulkan driver, lavapipe) and
on a Wayland and X11 desktop (WSLg on Windows 11, software Vulkan), not yet with a GPU driver.
Reports are welcome: [the GPU check](#checking-a-gpu) below does the testing and packs the logs.

## Building

From any machine with Docker (Linux, or Windows with Docker Desktop), from the repository root:

```sh
FAF_LINUX_CMAKE_ARGS="-DFAF_ULTRA=ON -DFAF_LINUX_CLIENT=ON" tools/linux-build/build.sh faf_client
```

`tools/linux-build/Dockerfile` is the pinned build machine (Ubuntu 24.04, clang 18, libc++); the
simulation's Windows/Linux parity is checked with exactly that toolchain, so build with it. The
result is in `build/linux-x64/`: the game, `faf`, and the shader compiler it loads from its own
folder, `libdxcompiler.so` and `libdxil.so`. Keep the three together. That build needs glibc 2.38
or newer to run.

The first configure downloads the client's dependencies (SDL 3, FAudio, DirectX-Headers,
DirectXMath, DXC for Linux), each pinned by SHA256 in `cmake/FafDependencies.cmake`. The submodules
and boost come from the checkout (`cmake -DFAF_DEPENDENCIES_ONLY="WildMagic3p8;zlib-1.2.3;boost_1_34_1;vulkan"
-P cmake/FafDependencies.cmake` prepares them, as the workflows do).

### The release build

The download (below) is built for older distributions in a second image,
`tools/linux-build/Dockerfile.release`: Ubuntu 22.04 with LLVM's clang 18.1.8 and libc++ 18
(apt.llvm.org), libc++ again linked in, and the `linux-clang-x64-release-portable` preset (the
`release` pin set in `cmake/toolchains/linux-clang-pins.cmake`). Its programs need glibc 2.34. The
dedicated server, the parity workflows and the parity checks keep the 24.04 image.

```sh
FAF_LINUX_IMAGE=release FAF_LINUX_CMAKE_ARGS="-DFAF_ULTRA=ON -DFAF_LINUX_CLIENT=ON" \
  tools/linux-build/build.sh faf_client        # build/linux-x64-release/, volume faf-linux-release
tools/linux-build/package.sh [version]         # build/package/faf-linux-x86_64-<version>.tar.gz
tools/linux-build/check-package.sh build/package/faf-linux-x86_64-<version>.tar.gz
```

Microsoft's DXC libraries are built on glibc 2.38 and call five newer functions
(`__isoc23_strtol`, `__isoc23_strtoul`, `__isoc23_strtoll`, `arc4random`, `fmod@GLIBC_2.38`).
`package.sh` adds `libfaf-glibc-compat.so` (`tools/linux-build/glibc-compat/`) to their
dependencies and marks their GLIBC_2.36 and 2.38 requirements weak
(`tools/linux-build/elf_weak_versions.py`), so they load on glibc 2.34 and take those functions
from the compat library; on a newer glibc they keep using glibc's own. It fails when the game or a
library still needs more than glibc 2.34. `check-package.sh` loads everything (`ldd -r`) in plain
`ubuntu:22.04`, `debian:12` and `almalinux:9` containers.

The workflow `.github/workflows/linux-client.yml` does the same on GitHub: on every push to
`faf-neural-ai` that touches the engine and when started by hand it keeps the archive as an
artifact, and a `linux-v<version>` tag publishes it as that tag's release.

## Running

### What the game needs

- x86-64 Linux with glibc 2.34 or newer for the download: Ubuntu 22.04, Debian 12, Linux Mint 21,
  RHEL, Rocky and AlmaLinux 9, Fedora 35 or later (Ubuntu 20.04 and Debian 11 are too old). A build
  from the parity image needs glibc 2.38 (Ubuntu 24.04, Fedora 39, Debian 13 or later). libc++ is
  linked in; the DXC libraries need libstdc++ (GCC 11 or newer) and zlib, which every desktop has.
- A Vulkan 1.3 driver (`libvulkan.so.1` and Mesa or the GPU vendor's driver), with BC texture
  compression and dynamic rendering.
- X11 or Wayland, and PipeWire, PulseAudio or ALSA for sound. SDL loads them at run time.

The game data is the same as on Windows: the retail Forged Alliance install (Steam or GOG) and
FAF's own files from its client's data folder: `bin/init_faf.lua`, `fa_path.lua` and `gamedata/`.
FAF's client keeps that folder in `~/.faforever` on Linux (`C:\ProgramData\FAForever` on Windows;
its Settings can move it) and fills it the first time it starts a game or a replay.

### The download

`faf-linux-x86_64-<version>.tar.gz` (from a `linux-v*` release, or the Linux client workflow's
artifact) holds:

```
faf-launch            the launcher
README.txt
bin/faf               the game
bin/fafengine/        the engine's own Lua (Graphics API option, replay timeline), next to faf
bin/libdxcompiler.so  DXC
bin/libdxil.so
bin/libfaf-glibc-compat.so
licenses/             the third-party licences (THIRD_PARTY_NOTICES.txt)
```

Unpack it anywhere and start `./faf-launch`. It finds

- Forged Alliance: `--fa DIR` or `FAF_FA_PATH`, else `fa_path` in FAF's `fa_path.lua` (or the
  client's `installationPath`), else `steamapps/common/Supreme Commander Forged Alliance` in Steam's
  library folders (native, `~/.steam/steam` and Flatpak Steam);
- FAF's data folder: `--faf-data DIR` or `FAF_DATA_DIR`, else the client's data folder from
  `~/.faforever/client.prefs`, else `~/.faforever`;
- the vault (optional): `--vault DIR` or `FAF_VAULT_PATH`, else `custom_vault_path` in FAF's
  `fa_path.lua`,

and says what is missing and how to get it when one is not there. It then copies FAF's
`bin/init_faf.lua` into `~/.local/share/faf-re/init/bin/`, writes a `fa_path.lua` naming those
folders beside it, links `gamedata` to FAF's, and runs `bin/faf /init .../bin/init_faf.lua` with the
rest of its arguments. FAF's own files are left alone (its client rewrites `fa_path.lua` for each
game). `./faf-launch --show` prints what it found.

```sh
./faf-launch /windowed 1920 1080
./faf-launch /replay ~/Downloads/game.scfareplay
./faf-launch --fa "/games/Supreme Commander Forged Alliance" --faf-data ~/faf-data
```

### Without the launcher

Put the data in one folder:

```
faf-data/
  bin/init_faf.lua      FAF's init file, unchanged
  fa_path.lua           fa_path = "/path/to/Supreme Commander Forged Alliance"
                        custom_vault_path = "/path/to/vault"   (optional: maps and mods)
  gamedata/             FAF's *.nx2 files
```

then start the game:

```sh
./faf /init faf-data/bin/init_faf.lua
```

All the usual switches work, for example `/windowed 1920 1080`, `/replay <file>.scfareplay`,
`/map SCMP_009`, `/nomovie`, `/nosound`, `/log <file>`.

## Files

| What | Where |
|---|---|
| `Game.prefs`, MSDF font cache | `$XDG_DATA_HOME` (default `~/.local/share`)`/Gas Powered Games/Supreme Commander Forged Alliance/` |
| Saved games, replays | `~/Documents/My Games/Gas Powered Games/Supreme Commander Forged Alliance/` |
| Compiled shaders | `$XDG_CACHE_HOME` (default `~/.cache`)`/FAForever/ShaderCache/` |

## Online play

The lockstep transport is the Windows game's own code (`moho/net`: the UDP and TCP connectors, the
GPGNet link, the LAN discovery socket). On Linux its sockets are BSD sockets, and Winsock's event
model, which it runs on (`WSAEventSelect`, `WSAWaitForMultipleEvents`), comes from
`src/platform/posix/WinsockShim.cpp`: one thread `poll`s the selected sockets and sets their
events as Winsock would. The packets are the same bytes as the Windows game's, so Linux and Windows
players share a game (tested against our Windows build; its wire format is the shipped game's).

| How | State on Linux |
|---|---|
| FAF, through FAF's ICE adapter | The game side works: `/gpgnet host:port` to the adapter, lockstep UDP to the local ports it names. Tested against an emulated FAF client and adapter (below); not yet with FAF's Java adapter and the FAF client on a Linux desktop |
| Direct games (`/hostgame`, `/joingame`, the custom game lobby's direct connect) | Same transport, without GPGNet. Tested up to the lobby: a Windows client's `/joingame` joined a Linux `/hostgame` lobby and both reported the connection established; launching from that lobby was not tried |
| LAN discovery (the LAN game list) | The host's side tested: a Linux `/hostgame` lobby listens on UDP port 15000 and answered a discovery request with its game. The searching side (the broadcast) not tested across machines yet |
| In-engine peer links (`/nativenet`) | Not in the Linux build |

Tested on 10/11/2026: the Linux client in Docker Desktop (Xvfb, lavapipe) and the Windows x64
client (`main_x64.exe`, FAF_ULTRA) on the same PC, in FAF's autolobby on Seton's Clutch with four
AI armies added at launch (`tools/scenarios/mpemu-ai-armies`) and sandbox victory, each run with
`/checksumlog`. `tools/linux-build/mixed-lockstep.py` was the FAF client of both games (GPGNet) and
stood in for the ICE adapter between them: Docker's NAT rewrites the container's source address,
and a FAF lobby takes a peer only at the address GPGNet named, so the game traffic went through a
relay with one local socket per side, as the adapter's.

- Windows hosting, Linux joining: 4975 beats (8 min 17 s of game time), the two checksum logs
  identical on every beat (100 non-zero digests at the default checksum period of 50), no
  `Checksum for beat N mismatched` in either log.
- Linux hosting, Windows joining: 3613 beats (6 min 1 s), the checksum logs identical on every beat
  (73 non-zero digests), no mismatch line.

To repeat it, start `python tools/linux-build/mixed-lockstep.py --host win` (or `--host linux`) on
the Windows host; it prints the `/gpgnet` and autolobby switches of both games. The Linux game runs
through `tools/linux-build/run.sh`, its `/gpgnet` address being host.docker.internal's.

## Determinism

The simulation does not use the host's maths library: its transcendental functions are the
engine's own x87 code and `legacy/math/Libm.cpp`. A build step (`faf_sim_libm_gate`,
`tests/linux/libm_allowlist.txt`) fails the build if sim code calls libm outside a reviewed
list. Every thread gets the Windows x87 control word and MXCSR (`platform/posix/FpEnvPosix.cpp`).
Where the order of equal elements matters, the sim sorts with the recovered MSVC8 sort
(`legacy/algorithms/Sort.h`), and `tests/linux/OrderGate.py` reviews every sort and hash-container
walk in the sim ([SIM_ORDER_AUDIT.md](SIM_ORDER_AUDIT.md)).

Checked on 10/11/2026: ten FAF replays (2 to 12 players, about 175,000 beats in all) gave
identical checksums on every beat on Linux and Windows. Nine match every checksum the live players
recorded; the tenth, 28000018, mismatches from beat 2550 on Linux, on Windows and on the original
`ForgedAlliance.exe` alike (`tools/parity-data/expected/3835.json`). A full replay played in the
Linux client matched the Linux headless runner on every beat.

The release build (Ubuntu 22.04, clang 18.1.8 instead of 18.1.3, glibc 2.35 headers) was checked
the same way on 10/11/2026: `headless` from `tools/linux-build/Dockerfile.release` played the ten
replays with `/checksumlog`, and every one matched the Windows build's log on every beat it logged
(174,500 beats, 3,495 of them with a checksum; 28000018 again matches Windows, not the recorded
game). The packaged client played `a1` in a plain `ubuntu:22.04` container (Xvfb, Mesa's lavapipe)
and the release `headless` played it in `debian:12` and `almalinux:9`, all matching Windows on every
beat. The host's maths library still plays no part: `fmod` is the only libm function whose symbol
version differs between the two builds, and it is exact.

The engine is x86-64 only: the x87 maths is assembly (`legacy/math/X87Math64.S`), so an ARM build
would need it emulated first.

## Checking a GPU

`tools/linux-build/gpu-check/gpu-check.sh` tests a machine in one go, from any Linux desktop (a live
USB is enough; for an NVIDIA card pick a system that ships NVIDIA's driver, such as Pop!_OS's NVIDIA
image). It reports the system and the Vulkan device, plays FAF replay 27796180 (8 players, downloaded
from replay.faforever.com) flat out and compares every hashed beat with the Windows build's
checksums (`27796180.checksums`), plays it at normal speed with the render profiler on, and packs
the logs into one archive:

```sh
tools/linux-build/gpu-check/gpu-check.sh --game <folder with faf> \
    --fa <Forged Alliance install> --faf <FAF data folder>
```

The Forged Alliance install and FAF's `C:\ProgramData\FAForever` can be read straight from a
mounted Windows drive.
