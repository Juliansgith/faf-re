# Engine addition (faf-neural-ai): FAudio for the native Linux client's sound: XAudio2, X3DAudio and
# XACT3 (its FACT API), which play FA's original .xwb/.xsb/.xgs banks (moho/audio). Static, on the
# SDL 3 the client builds (cmake/deps/sdl3).
set(_faudio "${FAF_DEPENDENCIES_DIR}/FAudio")
if(NOT EXISTS "${_faudio}/CMakeLists.txt")
  message(FATAL_ERROR "FAudio is missing: ${_faudio} (cmake/FafDependencies.cmake downloads it)")
endif()

set(BUILD_SHARED_LIBS OFF)
set(BUILD_SDL3 ON CACHE BOOL "" FORCE)
set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
set(XNASONG OFF CACHE BOOL "" FORCE)
set(SDL3_INCLUDE_DIRS "${FAF_DEPENDENCIES_DIR}/SDL3/include")
set(SDL3_LIBRARIES SDL3::SDL3-static)
add_subdirectory("${_faudio}" "${CMAKE_BINARY_DIR}/deps/faudio-build" EXCLUDE_FROM_ALL)
