#include "moho/collision/CColPrimitiveBase.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <emmintrin.h>
#include <limits>

#include "gpg/core/utils/Global.h"
#include "moho/math/MathReflection.h"
#include "moho/math/QuaternionMath.h"
#include "moho/render/camera/VTransform.h"
#include "moho/math/Wm3Segment3FafExtras.h"
#include "Wm3ContBox3.h"
#include "Wm3DistVector3Box3.h"
#include "Wm3IntrBox3Sphere3.h"
#include "Wm3IntrSegment3Box3.h"
#include "Wm3IntrSegment3Sphere3.h"

namespace
{
  constexpr float kAxisLengthSqEpsilon = 1.0e-6f;
  constexpr float kSupportSelectionEpsilon = 1.0e-3f;

  [[nodiscard]] Wm3::Vec3f BuildBoxCenter(const Wm3::Box3f& box) noexcept
  {
    return {box.Center[0], box.Center[1], box.Center[2]};
  }

  [[nodiscard]] Wm3::Vec3f BoxAxis(const Wm3::Box3f& box, const int axisIndex) noexcept
  {
    return {box.Axis[axisIndex][0], box.Axis[axisIndex][1], box.Axis[axisIndex][2]};
  }

  /**
   * Address: 0x00475550 (FUN_00475550, sub_475550)
   *
   * IDA signature:
   * float* __usercall sub_475550@<eax>(float* box@<eax>, float* axis@<ecx>,
   *     float* outMin, float* outMax);
   *
   * What it does:
   * Projects one oriented box onto a direction and returns the interval it
   * covers - the centre's projection, give or take the box's radius along that
   * direction. This is the per-axis half of the separating-axis test:
   * ComputeBoxBoxContactManifold calls it once per box for each of the fifteen
   * candidate axes and declares a miss as soon as two intervals fail to meet.
   *
   * The axis is not required to be unit length. Callers that need a real
   * distance divide by its length afterwards, which is what the manifold's
   * overlap normalisation does.
   */
  void ProjectBoxOntoAxis(const Wm3::Box3f& box, const Wm3::Vec3f& axis, float* outMin, float* outMax) noexcept
  {
    const Wm3::Vec3f center = BuildBoxCenter(box);
    const Wm3::Vec3f axis0 = BoxAxis(box, 0);
    const Wm3::Vec3f axis1 = BoxAxis(box, 1);
    const Wm3::Vec3f axis2 = BoxAxis(box, 2);

    const float centerProjection = Wm3::Vector3f::Dot(axis, center);

    // Summed third-axis first, then second, then first, because that is the
    // order the binary accumulates in and float addition does not associate.
    const float radius = (std::fabs(Wm3::Vector3f::Dot(axis, axis2)) * box.Extent[2] +
                           std::fabs(Wm3::Vector3f::Dot(axis, axis1)) * box.Extent[1]) +
      std::fabs(Wm3::Vector3f::Dot(axis, axis0)) * box.Extent[0];

    *outMin = centerProjection - radius;
    *outMax = centerProjection + radius;
  }

