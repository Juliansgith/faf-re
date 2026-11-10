// Engine addition (faf-neural-ai, FAF_ULTRA): the msvc8 math helpers as plain float/double code.
//
// X87Math.cpp reproduces the 2007 engine's x87 arithmetic bit for bit: inline x87 on x86, and on x64 a
// call into hand-written x87 assembly (X87Math64.asm) for every sin, cos, atan2, pow, vector scale and
// box projection the sim does. The x87 transcendental instructions take ~100 cycles, every call is
// opaque to the optimiser (no inlining, no vectorisation) and saves/restores the x87 control word.
// An FAF_ULTRA build does not keep retail's results, so here every helper is the same formula in SSE/AVX
// float or double arithmetic, small enough for (Thin)LTO to inline into its callers. Semantics (operation
// order, the special cases callers rely on: NaN for domain errors, 0x80000000 for out-of-range casts,
// fmod/frexp of zero) are kept; only rounding differs.
#if defined(FAF_ULTRA_FP)

#include "legacy/math/X87Math.h"
#include "legacy/math/Libm.h"

#include <bit>
#include <cmath>
#include <xmmintrin.h>

extern "C" int global_mode_sse2 = 1;
extern "C" int global_compat_flag = 1;

extern "C" int __cdecl SetSse2Mode(const int enableSse2)
{
  const int mode = (enableSse2 != 0) ? global_compat_flag : 0;
  global_mode_sse2 = mode;
  return mode;
}

namespace fa
{
  namespace
  {
    [[nodiscard]] inline double DomainErrorNaN() noexcept
    {
      return std::bit_cast<double>(0xFFF8000000000000ull);
    }
  } // namespace

  std::int32_t RoundToInt(const float value) noexcept
  {
    // cvtss2si: round to nearest even under the default MXCSR, 0x80000000 for NaN/out of range (as fistp)
    return _mm_cvtss_si32(_mm_set_ss(value));
  }

  std::int32_t FloorToIntX87(const float value) noexcept
  {
    if (value != value) {
      return 0x7FFFFFFF; // a NaN floors to 0x7FFFFFFF (the -1 is added to fistp's 0x80000000)
    }
    // cvtss2si of floor: 0x80000000 outside the int range, as fistp
    return _mm_cvtss_si32(_mm_set_ss(std::floor(value)));
  }

  std::int32_t CeilToIntX87(const float value) noexcept
  {
    // a NaN adds nothing and stays the integer indefinite 0x80000000, as does out of range
    return _mm_cvtss_si32(_mm_set_ss(std::ceil(value)));
  }

  float TwisterWordInRange(const float lower, const float upper, const std::uint32_t twisterWord) noexcept
  {
    // lower + (upper - lower) * word * 2^-32 in double, rounded to float once
    constexpr double kInvTwoTo32 = 2.3283064365386962890625e-10;
    return static_cast<float>(
      static_cast<double>(twisterWord) * (static_cast<double>(upper) - static_cast<double>(lower)) * kInvTwoTo32
      + static_cast<double>(lower)
    );
  }

  void ScaleVectorByMaxLengthX87(float& x, float& y, float& z, const float maxLength) noexcept
  {
    const float lengthSq = (x * x) + (y * y) + (z * z);
    if ((maxLength * maxLength) >= lengthSq) {
      return;
    }
    const float scale = maxLength / std::sqrt(lengthSq);
    x *= scale;
    y *= scale;
    z *= scale;
  }

  void ScaleVectorToLengthX87(
    float* const destLanes, const float* const sourceLanes, const float lengthSq, const float targetLength
  ) noexcept
  {
    const float scale = targetLength / std::sqrt(lengthSq);
    const float sx = sourceLanes[0], sy = sourceLanes[1], sz = sourceLanes[2];
    destLanes[0] = sx * scale;
    destLanes[1] = sy * scale;
    destLanes[2] = sz * scale;
  }

