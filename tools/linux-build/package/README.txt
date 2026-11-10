faf-re native Linux client @VERSION@ (x86-64)
==============================================

A native Linux build of the faf-re engine, the reconstruction of the Supreme Commander: Forged Alliance
engine, for FAF (Forged Alliance Forever). Window, input and sound through SDL 3, Vulkan rendering, the
same simulation as on Windows. The engine: https://github.com/Draiget/faf-re. This Linux build and
its guide: https://github.com/Juliansgith/faf-re, docs/LINUX_CLIENT.md.

What it needs
-------------

- A 64-bit x86 Linux with glibc 2.34 or newer: Ubuntu 22.04, Debian 12, Linux Mint 21, RHEL, Rocky or
  AlmaLinux 9, Fedora 35, or anything later (Arch, openSUSE Tumbleweed). Not Ubuntu 20.04 or Debian 11.
  Check yours with `ldd --version`.
- A Vulkan 1.3 driver (Mesa or the GPU vendor's) with BC texture compression, and libvulkan.so.1.
- X11 or Wayland, and PipeWire, PulseAudio or ALSA for sound.
- Forged Alliance itself (the Steam or GOG copy; a Steam install made for Proton is fine).
- FAF's game data: FAF's client (https://faforever.com) downloads it into its data folder, ~/.faforever
  on Linux, the first time it starts a game or a replay. The game uses that folder's bin/init_faf.lua
  and gamedata/*.nx2.

What works: single player, skirmish against the AI, replays, movies, and online lockstep games with
Windows players in the same game (the game side; starting one from FAF's client on Linux has not
been tried yet, see "Online play" in the guide).

So far this build has only run on software Vulkan (Mesa lavapipe), never on a GPU driver. If it
misbehaves on yours, the guide's "Checking a GPU" has a script that tests it and packs the logs.

Starting it
-----------

    ./faf-launch                                   finds everything itself, or says what is missing
    ./faf-launch /windowed 1920 1080               game switches go after the launcher's options
    ./faf-launch /replay ~/Downloads/game.scfareplay
    ./faf-launch --show                            prints what it found and stops

The launcher's options (each also as an environment variable):

    --fa DIR         the Forged Alliance install           FAF_FA_PATH
                     default: fa_path in FAF's fa_path.lua, else Steam's library folders
    --faf-data DIR   FAF's data folder (bin/init_faf.lua, gamedata/)   FAF_DATA_DIR
                     default: the FAF client's data folder (~/.faforever)
    --vault DIR      maps and mods (optional)              FAF_VAULT_PATH
                     default: custom_vault_path in FAF's fa_path.lua

It writes a fa_path.lua naming those folders next to a copy of FAF's init file in
~/.local/share/faf-re/init/ (FAF's own files are not changed) and starts bin/faf with it.

Where the game keeps its files
------------------------------

    Game.prefs, font cache       ~/.local/share/Gas Powered Games/Supreme Commander Forged Alliance/
    Saved games, replays         ~/Documents/My Games/Gas Powered Games/Supreme Commander Forged Alliance/
    Compiled shaders             ~/.cache/FAForever/ShaderCache/

What is in this folder
----------------------

    faf-launch                       the launcher
    bin/faf                          the game
    bin/fafengine/                   the engine's own Lua (options, replay timeline); keep it next to faf
    bin/libdxcompiler.so, libdxil.so Microsoft's DirectX Shader Compiler, which compiles the shaders
    bin/libfaf-glibc-compat.so       the few newer glibc functions those two call, for older systems
    licenses/                        the licences of the third-party code (THIRD_PARTY_NOTICES.txt)

Keep the bin/ files together. faf-re is not affiliated with or endorsed by the game's original
developers or publishers; see LEGAL.md in the repository.
