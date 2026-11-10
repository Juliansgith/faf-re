#include "CSimResources.h"

#include "legacy/math/X87Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <limits>
#include <new>



namespace
{
  // Engine addition (performance): DepositIsInArea's bucket index of the deposit rects, one per process
  // (one sim at a time), rebuilt when the deposits change (AddDeposit, or another list in their place).
  constexpr int kDepositBucketShift = 4;
  constexpr std::int64_t kDepositWideBuckets = 64;

  struct DepositIndex
  {
    const void* owner = nullptr;
    const void* data = nullptr;
    std::size_t size = 0;
    std::uint64_t generation = 0;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> buckets;
    std::vector<std::uint32_t> wide; // deposits spanning more than kDepositWideBuckets buckets: always tested
  };
  DepositIndex sDepositIndex;
  std::uint64_t sDepositGeneration = 1;

  [[nodiscard]] std::uint32_t DepositBucketKey(const int bx, const int bz) noexcept
  {
    return (static_cast<std::uint32_t>(bx) << 16) ^ (static_cast<std::uint32_t>(bz) & 0xFFFFu);
  }

  void NoteDepositsChanged() noexcept
  {
    ++sDepositGeneration;
  }
} // namespace

namespace
{
  /**
   * Shared inlined corner conversion of 0x00545E80 / 0x00545FD0 / 0x005465C0:
   * `fld` + `fistp` (round-to-nearest under the caller's x87 control word, not
   * truncation) into a 16-bit cell lane (`movsx reg, word ptr`). A Mass marker
   * at x = 875.4999 lands its 1x1 deposit on cell 875, which truncation put on
   * cell 874 -- and an extractor on cell 875 then failed DepositIsInArea.
   */
  [[nodiscard]] std::int16_t DepositCornerCell(const float centre, const int extent) noexcept
  {
    return static_cast<std::int16_t>(fa::RoundToInt(centre - static_cast<float>(extent) * 0.5f));
  }

  [[nodiscard]] gpg::Rect2i BuildRectFromPointAndSize(const Wm3::Vec3f& point, const Wm3::Vec2i& size) noexcept
  {
    const int x0 = DepositCornerCell(point.x, size.x);
    const int z0 = DepositCornerCell(point.z, size.y);
    return {x0, z0, x0 + size.x, z0 + size.y};
  }

  [[nodiscard]] bool HasPositiveArea(const gpg::Rect2i& rect) noexcept
  {
    return rect.x0 < rect.x1 && rect.z0 < rect.z1;
  }

  [[nodiscard]] bool HasPositiveArea(const gpg::Rect2f& rect) noexcept
  {
    return rect.x0 < rect.x1 && rect.z0 < rect.z1;
  }

  [[nodiscard]] bool RectsOverlapStrict(const gpg::Rect2i& lhs, const gpg::Rect2i& rhs) noexcept
  {
    return lhs.x0 < rhs.x1 && rhs.x0 < lhs.x1 && lhs.z0 < rhs.z1 && rhs.z0 < lhs.z1 && HasPositiveArea(lhs) &&
      HasPositiveArea(rhs);
  }
} // namespace

namespace moho
{
  gpg::RType* CSimResources::sType = nullptr;

  /**
   * Address: 0x00545DC0 (FUN_00545DC0, ??0CSimResources@Moho@@QAE@@Z)
   *
   * What it does:
   * Constructs mutex/vector members for resource deposit bookkeeping.
   */
  CSimResources::CSimResources() = default;

  /**
   * Address: 0x00546E90 (FUN_00546E90, Moho::CSimResources::operator new)
   *
   * What it does:
   * Allocates and constructs one `CSimResources` instance.
   */
  CSimResources* CSimResources::Create()
  {
    void* const storage = ::operator new(sizeof(CSimResources), std::nothrow);
    if (storage == nullptr) {
      return nullptr;
    }

    try {
      return ::new (storage) CSimResources();
    } catch (...) {
      ::operator delete(storage);
      throw;
    }
  }

  /**
   * Address: 0x00545E10 (FUN_00545E10, core dtor body)
   * Address: 0x00546A00 (FUN_00546A00, Moho::CSimResources::dtr)
   *
   * What it does:
   * Destructs mutex/vector members (deleting-thunk behavior is compiler
   * generated).
   */
  CSimResources::~CSimResources() = default;

  /**
   * Address: 0x00545F10 (FUN_00545F10, Moho::CSimResources::AddDeposit)
   *
   * What it does:
   * Adds one deposit rectangle and type under the resource lock.
   */
  void CSimResources::AddDeposit(const EDepositType type, gpg::Rect2i* const pos)
  {
    boost::mutex::scoped_lock lock(lock_);
    NoteDepositsChanged();

    ResourceDeposit deposit{};
    deposit.footprintRect = *pos;
    deposit.depositType = type;
    deposits_.push_back(deposit);
  }