  /**
   * Address: 0x004752F0 (FUN_004752F0, sub_4752F0)
   *
   * IDA signature:
   * float* __usercall sub_4752F0@<eax>(float* box@<eax>, float* direction@<edx>, float* out@<ecx>);
   *
   * What it does:
   * Returns the corner of the box lying furthest against `direction`. Starts
   * at the centre and steps one extent along each axis, subtracting where the
   * axis projects positively and adding where it projects negatively. An axis
   * inside the dead band contributes nothing, so a direction parallel to a
   * face yields a point on that face rather than an arbitrary corner.
   */
  [[nodiscard]] Wm3::Vec3f
  ComputeSupportPointAgainstDirection(const Wm3::Box3f& box, const Wm3::Vec3f& direction) noexcept
  {
    Wm3::Vec3f support = BuildBoxCenter(box);
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      const Wm3::Vec3f axis = BoxAxis(box, axisIndex);
      const float projection = Wm3::Vector3f::Dot(direction, axis);
      const float extent = box.Extent[axisIndex];
      if (projection < -kSupportSelectionEpsilon) {
        support = support + axis * extent;
      } else if (projection > kSupportSelectionEpsilon) {
        support = support - axis * extent;
      }
    }
    return support;
  }

  /**
   * What it does:
   * `std::fmin`/`std::fmax` for floats with the CRT's exact results, inline. The CRT versions are calls
   * into ucrtbase that the compiler cannot inline, and the box-box SAT below makes two of them per
   * candidate axis: in large battles about 7 % of the sim thread was spent inside `fminf`/`fmaxf`
   * alone. Same results bit for bit, checked against ucrtbase on 20 million random bit patterns plus
   * signed zeros, infinities and quiet/signalling NaNs: a NaN argument yields the other one (two NaNs:
   * the second, quieted), and between +0 and -0 min gives -0 and max gives +0.
   */
  [[nodiscard]] inline float QuietedNaN(const float value) noexcept
  {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) | 0x00400000u);
  }

  [[nodiscard]] inline float CrtFminf(const float x, const float y) noexcept
  {
    if (x < y) {
      return x;
    }
    if (y < x) {
      return y;
    }
    if (x == y) {
      return std::signbit(x) ? x : y;
    }
    if (y != y) {
      return (x != x) ? QuietedNaN(y) : x;
    }
    return y;
  }

  [[nodiscard]] inline float CrtFmaxf(const float x, const float y) noexcept
  {
    if (x > y) {
      return x;
    }
    if (y > x) {
      return y;
    }
    if (x == y) {
      return std::signbit(x) ? y : x;
    }
    if (y != y) {
      return (x != x) ? QuietedNaN(y) : x;
    }
    return y;
  }

  struct BoxBoxContactManifold
  {
    Wm3::Vec3f pointOnA;          // +0x00
    Wm3::Vec3f pointOnB;          // +0x0C
    Wm3::Vec3f penetrationNormal; // +0x18
    float penetrationDepth;       // +0x24
  };
  static_assert(sizeof(BoxBoxContactManifold) == 0x28, "BoxBoxContactManifold size must be 0x28");

  /**
   * Engine addition, not recovered from the binary: the box-box SAT's fifteen
   * candidate axes laid out four to an SSE register (lane 15 is padding), and
   * what ComputeSatOverlaps derives for each of them.
   */
  struct SatAxisLanes
  {
    alignas(16) float x[16];
    alignas(16) float y[16];
    alignas(16) float z[16];
    alignas(16) float lengthSq[16];          // LengthSq(axis)
    alignas(16) float invLength[16];         // 1 / sqrt(LengthSq(axis))
    alignas(16) float normalizedOverlap[16]; // interval overlap along the axis * invLength
  };

  /** One box's projection inputs, each broadcast to all four lanes. */
  struct SatBoxLanes
  {
    __m128 cx, cy, cz;
    __m128 a0x, a0y, a0z;
    __m128 a1x, a1y, a1z;
    __m128 a2x, a2y, a2z;
    __m128 e0, e1, e2;

    explicit SatBoxLanes(const Wm3::Box3f& box) noexcept
      : cx(_mm_set1_ps(box.Center[0]))
      , cy(_mm_set1_ps(box.Center[1]))
      , cz(_mm_set1_ps(box.Center[2]))
      , a0x(_mm_set1_ps(box.Axis[0][0]))
      , a0y(_mm_set1_ps(box.Axis[0][1]))
      , a0z(_mm_set1_ps(box.Axis[0][2]))
      , a1x(_mm_set1_ps(box.Axis[1][0]))
      , a1y(_mm_set1_ps(box.Axis[1][1]))
      , a1z(_mm_set1_ps(box.Axis[1][2]))
      , a2x(_mm_set1_ps(box.Axis[2][0]))
      , a2y(_mm_set1_ps(box.Axis[2][1]))
      , a2z(_mm_set1_ps(box.Axis[2][2]))
      , e0(_mm_set1_ps(box.Extent[0]))
      , e1(_mm_set1_ps(box.Extent[1]))
      , e2(_mm_set1_ps(box.Extent[2]))
    {}
  };

  /**
   * `ProjectBoxOntoAxis` for four axes at once. Each lane performs the scalar
   * function's operations in its order - the three dot products as
   * `(x*x' + y*y') + z*z'`, the radius as `(|d2|*e2 + |d1|*e1) + |d0|*e0`,
   * then centre minus / plus radius - and packed SSE arithmetic rounds every
   * lane exactly as the scalar SSE instructions do, so the intervals are the
   * scalar ones bit for bit.
   */
  inline void ProjectBoxOntoAxes4(
    const SatBoxLanes& box, const __m128 ax, const __m128 ay, const __m128 az, __m128& outMin, __m128& outMax
  ) noexcept
  {
    const __m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    const __m128 centerProjection =
      _mm_add_ps(_mm_add_ps(_mm_mul_ps(ax, box.cx), _mm_mul_ps(ay, box.cy)), _mm_mul_ps(az, box.cz));
    const __m128 d2 = _mm_and_ps(
      _mm_add_ps(_mm_add_ps(_mm_mul_ps(ax, box.a2x), _mm_mul_ps(ay, box.a2y)), _mm_mul_ps(az, box.a2z)), absMask
    );
    const __m128 d1 = _mm_and_ps(
      _mm_add_ps(_mm_add_ps(_mm_mul_ps(ax, box.a1x), _mm_mul_ps(ay, box.a1y)), _mm_mul_ps(az, box.a1z)), absMask
    );
    const __m128 d0 = _mm_and_ps(
      _mm_add_ps(_mm_add_ps(_mm_mul_ps(ax, box.a0x), _mm_mul_ps(ay, box.a0y)), _mm_mul_ps(az, box.a0z)), absMask
    );
    const __m128 radius =
      _mm_add_ps(_mm_add_ps(_mm_mul_ps(d2, box.e2), _mm_mul_ps(d1, box.e1)), _mm_mul_ps(d0, box.e0));
    outMin = _mm_sub_ps(centerProjection, radius);
    outMax = _mm_add_ps(centerProjection, radius);
  }

  /**
   * `CrtFminf` on four lanes that hold no NaN: the smaller value, and for
   * equal values `x` when it is negative, else `y` - for equal values that
   * is the bitwise OR (they differ only as +0 / -0, and the OR is -0 when
   * either is).
   */
  inline __m128 CrtFminf4(const __m128 x, const __m128 y) noexcept
  {
    const __m128 less = _mm_cmplt_ps(x, y);
    const __m128 greater = _mm_cmplt_ps(y, x);
    const __m128 equal = _mm_cmpeq_ps(x, y);
    return _mm_or_ps(
      _mm_or_ps(_mm_and_ps(less, x), _mm_and_ps(greater, y)), _mm_and_ps(equal, _mm_or_ps(x, y))
    );
  }

  /** `CrtFmaxf` likewise; for equal values the result is the bitwise AND (-0 only when both are). */
  inline __m128 CrtFmaxf4(const __m128 x, const __m128 y) noexcept
  {
    const __m128 greater = _mm_cmpgt_ps(x, y);
    const __m128 less = _mm_cmpgt_ps(y, x);
    const __m128 equal = _mm_cmpeq_ps(x, y);
    return _mm_or_ps(
      _mm_or_ps(_mm_and_ps(greater, x), _mm_and_ps(less, y)), _mm_and_ps(equal, _mm_and_ps(x, y))
    );
  }

  /**
   * The separating-axis loop of ComputeBoxBoxContactManifold (0x00474830),
   * four axes at a time. Returns false when any axis separates the boxes -
   * the scalar loop stops at the first one, but only whether one exists is
   * observable. Otherwise fills, per axis, the interval overlap
   * `CrtFminf(lhsMax, rhsMax) - CrtFmaxf(lhsMin, rhsMin)` normalised by the
   * axis length, plus the squared length and inverse length the best-axis
   * selection reads. Only for inputs within SatInputsInLaneRange.
   */
  [[nodiscard]] bool ComputeSatOverlaps(const Wm3::Box3f& lhs, const Wm3::Box3f& rhs, SatAxisLanes& lanes) noexcept
  {
    const SatBoxLanes lhsLanes(lhs);
    const SatBoxLanes rhsLanes(rhs);

    alignas(16) float overlap[16];
    for (int base = 0; base < 16; base += 4) {
      const __m128 ax = _mm_load_ps(lanes.x + base);
      const __m128 ay = _mm_load_ps(lanes.y + base);
      const __m128 az = _mm_load_ps(lanes.z + base);

      __m128 lhsMin;
      __m128 lhsMax;
      __m128 rhsMin;
      __m128 rhsMax;
      ProjectBoxOntoAxes4(lhsLanes, ax, ay, az, lhsMin, lhsMax);
      ProjectBoxOntoAxes4(rhsLanes, ax, ay, az, rhsMin, rhsMax);

      const __m128 separated = _mm_or_ps(_mm_cmpgt_ps(lhsMin, rhsMax), _mm_cmpgt_ps(rhsMin, lhsMax));
      if (_mm_movemask_ps(separated) != 0) {
        return false;
      }

      // SatInputsInLaneRange keeps NaN out of the intervals, which is what the
      // four-lane CrtFminf / CrtFmaxf need.
      _mm_store_ps(overlap + base, _mm_sub_ps(CrtFminf4(lhsMax, rhsMax), CrtFmaxf4(lhsMin, rhsMin)));
    }

    // `LengthSq(axis)` as `(x*x + y*y) + z*z`, `1.0f / sqrt(lengthSq)` and
    // `overlap * inverse`: sqrtps and divps are correctly rounded per lane,
    // like the scalar sqrtss and divss.
    const __m128 one = _mm_set1_ps(1.0f);
    for (int base = 0; base < 16; base += 4) {
      const __m128 ax = _mm_load_ps(lanes.x + base);
      const __m128 ay = _mm_load_ps(lanes.y + base);
      const __m128 az = _mm_load_ps(lanes.z + base);
      const __m128 lengthSq = _mm_add_ps(_mm_add_ps(_mm_mul_ps(ax, ax), _mm_mul_ps(ay, ay)), _mm_mul_ps(az, az));
      const __m128 invLength = _mm_div_ps(one, _mm_sqrt_ps(lengthSq));
      _mm_store_ps(lanes.lengthSq + base, lengthSq);
      _mm_store_ps(lanes.invLength + base, invLength);
      _mm_store_ps(lanes.normalizedOverlap + base, _mm_mul_ps(_mm_load_ps(overlap + base), invLength));
    }
    return true;
  }

  /**
   * Address: 0x00474830 (FUN_00474830, box-vs-box SAT manifold helper)
   *
   * What it does:
   * Runs SAT overlap checks for two OBBs and returns witness points, penetration
   * normal, and depth for the best separating axis candidate.
   *
   * The recovered body, one axis at a time. ComputeBoxBoxContactManifold
   * below uses it for inputs the four-lane version does not take (see
   * SatInputsInLaneRange).
   */
  [[nodiscard]] bool ComputeBoxBoxContactManifoldScalar(
    const Wm3::Box3f& lhs, const Wm3::Box3f& rhs, BoxBoxContactManifold* outManifold
  ) noexcept
  {
    const Wm3::Vec3f lhsAxes[3] = {BoxAxis(lhs, 0), BoxAxis(lhs, 2), BoxAxis(lhs, 1)};
    const Wm3::Vec3f rhsAxes[3] = {BoxAxis(rhs, 0), BoxAxis(rhs, 2), BoxAxis(rhs, 1)};

    std::array<Wm3::Vec3f, 15> candidateAxes{};
    std::array<float, 15> overlapAlongAxis{};
    int axisCount = 0;
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      candidateAxes[axisCount++] = lhsAxes[axisIndex];
    }
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      candidateAxes[axisCount++] = rhsAxes[axisIndex];
    }
    for (int lhsAxisIndex = 0; lhsAxisIndex < 3; ++lhsAxisIndex) {
      for (int rhsAxisIndex = 0; rhsAxisIndex < 3; ++rhsAxisIndex) {
        candidateAxes[axisCount++] = Wm3::Vector3f::Cross(lhsAxes[lhsAxisIndex], rhsAxes[rhsAxisIndex]);
      }
    }

    for (int axisIndex = 0; axisIndex < axisCount; ++axisIndex) {
      float lhsMin = 0.0f;
      float lhsMax = 0.0f;
      float rhsMin = 0.0f;
      float rhsMax = 0.0f;
      ProjectBoxOntoAxis(lhs, candidateAxes[axisIndex], &lhsMin, &lhsMax);
      ProjectBoxOntoAxis(rhs, candidateAxes[axisIndex], &rhsMin, &rhsMax);

      if (lhsMin > rhsMax || rhsMin > lhsMax) {
        return false;
      }
      overlapAlongAxis[axisIndex] = CrtFminf(lhsMax, rhsMax) - CrtFmaxf(lhsMin, rhsMin);
    }

    float bestDepth = std::numeric_limits<float>::max();
    Wm3::Vec3f bestAxis{};
    int bestAxisIndex = -1;
    bool hasBestAxis = false;
    for (int axisIndex = 0; axisIndex < axisCount; ++axisIndex) {
      const Wm3::Vec3f axis = candidateAxes[axisIndex];
      const float axisLengthSq = Wm3::Vector3f::LengthSq(axis);
      if (axisLengthSq < kAxisLengthSqEpsilon) {
        continue;
      }

      const float invAxisLength = 1.0f / Wm3::SqrtfBinary(axisLengthSq);
      const float normalizedOverlap = overlapAlongAxis[axisIndex] * invAxisLength;
      if (!hasBestAxis || normalizedOverlap < bestDepth) {
        bestDepth = normalizedOverlap;
        bestAxis = axis * invAxisLength;
        bestAxisIndex = axisIndex;
        hasBestAxis = true;
      }
    }

    if (!hasBestAxis) {
      return false;
    }

    const Wm3::Vec3f centerDelta = BuildBoxCenter(rhs) - BuildBoxCenter(lhs);
    if (Wm3::Vector3f::Dot(centerDelta, bestAxis) > 0.0f) {
      bestAxis = bestAxis * -1.0f;
    }

    outManifold->penetrationNormal = bestAxis;
    outManifold->penetrationDepth = bestDepth;

    if (bestAxisIndex <= 2) {
      const Wm3::Vec3f supportOnA = ComputeSupportPointAgainstDirection(lhs, bestAxis * -1.0f);
      outManifold->pointOnB = supportOnA;
      outManifold->pointOnA = supportOnA - bestAxis * bestDepth;
      return true;
    }

    if (bestAxisIndex <= 5) {
      const Wm3::Vec3f supportOnA = ComputeSupportPointAgainstDirection(lhs, bestAxis);
      outManifold->pointOnA = supportOnA;
      outManifold->pointOnB = supportOnA + bestAxis * bestDepth;
      return true;
    }

    const int edgeAxisIndex = bestAxisIndex - 6;
    const int lhsEdgeIndex = edgeAxisIndex / 3;
    const int rhsEdgeIndex = edgeAxisIndex % 3;

    const Wm3::Vec3f supportOnA = ComputeSupportPointAgainstDirection(lhs, bestAxis);
    const Wm3::Vec3f supportOnB = ComputeSupportPointAgainstDirection(rhs, bestAxis * -1.0f);

    const Wm3::Vec3f lineAxisLhs = lhsAxes[lhsEdgeIndex];
    const Wm3::Vec3f lineAxisRhs = rhsAxes[rhsEdgeIndex];
    const Wm3::Vec3f crossRhsAndNormal = Wm3::Vector3f::Cross(lineAxisRhs, bestAxis);
    const float denominator = Wm3::Vector3f::Dot(lineAxisLhs, crossRhsAndNormal);
    if (std::fabs(denominator) < kAxisLengthSqEpsilon) {
      return false;
    }

    const float t =
      (Wm3::Vector3f::Dot(supportOnB, crossRhsAndNormal) - Wm3::Vector3f::Dot(supportOnA, crossRhsAndNormal)) /
      denominator;
    outManifold->pointOnA = supportOnA + lineAxisLhs * t;
    outManifold->pointOnB = outManifold->pointOnA + bestAxis * bestDepth;
    return true;
  }

  /**
   * True when every centre, axis and extent component of both boxes is finite
   * and at most 2^24 in magnitude. Then no product or sum in the SAT can
   * overflow, so no lane ever holds an infinity or a NaN that is read later
   * (a zero-length axis's lanes may, but the selection skips those axes),
   * and the recovered tail after the axis choice sees only finite values.
   * That matters because for NaN operands x86 returns whichever NaN the
   * instruction names first, and for commutative operations that order is the
   * compiler's choice; with no NaNs, packed and scalar arithmetic agree bit
   * for bit whatever the operand order. Game boxes are map-sized (the largest
   * extent is the 1000-unit height of the path sweeps).
   */
  [[nodiscard]] bool SatInputsInLaneRange(const Wm3::Box3f& lhs, const Wm3::Box3f& rhs) noexcept
  {
    static_assert(sizeof(Wm3::Box3f) == 15 * sizeof(float), "Box3f must be 15 packed floats");
    const __m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    const __m128 limit = _mm_set1_ps(16777216.0f);
    int inRange = 0xF;
    for (const Wm3::Box3f* box : {&lhs, &rhs}) {
      const float* const f = &box->Center.x;
      // Four loads cover the fifteen floats; the last one overlaps the third.
      for (const int offset : {0, 4, 8, 11}) {
        const __m128 value = _mm_and_ps(_mm_loadu_ps(f + offset), absMask);
        inRange &= _mm_movemask_ps(_mm_cmple_ps(value, limit)); // NaN compares false
      }
    }
    return inRange == 0xF;
  }

  /**
   * Address: 0x00474830 (FUN_00474830, box-vs-box SAT manifold helper)
   *
   * What it does:
   * Runs SAT overlap checks for two OBBs and returns witness points, penetration
   * normal, and depth for the best separating axis candidate.
   *
   * Engine change: the fifteen axis projections and axis normalisations run
   * four axes at a time (ComputeSatOverlaps, bit-identical per lane) for
   * inputs within SatInputsInLaneRange - every box the game builds - and the
   * rest is the recovered body. Other inputs take the recovered body as a
   * whole (ComputeBoxBoxContactManifoldScalar).
   */
  [[nodiscard]] bool ComputeBoxBoxContactManifold(
    const Wm3::Box3f& lhs, const Wm3::Box3f& rhs, BoxBoxContactManifold* outManifold
  ) noexcept
  {
    if (!SatInputsInLaneRange(lhs, rhs)) {
      return ComputeBoxBoxContactManifoldScalar(lhs, rhs, outManifold);
    }

    const Wm3::Vec3f lhsAxes[3] = {BoxAxis(lhs, 0), BoxAxis(lhs, 2), BoxAxis(lhs, 1)};
    const Wm3::Vec3f rhsAxes[3] = {BoxAxis(rhs, 0), BoxAxis(rhs, 2), BoxAxis(rhs, 1)};

    std::array<Wm3::Vec3f, 15> candidateAxes{};
    int axisCount = 0;
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      candidateAxes[axisCount++] = lhsAxes[axisIndex];
    }
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      candidateAxes[axisCount++] = rhsAxes[axisIndex];
    }
    for (int lhsAxisIndex = 0; lhsAxisIndex < 3; ++lhsAxisIndex) {
      for (int rhsAxisIndex = 0; rhsAxisIndex < 3; ++rhsAxisIndex) {
        candidateAxes[axisCount++] = Wm3::Vector3f::Cross(lhsAxes[lhsAxisIndex], rhsAxes[rhsAxisIndex]);
      }
    }

    SatAxisLanes lanes;
    for (int axisIndex = 0; axisIndex < axisCount; ++axisIndex) {
      lanes.x[axisIndex] = candidateAxes[axisIndex].x;
      lanes.y[axisIndex] = candidateAxes[axisIndex].y;
      lanes.z[axisIndex] = candidateAxes[axisIndex].z;
    }
    // Lane 15 repeats axis 0: its separation test adds nothing new and its
    // overlap is never read.
    lanes.x[15] = lanes.x[0];
    lanes.y[15] = lanes.y[0];
    lanes.z[15] = lanes.z[0];
    if (!ComputeSatOverlaps(lhs, rhs, lanes)) {
      return false;
    }

    float bestDepth = std::numeric_limits<float>::max();
    int bestAxisIndex = -1;
    bool hasBestAxis = false;
    for (int axisIndex = 0; axisIndex < axisCount; ++axisIndex) {
      if (lanes.lengthSq[axisIndex] < kAxisLengthSqEpsilon) {
        continue;
      }

      const float normalizedOverlap = lanes.normalizedOverlap[axisIndex];
      if (!hasBestAxis || normalizedOverlap < bestDepth) {
        bestDepth = normalizedOverlap;
        bestAxisIndex = axisIndex;
        hasBestAxis = true;
      }
    }

    if (!hasBestAxis) {
      return false;
    }
    Wm3::Vec3f bestAxis = candidateAxes[bestAxisIndex] * lanes.invLength[bestAxisIndex];

    const Wm3::Vec3f centerDelta = BuildBoxCenter(rhs) - BuildBoxCenter(lhs);
    if (Wm3::Vector3f::Dot(centerDelta, bestAxis) > 0.0f) {
      bestAxis = bestAxis * -1.0f;
    }

    outManifold->penetrationNormal = bestAxis;
    outManifold->penetrationDepth = bestDepth;

    if (bestAxisIndex <= 2) {
      const Wm3::Vec3f supportOnA = ComputeSupportPointAgainstDirection(lhs, bestAxis * -1.0f);
      outManifold->pointOnB = supportOnA;
      outManifold->pointOnA = supportOnA - bestAxis * bestDepth;
      return true;
    }

    if (bestAxisIndex <= 5) {
      const Wm3::Vec3f supportOnA = ComputeSupportPointAgainstDirection(lhs, bestAxis);
      outManifold->pointOnA = supportOnA;
      outManifold->pointOnB = supportOnA + bestAxis * bestDepth;
      return true;
    }

    const int edgeAxisIndex = bestAxisIndex - 6;
    const int lhsEdgeIndex = edgeAxisIndex / 3;
    const int rhsEdgeIndex = edgeAxisIndex % 3;

    const Wm3::Vec3f supportOnA = ComputeSupportPointAgainstDirection(lhs, bestAxis);
    const Wm3::Vec3f supportOnB = ComputeSupportPointAgainstDirection(rhs, bestAxis * -1.0f);

    const Wm3::Vec3f lineAxisLhs = lhsAxes[lhsEdgeIndex];
    const Wm3::Vec3f lineAxisRhs = rhsAxes[rhsEdgeIndex];
    const Wm3::Vec3f crossRhsAndNormal = Wm3::Vector3f::Cross(lineAxisRhs, bestAxis);
    const float denominator = Wm3::Vector3f::Dot(lineAxisLhs, crossRhsAndNormal);
    if (std::fabs(denominator) < kAxisLengthSqEpsilon) {
      return false;
    }

    const float t =
      (Wm3::Vector3f::Dot(supportOnB, crossRhsAndNormal) - Wm3::Vector3f::Dot(supportOnA, crossRhsAndNormal)) /
      denominator;
    outManifold->pointOnA = supportOnA + lineAxisLhs * t;
    outManifold->pointOnB = outManifold->pointOnA + bestAxis * bestDepth;
    return true;
  }
} // namespace

