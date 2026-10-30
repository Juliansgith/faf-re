# Code generation every object linked into the engine is built with: the
# engine itself and each static library under cmake/deps (Wild Magic's math is
# used by the simulation; wx and the image codecs get the same rules so no
# object differs by accident).
#
# The simulation must compute bit for bit what the 2007 engine computed (replays
# carry every player's per-beat checksum; any difference is a desync). The MSVC
# build gets there with /fp:precise, /arch:SSE2 on x86 and the x87 helpers in
# src/legacy/math. clang has to be held to the same code:
#
#  - /fp:precise without contraction. clang's precise model contracts a*b+c
#    into FMA where the target has it; MSVC's /fp:precise never does. No
#    -march/-mavx/-mfma anywhere: the baseline x86-64 / SSE2 ISA only.
#
#  - Math library calls stay calls. With the functions recognised as builtins,
#    LLVM folds them on constant arguments with the compiler's own libm,
#    rewrites pow(x, 2.0) to x*x and pow(2.0, x) to exp2, and - because it then
#    treats them as not reading memory - may move them across the
#    _controlfp/_set_SSE2_enable calls that pick the CRT's x87 bodies at PC24.
#    The game's results come from those bodies (pow(21.4163856f, 2) is not
#    x*x there), so the transcendental functions are not builtins. Exact
#    functions (fabs, floor, ceil, trunc, fmod, ldexp, ...) are unaffected and
#    keep their builtins.
#
#  - x86: sqrt is a CRT call as well, the clang counterpart of
#    platform/X87SqrtCall.h's `#pragma function(sqrt)` (which clang-cl also
#    honours; the flags make it hold for every library too). Under the sim's
#    _PC_24 control word the CRT's fsqrt rounds to 24 bits where an inlined
#    sqrtsd would not. x64 keeps sqrt inline: MSVC x64 inlines sqrtsd too.
#
#  - Undefined behaviour MSVC does not exploit stays unexploited: no
#    type-based alias analysis, wrapping signed overflow, null checks kept,
#    float-to-int conversions that behave like cvttss2si on out-of-range
#    values (MSVC's 0x80000000), no forward-progress assumption for loops, and
#    no "unreachable" after a missing return.

add_library(faf_codegen INTERFACE)
add_library(faf::codegen ALIAS faf_codegen)

set(_faf_libm_calls
  acos asin atan atan2 cos cosh sin sinh tan tanh
  exp exp2 expm1 log log10 log1p log2 logb pow cbrt hypot erf erfc tgamma lgamma)

