# Native Linux client

The game runs natively on Linux x86-64. The window, input and audio go through SDL 3, the
renderer is Vulkan, sound is XACT through FAudio, and fonts come from FreeType. The simulation is
the same code as on Windows and gives the same results, bit for bit (see [Determinism](#determinism)).

## What works

- Single player: the front end (profiles, options, the FAF game lobby), skirmish against the AI,
  save and load, the score screen.
- Replays: the Replay menu, `/replay <file>.scfareplay`, the replay timeline, speed up and down.
- Input: mouse (clicks, drag-select, wheel zoom), keyboard (hotkeys, arrow-key pan, chat and text
  fields), windowed mode (fullscreen goes through SDL too but has not been tried yet).
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

The client has been tested in a container (Xvfb with Mesa's software Vulkan driver, lavapipe),
not yet on a desktop with a GPU or under Wayland. Reports are welcome.

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

The engine is x86-64 only: the x87 maths is assembly (`legacy/math/X87Math64.S`), so an ARM build
would need it emulated first.
