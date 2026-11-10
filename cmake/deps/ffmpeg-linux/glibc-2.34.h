/*
 * Engine addition (faf-neural-ai): force-included into FFmpeg's sources in the release build
 * (FAF_LINUX_PINS=release, glibc 2.35 headers, for glibc 2.34 and up; see
 * cmake/toolchains/linux-clang-pins.cmake). glibc 2.35 added a new hypot, hypot@GLIBC_2.35, and
 * links new calls to it; this binds FFmpeg's calls to the hypot every glibc since 2.2.5 has, so the
 * client still loads on glibc 2.34 (tools/linux-build/package.sh checks it). FFmpeg only: the
 * simulation does not call libm's hypot, and the movie decoder's results are only drawn.
 */
/* Force-included first, before anything has defined __GLIBC__. */
#include <features.h>
#if defined(__x86_64__) && defined(__GLIBC__)
__asm__(".symver hypot,hypot@GLIBC_2.2.5");
#endif
