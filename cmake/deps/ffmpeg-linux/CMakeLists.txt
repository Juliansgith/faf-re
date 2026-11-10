# Engine addition (faf-neural-ai): FFmpeg 7.1.3 for the native Linux client's SFD movies
# (moho/movie/sfd/FfmpegSfdDecoder.cpp): avutil, swresample, swscale, avcodec and avformat as static
# libraries, with the configuration the Windows build uses (dependencies/ffmpeg/faf/generate.sh,
# docs/FAF_SFD_COMPATIBILITY.md): MPEG-1/2 video, ADX and MPEG audio, the MPEG program stream
# demuxer and the file protocol, nothing else. The source is the release archive
# (cmake/FafDependencies.cmake, ffmpeg-linux), the same 7.1.3 as the Windows submodule.
#
# FFmpeg's build is GNU make, which the build container does not have. As on Windows, its configure
# runs here once (configure only writes files: config.h, config_components.h, the component lists,
# ffbuild/config.mak) and gen_sources.py reads the libraries' object lists out of the Makefiles; the
# libraries are then ordinary CMake targets that Ninja compiles with the rest of the client. The
# configure output is cached in the build tree and redone when the options, the compiler or the source
# archive change.
#
# Linux-only additions to the Windows options: --disable-autodetect (no system library is picked up:
# no zlib, iconv, xlib, libdrm, vaapi/vdpau, Vulkan, ...) and --enable-pic (the client is a PIE). The
# libraries need only libm and pthreads from glibc, so the client's run-time dependencies stay glibc,
# libm and libgcc_s. configure also adds -latomic when the system has it; x86-64's atomics are inline,
# so it is dropped.
#
# Result: faf_ffmpeg (INTERFACE: the five libraries in link order) and FAF_FFMPEG_INCLUDE_DIRS (the
# public headers) in the parent scope.
set(_ffmpeg "${FAF_DEPENDENCIES_DIR}/ffmpeg-linux")
if(NOT EXISTS "${_ffmpeg}/configure")
  message(FATAL_ERROR "FFmpeg is missing: ${_ffmpeg} (cmake/FafDependencies.cmake downloads it)")
endif()
find_program(FAF_PYTHON3 NAMES python3 python REQUIRED)
find_program(FAF_SH sh REQUIRED)

set(_ffmpeg_options --cc=${CMAKE_C_COMPILER})
foreach(_tool AR NM RANLIB)
  if(CMAKE_${_tool})
    string(TOLOWER "${_tool}" _option)
    list(APPEND _ffmpeg_options --${_option}=${CMAKE_${_tool}})
  endif()
endforeach()
list(APPEND _ffmpeg_options
  --disable-debug --disable-autodetect --enable-pic
  # The Windows build's configuration (dependencies/ffmpeg/faf/generate.sh).
  --disable-x86asm --disable-everything --disable-programs --disable-doc --disable-network
  --enable-static --disable-shared
  --enable-avcodec --enable-avformat --enable-avutil --enable-swscale --enable-swresample
  --enable-decoder=mpeg1video,mpeg2video,adpcm_adx,mp1,mp2,mp3
  --enable-parser=mpegvideo,mpegaudio,adx
  --enable-demuxer=mpegps,mpegvideo,adx,mp3
  --enable-protocol=file)
set(_ffmpeg_libraries avutil swresample swscale avcodec avformat)

set(_cfg "${CMAKE_CURRENT_BINARY_DIR}/config")
set(_stamp_file "${_cfg}/faf-configure.stamp")
set(_archive_hash "")
if(EXISTS "${_ffmpeg}/.faf-download")
  file(READ "${_ffmpeg}/.faf-download" _archive_hash)
endif()
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/gen_sources.py" _gen_hash)
string(SHA256 _stamp "${_ffmpeg_options};${_archive_hash};${_gen_hash};${CMAKE_C_COMPILER_VERSION}")
set(_have_stamp "")
if(EXISTS "${_stamp_file}")
  file(READ "${_stamp_file}" _have_stamp)