  /**
   * Address: 0x00545E80 (FUN_00545E80, Moho::CSimResources::AddDepositPoint)
   *
   * What it does:
   * Converts center+size to grid rect and forwards to AddDeposit.
   */
  void CSimResources::AddDepositPoint(EDepositType type, Wm3::Vec3f* pos, Wm3::Vec2i* size)
  {
    gpg::Rect2i rect = BuildRectFromPointAndSize(*pos, *size);
    AddDeposit(type, &rect);
  }

  /**
   * Address: 0x00545FC0 (FUN_00545FC0, Moho::CSimResources::GetDeposits1)
   */
  const fa::vector<ResourceDeposit>& CSimResources::GetDeposits() const
  {
    return deposits_;
  }

  /**
   * Address: 0x00545FB0 (FUN_00545FB0, Moho::CSimResources::GetDeposits2)
   */
  fa::vector<ResourceDeposit>& CSimResources::GetDeposits()
  {
    return deposits_;
  }

  /**
   * Address: 0x00546060 (FUN_00546060, Moho::CSimResources::IsDepositAt)
   *
   * What it does:
   * Tests strict rectangle overlap against deposits of the requested type.
   */
  bool CSimResources::IsDepositAt(gpg::Rect2i* pos, EDepositType type)
  {
    boost::mutex::scoped_lock lock(lock_);
    for (const ResourceDeposit& deposit : deposits_) {
      if (deposit.depositType == type && RectsOverlapStrict(*pos, deposit.footprintRect)) {
        return true;
      }
    }
    return false;
  }

  /**
   * Address: 0x00545FD0 (FUN_00545FD0, Moho::CSimResources::IsDepositAtPoint)
   *
   * What it does:
   * Converts center+size to grid rect and reuses IsDepositAt.
   */
  bool CSimResources::IsDepositAtPoint(Wm3::Vec3f* pos, Wm3::Vec2i* size, EDepositType type)
  {
    gpg::Rect2i rect = BuildRectFromPointAndSize(*pos, *size);
    return IsDepositAt(&rect, type);
  }

  /**
   * Address: 0x00546470 (FUN_00546470, Moho::CSimResources::DepositCollides)
   *
   * What it does:
   * Emits deposits whose terrain AABB intersects the provided solid, optionally
   * filtering by deposit type.
   */
  void CSimResources::DepositCollides(
    CGeomSolid3* solid, CHeightField* field, gpg::fastvector<ResourceDeposit>* outDeposits, EDepositType type
  )
  {
    for (const ResourceDeposit& deposit : deposits_) {
      if (type != kNone && deposit.depositType != type) {
        continue;
      }
      if (deposit.Intersects(*solid, *field)) {
        outDeposits->PushBack(deposit);
      }
    }
  }

