# Engine addition (faf-neural-ai): the native Linux client's source list (cmake/FafLinux.cmake,
# FAF_LINUX_CLIENT). FAF_LINUX_CLIENT_EXCLUDE: client translation units the Linux client does not
# compile (regexes on the paths in src/sources.cmake); FAF_LINUX_CLIENT_SOURCES: what it compiles
# instead, here in src/platform/sdl.

set(FAF_LINUX_CLIENT_EXCLUDE
  # Direct3D 9, 10, 11 and 12 and Effects 11: Vulkan is the Linux renderer.
  "^gpg/gal/backends/(d3d9|d3d10|d3d11|d3d12)/"
  "/FX11/"
  # The lockstep UDP transport (Winsock event sockets): online play stays Windows-only.
  "^moho/net/(CNetDatagramSocketImpl|CNetTCPBuf|CNetTCPConnection|CNetTCPConnector|CNetTCPServerImpl|CNetUDPConnection|CNetUDPConnector|NetPeerTransport|INetDatagramSocket|INetTCPServer|INetTCPSocket)\\.cpp$"
  # Windows crash plumbing (dbghelp, minidumps, the crash dialog); platform/posix has the rest.
  "^gpg/core/diagnostics/(CrashHooks|CrashReport|ProbeWatch|CrashReportDialog)\\.cpp$"
  "^moho/misc/WinPlatform\\.cpp$"
  "/libs/thread/src/tss_"
  # The wx tool windows: script debugger, log window, emitter and reference editors.
  "^moho/misc/(ScrDebugWindow|ScrFileCtrl|ScrSourceCtrl|ScrWatchCtrl|ScrGotoDialog|ScrPauseEvent|TreeData)\\.cpp$"
  "^moho/app/(WEmitterWx|WRefEditDialog|WWinLogWindow)\\.cpp$")

set(FAF_LINUX_CLIENT_SOURCES)