namespace moho
{
  /**
   * Address: 0x004FFDE0 (FUN_004FFDE0, Moho::CColPrimitiveBase::CColPrimitiveBase)
   *
   * What it does:
   * Initializes one collision-primitive base runtime lane.
   */
  CColPrimitiveBase::CColPrimitiveBase() = default;

  /**
   * Address: 0x0067AC40 (FUN_0067AC40, inlined construction payload)
   *
   * What it does:
   * Initializes box primitive state from local box and stores local-center copy.
   */
  CColPrimitive<Wm3::Box3f>::CColPrimitive(const Wm3::Box3f& localBox)
    : mShape(localBox)
    , mLocalCenter(localBox.Center[0], localBox.Center[1], localBox.Center[2])
  {}

  /**
   * Address: 0x004FFC20 (FUN_004FFC20, Moho::CColPrimitive_Box::GetBoundingBox)
   *
   * IDA signature:
   * int __thiscall sub_4FFC20(char* this, int scratchOut);
   *
   * What it does:
   * Computes the world-space AABB from center/basis/extents.
   */
  Wm3::AxisAlignedBox3f CColPrimitive<Wm3::Box3f>::GetBoundingBox() const
  {
    Wm3::Vec3f minimum{};
    Wm3::Vec3f maximum{};
    mShape.ComputeAABB(minimum, maximum);
    return {minimum, maximum};
  }