# Engine addition (dedicated-server plan WPL.2): the same rules for the Linux
# build (clang with the GNU driver, cmake/toolchains/linux-clang-x64.cmake),
# spelled for that driver, plus what the GNU driver defaults differently from
# clang-cl:
#
#  - -fno-math-errno: clang-cl's default. With errno semantics clang keeps
#    sqrt as a call guarded for negative inputs where clang-cl inlines sqrtsd.
#  - -fsigned-char: plain char signed, as with MSVC (the x86-64 Linux default
#    too; spelled out so no target can differ).
#  - Function and data sections, collected at link time: /Gy and /OPT:REF.
#
# Never -ffast-math or -Ofast: checked below.
if(FAF_LINUX)
  set(_faf_no_builtin "")
  foreach(_fn IN LISTS _faf_libm_calls)
    list(APPEND _faf_no_builtin "-fno-builtin-${_fn}" "-fno-builtin-${_fn}f" "-fno-builtin-${_fn}l")
  endforeach()
  foreach(_flags IN ITEMS CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE)
    if("${${_flags}}" MATCHES "-ffast-math|-Ofast|-ffp-contract=(on|fast)|-march=|-mfma|-mavx")
      message(FATAL_ERROR "${_flags} contains a flag that changes floating-point results: ${${_flags}}")
    endif()
  endforeach()
  # FAF_ULTRA / FAF_ULTRA_FP (see below) for the Linux build too (benchmarks on the server)
  option(FAF_ULTRA "Maximum-speed codegen (-O3, x86-64-v3, ThinLTO); results stay retail's" OFF)
  option(FAF_ULTRA_FP "With FAF_ULTRA: relaxed floating point (results differ from retail)" OFF)
  set(_faf_linux_fp "-ffp-model=precise;-ffp-contract=off;-fno-math-errno;${_faf_no_builtin}")
  set(_faf_linux_perf "")
  if(FAF_ULTRA)
    set(_faf_linux_perf "-O3;-march=x86-64-v3;-mtune=znver3;-flto=thin")
    target_link_options(faf_codegen INTERFACE -flto=thin -fuse-ld=lld -Wl,--thinlto-jobs=all)
    target_compile_definitions(faf_codegen INTERFACE FAF_ULTRA=1)
    if(FAF_ULTRA_FP)
      set(_faf_linux_fp "-ffp-contract=fast;-fno-math-errno;-fno-trapping-math;-fno-signed-zeros;-freciprocal-math;-fassociative-math")
      target_compile_definitions(faf_codegen INTERFACE FAF_ULTRA_FP=1)
    endif()
    set(FAF_ULTRA_PGO_USE "" CACHE FILEPATH "With FAF_ULTRA: a merged .profdata to optimise with (PGO)")
    option(FAF_ULTRA_PGO_GEN "With FAF_ULTRA: an instrumented build that writes a .profraw" OFF)
    if(FAF_ULTRA_PGO_GEN)
      list(APPEND _faf_linux_perf "-fprofile-instr-generate")
      target_compile_definitions(faf_codegen INTERFACE FAF_ULTRA_PGO_GEN=1)
      target_link_options(faf_codegen INTERFACE -fprofile-instr-generate)
    elseif(FAF_ULTRA_PGO_USE)
      list(APPEND _faf_linux_perf "-fprofile-instr-use=${FAF_ULTRA_PGO_USE}" "-Wno-profile-instr-unprofiled"
           "-Wno-profile-instr-out-of-date" "-Wno-backend-plugin")
    endif()
  endif()
  target_compile_options(faf_codegen INTERFACE
    "$<$<COMPILE_LANGUAGE:C,CXX>:${_faf_linux_fp};-fno-strict-aliasing;-fwrapv;-fno-delete-null-pointer-checks;-fno-strict-float-cast-overflow;-fno-finite-loops;-fsigned-char;-ffunction-sections;-fdata-sections;-Wno-overriding-option;${_faf_linux_perf}>"
    $<$<COMPILE_LANGUAGE:CXX>:-fno-strict-return>)
  target_link_options(faf_codegen INTERFACE -Wl,--gc-sections)
  return()
endif()

# Engine addition (faf-neural-ai, 2026-10-30): two speed switches, Windows clang-cl only.
#  - FAF_ULTRA: codegen only, results stay retail's: /O2 -> -O3, x86-64-v3 (AVX2/BMI2; Zen 2+ / Haswell+;
#    FMA instructions exist but -ffp-contract=off still forbids contracting a*b+c into one), tuned for Zen 3,
#    ThinLTO (FAF_ULTRA_LTO) and PGO (FAF_ULTRA_PGO_GEN / FAF_ULTRA_PGO_USE). Every exactness rule below stays,
#    so a replay keeps matching every checksum (the benchmark can play the real battle).
#  - FAF_ULTRA_FP (with FAF_ULTRA): trades retail exactness for speed: FMA contraction, reassociation,
#    reciprocals, libm builtins, and legacy/math/X87MathUltra.cpp instead of the x87 emulation. Results
#    differ from the 2007 engine (replays desync). The flags that protect the decompiled code from undefined
#    behaviour stay (no strict aliasing, wrapping overflow, null checks, cvttss2si-like casts, strict return).
option(FAF_ULTRA "Maximum-speed codegen (-O3, x86-64-v3, ThinLTO, PGO); results stay retail's" OFF)
option(FAF_ULTRA_FP "With FAF_ULTRA: relaxed floating point (results differ from retail)" OFF)
option(FAF_ULTRA_LTO "With FAF_ULTRA: ThinLTO" ON)
set(FAF_ULTRA_PGO_USE "" CACHE FILEPATH "With FAF_ULTRA: a merged .profdata to optimise with (PGO)")
option(FAF_ULTRA_PGO_GEN "With FAF_ULTRA: an instrumented build that writes a .profraw" OFF)
set(_faf_ultra_perf "")
if(FAF_ULTRA AND NOT FAF_LINUX)
  set(_faf_ultra_perf /clang:-O3 /clang:-march=x86-64-v3 /clang:-mtune=znver3 /Gw -Wno-unused-command-line-argument)
  if(FAF_ULTRA_LTO)
    list(APPEND _faf_ultra_perf /clang:-flto=thin)
    target_link_options(faf_codegen INTERFACE /opt:lldltojobs=all /opt:lldlto=3 /OPT:REF /OPT:ICF)
  endif()
  if(FAF_ULTRA_PGO_GEN)
    list(APPEND _faf_ultra_perf /clang:-fprofile-instr-generate)
    target_compile_definitions(faf_codegen INTERFACE FAF_ULTRA_PGO_GEN=1)
    file(GLOB _faf_profile_rt "${FAF_LLVM_ROOT}/lib/clang/*/lib/windows/clang_rt.profile-x86_64.lib")
    target_link_options(faf_codegen INTERFACE ${_faf_profile_rt})
  elseif(FAF_ULTRA_PGO_USE)
    list(APPEND _faf_ultra_perf "/clang:-fprofile-instr-use=${FAF_ULTRA_PGO_USE}" -Wno-profile-instr-unprofiled
         -Wno-profile-instr-out-of-date -Wno-backend-plugin)
  endif()
  target_compile_definitions(faf_codegen INTERFACE FAF_ULTRA=1)
  message(STATUS "FAF_ULTRA: -O3 x86-64-v3 LTO=${FAF_ULTRA_LTO} PGO gen=${FAF_ULTRA_PGO_GEN} use=${FAF_ULTRA_PGO_USE} FP=${FAF_ULTRA_FP}")
