# Engine addition (faf-neural-ai): SDL 3 for the native Linux client (src/platform/sdl): window,
# events, Vulkan surface, audio device. Static, with only the subsystems the client uses; the X11,
# Wayland and audio libraries are loaded at run time (SDL's *_SHARED defaults), so the program
# starts on either display server without them being link-time dependencies.
set(_sdl "${FAF_DEPENDENCIES_DIR}/SDL3")
if(NOT EXISTS "${_sdl}/CMakeLists.txt")
  message(FATAL_ERROR "SDL3 is missing: ${_sdl} (cmake/FafDependencies.cmake downloads it)")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
# Not used by the client.
foreach(_off SDL_CAMERA SDL_HAPTIC SDL_HIDAPI SDL_JOYSTICK SDL_SENSOR SDL_POWER SDL_GPU SDL_RENDER)
  set(${_off} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${_sdl}" "${CMAKE_BINARY_DIR}/deps/sdl3-build" EXCLUDE_FROM_ALL)