  /**
   * Address: 0x004FF130 (FUN_004FF130, Moho::CColPrimitive_Box::GetSphere)
   *
   * What it does:
   * Box primitive has no sphere view and returns null.
   */
  const Wm3::Sphere3f* CColPrimitive<Wm3::Box3f>::GetSphere() const
  {
    return nullptr;
  }

  /**
   * Address: 0x004FF140 (FUN_004FF140, Moho::CColPrimitive_Box::GetBox)
   *
   * What it does:
   * Returns pointer to box payload at +0x04.
   */
  const Wm3::Box3f* CColPrimitive<Wm3::Box3f>::GetBox() const
  {
    return &mShape;
  }

  /**
   * Address: 0x004FF470 (FUN_004FF470, Moho::CColPrimitive_Box::SetTransform)
   *
   * IDA signature:
   * int __thiscall sub_100FF470(int this, float* transformPayload);
   *
   * What it does:
   * Rotates the local center by the orientation (`MultQuadVec`, 0x00452D40),
   * adds the world position, and takes the box axes from
   * `VAxes3::VAxes3(orient)` (0x004EC590), exactly the two calls the binary
   * makes; they used to be open-coded here as a private basis build.
   */
  void CColPrimitive<Wm3::Box3f>::SetTransform(const VTransform& transform)
  {
    Wm3::Vec3f rotatedCenter{};
    MultQuadVec(&rotatedCenter, &mLocalCenter, &transform.orient_);
    mShape.Center.x = rotatedCenter.x + transform.pos_.x;
    mShape.Center.y = transform.pos_.y + rotatedCenter.y;
    mShape.Center.z = transform.pos_.z + rotatedCenter.z;

    const VAxes3 axes(transform.orient_);
    mShape.Axis[0] = axes.vX;
    mShape.Axis[1] = axes.vY;
    mShape.Axis[2] = axes.vZ;
  }

