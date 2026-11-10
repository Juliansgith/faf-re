#pragma once

// FAF addition, not in the shipped binary.
//
// The platform half of the Vulkan backend: the instance extension that makes
// window surfaces and the surface for one window. Only this unit knows the
// window system (VK_KHR_win32_surface); another platform replaces it alone.
// The Linux client's half (same file, !_WIN32) goes through SDL 3: the window
// is the client's SDL_Window*, and SDL names the X11 or Wayland extensions.

#include "VulkanLoader.h"

#if !defined(_WIN32)
#include <cstdint>
#include <vector>
#endif

namespace gpg::gal::vulkan
{
  /** The platform's surface extension name. */
  [[nodiscard]] const char* PlatformSurfaceExtension() noexcept;

  /** Creates a surface for `window` (an HWND on Windows, an SDL_Window* on Linux); VK_NULL_HANDLE on failure, `result` says why. */
  [[nodiscard]] VkSurfaceKHR CreateWindowSurface(VkInstance instance, void* window, VkResult& result);

#if !defined(_WIN32)
  /** VK_KHR_surface and every window-system extension SDL_Vulkan_GetInstanceExtensions asks for. */
  [[nodiscard]] std::vector<const char*> PlatformInstanceExtensions();

  /** The drawable size of `window` in pixels (SDL_GetWindowSizeInPixels); false when unknown. */
  [[nodiscard]] bool WindowPixelSize(void* window, std::uint32_t& width, std::uint32_t& height) noexcept;

  /**
   * Puts `window` in or out of full screen. `Head::mWindowed` is the binary's full-screen flag (the
   * Direct3D backends make an exclusive full-screen swap chain of the head's size from it). On Linux
   * the window covers the desktop at the desktop's own mode instead, and the head's back buffer is
   * scaled into it at present (the scaled swap chain), so no display mode is changed.
   */
  void ApplyWindowFullscreen(void* window, bool fullscreen) noexcept;
#endif
} // namespace gpg::gal::vulkan