endif()
if(NOT _have_stamp STREQUAL _stamp OR NOT EXISTS "${_cfg}/sources.cmake")
  message(STATUS "FFmpeg (Linux): running configure in ${_cfg}")
  file(REMOVE_RECURSE "${_cfg}")
  file(MAKE_DIRECTORY "${_cfg}")
  execute_process(COMMAND "${FAF_SH}" "${_ffmpeg}/configure" ${_ffmpeg_options}
    WORKING_DIRECTORY "${_cfg}" RESULT_VARIABLE _rc OUTPUT_FILE "${_cfg}/configure.out" ERROR_FILE "${_cfg}/configure.out")
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "FFmpeg (Linux): configure failed (${_rc}); see ${_cfg}/configure.out and ${_cfg}/ffbuild/config.log")
  endif()
  # What FFmpeg's Makefile generates before compiling.
  execute_process(COMMAND "${FAF_SH}" "${_ffmpeg}/ffbuild/version.sh" "${_ffmpeg}" libavutil/ffversion.h
    WORKING_DIRECTORY "${_cfg}" RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "FFmpeg (Linux): ffbuild/version.sh failed (${_rc})")
  endif()
  execute_process(COMMAND "${FAF_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_sources.py" "${_ffmpeg}"
                          "${_cfg}/ffbuild/config.mak" "${_cfg}/sources.cmake" ${_ffmpeg_libraries}
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "FFmpeg (Linux): gen_sources.py failed (${_rc})")
  endif()
  file(WRITE "${_stamp_file}" "${_stamp}")
endif()

set(FFMPEG_FAF_SOURCE_DIR "${_ffmpeg}")
include("${_cfg}/sources.cmake")

# FFmpeg's own optimisation and flags (FFMPEG_FAF_COMPILE_OPTIONS has configure's -O3), not CMake's
# per-configuration ones: FFmpeg relies on dead-code elimination for disabled components.
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_C_FLAGS_${_config} "")
endforeach()

# The public header configure generates (libavutil/avconfig.h), on its own so that the client does not
# see config.h.
configure_file("${_cfg}/libavutil/avconfig.h" "${CMAKE_CURRENT_BINARY_DIR}/include/libavutil/avconfig.h" COPYONLY)

set(_ffmpeg_deps_avutil "")
set(_ffmpeg_deps_swresample avutil)
set(_ffmpeg_deps_swscale avutil)
set(_ffmpeg_deps_avcodec avutil)
set(_ffmpeg_deps_avformat avcodec avutil)
foreach(_lib IN LISTS _ffmpeg_libraries)
  string(TOUPPER "${_lib}" _LIB)
  set(_sources "${FFMPEG_FAF_${_LIB}_SOURCES}")
  list(TRANSFORM _sources PREPEND "${_ffmpeg}/")
  add_library(faf_ffmpeg_${_lib} STATIC EXCLUDE_FROM_ALL ${_sources})
  set_target_properties(faf_ffmpeg_${_lib} PROPERTIES OUTPUT_NAME ${_lib})
  # FFmpeg's compile command: -I. -I$(SRC_PATH) $(CPPFLAGS) $(CFLAGS), plus ffbuild/library.mak's.
  target_include_directories(faf_ffmpeg_${_lib} PRIVATE "${_cfg}" "${_ffmpeg}" ${FFMPEG_FAF_INCLUDE_DIRECTORIES})
  target_compile_definitions(faf_ffmpeg_${_lib} PRIVATE ${FFMPEG_FAF_DEFINITIONS} HAVE_AV_CONFIG_H BUILDING_${_lib})
  target_compile_options(faf_ffmpeg_${_lib} PRIVATE ${FFMPEG_FAF_COMPILE_OPTIONS} ${FAF_FFMPEG_WARNING_OPTIONS})
  foreach(_dep IN LISTS _ffmpeg_deps_${_lib})
    target_link_libraries(faf_ffmpeg_${_lib} PUBLIC faf_ffmpeg_${_dep})
  endforeach()
  # configure's EXTRALIBS-<lib>: libm and -pthread (the client links with -pthread); not libatomic.
  set(_system_libs "${FFMPEG_FAF_${_LIB}_SYSTEM_LIBS}")
  list(REMOVE_ITEM _system_libs atomic -pthread pthread)
  target_link_libraries(faf_ffmpeg_${_lib} PUBLIC ${_system_libs})
endforeach()

add_library(faf_ffmpeg INTERFACE)
target_link_libraries(faf_ffmpeg INTERFACE
  faf_ffmpeg_avformat faf_ffmpeg_avcodec faf_ffmpeg_swscale faf_ffmpeg_swresample faf_ffmpeg_avutil)
set(FAF_FFMPEG_INCLUDE_DIRS "${_ffmpeg}" "${CMAKE_CURRENT_BINARY_DIR}/include" PARENT_SCOPE)