endif()
if(FAF_ULTRA AND FAF_ULTRA_FP AND NOT FAF_LINUX)
  set(_faf_ultra_fp_options
    /clang:-ffp-contract=fast
    /clang:-fno-math-errno
    /clang:-fno-trapping-math
    /clang:-fno-signed-zeros
    /clang:-freciprocal-math
    /clang:-fassociative-math
    /clang:-fno-strict-aliasing
    /clang:-fwrapv
    /clang:-fno-delete-null-pointer-checks
    /clang:-fno-strict-float-cast-overflow
    /clang:-fno-finite-loops
    /Gy
    -Wno-overriding-option)
  target_compile_options(faf_codegen INTERFACE
    "$<$<COMPILE_LANGUAGE:C,CXX>:${_faf_ultra_fp_options};${_faf_ultra_perf}>"
    $<$<COMPILE_LANGUAGE:CXX>:/clang:-fno-strict-return>)
  target_compile_definitions(faf_codegen INTERFACE FAF_ULTRA_FP=1 $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
  return()
endif()

set(_faf_no_builtin "")
foreach(_fn IN LISTS _faf_libm_calls)
  list(APPEND _faf_no_builtin "/clang:-fno-builtin-${_fn}" "/clang:-fno-builtin-${_fn}f" "/clang:-fno-builtin-${_fn}l")
endforeach()
if(FAF_TARGET_ARCH STREQUAL "x86")
  list(APPEND _faf_no_builtin /clang:-fno-builtin-sqrt /clang:-fno-builtin-sqrtf /clang:-fno-builtin-sqrtl)
endif()

set(_faf_codegen_options
  /fp:precise
  /clang:-ffp-contract=off
  ${_faf_no_builtin}
  /clang:-fno-strict-aliasing
  /clang:-fwrapv
  /clang:-fno-delete-null-pointer-checks
  /clang:-fno-strict-float-cast-overflow
  /clang:-fno-finite-loops
  # Function-level linking, as /Gy in the MSVC Release configurations.
  /Gy
  # "overriding -ffp-model=precise with -ffp-contract=off": intended, see above.
  -Wno-overriding-option)
if(FAF_TARGET_ARCH STREQUAL "x86")
  # MSVC's x86 default; spelled out so clang cannot pick another baseline.
  list(APPEND _faf_codegen_options /arch:SSE2)
endif()

# C and C++ only: the x64 MASM file goes through llvm-ml, which takes none of
# these.
target_compile_options(faf_codegen INTERFACE
  "$<$<COMPILE_LANGUAGE:C,CXX>:${_faf_codegen_options};${_faf_ultra_perf}>"
  $<$<COMPILE_LANGUAGE:CXX>:/clang:-fno-strict-return>)

# Checked iterators off in Debug as well, for every library: the setting changes
# the size of every std container, and a mismatch between the engine and a
# library changes object layout (LNK2038).
target_compile_definitions(faf_codegen INTERFACE $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
