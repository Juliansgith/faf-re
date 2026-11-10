// FAF addition, not in the shipped binary: see VulkanSurface.h.

#if defined(_WIN32)

#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include "VulkanSurface.h"

namespace gpg::gal::vulkan
{
  const char* PlatformSurfaceExtension() noexcept
  {
    return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
  }

  VkSurfaceKHR CreateWindowSurface(const VkInstance instance, void* const window, VkResult& result)
  {
    const auto create =
      reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(Vk().vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (create == nullptr) {
      result = VK_ERROR_EXTENSION_NOT_PRESENT;
      return VK_NULL_HANDLE;
    }
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = ::GetModuleHandleW(nullptr);
    info.hwnd = static_cast<HWND>(window);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    result = create(instance, &info, nullptr, &surface);
    return (result == VK_SUCCESS) ? surface : VK_NULL_HANDLE;
  }
} // namespace gpg::gal::vulkan

#else // The Linux client: the window is the client's SDL_Window* (X11 or Wayland, SDL decides).

#include "VulkanSurface.h"

#include <cstring>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "gpg/core/utils/Logging.h"

namespace gpg::gal::vulkan
{
  namespace
  {
    /**
     * SDL hands out the instance extensions only once its Vulkan library is loaded: a window
     * created with SDL_WINDOW_VULKAN does that, and so does this call (reference counted, and
     * the same libvulkan.so.1 that VulkanLoader opens, so both use one loader).
     */
    [[nodiscard]] bool SdlVulkanReady() noexcept
    {
      static const bool loaded = [] {
        if (!SDL_Vulkan_LoadLibrary(nullptr)) {
          gpg::Warnf("Vulkan: SDL could not load the Vulkan library: %s", SDL_GetError());
          return false;
        }
        return true;
      }();
      return loaded;
    }
  } // namespace

  const char* PlatformSurfaceExtension() noexcept
  {
    // The first window-system extension SDL asks for (VK_KHR_xlib_surface, VK_KHR_wayland_surface ...);
    // PlatformInstanceExtensions() has all of them.
    if (SdlVulkanReady()) {
      Uint32 count = 0U;
      const char* const* const names = SDL_Vulkan_GetInstanceExtensions(&count);
      for (Uint32 index = 0U; names != nullptr && index < count; ++index) {
        if (std::strcmp(names[index], VK_KHR_SURFACE_EXTENSION_NAME) != 0) {
          return names[index];
        }
      }
    }
    return VK_KHR_SURFACE_EXTENSION_NAME;
  }

  std::vector<const char*> PlatformInstanceExtensions()
  {
    std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME};
    if (!SdlVulkanReady()) {
      return extensions;
    }
    Uint32 count = 0U;
    const char* const* const names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (names == nullptr) {
      gpg::Warnf("Vulkan: SDL lists no instance extensions: %s", SDL_GetError());
      return extensions;
    }
    for (Uint32 index = 0U; index < count; ++index) {
      bool present = false;
      for (const char* const existing : extensions) {
        present = present || std::strcmp(existing, names[index]) == 0;
      }
      if (!present) {
        extensions.push_back(names[index]); // SDL keeps the strings for the process's life
      }
    }
    return extensions;
  }

  VkSurfaceKHR CreateWindowSurface(const VkInstance instance, void* const window, VkResult& result)
  {
    auto* const sdlWindow = static_cast<SDL_Window*>(window);
    if (sdlWindow == nullptr || !SdlVulkanReady()) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      return VK_NULL_HANDLE;
    }
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(sdlWindow, instance, nullptr, &surface)) {
      // SDL 3.4 turns a window created without SDL_WINDOW_VULKAN into a Vulkan one here, so this is
      // the window system refusing (no X11/Wayland surface support in the driver, a lost display ...).
      gpg::Warnf("Vulkan: SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
      result = VK_ERROR_SURFACE_LOST_KHR;
      return VK_NULL_HANDLE;
    }
    result = VK_SUCCESS;
    return surface;
  }

  bool WindowPixelSize(void* const window, std::uint32_t& width, std::uint32_t& height) noexcept
  {
    int w = 0;
    int h = 0;
    if (window == nullptr || !SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(window), &w, &h) || w <= 0 || h <= 0) {
      return false;
    }
    width = static_cast<std::uint32_t>(w);
    height = static_cast<std::uint32_t>(h);
    return true;
  }

  void ApplyWindowFullscreen(void* const window, const bool fullscreen) noexcept
  {
    auto* const sdlWindow = static_cast<SDL_Window*>(window);
    if (sdlWindow == nullptr) {
      return;
    }
    const bool isFullscreen = (SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_FULLSCREEN) != 0;
    if (isFullscreen == fullscreen) {
      return;
    }
    (void)SDL_SetWindowFullscreenMode(sdlWindow, nullptr); // the desktop's mode: borderless full screen
    if (!SDL_SetWindowFullscreen(sdlWindow, fullscreen)) {
      gpg::Warnf("Vulkan: SDL_SetWindowFullscreen(%d) failed: %s", fullscreen ? 1 : 0, SDL_GetError());
      return;
    }
    // The swap chain is made right after this, from the window's new size.
    (void)SDL_SyncWindow(sdlWindow);
    gpg::Logf("Vulkan: window %s", fullscreen ? "full screen (desktop mode, the frame scaled into it)" : "windowed");
  }
} // namespace gpg::gal::vulkan

#endif
