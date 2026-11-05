#include "stdafx.h"
#include "CityInfrastructure.h"
#include "Overworld.h"
#include "Profiler.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace tutorial
{
namespace
{
CityInfrastructurePlan published;
constexpr unsigned kMaximumAttempts = 4;
constexpr float kMaximumPathLength = 32; // Local units: at most 64 metres of extra walkway per role.
bool ValidFootprint(const SceneryProp &prop)
{
    return prop.model && prop.model->resourceId && prop.solid && prop.elevation <= .1f &&
           std::isfinite(prop.minimum.x) && std::isfinite(prop.minimum.y) && std::isfinite(prop.maximum.x) &&
           std::isfinite(prop.maximum.y) && prop.minimum.x < prop.maximum.x &&
           prop.minimum.y < prop.maximum.y;
}
void Bind(CityInfrastructureSite &site, const SceneryProp &prop, size_t index, Vector2 pad)
{
    const auto &mesh = models::ResourceAssets::Get(prop.model->resourceId);
    site.sourceIndex = index;
    site.asset = mesh.name;
    site.equipment = prop.position;
    site.standing = pad;
    site.minimum = prop.minimum;
    site.maximum = prop.maximum;
    site.nativeSizeMeters = {(mesh.maximum.x - mesh.minimum.x) * 2, (mesh.maximum.y - mesh.minimum.y) * 2,
                             (mesh.maximum.z - mesh.minimum.z) * 2};
}
} // namespace
const std::array<CityInfrastructureProfile, 5> &CityInfrastructure::Profiles()
{
    using S = models::SceneryType;
    static const std::array<CityInfrastructureProfile, 5> profiles = {
        {{"halo_07",
          "주거·관문 관측 지원",
          {{{"transit", "주거 교통 안내", {S::TransitShelter, S::Wayfinding}},
            {"charging", "주거 이동수단 충전 설비", {S::ChargePost, S::Count}},
            {"power", "주거 전력 분배 설비", {S::Transformer, S::Count}}}}},
         {"cinder",
          "광산 교대·공업 물류 지원",
          {{{"cargo", "광산 교대 물류 설비", {S::CargoContainer, S::Count}},
            {"services", "공업 배관·환기 설비", {S::PipeRack, S::HVAC}},
            {"power", "공업 전력 분배 설비", {S::Transformer, S::Count}}}}},
         {"veil",
          "관측 기록·측정 환경 지원",
          {{{"relay", "관측 기록 중계 설비", {S::CommRelay, S::Count}},
            {"power", "관측 지원 태양광 설비", {S::SolarArray, S::Count}},
            {"environment", "관측 환경 유지 설비", {S::HVAC, S::WaterTank}}}}},
         {"lumen",
          "피난 생활·필수 자원 지원",
          {{{"shelter", "피난 이동 대기 구역", {S::TransitShelter, S::Count}},
            {"water", "생활 용수 저장 설비", {S::WaterTank, S::Count}},
            {"power", "생활 전력 태양광 설비", {S::SolarArray, S::Count}}}}},
         {"echo",
          "통신 복구·이동 지원",
          {{{"relay", "도시 통신 중계 설비", {S::CommRelay, S::Count}},
            {"charging", "통신 점검 이동수단 충전 설비", {S::ChargePost, S::Count}},
            {"power", "통신 지원 전력 분배 설비", {S::Transformer, S::Count}}}}}}};
    return profiles;
}
const char *CityInfrastructure::AccessName(InfrastructureAccess state)
{
    switch (state)
    {
    case InfrastructureAccess::Missing:
        return "missing_asset";
    case InfrastructureAccess::Blocked:
        return "unconnected";
    case InfrastructureAccess::Connected:
        return "connected";
    }
    return "unknown";
}
std::array<Vector2, 4> CityInfrastructure::Pads(const SceneryProp &prop)
{
    const float x = (prop.minimum.x + prop.maximum.x) * .5f;
    const float y = (prop.minimum.y + prop.maximum.y) * .5f;
    return {{{x, prop.minimum.y - .65f},
             {prop.maximum.x + .65f, y},
             {x, prop.maximum.y + .65f},
             {prop.minimum.x - .65f, y}}};
}
CityInfrastructurePlan CityInfrastructure::Prepare(const GameState &world,
                                                   const FacilityApproachPlan &facilities)
{
    profiling::Scope timer(profiling::Timer::CityInfrastructurePrepare);
    CityInfrastructurePlan plan;
    plan.prepared = true;
    auto connected = CityWalkways::BaseSegments();
    for (const auto &facility : facilities.facilities)
    {
        for (size_t i = 1; i < facility.path.size(); ++i)
        {
            connected.push_back({facility.path[i - 1], facility.path[i]});
        }
    }
    const auto clear = CityWalkways::StaticClearance(world);
    const auto &props = Overworld::Scenery().Props();
    std::set<size_t> used;
    for (size_t city = 0; city < Profiles().size(); ++city)
    {
        const auto center = Overworld::Cities()[city].center;
        const auto &profile = Profiles()[city];
        for (size_t role = 0; role < profile.roles.size(); ++role)
        {
            const auto &definition = profile.roles[role];
            auto &site = plan.sites[city * profile.roles.size() + role];
            site.city = int(city);
            site.id = std::string(profile.regionId) + "/infrastructure/" + definition.key;
            struct Candidate
            {
                float distance;
                size_t prop, pad;
                Vector2 position;
            };
            std::vector<Candidate> candidates;
            for (size_t index = 0; index < props.size(); ++index)
            {
                const auto &prop = props[index];
                if (used.count(index) || !ValidFootprint(prop) ||
                    Game::Distance(prop.position, center) > 80 || prop.type == models::SceneryType::Count ||
                    std::find(definition.assets.begin(), definition.assets.end(), prop.type) ==
                        definition.assets.end())
                {
                    continue;
                }
                site.access = InfrastructureAccess::Blocked;
                const auto pads = Pads(prop);
                for (size_t pad = 0; pad < pads.size(); ++pad)
                {
                    if (!clear(pads[pad]))
                    {
                        continue;
                    }
                    float distance = 16.01f;
                    for (const auto &segment : connected)
                    {
                        distance =
                            (std::min)(distance,
                                       Game::Distance(pads[pad], CityWalkways::Project(pads[pad], segment)));
                    }
                    if (distance <= 16)
                    {
                        candidates.push_back({distance, index, pad, pads[pad]});
                    }
                }
                if (site.sourceIndex == (std::numeric_limits<size_t>::max)())
                {
                    Bind(site, prop, index, pads.front());
                }
            }
            std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
                return std::tie(a.distance, a.prop, a.pad) < std::tie(b.distance, b.prop, b.pad);
            });
            for (const auto &candidate : candidates)
            {
                if (site.attempts == kMaximumAttempts)
                {
                    break;
                }
                ++site.attempts;
                unsigned visited = 0;
                auto path = FacilityApproaches::Connect(candidate.position, connected, clear, &visited);
                site.visitedNodes += visited;
                float length = 0;
                for (size_t i = 1; i < path.size(); ++i)
                {
                    length += Game::Distance(path[i - 1], path[i]);
                }
                if (path.empty() || length > kMaximumPathLength)
                {
                    continue;
                }
                Bind(site, props[candidate.prop], candidate.prop, candidate.position);
                site.path = std::move(path);
                site.pathLengthMeters = length * 2;
                site.access = InfrastructureAccess::Connected;
                CityWalkways::Append(plan, site.path, connected);
                plan.moduleCities.resize(plan.modules.size(), int(city));
                used.insert(candidate.prop);
                break;
            }
            profiling::Count(profiling::Counter::CityInfrastructureAttempts, site.attempts);
            profiling::Count(profiling::Counter::CityInfrastructureVisitedNodes, site.visitedNodes);
            profiling::Count(site.access == InfrastructureAccess::Connected
                                 ? profiling::Counter::CityInfrastructureConnections
                                 : profiling::Counter::CityInfrastructureFailures);
        }
    }
    return plan;
}
void CityInfrastructure::Publish(CityInfrastructurePlan plan) noexcept
{
    published = std::move(plan);
}
const CityInfrastructurePlan &CityInfrastructure::Get()
{
    return published;
}
bool CityInfrastructure::Contains(Vector2 position)
{
    return CityWalkways::Contains(published, position);
}
} // namespace tutorial
