#include "stdafx.h"
#include "Level.h"
#include "Overworld.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
using tutorial::Vector2;
struct SupportActor
{
    std::uint64_t id;
    Vector2 &position;
    tutorial::MovementFacing &facing;
    game::NpcCombatState &combat;
    std::vector<Vector2> &route;
    size_t &cursor;
};
struct Patient
{
    std::uint64_t id;
    Vector2 position;
    game::NpcCombatState *combat;
};
using Route = std::function<std::vector<Vector2>(Vector2, Vector2)>;
using MoveStep = std::function<float(SupportActor &, float)>;
using Sight = std::function<bool(Vector2, Vector2)>;
using Danger = std::function<bool(Vector2)>;
float Distance(Vector2 a, Vector2 b)
{
    return tutorial::Game::Distance(a, b);
}
void Cancel(SupportActor &actor)
{
    if (actor.combat.support.task != ai::SupportTask::None)
    {
        actor.route.clear();
        actor.cursor = 0;
        actor.combat.support = {};
    }
}
bool Run(SupportActor &actor, ai::SupportTask task, std::uint64_t subject, Vector2 goal, float stop, float dt,
         bool safe, int &budget, const Route &route, const MoveStep &move)
{
    auto &job = actor.combat.support;
    if (job.task != task || job.subjectId != subject)
    {
        job = {};
        job.task = task;
        job.subjectId = subject;
        job.goal = {goal.x, goal.y, 0};
        actor.route.clear();
        actor.cursor = 0;
        profiling::Count(profiling::Counter::NpcSupportAssignments);
    }
    else if (Distance({job.goal.x, job.goal.y}, goal) > 1)
    {
        job.goal = {goal.x, goal.y, 0};
        actor.route.clear();
        actor.cursor = 0;
        job.progressSeconds = 0;
    }
    job.retrySeconds = (std::max)(0.f, job.retrySeconds - dt);
    const float distance = Distance(actor.position, goal);
    safe = safe && actor.combat.hitGrace <= 0;
    if (safe && distance > stop)
    {
        if (actor.cursor >= actor.route.size() && job.retrySeconds <= 0 && budget > 0)
        {
            --budget;
            actor.route = route(actor.position, {job.goal.x, job.goal.y});
            actor.cursor = 0;
            job.retrySeconds = ai::Support::kRetrySeconds;
            profiling::Count(profiling::Counter::NpcSupportPathRequests);
        }
        const float wanted = (std::min)(ai::Support::kMoveSpeed * dt, distance - stop);
        const float actual = move(actor, wanted);
        job.stalledSeconds = actual < wanted * .2f ? job.stalledSeconds + dt : 0;
        if (job.stalledSeconds > ai::Support::kRetrySeconds)
        {
            actor.route.clear();
            actor.cursor = 0;
        }
        if (job.stalledSeconds >= ai::Support::kFailureSeconds)
        {
            job = {};
            job.retrySeconds = ai::Support::kFailureRetrySeconds;
            return false;
        }
    }
    else
    {
        actor.facing.walking = false;
    }
    if (!safe)
    {
        profiling::Count(profiling::Counter::NpcSupportBlockedTicks);
    }
    return ai::Support::Advance(job, dt, Distance(actor.position, goal) <= stop + .05f, safe);
}
std::array<bool, 2> Aid(std::vector<SupportActor> &actors, const std::vector<Patient> &patients, float dt,
                        int &budget, const Route &route, const MoveStep &move, const Sight &sight,
                        const Danger &danger)
{
    std::array<bool, 2> handled{};
    const auto player = tutorial::Game::Get().GetState().playerPosition;
    const Patient *assigned = nullptr;
    for (auto &actor : actors)
    {
        if (actor.combat.health <= 0)
        {
            Cancel(actor);
            continue;
        }
        if (actor.combat.support.task == ai::SupportTask::None && actor.combat.support.retrySeconds > 0)
        {
            actor.combat.support.retrySeconds = (std::max)(0.f, actor.combat.support.retrySeconds - dt);
            continue;
        }
        if (actor.id == 2 && assigned)
        {
            Run(actor, ai::SupportTask::Cover, assigned->id, assigned->position, ai::Support::kCoverDistance,
                dt, true, budget, route, move);
            handled[1] = true;
            continue;
        }
        const Patient *best = nullptr;
        float nearest = ai::Support::kSearchRange;
        for (const auto &patient : patients)
        {
            const float distance = Distance(actor.position, patient.position);
            if (patient.combat == &actor.combat || patient.combat->health > 0 ||
                patient.combat->downedSeconds <= 0 || distance >= nearest ||
                Distance(player, patient.position) > ai::Support::kPlayerLeash)
            {
                continue;
            }
            const bool remembered = actor.combat.support.task == ai::SupportTask::Aid &&
                                    actor.combat.support.subjectId == patient.id;
            if (remembered || sight(actor.position, patient.position))
            {
                best = &patient;
                nearest = distance;
            }
        }
        if (!best)
        {
            if (actor.combat.support.task == ai::SupportTask::Aid)
            {
                Cancel(actor);
            }
            continue;
        }
        assigned = best;
        handled[size_t(actor.id - 1)] = true;
        const bool safe = !danger(actor.position) && !danger(best->position);
        // A remembered patient across a wall still needs an actual approach before aid can begin.
        const float stop = sight(actor.position, best->position) ? ai::Support::kAidDistance : .1f;
        if (Run(actor, ai::SupportTask::Aid, best->id, best->position, stop, dt, safe, budget, route, move))
        {
            best->combat->health = 50;
            best->combat->downedSeconds = 0;
            best->combat->hitGrace = 3;
            profiling::Count(profiling::Counter::NpcSupportAids);
            Cancel(actor);
        }
        if (actor.combat.support.task == ai::SupportTask::None)
        {
            handled[size_t(actor.id - 1)] = false;
        }
    }
    return handled;
}
void CancelUnassignedCover(std::vector<SupportActor> &actors, const std::array<bool, 2> &handled)
{
    for (auto &actor : actors)
    {
        if (!handled[size_t(actor.id - 1)] && actor.combat.support.task == ai::SupportTask::Cover)
        {
            Cancel(actor);
        }
    }
}
std::optional<Vector2> Pad(Vector2 anchor, const std::function<bool(Vector2)> &clear)
{
    for (float radius : {1.5f, 2.5f, 3.5f})
    {
        for (int i = 0; i < 8; ++i)
        {
            Vector2 p{anchor.x + std::cos(i * tutorial::kPi / 4) * radius,
                      anchor.y + std::sin(i * tutorial::kPi / 4) * radius};
            if (clear(p))
            {
                return p;
            }
        }
    }
    return std::nullopt;
}
} // namespace
namespace game
{
std::array<bool, 2> MineRescue::UpdateSupport(float dt)
{
    profiling::Scope timer(profiling::Timer::NpcSupport);
    if (!std::isfinite(dt) || dt <= 0)
    {
        return {};
    }
    dt = (std::min)(dt, .05f);
    auto &g = tutorial::Game::Get();
    std::vector<SupportActor> actors;
    if (g.HasCompanion(tutorial::CompanionId::Ria, tutorial::CompanionPlace::Mine))
    {
        actors.push_back(
            {1, state.riaPosition, state.riaFacing, state.riaCombat, state.riaRoute, state.riaRouteIndex});
    }
    if (g.HasCompanion(tutorial::CompanionId::Kain, tutorial::CompanionPlace::Mine))
    {
        actors.push_back({2, state.kain.position, state.kain.facing, state.kain.combat, state.kain.route,
                          state.kain.routeIndex});
    }
    std::vector<Patient> patients;
    for (auto &actor : actors)
    {
        patients.push_back({actor.id, actor.position, &actor.combat});
    }
    for (auto &npc : state.survivors)
    {
        patients.push_back({npc.id + 3, npc.position, &npc.combat});
    }
    int budget = ai::Support::kPathBudget;
    auto handled = Aid(
        actors, patients, dt, budget, [&](Vector2 a, Vector2 b) { return level.Map().FindRoute(a, b); },
        [&](SupportActor &a, float amount) {
            const auto before = a.position;
            const auto cursor = a.cursor;
            float travelled = WalkRoute(a.position, a.route, a.cursor, amount, a.facing, dt);
            const auto player = g.GetState().playerPosition;
            if (Distance(a.position, player) < .55f &&
                Distance(a.position, player) < Distance(before, player))
            {
                a.position = before;
                a.cursor = cursor;
                a.facing.Update(before, before, dt);
                travelled = 0;
            }
            if (a.id == 1)
            {
                state.riaMovement = travelled;
            }
            return travelled;
        },
        [&](Vector2 a, Vector2 b) { return level.Map().HasLineOfSight(a, b); },
        [&](Vector2 p) {
            return std::any_of(state.enemies.begin(), state.enemies.end(), [&](const Enemy &enemy) {
                return enemy.health > 0 && Distance(p, enemy.position) < ai::Support::kDangerRange &&
                       level.Map().HasLineOfSight(p, enemy.position);
            });
        });
    CancelUnassignedCover(actors, handled);
    return handled;
}
std::array<bool, 2> FrontierParty::UpdateSupport(float dt)
{
    profiling::Scope timer(profiling::Timer::NpcSupport);
    if (!std::isfinite(dt) || dt <= 0)
    {
        return {};
    }
    dt = (std::min)(dt, .05f);
    auto &g = tutorial::Game::Get();
    std::vector<SupportActor> actors;
    for (size_t i = 0; i < 2; ++i)
    {
        if (g.HasCompanion(tutorial::CompanionId(i), tutorial::CompanionPlace::Frontier))
        {
            auto &p = i == 0 ? state.ria : state.kain;
            actors.push_back({i + 1, p.position, p.facing, p.combat, p.route, p.waypoint});
        }
    }
    std::vector<Patient> patients;
    for (auto &a : actors)
    {
        patients.push_back({a.id, a.position, &a.combat});
    }
    for (size_t i = 0; i < state.survivors.size(); ++i)
    {
        auto &p = state.survivors[i];
        if (!p.arrived)
        {
            patients.push_back({i + 3, p.position, &p.combat});
        }
    }
    const Route route = [&](Vector2 a, Vector2 b) { return level.FindRoute(a, b); };
    const MoveStep move = [&](SupportActor &a, float amount) {
        auto &person = a.id == 1 ? state.ria : state.kain;
        const auto before = person.position;
        const auto cursor = person.waypoint;
        Move(person, amount, dt);
        const auto player = g.GetState().playerPosition;
        const bool crowded = (Distance(person.position, player) < .55f &&
                              Distance(person.position, player) < Distance(before, player)) ||
                             std::any_of(patients.begin(), patients.end(), [&](const Patient &p) {
                                 return p.combat != &a.combat &&
                                        Distance(person.position, p.position) < .55f &&
                                        Distance(person.position, p.position) < Distance(before, p.position);
                             });
        if (crowded)
        {
            person.position = before;
            person.waypoint = cursor;
            person.facing.Update(before, before, dt);
        }
        return Distance(before, person.position);
    };
    const Sight sight = [&](Vector2 a, Vector2 b) { return level.HasLineOfSight(a, b); };
    const Danger danger = [&](Vector2 p) {
        return state.hostile && !state.negotiated &&
               std::any_of(state.scrappers.begin(), state.scrappers.end(), [&](const FrontierPerson &enemy) {
                   return enemy.combat.health > 0 &&
                          Distance(p, enemy.position) < ai::Support::kDangerRange && sight(p, enemy.position);
               });
    };
    int budget = ai::Support::kPathBudget;
    auto handled = Aid(actors, patients, dt, budget, route, move, sight, danger);
    if (actors.empty() || actors[0].id != 1 || actors[0].combat.health <= 0 || handled[0] ||
        (actors[0].combat.support.task == ai::SupportTask::None && actors[0].combat.support.retrySeconds > 0))
    {
        CancelUnassignedCover(actors, handled);
        return handled;
    }
    auto &worker = actors[0];
    const auto player = g.GetState().playerPosition;
    const Vector2 stations[] = {{10.5f, 10.5f}, {33.5f, 9.5f}, {34.5f, 10.5f}};
    size_t chosen = 3;
    float nearest = ai::Support::kSearchRange;
    for (size_t i = 0; i < 3; ++i)
    {
        const float distance = Distance(player, stations[i]);
        if (distance < nearest && Distance(worker.position, player) < ai::Support::kPlayerLeash)
        {
            nearest = distance;
            chosen = i;
        }
    }
    if (chosen == 3)
    {
        for (auto &a : actors)
        {
            Cancel(a);
        }
        return handled;
    }
    const bool carrying = worker.combat.support.task == ai::SupportTask::Supply;
    const auto goal =
        Pad({stations[chosen].x - (carrying ? 2.f : 0), stations[chosen].y + (carrying ? 1.f : 0)},
            [&](Vector2 p) { return !level.IsBlocked(p) && sight(p, p); });
    if (!goal)
    {
        Cancel(worker);
        CancelUnassignedCover(actors, handled);
        return handled;
    }
    handled[0] = true;
    const auto task = carrying ? ai::SupportTask::Supply : ai::SupportTask::Inspect;
    if (Run(worker, task, chosen + 1, *goal, ai::Support::kWorkDistance, dt,
            !danger(worker.position) && !danger(*goal), budget, route, move))
    {
        worker.combat.support.task = carrying ? ai::SupportTask::Inspect : ai::SupportTask::Supply;
        worker.combat.support.subjectId = 0; // The next phase must acquire its own exact route goal.
        worker.combat.support.progressSeconds = 0;
        worker.combat.support.performing = false;
        worker.route.clear();
        worker.cursor = 0;
        profiling::Count(profiling::Counter::NpcSupportWorkCycles);
    }
    if (worker.combat.support.task == ai::SupportTask::None)
    {
        handled[0] = false;
        CancelUnassignedCover(actors, handled);
        return handled;
    }
    for (auto &guard : actors)
    {
        if (guard.id == 2 && guard.combat.health > 0 && !handled[1] &&
            !(guard.combat.support.task == ai::SupportTask::None && guard.combat.support.retrySeconds > 0))
        {
            Run(guard, ai::SupportTask::Cover, chosen + 1, worker.position, ai::Support::kCoverDistance, dt,
                true, budget, route, move);
            handled[1] = true;
        }
    }
    return handled;
}
} // namespace game
