#include "stdafx.h"
#include "Level.h"
#include "Civic.h"
#include "Overworld.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

namespace
{
models::Point FlatPoint(tutorial::Vector2 p)
{
    return {p.x, p.y, 0};
}
models::Point SurfaceBody(const world::SurfacePoint &point)
{
    const auto p = tutorial::PlanetSecurity::Local(point);
    return {p.x, p.y, tutorial::PlanetSecurity::Elevation(point)};
}
models::Point CivicBody(tutorial::Vector2 position)
{
    auto &g = tutorial::Game::Get();
    auto p = FlatPoint(position);
    if (g.GetState().surfaceExploration && g.Surface() && g.Surface()->Ready())
    {
        p.z = float((g.Surface()->ContactAt(position.x * 2, position.y * 2).heightMeters -
                     g.Surface()->Origin().origin.altitudeMeters) /
                    2);
    }
    return p;
}
bool BlocksHorizontalShot(models::Point from, models::Point target, models::Point feet, float spread)
{
    if (feet.z > from.z + ai::CooperationSettings::kFootClearance ||
        feet.z + ai::CooperationSettings::kBodyHeight < from.z)
    {
        return false;
    }
    feet.z = from.z;
    return ai::Cooperation::BlocksShot(from, target, feet, spread);
}
} // namespace
namespace game
{
bool MineCombat::FriendlyFireBlocked(tutorial::Vector2 from, tutorial::Vector2 target,
                                     const NpcCombatState &shooter) const
{
    const float spread = shooter.pistol.GetSpread() * ai::CooperationSettings::kProjectedSpreadScale;
    auto blocks = [&](tutorial::Vector2 p, const NpcCombatState *identity = nullptr) {
        return identity != &shooter &&
               ai::Cooperation::BlocksShot(FlatPoint(from), FlatPoint(target), FlatPoint(p), spread);
    };
    auto &g = tutorial::Game::Get();
    if (blocks(g.GetState().playerPosition) ||
        (g.HasCompanion(tutorial::CompanionId::Ria, tutorial::CompanionPlace::Mine) &&
         blocks(state.riaPosition, &state.riaCombat)) ||
        (g.HasCompanion(tutorial::CompanionId::Kain, tutorial::CompanionPlace::Mine) &&
         blocks(state.kain.position, &state.kain.combat)))
    {
        return true;
    }
    return std::any_of(state.survivors.begin(), state.survivors.end(),
                       [&](const RescueNpc &person) { return blocks(person.position, &person.combat); });
}
bool FrontierCombat::FriendlyFireBlocked(const FrontierPerson &shooter, tutorial::Vector2 target,
                                         ChapterTeam team) const
{
    const float spread = shooter.combat.pistol.GetSpread() * ai::CooperationSettings::kProjectedSpreadScale;
    auto blocks = [&](const FrontierPerson &person) {
        return &person != &shooter && !person.arrived &&
               ai::Cooperation::BlocksShot(FlatPoint(shooter.position), FlatPoint(target),
                                           FlatPoint(person.position), spread);
    };
    if (team == ChapterTeam::Scrapper)
    {
        return std::any_of(state.scrappers.begin(), state.scrappers.end(), blocks);
    }
    auto &g = tutorial::Game::Get();
    return ai::Cooperation::BlocksShot(FlatPoint(shooter.position), FlatPoint(target),
                                       FlatPoint(g.GetState().playerPosition), spread) ||
           (g.HasCompanion(tutorial::CompanionId::Ria, tutorial::CompanionPlace::Frontier) &&
            blocks(state.ria)) ||
           (g.HasCompanion(tutorial::CompanionId::Kain, tutorial::CompanionPlace::Frontier) &&
            blocks(state.kain)) ||
           std::any_of(state.survivors.begin(), state.survivors.end(), blocks);
}
void MineCombat::ShareThreats()
{
    profiling::Scope timer(profiling::Timer::NpcCooperation);
    if (tutorial::HospitalRecovery::Active())
    {
        return;
    }
    std::vector<ai::ThreatReport> sightings;
    for (size_t i = 0; i < state.enemies.size(); ++i)
    {
        const auto &enemy = state.enemies[i];
        if (enemy.health > 0 && enemy.threat.Visible())
        {
            sightings.push_back(
                {i + 1, FlatPoint(enemy.position), FlatPoint(enemy.target), enemy.threat.Remaining()});
        }
    }
    if (sightings.empty())
    {
        return;
    }
    for (size_t i = 0; i < state.enemies.size(); ++i)
    {
        auto &enemy = state.enemies[i];
        if (enemy.health <= 0 || enemy.threat.Known())
        {
            continue;
        }
        if (const auto report = ai::Cooperation::NearestReport(FlatPoint(enemy.position), i + 1, sightings))
        {
            enemy.target = {report->target.x, report->target.y};
            enemy.threat.Remember(ai::Stimulus::Report, report->remainingSeconds);
            enemy.decision = ai::Decision::Investigate;
            enemy.teamwork = {ai::TeamTask::Investigate, 0, report->reporterId};
            profiling::Count(profiling::Counter::NpcReportsShared);
        }
    }
}
} // namespace game
namespace tutorial
{
void PlanetSecurity::ShareThreats()
{
    profiling::Scope timer(profiling::Timer::NpcCooperation);
    auto &law = CivicJustice::State();
    if (law.wanted == 0 || !CivicJustice::IsExterior())
    {
        return;
    }
    std::vector<ai::ThreatReport> sightings;
    for (const auto &officer : law.officers)
    {
        if (officer.health > 0 && !officer.departing && officer.threat.Visible() &&
            Game::Distance(officer.position, Game::Get().GetState().playerPosition) <= 300)
        {
            sightings.push_back({officer.id, FlatPoint(officer.position), FlatPoint(officer.lastKnown),
                                 officer.threat.Remaining()});
        }
    }
    if (sightings.empty())
    {
        return;
    }
    for (auto &officer : law.officers)
    {
        if (officer.health <= 0 || officer.departing || officer.threat.Known())
        {
            continue;
        }
        if (const auto report =
                ai::Cooperation::NearestReport(FlatPoint(officer.position), officer.id, sightings))
        {
            officer.lastKnown = {report->target.x, report->target.y};
            officer.threat.Remember(ai::Stimulus::Report, report->remainingSeconds);
            officer.decision = ai::Decision::Investigate;
            officer.teamwork = {ai::TeamTask::Investigate, 0, report->reporterId};
            profiling::Count(profiling::Counter::NpcReportsShared);
        }
    }
}
bool CivicJustice::FriendlyFireBlocked(const SecurityOfficer &shooter, Vector2 target)
{
    profiling::Scope timer(profiling::Timer::NpcCooperation);
    auto &g = Game::Get();
    const auto &world = g.GetState();
    const float spread = shooter.weapon.GetSpread();
    auto from = CivicBody(shooter.position);
    from.z += models::WorldScale::aimHeightPixels / 44;
    const models::Point to{target.x, target.y, from.z};
    for (const auto &other : State().officers)
    {
        if (&other != &shooter && BlocksHorizontalShot(from, to, CivicBody(other.position), spread))
        {
            return true;
        }
    }
    for (const auto &resident : world.residents)
    {
        if (size_t(&resident - world.residents.data()) == Overworld::kKainResidentSlot &&
            game::GameLevels::Chapter().GetState().kainJoined)
        {
            continue;
        }
        auto p = FlatPoint(resident.position);
        if (world.surfaceExploration && g.SurfaceCities())
        {
            const auto point = g.SurfaceCities()->Point(*g.Surface(), resident.cityIndex, resident.position);
            if (!point)
            {
                continue;
            }
            p = SurfaceBody(*point);
        }
        if (BlocksHorizontalShot(from, to, p, spread))
        {
            return true;
        }
    }
    return false;
}
bool PlanetSecurity::CompanionFireBlocked(CompanionId id, Vector2 target)
{
    auto &g = Game::Get();
    const auto &members = g.SurfaceParty()->State().members;
    const auto &shooter = members.at(size_t(id));
    auto from = SurfaceBody(shooter.position);
    from.z += models::WorldScale::aimHeightPixels / 44;
    const models::Point to{target.x, target.y, from.z};
    const float spread = shooter.combat.pistol.GetSpread();
    if (BlocksHorizontalShot(from, to, SurfaceBody(g.Surface()->Pose().origin), spread))
    {
        return true;
    }
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (i != size_t(id) && members[i].joined && members[i].place == CompanionPlace::Surface &&
            BlocksHorizontalShot(from, to, SurfaceBody(members[i].position), spread))
        {
            return true;
        }
    }
    for (const auto &resident : g.GetState().residents)
    {
        if (size_t(&resident - g.GetState().residents.data()) == Overworld::kKainResidentSlot &&
            game::GameLevels::Chapter().GetState().kainJoined)
        {
            continue;
        }
        const auto p = g.SurfaceCities()
                           ? g.SurfaceCities()->Point(*g.Surface(), resident.cityIndex, resident.position)
                           : std::nullopt;
        if (p && BlocksHorizontalShot(from, to, SurfaceBody(*p), spread))
        {
            return true;
        }
    }
    return false;
}
} // namespace tutorial