  void ScaleVectorX87(float* const destLanes, const float* const sourceLanes, const float scale) noexcept
  {
    const float sx = sourceLanes[0], sy = sourceLanes[1], sz = sourceLanes[2];
    destLanes[0] = sx * scale;
    destLanes[1] = sy * scale;
    destLanes[2] = sz * scale;
  }

  float BoxRadiusOnAxisX87(const float* const boxLanes, const float* const axisLanes) noexcept
  {
    // Wm3::Box3f: center(0..2), axis0(3..5), axis1(6..8), axis2(9..11), extent(12..14)
    const float ax = axisLanes[0], ay = axisLanes[1], az = axisLanes[2];
    const float d0 = (boxLanes[4] * ay + boxLanes[5] * az) + boxLanes[3] * ax;
    const float d1 = (boxLanes[7] * ay + boxLanes[8] * az) + boxLanes[6] * ax;
    const float d2 = (boxLanes[10] * ay + boxLanes[11] * az) + boxLanes[9] * ax;
    return (std::fabs(d2) * boxLanes[14] + std::fabs(d1) * boxLanes[13]) + std::fabs(d0) * boxLanes[12];
  }

  double fmod(const double x, const double y) noexcept
  {
    if (x == 0.0 && y != 0.0 && std::isfinite(y)) {
      return 0.0;
    }
    if (std::fabs(x) < std::fabs(y)) {
      return x;
    }
    if (!std::isnan(x) && !std::isnan(y)) {
      if (std::isinf(x)) {
        return std::bit_cast<double>(std::signbit(x) ? 0xFFF8000000000000ull : 0x7FF8000000000000ull);
      }
      if (y == 0.0) {
        return std::bit_cast<double>(0x7FF8000000000000ull);
      }
    }
    return std::fmod(x, y);
  }

  double frexp(const double x, int* const exponent) noexcept
  {
    if (!std::isfinite(x)) {
      *exponent = -1;
      return std::isnan(x) ? x : std::bit_cast<double>(0xFFF8000000000000ull);
    }
    if (x == 0.0) {
      *exponent = 0;
      return 0.0;
    }
    return std::frexp(x, exponent);
  }

  float MultiplyX87(const float a, const float b) noexcept
  {
    return a * b;
  }

  float MultiplyIntegerX87(const std::int32_t integer, const float factor) noexcept
  {
    return static_cast<float>(static_cast<double>(integer) * static_cast<double>(factor));
  }

  double HalfSumX87(const float a, const float b) noexcept
  {
    return (static_cast<double>(a) + static_cast<double>(b)) * 0.5;
  }

  float HalfAbsDifferenceX87(const float minuend, const float subtrahend) noexcept
  {
    return std::fabs(minuend - subtrahend) * 0.5f;
  }

  void ScaleVectorToLengthOfX87(
    float* const destLanes, const float* const sourceLanes, const float* const targetLanes, const float sourceLengthSq
  ) noexcept
  {
    // double keeps a short source against a long target from overflowing the ratio (as x87 did)
    const double tx = targetLanes[0], ty = targetLanes[1], tz = targetLanes[2];
    const double scale = std::sqrt(((tx * tx + ty * ty) + tz * tz) / static_cast<double>(sourceLengthSq));
    const double sx = sourceLanes[0], sy = sourceLanes[1], sz = sourceLanes[2];
    destLanes[0] = static_cast<float>(sx * scale);
    destLanes[1] = static_cast<float>(sy * scale);
    destLanes[2] = static_cast<float>(sz * scale);
  }

  float BilinearHeight(
    const float h00, const float h01, const float h10, const float h11, const float fracX, const float fracZ
  ) noexcept
  {
    const float row0 = h00 + (h01 - h00) * fracZ;
    const float row1 = h10 + (h11 - h10) * fracZ;
    return row0 + (row1 - row0) * fracX;
  }

