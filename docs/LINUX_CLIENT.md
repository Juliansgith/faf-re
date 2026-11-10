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

Not there yet:

- **Online play.** The lockstep network transport (`moho/net`, Windows sockets) and the in-engine
  peer links (`src/netlink`, ICE) are left out of the Linux build, so FAF multiplayer and LAN
  games are Windows-only for now. `/connectserver` (the server-authoritative mode) is in.
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
folder, `libdxcompiler.so` and `libdxil.so`. Keep the three together.

The first configure downloads the client's dependencies (SDL 3, FAudio, DirectX-Headers,
DirectXMath, DXC for Linux), each pinned by SHA256 in `cmake/FafDependencies.cmake`.

## Running

What the game needs at run time:

- glibc 2.38 or newer (Ubuntu 24.04, Fedora 39, Debian 13 or later). libc++ is linked in.
- A Vulkan 1.3 driver (`libvulkan.so.1` and Mesa or the GPU vendor's driver), with BC texture
  compression and dynamic rendering.
- X11 or Wayland, and PipeWire, PulseAudio or ALSA for sound. SDL loads them at run time.

The game data is the same as on Windows: the retail Forged Alliance install (Steam or GOG) and
FAF's own files from its client's data folder (`C:\ProgramData\FAForever` on Windows): `bin/init_faf.lua`,
`fa_path.lua` and `gamedata/`. Put them in one folder:

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
tools/linux-build/gpu-check/gpu-check.sh --game <folder with faf>     --fa <Forged Alliance install> --faf <FAF data folder>
```

The Forged Alliance install and FAF's `C:\ProgramData\FAForever` can be read straight from a
mounted Windows drive.