  /**
   * Address: 0x00546650 (FUN_00546650, Moho::CSimResources::DepositIsInArea)
   *
   * What it does:
   * Returns true when the query area and one deposit of the requested type
   * are in a containment relation - either the area encloses the deposit or
   * the deposit encloses the area. This is the gate `OCCUPY_Check` runs for
   * `RULEUBR_OnMassDeposit` / `RULEUBR_OnHydrocarbonDeposit`, i.e. the only
   * thing that keeps a mass extractor on its mass point.
   *
   * The shipped body picks which way round to test from the two `x0`s
   * (0x005466BC `cmp esi, edx` / `jg`), and the two arms are exact mirrors of
   * each other:
   *
   *   area.x0 <= deposit.x0  ->  area encloses deposit
   *       0x005466C0  area.x1 >= deposit.x1
   *       0x005466C8  deposit.z0 >= area.z0
   *       0x005466D0  area.z1 >= deposit.z1
   *   area.x0 >= deposit.x0  ->  deposit encloses area
   *       0x005466DC  deposit.x1 >= area.x1
   *       0x005466E4  area.z0 >= deposit.z0
   *       0x005466EC  deposit.z1 >= area.z1
   *
   * When the first arm fails, 0x005466D8 re-tests `area.x0 < deposit.x0` and
   * only advances to the next deposit on a *strict* less-than; equal `x0`s
   * fall through into the second arm, so a deposit is given both tests.
   *
   * Both arms had previously been recovered with the *same* comparison - the
   * enclosing arm carried the enclosed arm's three tests. For the 1x1 rects
   * this function is actually called with (a mass point and a mass
   * extractor's footprint), that degenerates to "area.x0 <= deposit.x0 &&
   * area.z0 == deposit.z0", i.e. a mass extractor could be placed on any cell
   * in the same Z row at or west of any mass point on the map.
   */
  namespace
  {
    /// The index of `resources`' deposits, rebuilt first when they changed; the caller holds the lock.
    [[nodiscard]] const DepositIndex& DepositIndexFor(const CSimResources& resources)
    {
      const fa::vector<ResourceDeposit>& deposits = resources.deposits_;
      DepositIndex& index = sDepositIndex;
      const void* const data = deposits.empty() ? nullptr : &*deposits.begin();
      if (index.owner == &resources && index.data == data && index.size == deposits.size() &&
          index.generation == sDepositGeneration) {
        return index;
      }
      index.owner = &resources;
      index.data = data;
      index.size = deposits.size();
      index.generation = sDepositGeneration;
      index.buckets.clear();
      index.wide.clear();
      std::uint32_t depositIndex = 0;
      for (const ResourceDeposit& deposit : deposits) {
        const gpg::Rect2i& rect = deposit.footprintRect;
        const int bx0 = std::min(rect.x0, rect.x1) >> kDepositBucketShift;
        const int bx1 = std::max(rect.x0, rect.x1) >> kDepositBucketShift;
        const int bz0 = std::min(rect.z0, rect.z1) >> kDepositBucketShift;
        const int bz1 = std::max(rect.z0, rect.z1) >> kDepositBucketShift;
        if (static_cast<std::int64_t>(bx1 - bx0 + 1) * (bz1 - bz0 + 1) > kDepositWideBuckets) {
          index.wide.push_back(depositIndex++);
          continue;
        }
        for (int bx = bx0; bx <= bx1; ++bx) {
          for (int bz = bz0; bz <= bz1; ++bz) {
            index.buckets[DepositBucketKey(bx, bz)].push_back(depositIndex);
          }
        }
        ++depositIndex;
      }
      return index;
    }
  } // namespace

  bool CSimResources::DepositIsInArea(EDepositType type, gpg::Rect2i* area)
  {
    boost::mutex::scoped_lock lock(lock_);
    const auto contains = [type, area](const ResourceDeposit& deposit) {
      if (deposit.depositType != type) {
        return false;
      }

      const gpg::Rect2i& rect = deposit.footprintRect;
      if (area->x0 <= rect.x0) {
        if (area->x1 >= rect.x1 && rect.z0 >= area->z0 && area->z1 >= rect.z1) {
          return true;
        }
        if (area->x0 < rect.x0) {
          return false;
        }
      }

      return rect.x1 >= area->x1 && area->z0 >= rect.z0 && rect.z1 >= area->z1;
    };

    // Engine addition (performance): either arm of the test needs the deposit's x and z spans to overlap
    // the area's, so only the deposits indexed under the buckets the area spans can answer (OCCUPY_Check
    // asks this for every placement an AI tries, and the scan went over every deposit on the map).
    const DepositIndex& index = DepositIndexFor(*this);
    const int bx0 = std::min(area->x0, area->x1) >> kDepositBucketShift;
    const int bx1 = std::max(area->x0, area->x1) >> kDepositBucketShift;
    const int bz0 = std::min(area->z0, area->z1) >> kDepositBucketShift;
    const int bz1 = std::max(area->z0, area->z1) >> kDepositBucketShift;
    if (static_cast<std::int64_t>(bx1 - bx0 + 1) * (bz1 - bz0 + 1) > static_cast<std::int64_t>(deposits_.size())) {
      for (const ResourceDeposit& deposit : deposits_) {
        if (contains(deposit)) {
          return true;
        }
      }
      return false;
    }
    for (const std::uint32_t depositIndex : index.wide) {
      if (contains(deposits_[depositIndex])) {
        return true;
      }
    }
    for (int bx = bx0; bx <= bx1; ++bx) {
      for (int bz = bz0; bz <= bz1; ++bz) {
        const auto bucket = index.buckets.find(DepositBucketKey(bx, bz));
        if (bucket == index.buckets.end()) {
          continue;
        }
        for (const std::uint32_t depositIndex : bucket->second) {
          if (contains(deposits_[depositIndex])) {
            return true;
          }
        }
      }
    }
    return false;
  }

  /**
   * Address: 0x005465C0 (FUN_005465C0, Moho::CSimResources::DepositIsInAreaPoint)
   *
   * What it does:
   * Converts center+size to grid rect and reuses DepositIsInArea.
   */
  bool CSimResources::DepositIsInAreaPoint(EDepositType type, Wm3::Vec3f* pos, Wm3::Vec2i* size)
  {
    gpg::Rect2i rect = BuildRectFromPointAndSize(*pos, *size);
    return DepositIsInArea(type, &rect);
  }