  /**
   * Address: 0x004FFBE0 (FUN_004FFBE0, Moho::CColPrimitive_Box::GetCenter)
   *
   * What it does:
   * Writes the primitive's LOCAL center to caller output.
   *
   * 0x004FFBE4/E9/EF read `[ecx+40h]`, `[ecx+44h]`, `[ecx+48h]` -- that is
   * `mLocalCenter`, not `mShape.Center`. `SetTransform` (0x004FF470) settles
   * which is which: it rotates `[edi+40h]` through `MultQuadVec`
   * (`lea esi, [edi+40h]` at 0x004FF47D), adds the world position, and stores
   * the result into `[edi+4]`/`[edi+8]`/`[edi+0Ch]` -- `mShape.Center`. So
   * `mShape` is the world-space volume the grid and collision queries use, and
   * `mLocalCenter` is the untransformed offset this accessor exposes.
   *
   * Returning the world centre here put `Entity::GetTerrainCollisionGeom`'s
   * corner points in world space, and `CUnitMotion::HandleGroundCollision`
   * (0x006BC460) multiplies them by the body's world matrix again -- landing
   * every ground-contact sample at roughly twice the unit's world position.
   * `SPhysBody::ApplyGroundCollisionResponse` takes `relative = sample - mPos`
   * as a lever arm, so a unit 4.5 units long got an arm of several hundred,
   * and the angular impulse it accumulated grew by four orders of magnitude in
   * a single contact instead of damping.
   */
  Wm3::Vec3f* CColPrimitive<Wm3::Box3f>::GetCenter(Wm3::Vec3f* outCenter) const
  {
    *outCenter = mLocalCenter;
    return outCenter;
  }

