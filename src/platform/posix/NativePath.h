#pragma once

// Engine addition (faf-neural-ai): a file name from the engine's Windows-style paths ('\fa\movies\uef_load.sfd',
// the VFS's lower-cased spelling) as POSIX open() needs it: forward slashes, and each component spelled as on
// disk, found case-insensitively as NTFS would (platform/posix/Win32Shim.cpp, plan WPL.15). For code that
// hands a path to a library instead of the shimmed Win32 file calls (the SFD movie decoder's FFmpeg).
// Linux build only.

#if !defined(_WIN32)

#include <string>

namespace faf::posix
{
  [[nodiscard]] std::string NativePath(const char* path);
} // namespace faf::posix

#endif // !defined(_WIN32)
