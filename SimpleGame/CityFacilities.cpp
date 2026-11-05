#include "stdafx.h"
#include "CityFacilities.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace tutorial
{
namespace
{
constexpr float kStandOff = .75f, kSearchRadius = 4.f, kGridStep = .5f, kSegmentStep = .125f;
constexpr int kHalfGrid = 8, kGridSize = kHalfGrid * 2 + 1, kGridCount = kGridSize * kGridSize;
bool Finite(Vector2 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}
bool Connected(Vector2 a, Vector2 b, const CityFacilities::ClearPoint &clear)
{
    const int steps = (std::max)(1, int(std::ceil(Game::Distance(a, b) / kSegmentStep)));
    for (int i = 0; i <= steps; ++i)
    {
        const float t = float(i) / steps;
        if (!clear({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}))
        {
            return false;
        }
    }
    return true;
}
} // namespace

const std::array<CityFacility, 4> &CityFacilities::All()
{
    static const std::array<CityFacility, 4> facilities = {{{"halo_07/home",
                                                             "거주지 / 출입 [E]",
                                                             FacilityRole::Residence,
                                                             InteractionTarget::HomeEntrance,
                                                             kHomeDoor,
                                                             {2, 2},
                                                             {2.4f, 2.4f},
                                                             {0, 1},
                                                             true},
                                                            {"halo_07/observatory",
                                                             "전망대 / 승강기 [E]",
                                                             FacilityRole::Observation,
                                                             InteractionTarget::GateBeacon,
                                                             kBeaconPosition,
                                                             kObservatoryStairs,
                                                             kObservatoryStairs,
                                                             {0, 1},
                                                             true},
                                                            {"halo_07/receiver",
                                                             "도시 수신망 / 조사 기록 [E]",
                                                             FacilityRole::Communications,
                                                             InteractionTarget::CityReceiver,
                                                             kCityReceiverPosition,
                                                             {},
                                                             {},
                                                             {0, 1},
                                                             false},
                                                            {"halo_07/power",
                                                             "주거 구역 / 전력 장치 [E]",
                                                             FacilityRole::Utilities,
                                                             InteractionTarget::PowerRelay,
                                                             kRelayPosition,
                                                             {},
                                                             {},
                                                             {0, 1},
                                                             false}}};
    return facilities;
}
const CityFacility *CityFacilities::Find(InteractionTarget target)
{
    for (const auto &facility : All())
    {
        if (facility.target == target)
        {
            return &facility;
        }
    }
    return nullptr;
}
const CityFacility *CityFacilities::ExitAt(Vector2 exterior)
{
    for (const auto &facility : All())
    {
        if (facility.hasInterior && Game::Distance(facility.exterior, exterior) < .01f)
        {
            return &facility;
        }
    }
    return nullptr;
}
Vector2 CityFacilities::StandingPoint(const CityFacility &facility)
{
    return {facility.exterior.x + facility.outward.x * kStandOff,
            facility.exterior.y + facility.outward.y * kStandOff};
}
std::optional<Vector2> CityFacilities::FindExit(const CityFacility &facility, const ClearPoint &staticClear,
                                                const ClearPoint &available, unsigned *visitedNodes)
{
    if (visitedNodes)
    {
        *visitedNodes = 0;
    }
    if (!facility.hasInterior || !staticClear || !available || !Finite(facility.exterior) ||
        !Finite(facility.outward) ||
        std::fabs(std::hypot(facility.outward.x, facility.outward.y) - 1) > .001f)
    {
        return std::nullopt;
    }
    // A solid elevator control can occupy the marker itself; its authored front pad is the exit root.
    auto root = facility.exterior;
    if (!staticClear(root))
    {
        if (facility.role != FacilityRole::Observation)
        {
            return std::nullopt;
        }
        root = StandingPoint(facility);
    }
    if (!staticClear(root))
    {
        return std::nullopt;
    }
    const Vector2 right{facility.outward.y, -facility.outward.x};
    auto position = [&](int index) {
        const float x = (index % kGridSize - kHalfGrid) * kGridStep;
        const float y = (index / kGridSize - kHalfGrid) * kGridStep;
        return Vector2{root.x + right.x * x + facility.outward.x * y,
                       root.y + right.y * x + facility.outward.y * y};
    };
    std::array<bool, kGridCount> visited = {};
    std::array<int, kGridCount> queue = {};
    int head = 0, tail = 0;
    constexpr int start = kHalfGrid * kGridSize + kHalfGrid;
    queue[tail++] = start;
    visited[start] = true;
    constexpr std::array<std::pair<int, int>, 8> neighbours = {
        {{0, 1}, {1, 0}, {-1, 0}, {1, 1}, {-1, 1}, {0, -1}, {1, -1}, {-1, -1}}};
    while (head < tail)
    {
        const int index = queue[head++];
        if (visitedNodes)
        {
            ++*visitedNodes;
        }
        const auto p = position(index);
        if (available(p))
        {
            return p;
        }
        for (const auto &delta : neighbours)
        {
            const int x = index % kGridSize + delta.first, y = index / kGridSize + delta.second;
            if (x < 0 || x >= kGridSize || y < 0 || y >= kGridSize)
            {
                continue;
            }
            const int next = y * kGridSize + x;
            if (visited[next])
            {
                continue;
            }
            const auto q = position(next);
            const Vector2 offset{q.x - facility.exterior.x, q.y - facility.exterior.y};
            if (offset.x * facility.outward.x + offset.y * facility.outward.y < -.001f ||
                std::hypot(offset.x, offset.y) > kSearchRadius || !Connected(p, q, staticClear))
            {
                continue;
            }
            visited[next] = true;
            queue[tail++] = next;
        }
    }
    return std::nullopt;
}
} // namespace tutorial