  /**
   * Address: 0x004FFC00 (FUN_004FFC00, Moho::CColPrimitive_Box::SetCenter)
   *
   * What it does:
   * Copies caller center into the primitive's LOCAL center.
   *
   * 0x004FFC06/0C/12 store to `[ecx+40h]`, `[ecx+44h]`, `[ecx+48h]` --
   * `mLocalCenter`, the same lane `GetCenter` reads. The world-space
   * `mShape.Center` is derived from it by `SetTransform`, never written here.
   */
  const Wm3::Vec3f* CColPrimitive<Wm3::Box3f>::SetCenter(const Wm3::Vec3f* center)
  {
    mLocalCenter = *center;
    return center;
  }

  /**
   * Address: 0x004FF2D0 (FUN_004FF2D0, Moho::CColPrimitive_Box::CollideLine)
   *
   * What it does:
   * Tests segment-vs-box and fills first hit point, separation direction, and distance from line start.
   *
   * The query is Wild Magic's own `IntrSegment3Box3f` on the stack (ctor
   * 0x00A463A0 with `bSolid = false`, `Find` 0x00A462A0, `GetPoint(0)`
   * 0x00A46300 once per output, `~Intersector` 0x00A45500). The outputs are
   * written direction, distance, position, in that order.
   */
  bool CColPrimitive<Wm3::Box3f>::CollideLine(
    const Wm3::Vec3f* lineStart, const Wm3::Vec3f* lineEnd, CollisionSegmentResult* outResult
  ) const
  {
    const Wm3::Segment3f segment = Wm3::MakeSegment3fFromEndpoints(*lineStart, *lineEnd);
    Wm3::IntrSegment3Box3f intersection(segment, mShape, false);
    if (!intersection.Find()) {
      return false;
    }

    Wm3::Vec3f direction{};
    Wm3::Vector3f::NormalizeInto(BuildBoxCenter(mShape) - intersection.GetPoint(0), &direction);
    outResult->direction = direction;
    outResult->distanceFromLineStart =
      Wm3::SqrtfBinary(Wm3::Vector3f::LengthSq(intersection.GetPoint(0) - *lineStart));
    outResult->position = intersection.GetPoint(0);
    return true;
  }

  /**
   * Address: 0x004FF260 (FUN_004FF260, Moho::CColPrimitive_Box::CollideBox)
   *
   * What it does:
   * Runs OBB-vs-OBB SAT and returns minimum penetration axis/depth.
   */
  bool CColPrimitive<Wm3::Box3f>::CollideBox(const Wm3::Box3f* box, CollisionResult* outResult) const
  {
    BoxBoxContactManifold manifold{};
    if (!ComputeBoxBoxContactManifold(mShape, *box, &manifold)) {
      return false;
    }

    outResult->direction = manifold.penetrationNormal;
    outResult->penetrationDepth = manifold.penetrationDepth;
    return true;
  }

