# Engine addition (faf-neural-ai): DirectXTex for the native Linux client: the texture decoder and
# encoder the Vulkan backend uses (gpg/gal/backends/common/ImageCodecDirectXTex.cpp; Windows has
# cmake/deps/directxtex). DirectXTex takes its Windows types from DirectX-Headers' WSL stubs
# (wsl/winadapter.h), which differ from the engine's POSIX shim (src/platform/posix/win32), so this
# library - with the engine's codec file - compiles apart from the client's objects: no shim, no
# PosixCompat.h. Upstream's non-Windows source list (no WIC, no FlipRotate); PNG and BMP go through SDL 3.
set(_dxtex "${FAF_DEPENDENCIES_DIR}/DirectXTex/DirectXTex")
set(_dxheaders "${FAF_DEPENDENCIES_DIR}/DirectX-Headers/include")
set(_dxmath "${FAF_DEPENDENCIES_DIR}/DirectXMath/Inc")
foreach(_required "${_dxtex}/DirectXTex.h" "${_dxheaders}/wsl/winadapter.h" "${_dxmath}/DirectXMath.h")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Dependencies: ${_required} is missing (cmake/FafDependencies.cmake downloads it)")
  endif()
endforeach()

set(_faf_dxtex_sources "")
foreach(_name BC BC4BC5 BC6HBC7 DirectXTexCompress DirectXTexConvert DirectXTexDDS DirectXTexHDR DirectXTexImage
              DirectXTexMipmaps DirectXTexMisc DirectXTexNormalMaps DirectXTexPMAlpha DirectXTexResize DirectXTexTGA
              DirectXTexUtil)
  list(APPEND _faf_dxtex_sources "${_dxtex}/${_name}.cpp")
endforeach()
set_source_files_properties(${_faf_dxtex_sources} PROPERTIES COMPILE_OPTIONS "-w")

add_library(faf_directxtex_linux STATIC
  ${_faf_dxtex_sources}
  "${PROJECT_SOURCE_DIR}/src/gpg/gal/backends/common/ImageCodecDirectXTex.cpp")
target_include_directories(faf_directxtex_linux PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"            # sal.h: SAL annotations, empty
  "${_dxheaders}"
  "${_dxheaders}/wsl/stubs"
  "${_dxheaders}/directx"
  "${_dxmath}"
  "${_dxtex}"
  "${PROJECT_SOURCE_DIR}/src")
target_compile_options(faf_directxtex_linux PRIVATE -fPIC -pthread)
target_link_libraries(faf_directxtex_linux PRIVATE SDL3::SDL3-static faf_codegen)