  float MulTwisterWord(const float factor, const std::uint32_t word) noexcept
  {
    return factor * static_cast<float>(word);
  }

  float sinf(const float x) noexcept { return std::sin(x); }
  float cosf(const float x) noexcept { return std::cos(x); }
  float tanf(const float x) noexcept { return std::tan(x); }
  float atan2f(const float y, const float x) noexcept { return std::atan2(y, x); }
  float atanf(const float x) noexcept { return std::atan(x); }

  float Atan2Difference(const float firstY, const float firstX, const float secondY, const float secondX) noexcept
  {
    return std::atan2(firstY, firstX) - std::atan2(secondY, secondX);
  }

  float Atan2Plus(const float y, const float x, const float addend) noexcept
  {
    return std::atan2(y, x) + addend;
  }

  float SinTimesCos(const float sinArg, const float cosArg) noexcept
  {
    return std::sin(sinArg) * std::cos(cosArg);
  }

  float SinTimes(const float sinArg, const float factor) noexcept
  {
    return std::sin(sinArg) * factor;
  }

  void SlerpSphericalBlend(
    float* const outLanes, const float* const q1Lanes, const float* const rhsLanes, const float angle,
    const float oneMinusT, const float t
  ) noexcept
  {
    const float inv = 1.0f / std::sin(angle);
    const float w1 = std::sin(angle * oneMinusT) * inv;
    const float w2 = std::sin(angle * t) * inv;
    for (int i = 0; i < 4; ++i) {
      outLanes[i] = q1Lanes[i] * w1 + rhsLanes[i] * w2;
    }
  }

  float IntrinsicExp(const float x) noexcept { return std::exp(x); }
  float IntrinsicLn(const float x) noexcept { return std::log(x); }
  float IntrinsicLog10(const float x) noexcept { return std::log10(x); }

  float IntrinsicLnQuotient(const float x, const double base) noexcept
  {
    return static_cast<float>(std::log(static_cast<double>(x)) / std::log(base));
  }

  float ScaledLnQuotient(const float numerator, const float denominator, const float scale) noexcept
  {
    return std::log(numerator / denominator) * scale;
  }

  float CosTimesCos(const float firstArg, const float secondArg) noexcept
  {
    return std::cos(firstArg) * std::cos(secondArg);
  }

  float asinf(const float x) noexcept
  {
    if (!std::isnan(x) && !(x >= -1.0f && x <= 1.0f)) {
      return static_cast<float>(DomainErrorNaN());
    }
    return std::asin(x);
  }

  float acosf(const float x) noexcept
  {
    if (!std::isnan(x) && !(x >= -1.0f && x <= 1.0f)) {
      return static_cast<float>(DomainErrorNaN());
    }
    return std::acos(x);
  }

  double acos(const double x) noexcept
  {
    if (!std::isnan(x) && !(x >= -1.0 && x <= 1.0)) {
      return DomainErrorNaN();
    }
    return std::acos(x);
  }

  double asin(const double x) noexcept
  {
    if (!std::isnan(x) && !(x >= -1.0 && x <= 1.0)) {
      return DomainErrorNaN();
    }
    return std::asin(x);
  }

  double pow(const double x, const double y) noexcept
  {
    if (x < 0.0 && std::isfinite(x) && std::isfinite(y) && y != std::floor(y)) {
      return DomainErrorNaN();
    }
    return std::pow(x, y);
  }

  float expf(const float x) noexcept { return std::exp(x); }
  float logf(const float x) noexcept { return std::log(x); }
  float powf(const float x, const float y) noexcept { return std::pow(x, y); }

  float VectorLengthZYXX87(const float* const lanes) noexcept
  {
    const float x = lanes[0], y = lanes[1], z = lanes[2];
    return std::sqrt((z * z + y * y) + x * x);
  }
} // namespace fa

#endif // FAF_ULTRA_FP