  /**
   * Address: 0x004FF150 (FUN_004FF150, Moho::CColPrimitive_Box::CollideSphere)
   *
   * What it does:
   * Tests sphere-vs-box overlap and fills penetration direction/depth.
   *
   * Wild Magic's `DistVector3Box3f` on the stack: ctor 0x00A45730,
   * `GetSquared` 0x00A45C00, `~Distance` 0x00A38C40.
   */
  bool CColPrimitive<Wm3::Box3f>::CollideSphere(const Wm3::Sphere3f* sphere, CollisionResult* outResult) const
  {
    Wm3::DistVector3Box3f distance(sphere->Center, mShape);
    const float squaredDistance = distance.GetSquared();
    if (sphere->Radius * sphere->Radius <= squaredDistance) {
      return false;
    }

    const Wm3::Vec3f center = BuildBoxCenter(mShape);
    const Wm3::Vec3f sphereToBox = sphere->Center - center;
    Wm3::Vec3f direction{};
    Wm3::Vector3f::NormalizeInto(sphereToBox, &direction);

    outResult->direction = direction;
    outResult->penetrationDepth = sphere->Radius - Wm3::SqrtfBinary(squaredDistance);
    return true;
  }

  /**
   * Address: 0x004FF450 (FUN_004FF450, Moho::CColPrimitive_Box::PointInShape)
   *
   * What it does:
   * Returns true when point lies inside oriented box extents.
   *
   * Calls Wild Magic's `InBox<float>` (0x00A3BC10, Wm3ContBox3.cpp) with the
   * point and `mShape`.
   */
  bool CColPrimitive<Wm3::Box3f>::PointInShape(const Wm3::Vec3f* point) const
  {
    return Wm3::InBox(*point, mShape);
  }

  /**
   * Address: 0x0067AD30 (FUN_0067AD30, inlined construction payload)
   *
   * What it does:
   * Initializes sphere primitive state from local center/radius.
   */
  CColPrimitive<Wm3::Sphere3f>::CColPrimitive(const Wm3::Vec3f& localCenter, const float radius)
    : mShape(localCenter, radius)
    , mLocalCenter(localCenter)
  {}

  /**
   * Address: 0x004FF9A0 (FUN_004FF9A0, Moho::CColPrimitive_Sphere::GetBoundingBox)
   *
   * IDA signature:
   * float *__thiscall sub_4FF9A0(float *this, float *a2);
   *
   * What it does:
   * Returns the axis-aligned bounds of `{center, radius}`.
   */
  Wm3::AxisAlignedBox3f CColPrimitive<Wm3::Sphere3f>::GetBoundingBox() const
  {
    const float radius = mShape.Radius;
    return {
      Wm3::Vec3f{mShape.Center.x - radius, mShape.Center.y - radius, mShape.Center.z - radius},
      Wm3::Vec3f{mShape.Center.x + radius, mShape.Center.y + radius, mShape.Center.z + radius},
    };
  }

  /**
   * Address: 0x004FE780 (FUN_004FE780, Moho::CColPrimitive_Sphere::GetSphere)
   *
   * What it does:
   * Returns pointer to sphere payload at +0x04.
   */
  const Wm3::Sphere3f* CColPrimitive<Wm3::Sphere3f>::GetSphere() const
  {
    return &mShape;
  }

  /**
   * Address: 0x004FE790 (FUN_004FE790, Moho::CColPrimitive_Sphere::GetBox)
   *
   * What it does:
   * Sphere primitive has no box view and returns null.
   */
  const Wm3::Box3f* CColPrimitive<Wm3::Sphere3f>::GetBox() const
  {
    return nullptr;
  }

  /**
   * Address: 0x004FEBC0 (FUN_004FEBC0, Moho::CColPrimitive_Sphere::SetTransform)
   *
   * IDA signature:
   * int __thiscall sub_4FEBC0(float *this, float *transformPayload);
   *
   * What it does:
   * Rotates the local center by the orientation (`MultQuadVec`, 0x00452D40)
   * and adds the world position.
   */
  void CColPrimitive<Wm3::Sphere3f>::SetTransform(const VTransform& transform)
  {
    Wm3::Vec3f rotatedCenter{};
    MultQuadVec(&rotatedCenter, &mLocalCenter, &transform.orient_);
    mShape.Center.x = transform.pos_.x + rotatedCenter.x;
    mShape.Center.y = transform.pos_.y + rotatedCenter.y;
    mShape.Center.z = transform.pos_.z + rotatedCenter.z;
  }

  /**
   * Address: 0x004FF960 (FUN_004FF960, Moho::CColPrimitive_Sphere::GetCenter)
   *
   * What it does:
   * Writes the primitive's LOCAL center to caller output.
   *
   * 0x004FF964/69/6F read `[ecx+14h]`, `[ecx+18h]`, `[ecx+1Ch]` --
   * `mLocalCenter`. `mShape` (the world-space sphere) sits at +0x04 and is
   * written by `SetTransform`; see the box primitive's `GetCenter` for the
   * full evidence and for what reading the world lane here costs.
   */
  Wm3::Vec3f* CColPrimitive<Wm3::Sphere3f>::GetCenter(Wm3::Vec3f* outCenter) const
  {
    *outCenter = mLocalCenter;
    return outCenter;
  }

  /**
   * Address: 0x004FF980 (FUN_004FF980, Moho::CColPrimitive_Sphere::SetCenter)
   *
   * What it does:
   * Copies caller center into the primitive's LOCAL center.
   *
   * 0x004FF986/8C/92 store to `[ecx+14h]`, `[ecx+18h]`, `[ecx+1Ch]` --
   * `mLocalCenter`, the same lane `GetCenter` reads.
   */
  const Wm3::Vec3f* CColPrimitive<Wm3::Sphere3f>::SetCenter(const Wm3::Vec3f* center)
  {
    mLocalCenter = *center;
    return center;
  }