  /**
   * Address: 0x00546760 (FUN_00546760, Moho::CSimResources::FindClosestDespoit)
   *
   * What it does:
   * Finds the closest deposit center of matching type within radius.
   */
  bool CSimResources::FindClosestDeposit(GridPos* from, GridPos* outPos, float radius, EDepositType type)
  {
    float closestDistance = (std::numeric_limits<float>::max)();
    int closestX = 0;
    int closestZ = 0;
    bool found = false;

    for (const ResourceDeposit& deposit : deposits_) {
      if (deposit.depositType != type) {
        continue;
      }

      const int centerX = deposit.footprintRect.x0 + ((deposit.footprintRect.x1 - deposit.footprintRect.x0) >> 1);
      const int centerZ = deposit.footprintRect.z0 + (deposit.footprintRect.z1 - deposit.footprintRect.z0) / 2;

      const float dx = static_cast<float>(from->x - centerX);
      const float dz = static_cast<float>(from->z - centerZ);
      const float distance = std::sqrt(dx * dx + dz * dz);

      if (distance < closestDistance && distance <= radius) {
        closestDistance = distance;
        closestX = centerX;
        closestZ = centerZ;
        found = true;
      }
    }

    if (found) {
      outPos->x = closestX;
      outPos->z = closestZ;
    }
    return found;
  }

  /**
   * Address: 0x00546860 (FUN_00546860, Moho::CSimResources::AreaHasDeposit)
   *
   * What it does:
   * Checks area/deposit overlap with type-specific footprint expansion.
   */
  bool CSimResources::AreaHasDeposit(EDepositType type, gpg::Rect2f* area)
  {
    boost::mutex::scoped_lock lock(lock_);
    for (const ResourceDeposit& deposit : deposits_) {
      if (deposit.depositType != type) {
        continue;
      }

      float minX = 0.0f;
      float minZ = 0.0f;
      float maxX = 0.0f;
      float maxZ = 0.0f;
      if (type == kMass) {
        minX = static_cast<float>(deposit.footprintRect.x0) - 0.5f;
        minZ = static_cast<float>(deposit.footprintRect.z0) - 0.5f;
        maxX = static_cast<float>(deposit.footprintRect.x1) + 0.5f;
        maxZ = static_cast<float>(deposit.footprintRect.z1) + 0.5f;
      } else if (type == kHydrocarbon) {
        minX = static_cast<float>(deposit.footprintRect.x0) - 1.5f;
        minZ = static_cast<float>(deposit.footprintRect.z0) - 1.5f;
        maxX = static_cast<float>(deposit.footprintRect.x1) + 1.5f;
        maxZ = static_cast<float>(deposit.footprintRect.z1) + 1.5f;
      }

      const bool overlaps = maxX > area->x0 && area->x1 > minX && maxZ > area->z0 && area->z1 > minZ;
      if (overlaps && HasPositiveArea(*area) && maxX > minX && minZ < maxZ) {
        return true;
      }
    }
    return false;
  }

  /**
   * Source parity: FAF binary patch helper `SimGetDepositsAroundPoint`.
   *
   * What it does:
   * Enumerates deposit centers around a query point and emits matching deposits
   * with Euclidean center distance.
   */
  void CSimResources::GetDepositsAroundPoint(
    const float x,
    const float z,
    float radius,
    const EDepositType type,
    gpg::fastvector<ResourceDepositDistance>* const outDeposits
  ) const
  {
    if (!outDeposits) {
      return;
    }

    outDeposits->Clear();

    // FAF patch parity (`SimGetDepositsAroundPoint`): early-out only for NaN.
    const float nanGuard = x + z + radius;
    if (nanGuard != nanGuard) {
      return;
    }

    radius *= radius;

    for (const ResourceDeposit& deposit : deposits_) {
      if (type != kNone && type != deposit.depositType) {
        continue;
      }

      const float centerX =
        (static_cast<float>(deposit.footprintRect.x0) + static_cast<float>(deposit.footprintRect.x1)) * 0.5f;
      const float centerZ =
        (static_cast<float>(deposit.footprintRect.z0) + static_cast<float>(deposit.footprintRect.z1)) * 0.5f;
      const float dx = centerX - x;
      const float dz = centerZ - z;
      const float distanceSquared = dx * dx + dz * dz;
      if (distanceSquared > radius) {
        continue;
      }

      ResourceDepositDistance hit{};
      hit.deposit = deposit;
      hit.centerDistance = std::sqrt(distanceSquared);
      outDeposits->PushBack(hit);
    }
  }
} // namespace moho
