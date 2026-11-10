# Engine addition (faf-neural-ai): the native Linux client's source list (cmake/FafLinux.cmake,
# FAF_LINUX_CLIENT). FAF_LINUX_CLIENT_EXCLUDE: client translation units the Linux client does not
# compile (regexes on the paths in src/sources.cmake); FAF_LINUX_CLIENT_SOURCES: what it compiles
# instead, here in src/platform/sdl.

set(FAF_LINUX_CLIENT_EXCLUDE
  # Direct3D 9, 10, 11 and 12 and Effects 11: Vulkan is the Linux renderer.
  "^gpg/gal/backends/(d3d9|d3d10|d3d11|d3d12)/"
  "/FX11/"
  # The in-engine peer links (src/netlink's ICE stack, libjuice and libdatachannel, is not in the
  # Linux build): platform/sdl/NetLinuxStubs.cpp has no links. The lockstep transport is in faf_sim.
  "^moho/net/NetlinkPeers\\.cpp$"
  # Windows crash plumbing (dbghelp, minidumps, the crash dialog); platform/posix has the rest.
  "^gpg/core/diagnostics/(CrashHooks|CrashReport|ProbeWatch|CrashReportDialog|FrameSampler)\\.cpp$"
  "^moho/misc/WinPlatform\\.cpp$"
  "/libs/thread/src/tss_"
  # The wx tool windows: script debugger, log window, emitter and reference editors.
  "^moho/misc/(ScrDebugWindow|ScrFileCtrl|ScrSourceCtrl|ScrWatchCtrl|ScrGotoDialog|ScrPauseEvent|TreeData)\\.cpp$"
  "^moho/app/(WEmitterWx|WRefEditDialog|WWinLogWindow)\\.cpp$"
  # DXGI factory and swap chain (COM), used only by the Direct3D backends (DxgiFormats.cpp stays).
  "^gpg/gal/backends/dxgi/(DxgiFactory|DxgiSwapChain)\\.cpp$"
  "^gpg/gal/DxgiAdapterInfo\\.cpp$"
  # Compiled with DirectXTex in faf_directxtex_linux (cmake/deps/directxtex-linux): DirectXTex's Windows
  # types come from DirectX-Headers' WSL stubs, which clash with the POSIX shim.
  "^gpg/gal/backends/common/ImageCodecDirectXTex\\.cpp$"
  # Already in faf_sim on Linux (cmake/FafLinux.cmake), which the client links.
  "^gpg/gal/MatrixPortable\\.cpp$"
  # The SFD movie decoder needs FFmpeg, which the Linux build does not have yet: platform/sdl's
  # FfmpegSfdDecoderStub.cpp fails every movie instead.
  "^moho/movie/sfd/FfmpegSfdDecoder\\.cpp$")

set(FAF_LINUX_CLIENT_SOURCES
  # The effect-source override layer lives with Direct3D 11 but has no Direct3D in it: Vulkan runs every
  # effect through it (docs/RENDERER_BACKENDS.md, "Effects"), as Direct3D 12 does.
  gpg/gal/backends/d3d11/EffectSourceD3D11.cpp
  # Vertex input tables and the mesh vertex formatters, shared by Direct3D 10/11/12 and Vulkan; no
  # Direct3D API in them.
  gpg/gal/backends/d3d11/D3D11VertexLayouts.cpp
  gpg/gal/backends/d3d10/HardwareVertexFormatterD3D10.cpp
  gpg/gal/backends/d3d10/Float16HardwareVertexFormatterD3D10.cpp
  # The frame-stall sampler's mark, a no-op without DbgHelp.
  gpg/core/diagnostics/posix/FrameSamplerPosix.cpp
  # Fonts without GDI: face name to font file, FreeType metrics and glyphs in GDI's terms.
  moho/render/font/FontFaceLinux.cpp
  # The application shell on SDL 3: wx-lite (the wx 2.4 API the engine's windows and input use, with
  # wxWindows/MSW's window procedure), the event pump that turns SDL's events into those windows'
  # messages, and the Win32 window / cursor / input calls on the SDL window.
  platform/sdl/WxLite.cpp
  platform/sdl/WxLiteWindows.cpp
  platform/sdl/SdlApp.cpp
  platform/sdl/SdlWin32.cpp
  # Movies fail without FFmpeg (see the exclusion above).
  platform/sdl/FfmpegSfdDecoderStub.cpp
  # What the client calls of the excluded online transport and peer links, and of the wx tool windows
  # (the log window's target, the entity dialog command).
  platform/sdl/NetLinuxStubs.cpp
  platform/sdl/ToolWindowsLinux.cpp
  # The gal resource bases' constructors and the Direct3D 12 settings, from the excluded backends.
  platform/sdl/GalLinux.cpp)