  /**
   * Address: 0x004FE9D0 (FUN_004FE9D0, Moho::CColPrimitive_Sphere::CollideLine)
   *
   * What it does:
   * Tests segment-vs-sphere and fills first hit point, separation direction, and distance from line start.
   *
   * Wild Magic's `IntrSegment3Sphere3f` on the stack (ctor 0x00A46A10,
   * `Find` 0x00A471C0, `GetPoint(0)` 0x00A46940 once per output), the same
   * shape as the box primitive's line test.
   */
  bool CColPrimitive<Wm3::Sphere3f>::CollideLine(
    const Wm3::Vec3f* lineStart, const Wm3::Vec3f* lineEnd, CollisionSegmentResult* outResult
  ) const
  {
    const Wm3::Segment3f segment = Wm3::MakeSegment3fFromEndpoints(*lineStart, *lineEnd);
    Wm3::IntrSegment3Sphere3f intersection(segment, mShape);
    if (!intersection.Find()) {
      return false;
    }

    Wm3::Vec3f direction{};
    Wm3::Vector3f::NormalizeInto(mShape.Center - intersection.GetPoint(0), &direction);
    outResult->direction = direction;
    outResult->distanceFromLineStart =
      Wm3::SqrtfBinary(Wm3::Vector3f::LengthSq(intersection.GetPoint(0) - *lineStart));
    outResult->position = intersection.GetPoint(0);
    return true;
  }

  /**
   * Address: 0x004FE860 (FUN_004FE860, Moho::CColPrimitive_Sphere::CollideBox)
   *
   * What it does:
   * Tests box-vs-sphere overlap and fills penetration direction/depth.
   *
   * One Wild Magic `IntrBox3Sphere3f` (ctor 0x00A41530) answers both the
   * static `Test` (0x00A41560) and the swept `Find` (0x00A43420), whose
   * contact point (0x00A414C0) gives the depth. `Find`'s time limit is this
   * translation unit's copy of `gpg::pInf` (0x010A9C04, set from `_FInf`
   * by 0x00BC74B0), not `FLT_MAX`, and both velocities are Wild Magic's
   * `Vector3<float>::ZERO` (0x00F3D21C).
   */
  bool CColPrimitive<Wm3::Sphere3f>::CollideBox(const Wm3::Box3f* box, CollisionResult* outResult) const
  {
    Wm3::IntrBox3Sphere3f intersection(*box, mShape);
    if (!intersection.Test()) {
      return false;
    }

    const Wm3::Vec3f boxCenter{box->Center[0], box->Center[1], box->Center[2]};
    const Wm3::Vec3f boxToSphere = boxCenter - mShape.Center;
    Wm3::Vec3f direction{};
    Wm3::Vector3f::NormalizeInto(boxToSphere, &direction);
    outResult->direction = direction;

    if (intersection.Find(gpg::pInf, Wm3::Vector3f::ZERO, Wm3::Vector3f::ZERO)) {
      const Wm3::Vec3f sphereToContact = intersection.GetContactPoint() - mShape.Center;
      outResult->penetrationDepth = mShape.Radius - Wm3::SqrtfBinary(Wm3::Vector3f::LengthSq(sphereToContact));
    }
    return true;
  }

  /**
   * Address: 0x004FE7A0 (FUN_004FE7A0, Moho::CColPrimitive_Sphere::CollideSphere)
   *
   * What it does:
   * Tests sphere-vs-sphere overlap and fills penetration direction/depth.
   */
  bool CColPrimitive<Wm3::Sphere3f>::CollideSphere(const Wm3::Sphere3f* sphere, CollisionResult* outResult) const
  {
    const Wm3::Vec3f delta = sphere->Center - mShape.Center;
    const float combinedRadius = sphere->Radius + mShape.Radius;
    const float squaredDistance = Wm3::Vector3f::LengthSq(delta);
    if (combinedRadius * combinedRadius <= squaredDistance) {
      return false;
    }

    Wm3::Vec3f direction{};
    Wm3::Vector3f::NormalizeInto(delta, &direction);
    outResult->direction = direction;
    outResult->penetrationDepth = combinedRadius - Wm3::SqrtfBinary(squaredDistance);
    return true;
  }

  /**
   * Address: 0x004FEB60 (FUN_004FEB60, Moho::CColPrimitive_Sphere::PointInShape)
   *
   * What it does:
   * Returns true when point lies strictly inside sphere.
   */
  bool CColPrimitive<Wm3::Sphere3f>::PointInShape(const Wm3::Vec3f* point) const
  {
    const Wm3::Vec3f delta = *point - mShape.Center;
    return mShape.Radius * mShape.Radius > Wm3::Vector3f::LengthSq(delta);
  }

  /**
   * Address: 0x00676A40 (FUN_00676A40, Moho::CColPrimitiveBase::Collide)
   * Mangled: ?Collide@CColPrimitiveBase@Moho@@QAE_NPAVCColPrimitiveBase@2@PAUCollisionResult@2@@Z
   *
   * What it does:
   * Dispatches shape-vs-shape collision by querying `with` shape type (box vs
   * sphere), then calling the matching `CollideBox` or `CollideSphere` virtual
   * on `this`.  Asserts unreachable if `with` has neither shape.
   */
  bool CColPrimitiveBase::Collide(
    const CColPrimitiveBase* with,
    CollisionResult* outResult
  ) const
  {
    if (const Wm3::Box3f* box = with->GetBox()) {
      return CollideBox(box, outResult);
    }
    const Wm3::Sphere3f* sphere = with->GetSphere();
    if (!sphere) {
      gpg::HandleAssertFailure(
        "Reached the supposably unreachable.",
        94,
        "c:\\work\\rts\\main\\code\\src\\core/ColMain.h"
      );
    }
    return CollideSphere(sphere, outResult);
  }
} // namespace moho
