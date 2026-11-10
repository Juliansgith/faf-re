#pragma once

// Engine addition (faf-neural-ai): what platform/posix/WinsockShim.cpp (Winsock's event model on
// poll(2)) and platform/posix/Win32Shim.cpp (the events) tell each other. Not a public header.

#include <Windows.h>

/// Win32Shim.cpp: whether the event `handle` is signalled now.
bool FafShimEventSignalled(HANDLE handle) noexcept;
/// Win32Shim.cpp: marks the event as selected for sockets (WSAEventSelect), so that taking or resetting
/// it, and closing it, calls the two functions below.
void FafShimWatchEvent(HANDLE handle) noexcept;

/// WinsockShim.cpp: a watched event was reset or taken by a wait; the watcher polls its sockets again.
void FafWsaEventCleared() noexcept;
/// WinsockShim.cpp: a watched event is being closed; its sockets are no longer selected.
void FafWsaForgetEvent(HANDLE handle) noexcept;
