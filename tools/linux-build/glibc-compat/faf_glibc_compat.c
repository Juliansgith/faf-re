/*
 * Engine addition (faf-neural-ai): the few glibc 2.36/2.38 functions Microsoft's Linux build of DXC
 * (libdxcompiler.so, libdxil.so; built on Ubuntu 24.04) calls, for the downloadable client on older
 * distributions (glibc 2.34 and up). tools/linux-build/package.sh builds this as
 * libfaf-glibc-compat.so, adds it to the DXC libraries' DT_NEEDED and marks their GLIBC_2.35+
 * version requirements weak (tools/linux-build/elf_weak_versions.py), so they load on glibc 2.34.
 *
 * The definitions here are unversioned. The dynamic linker searches the global scope (the program,
 * libc, libm) before a dlopen'ed library's own dependencies, so on glibc 2.38 and newer the DXC
 * libraries still bind to glibc's own functions; these are used only where glibc lacks them.
 *
 * The game's own code never calls them: faf is built against glibc 2.35 headers and needs 2.34.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/random.h>

#define FAF_COMPAT_EXPORT __attribute__((visibility("default")))

/*
 * glibc 2.38's C23 strtol family: as the C11 functions, and also reading a "0b"/"0B" prefix for base 0
 * and 2. DXC parses decimal and hexadecimal numbers (options, HLSL literals go through its own lexer).
 */
static int faf_binary_prefix(const char* text, int base)
{
  if (base != 0 && base != 2) {
    return 0;
  }
  while (*text == ' ' || (*text >= '\t' && *text <= '\r')) {
    ++text;
  }
  if (*text == '+' || *text == '-') {
    ++text;
  }
  return text[0] == '0' && (text[1] == 'b' || text[1] == 'B') && (text[2] == '0' || text[2] == '1');
}

static const char* faf_after_binary_prefix(const char* text)
{
  while (*text != '0') {
    ++text;
  }
  return text + 2;
}

#define FAF_C23_STRTO(name, type, c11)                                                   \
  FAF_COMPAT_EXPORT type name(const char* restrict text, char** restrict end, int base) \
  {                                                                                      \
    if (faf_binary_prefix(text, base)) {                                                 \
      const char* p = text;                                                              \
      while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {                                  \
        ++p;                                                                             \
      }                                                                                  \
      const int negative = *p == '-';                                                    \
      type value = c11(faf_after_binary_prefix(text), end, 2);                           \
      return negative ? (type)(0 - value) : value;                                       \
    }                                                                                    \
    return c11(text, end, base);                                                         \
  }

FAF_C23_STRTO(__isoc23_strtol, long, strtol)
FAF_C23_STRTO(__isoc23_strtoul, unsigned long, strtoul)
FAF_C23_STRTO(__isoc23_strtoll, long long, strtoll)
FAF_C23_STRTO(__isoc23_strtoull, unsigned long long, strtoull)

/* glibc 2.36's arc4random, on the getrandom system call (glibc 2.25). */
static void faf_random_bytes(void* buffer, size_t size)
{
  unsigned char* out = (unsigned char*)buffer;
  while (size > 0) {
    const ssize_t got = getrandom(out, size, 0);
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      abort(); /* as glibc's arc4random: it never returns without randomness */
    }
    out += got;
    size -= (size_t)got;
  }
}

FAF_COMPAT_EXPORT uint32_t arc4random(void)
{
  uint32_t value;
  faf_random_bytes(&value, sizeof value);
  return value;
}

/*
 * fmod@GLIBC_2.38 is glibc's fmod without the old SVID error-handling wrapper; the result is the same
 * exact remainder. x87 FPREM computes it exactly (the reduction is exact by construction), so this
 * needs no libm and cannot bind back to itself.
 */
FAF_COMPAT_EXPORT double fmod(double x, double y)
{
  long double rx = x;
  const long double ry = y;
  unsigned short status;
  do {
    __asm__ volatile("fprem\n\tfnstsw %1" : "+t"(rx), "=a"(status) : "u"(ry));
  } while (status & 0x0400); /* C2: the reduction is not complete yet */
  return (double)rx;
}
