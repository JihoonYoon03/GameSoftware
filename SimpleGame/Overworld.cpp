#include "stdafx.h"
#include "Overworld.h"
#include "Civic.h"
#include "Level.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace tutorial
{
// ============================================================================
// PlanetParty — persistent companions and explicit physical handoff between local simulations
// ============================================================================
PlanetParty::PlanetParty(PlanetPartyState initial) : state(std::move(initial))
{
}
void PlanetParty::Bind(std::shared_ptr<const world::GeneratedPlanet> plan)
{
    if (!plan || planet == plan)
    {
        return;
    }
    Validate(state, plan->terrain.Planet());
    planet = std::move(plan);
    state.worldKey = world::WorldIdentity::ContentKey(planet->terrain.Planet(), "overworld", "party_v1");
}
bool PlanetParty::Joined(CompanionId id) const
{
    return state.members.at(size_t(id)).joined;
}
bool PlanetParty::Owns(CompanionId id, CompanionPlace place) const
{
    const auto &member = state.members.at(size_t(id));
    return member.joined && member.place == place;
}
bool PlanetParty::Damage(CompanionId id, int amount, bool playerOwned)
{
    auto &m = state.members.at(size_t(id));
    if (!m.joined || amount <= 0 || m.combat.health <= 0 || (!playerOwned && m.combat.hitGrace > 0))
    {
        return false;
    }
    m.combat.health = (std::max)(0, m.combat.health - amount);
    m.combat.hitGrace = .45f;
    if (m.combat.health <= 0)
    {
        m.combat.downedSeconds = 12;
        m.walking = false;
    }
    if (playerOwned)
    {
        PlanetSecurity::Report(m.combat.health > 0 ? Offense::Assault : Offense::AggravatedAssault,
                               m.position);
    }
    Publish();
    return true;
}
void PlanetParty::Defend(CompanionId id, float yaw)
{
    auto &m = state.members.at(size_t(id));
    m.combat.engaged = true;
    m.combat.aimYaw = yaw;
    const auto &frame = Game::Get().Surface()->Origin();
    m.heading =
        world::SurfaceGeometry::Unit({frame.right.x * std::cos(yaw) + frame.forward.x * std::sin(yaw),
                                      frame.right.y * std::cos(yaw) + frame.forward.y * std::sin(yaw),
                                      frame.right.z * std::cos(yaw) + frame.forward.z * std::sin(yaw)});
    if (m.combat.pistol.GetRounds() == 0)
    {
        m.combat.pistol.Reload();
    }
    combat::Shot shot;
    if (m.combat.pistol.TryFire(yaw, shot))
    {
        auto p = PlanetSecurity::Local(m.position);
        auto &law = CivicJustice::State();
        if (law.bullets.size() < 128)
        {
            law.bullets.push_back(
                {p,
                 {std::cos(shot.angle), std::sin(shot.angle)},
                 shot.speed,
                 shot.range,
                 shot.damage,
                 false,
                 true,
                 PlanetSecurity::Elevation(m.position) + models::WorldScale::aimHeightPixels / 44});
            m.combat.muzzleFlash = .06f;
        }
    }
}
world::TangentFrame PlanetParty::Frame(CompanionPlace place) const
{
    return place == CompanionPlace::Surface
               ? Game::Get().Surface()->Origin()
               : planet->Region(place == CompanionPlace::Mine ? "mq00_mine" : "frontier").frame;
}
world::SurfacePoint PlanetParty::MinePoint(Vector2 local) const
{
    auto entry = game::GameLevels::Mine().GetState().entry;
    return world::SurfaceGeometry::FromLocal(Frame(CompanionPlace::Mine),
                                             {(local.x - entry.x) * 2, (local.y - entry.y) * 2, 0},
                                             planet->terrain.Planet().radiusMeters);
}
Vector2 PlanetParty::MineLocal(const world::SurfacePoint &point) const
{
    auto local = world::SurfaceGeometry::ToLocal(Frame(CompanionPlace::Mine), point,
                                                 planet->terrain.Planet().radiusMeters);
    auto entry = game::GameLevels::Mine().GetState().entry;
    return {entry.x + float(local.x / 2), entry.y + float(local.y / 2)};
}
void PlanetParty::Join()
{
    if (!planet || !planet->terrain.Planet().storyPlanet)
    {
        return;
    }
    auto &g = Game::Get();
    auto &mine = game::GameLevels::Mine().GetState();
    auto &field = game::GameLevels::Chapter().GetState();
    for (size_t i = 0; i < state.members.size(); ++i)
    {
        auto &member = state.members[i];
        if (member.joined || !(i == 0 ? mine.quest.IsRiaJoined() : field.kainJoined))
        {
            continue;
        }
        member.joined = true;
        if (g.IsSurfaceMine() && mine.generated)
        {
            member.place = CompanionPlace::Mine;
            member.position = MinePoint(i == 0 ? mine.riaPosition : mine.kain.position);
            member.combat = i == 0 ? mine.riaCombat : mine.kain.combat;
        }
        else if (field.generated && (field.active || i == 1))
        {
            member.place = CompanionPlace::Frontier;
            const auto &person = i == 0 ? field.ria : field.kain;
            member.position = game::GameLevels::Chapter().Surface().Point(person.position);
            member.combat = person.combat;
        }
        else
        {
            member.position =
                *g.SurfaceCities()->Point(*g.Surface(), 0, i == 0 ? kLiaPosition : kCityKainPosition);
            member.combat.health = CivicJustice::State().landmarkHealth[i == 0 ? 0 : 2];
            member.combat.downedSeconds = member.combat.health <= 0 ? 12.f : 0.f;
        }
        member.heading = Frame(member.place).right;
    }
}
void PlanetParty::Capture()
{
    if (!planet)
    {
        return;
    }
    Join();
    const auto &mine = game::GameLevels::Mine().GetState();
    const auto &field = game::GameLevels::Chapter().GetState();
    for (size_t i = 0; i < state.members.size(); ++i)
    {
        auto &member = state.members[i];
        if (!member.joined || member.place == CompanionPlace::Surface)
        {
            continue;
        }
        MovementFacing facing;
        if (member.place == CompanionPlace::Mine)
        {
            member.position = MinePoint(i == 0 ? mine.riaPosition : mine.kain.position);
            member.combat = i == 0 ? mine.riaCombat : mine.kain.combat;
            facing = i == 0 ? mine.riaFacing : mine.kain.facing;
        }
        else
        {
            const auto &person = i == 0 ? field.ria : field.kain;
            member.position = game::GameLevels::Chapter().Surface().Point(person.position);
            member.combat = person.combat;
            facing = person.facing;
        }
        member.combat.downedSeconds = (std::max)(0.f, member.combat.downedSeconds);
        auto frame = Frame(member.place);
        member.heading = world::SurfaceGeometry::Unit(
            {frame.right.x * std::cos(facing.yaw) + frame.forward.x * std::sin(facing.yaw),
             frame.right.y * std::cos(facing.yaw) + frame.forward.y * std::sin(facing.yaw),
             frame.right.z * std::cos(facing.yaw) + frame.forward.z * std::sin(facing.yaw)});
        member.walking = facing.walking;
    }
}
void PlanetParty::Publish()
{
    if (!planet)
    {
        return;
    }
    auto &mine = game::GameLevels::Mine().GetState();
    auto &field = game::GameLevels::Chapter().GetState();
    for (size_t i = 0; i < state.members.size(); ++i)
    {
        const auto &member = state.members[i];
        if (!member.joined || member.place == CompanionPlace::Surface)
        {
            continue;
        }
        auto frame = Frame(member.place);
        float yaw = float(std::atan2(world::SurfaceGeometry::Dot(member.heading, frame.forward),
                                     world::SurfaceGeometry::Dot(member.heading, frame.right)));
        MovementFacing facing;
        facing.yaw = yaw;
        facing.walking = member.walking;
        if (member.place == CompanionPlace::Mine)
        {
            if (i == 0)
            {
                mine.riaPosition = MineLocal(member.position);
                mine.riaCombat = member.combat;
                mine.riaFacing = facing;
            }
            else
            {
                mine.kain.position = MineLocal(member.position);
                mine.kain.combat = member.combat;
                mine.kain.facing = facing;
            }
        }
        else
        {
            auto local = world::SurfaceGeometry::ToLocal(frame, member.position,
                                                         planet->terrain.Planet().radiusMeters);
            auto &person = i == 0 ? field.ria : field.kain;
            person.position = {game::kFrontierEntry.x + float(local.x / 2),
                               game::kFrontierEntry.y + float(local.y / 2)};
            person.combat = member.combat;
            person.facing = facing;
        }
    }
}
void PlanetParty::ChangeOwner(size_t index, CompanionPlace place, const world::SurfacePoint &position)
{
    profiling::Count(profiling::Counter::CompanionHandoffs);
    auto &member = state.members[index];
    member.place = place;
    member.position = position;
    member.walking = false;
    member.combat.engaged = member.combat.evading = false;
    member.combat.muzzleFlash = 0;
    member.combat.aimYaw = 0; // Reacquire a target in the new adapter, never transport a stale aim.
    auto &mine = game::GameLevels::Mine().GetState();
    if (index == 0)
    {
        mine.riaRoute.clear();
        mine.riaRouteIndex = 0;
        mine.riaNavigationCooldown = 0;
    }
    else
    {
        mine.kain.route.clear();
        mine.kain.routeIndex = 0;
        mine.kain.navigationCooldown = 0;
    }
    auto &person =
        index == 0 ? game::GameLevels::Chapter().GetState().ria : game::GameLevels::Chapter().GetState().kain;
    person.route.clear();
    person.waypoint = 0;
}
bool PlanetParty::Blocked(const world::SurfacePoint &point, double clearance, int except) const
{
    if (!planet)
    {
        return false;
    }
    for (size_t i = 0; i < state.members.size(); ++i)
    {
        const auto &m = state.members[i];
        if (int(i) != except && m.joined && m.place != CompanionPlace::Mine &&
            world::SurfaceGeometry::Distance(planet->terrain.Planet().radiusMeters, point, m.position) <
                clearance + .32)
        {
            return true;
        }
    }
    return false;
}
bool PlanetParty::Clear(const world::SurfacePoint &point, int except) const
{
    auto &g = Game::Get();
    const auto &field = game::GameLevels::Chapter().GetState();
    if (field.generated && world::SurfaceGeometry::Distance(planet->terrain.Planet().radiusMeters, point,
                                                            planet->Region("frontier").center.position) < 150)
    {
        auto local = world::SurfaceGeometry::ToLocal(Frame(CompanionPlace::Frontier), point,
                                                     planet->terrain.Planet().radiusMeters);
        Vector2 p{game::kFrontierEntry.x + float(local.x / 2), game::kFrontierEntry.y + float(local.y / 2)};
        if (p.x >= 0 && p.y >= 0 && p.x < game::kFrontierWidth && p.y < game::kFrontierHeight &&
            field.tiles[size_t(int(p.y) * game::kFrontierWidth + int(p.x))] == game::FrontierTile::Canal)
        {
            return false;
        }
        auto occupied = [&](const game::FrontierPerson &person) {
            return person.combat.health > 0 && Game::Distance(p, person.position) < .65f;
        };
        if ((!field.kainJoined && occupied(field.kain)) ||
            std::any_of(field.survivors.begin(), field.survivors.end(), occupied) ||
            std::any_of(field.scrappers.begin(), field.scrappers.end(), [&](const auto &person) {
                return !(field.negotiated && person.arrived) && occupied(person);
            }))
        {
            return false;
        }
    }
    return g.Surface()->LandingClear(point, .32) && !Blocked(point, .32, except) &&
           !PlanetSecurity::OfficerBlocked(point, .32) &&
           (!g.SurfaceBike() || world::SurfaceGeometry::Distance(planet->terrain.Planet().radiusMeters,
                                                                 *g.SurfaceBike(), point) >= 1) &&
           (!g.SurfaceTraffic() || !g.SurfaceTraffic()->Blocked(point, .32)) &&
           (!g.SurfaceCities() || !g.SurfaceCities()->OccupiedAt(g.GetState(), *g.Surface(), point));
}
void PlanetParty::Update(float seconds)
{
    profiling::Scope timer(profiling::Timer::PlanetParty);
    seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .05f) : 0.f;
    if (!planet || !planet->terrain.Planet().storyPlanet)
    {
        return;
    }
    using G = world::SurfaceGeometry;
    auto &g = Game::Get();
    auto &mine = game::GameLevels::Mine();
    auto &field = game::GameLevels::Chapter();
    Join();
    const double radius = planet->terrain.Planet().radiusMeters;
    const auto &door = planet->Region("mq00_mine").access.position;
    for (size_t i = 0; i < state.members.size(); ++i)
    {
        auto &member = state.members[i];
        routeCooldown[i] = (std::max)(0.f, routeCooldown[i] - seconds);
        if (!member.joined)
        {
            continue;
        }
        if (member.place == CompanionPlace::Frontier && !field.IsActive())
        {
            ChangeOwner(i, CompanionPlace::Surface, member.position);
        }
        if (member.place == CompanionPlace::Surface && g.IsSurfaceMine() && member.combat.health > 0 &&
            G::Distance(radius, member.position, door) <= 6)
        {
            Vector2 entry = mine.GetState().entry;
            Vector2 candidate{entry.x + 1, entry.y + (i == 0 ? 1.f : -1.f)};
            if (!mine.Map().IsBlocked(candidate))
            {
                ChangeOwner(i, CompanionPlace::Mine, MinePoint(candidate));
            }
        }
        if (member.place == CompanionPlace::Surface && field.IsActive() &&
            G::Distance(radius, member.position, planet->Region("frontier").access.position) < 240)
        {
            auto local = G::ToLocal(Frame(CompanionPlace::Frontier), member.position, radius);
            const double x = game::kFrontierEntry.x + local.x / 2, y = game::kFrontierEntry.y + local.y / 2;
            if (x >= 0 && y >= 0 && x < game::kFrontierWidth && y < game::kFrontierHeight)
            {
                ChangeOwner(i, CompanionPlace::Frontier, member.position);
            }
        }
        bool simulated = (member.place == CompanionPlace::Mine && g.IsSurfaceMine()) ||
                         (member.place == CompanionPlace::Frontier && field.IsActive());
        if (simulated || seconds <= 0)
        {
            continue;
        }
        auto &combat = member.combat;
        combat.pistol.Update(seconds);
        combat.cooldown = (std::max)(0.f, combat.cooldown - seconds);
        combat.hitGrace = (std::max)(0.f, combat.hitGrace - seconds);
        combat.muzzleFlash = 0;
        combat.engaged = combat.evading = false;
        member.walking = false;
        if (combat.health <= 0)
        {
            combat.downedSeconds = (std::max)(0.f, combat.downedSeconds - seconds);
            if (combat.downedSeconds <= 0)
            {
                combat.health = 50;
                combat.hitGrace = 3;
            }
            continue;
        }
        if (member.place == CompanionPlace::Mine)
        {
            auto position = MineLocal(member.position);
            const auto entry = mine.GetState().entry;
            if (Game::Distance(position, entry) <= 1.8f)
            {
                auto frame = G::Frame(door);
                auto candidate = G::Advance(frame, (i == 0 ? -2. : 2.), -2., radius).origin;
                if (Clear(candidate, int(i)))
                {
                    candidate.altitudeMeters = g.Surface()->Stream().Contact(candidate).heightMeters;
                    ChangeOwner(i, CompanionPlace::Surface, candidate);
                }
            }
            else
            {
                // Only two actors; cache a path until its end rather than rerunning a map search per frame.
                auto &s = mine.GetState();
                auto &route = i == 0 ? s.riaRoute : s.kain.route;
                auto &index = i == 0 ? s.riaRouteIndex : s.kain.routeIndex;
                if (!route.empty() && Game::Distance(route.back(), entry) > .1f)
                {
                    route.clear();
                    index = 0;
                    routeCooldown[i] = 0;
                }
                if (index >= route.size() && routeCooldown[i] <= 0)
                {
                    route = mine.Map().FindRoute(position, entry);
                    index = 0;
                    routeCooldown[i] = .8f;
                }
                if (index < route.size())
                {
                    auto next = route[index];
                    float d = Game::Distance(position, next), step = (std::min)(4.f * seconds, d);
                    if (d > .001f)
                    {
                        const float yaw = std::atan2(next.y - position.y, next.x - position.x);
                        auto frame = Frame(CompanionPlace::Mine);
                        member.heading =
                            G::Unit({frame.right.x * std::cos(yaw) + frame.forward.x * std::sin(yaw),
                                     frame.right.y * std::cos(yaw) + frame.forward.y * std::sin(yaw),
                                     frame.right.z * std::cos(yaw) + frame.forward.z * std::sin(yaw)});
                        position = {position.x + (next.x - position.x) * step / d,
                                    position.y + (next.y - position.y) * step / d};
                        member.position = MinePoint(position);
                        member.walking = true;
                    }
                    if (d <= step + .01f)
                    {
                        ++index;
                    }
                }
            }
            continue;
        }
        auto target = g.IsSurfaceMine() ? door : g.Surface()->Pose().origin;
        const double distance = G::Distance(radius, member.position, target);
        if (distance <= (i == 0 ? 3.2 : 4.8))
        {
            continue;
        }
        auto frame = G::Frame(member.position);
        double angle = std::atan2(G::Dot(target.normal, frame.forward), G::Dot(target.normal, frame.right)),
               step = (std::min)(8. * seconds, distance - 2.5);
        // Bounded steps and steering check local obstacles without per-frame global pathfinding.
        for (double turn : {0., .45, -.45, .9, -.9})
        {
            auto candidate =
                G::Advance(frame, std::cos(angle + turn) * step, std::sin(angle + turn) * step, radius)
                    .origin;
            if (Clear(candidate, int(i)))
            {
                candidate.altitudeMeters = g.Surface()->Stream().Contact(candidate).heightMeters;
                member.heading = G::Unit(
                    {frame.right.x * std::cos(angle + turn) + frame.forward.x * std::sin(angle + turn),
                     frame.right.y * std::cos(angle + turn) + frame.forward.y * std::sin(angle + turn),
                     frame.right.z * std::cos(angle + turn) + frame.forward.z * std::sin(angle + turn)});
                member.position = candidate;
                member.walking = true;
                break;
            }
        }
    }
    Publish();
}
InteractionTarget PlanetParty::Nearby(float *distance) const
{
    auto &g = Game::Get();
    InteractionTarget result = InteractionTarget::None;
    double best = 2.7;
    for (size_t i = 0; planet && i < state.members.size(); ++i)
    {
        const auto &m = state.members[i];
        if (m.joined && m.place == CompanionPlace::Surface && m.combat.health > 0)
        {
            double d = world::SurfaceGeometry::Distance(planet->terrain.Planet().radiusMeters, m.position,
                                                        g.Surface()->Pose().origin);
            if (d < best)
            {
                best = d;
                result = i == 0 ? InteractionTarget::Lia : InteractionTarget::Kain;
            }
        }
    }
    if (distance)
    {
        *distance = result == InteractionTarget::None ? 1000000.f : float(best / 2);
    }
    return result;
}
std::array<bool, 2> PlanetParty::NearbyTravelers() const
{
    std::array<bool, 2> result = {};
    auto &g = Game::Get();
    for (size_t i = 0; planet && i < state.members.size(); ++i)
    {
        const auto &m = state.members[i];
        result[i] = m.joined && m.place != CompanionPlace::Mine && m.combat.health > 0 &&
                    world::SurfaceGeometry::Distance(planet->terrain.Planet().radiusMeters, m.position,
                                                     g.Surface()->Pose().origin) <= 12;
    }
    return result;
}
void PlanetParty::Arrive(const std::array<bool, 2> &travelers)
{
    auto &g = Game::Get();
    for (size_t i = 0; planet && i < travelers.size(); ++i)
    {
        if (!travelers[i])
        {
            continue;
        }
        for (unsigned ring = 2; ring <= 8; ++ring)
        {
            bool placed = false;
            for (unsigned j = 0; j < 8; ++j)
            {
                auto candidate = world::SurfaceGeometry::Advance(
                                     g.Surface()->Pose(), ring * std::cos(j * world::SurfaceGeometry::Pi / 4),
                                     ring * std::sin(j * world::SurfaceGeometry::Pi / 4),
                                     planet->terrain.Planet().radiusMeters)
                                     .origin;
                if (Clear(candidate, int(i)))
                {
                    candidate.altitudeMeters = g.Surface()->Stream().Contact(candidate).heightMeters;
                    ChangeOwner(i, CompanionPlace::Surface, candidate);
                    placed = true;
                    break;
                }
            }
            if (placed)
            {
                break;
            }
        }
    }
    Publish();
}
void PlanetParty::Validate(const PlanetPartyState &value, const world::PlanetDefinition &definition)
{
    using G = world::SurfaceGeometry;
    if (!value.worldKey.empty() &&
        value.worldKey != world::WorldIdentity::ContentKey(definition, "overworld", "party_v1"))
    {
        throw std::runtime_error("동료 기록의 행성이 다릅니다.");
    }
    for (const auto &m : value.members)
    {
        if (!m.joined)
        {
            continue;
        }
        if (!definition.storyPlanet || value.worldKey.empty() || unsigned(m.place) > 2 ||
            m.combat.health < 0 || m.combat.health > 100 || !std::isfinite(m.combat.downedSeconds) ||
            m.combat.downedSeconds < 0 || m.combat.downedSeconds > 120 || !std::isfinite(m.heading.x) ||
            !std::isfinite(m.heading.y) || !std::isfinite(m.heading.z) ||
            std::abs(G::Dot(m.heading, m.heading) - 1) > .01)
        {
            throw std::runtime_error("저장된 동료 상태가 올바르지 않습니다.");
        }
        for (float timer : {m.combat.cooldown, m.combat.hitGrace})
        {
            if (!std::isfinite(timer) || timer < 0 || timer > 120)
            {
                throw std::runtime_error("동료의 전투 대기 시간이 올바르지 않습니다.");
            }
        }
        if (!std::isfinite(m.combat.aimYaw))
        {
            throw std::runtime_error("동료의 조준 방향이 올바르지 않습니다.");
        }
        G::Validate(m.position);
    }
}

namespace
{
Vector2 Add(Vector2 a, Vector2 b)
{
    return {a.x + b.x, a.y + b.y};
}
Vector2 Sub(Vector2 a, Vector2 b)
{
    return {a.x - b.x, a.y - b.y};
}
Vector2 Mul(Vector2 a, float s)
{
    return {a.x * s, a.y * s};
}
float Dot(Vector2 a, Vector2 b)
{
    return a.x * b.x + a.y * b.y;
}
float Cross(Vector2 a, Vector2 b)
{
    return a.x * b.y - a.y * b.x;
}
Vector2 Unit(Vector2 v)
{
    return Mul(v, 1.f / (std::max)(.001f, std::sqrt(Dot(v, v))));
}
TrafficTurn ClassifyTurn(Vector2 in, Vector2 out)
{
    return Dot(in, out) > .82f   ? TrafficTurn::Straight
           : Cross(in, out) <= 0 ? TrafficTurn::Left
                                 : TrafficTurn::Right;
}
float SegmentDistance(Vector2 p, Vector2 a, Vector2 b)
{
    auto d = Sub(b, a);
    return Game::Distance(
        p, Add(a, Mul(d, Game::Clamp(Dot(Sub(p, a), d) / (std::max)(.0001f, Dot(d, d)), 0, 1))));
}
Vector2 Bezier(Vector2 a, Vector2 c, Vector2 d, Vector2 b, float t)
{
    float s = 1 - t;
    return Add(Add(Mul(a, s * s * s), Mul(c, 3 * s * s * t)), Add(Mul(d, 3 * s * t * t), Mul(b, t * t * t)));
}
Vector2 TurnPoint(const TrafficVehicle &car, float t)
{
    const auto &net = RoadNetwork::Get();
    const auto &in = net.lanes[car.lane], &out = net.lanes[car.nextLane];
    const float dot = Dot(in.direction, out.direction);
    const float chord = Game::Distance(in.end, out.start);
    // Quarter-circle control points match the native 90-degree road module's lane arcs.
    float handle = chord * (std::fabs(dot) < .001f ? .3905243f
                            : dot > .99f           ? 1.f / 3
                            : dot < -.8f           ? 2.f / 3
                                                   : .45f);
    return Bezier(in.end, Add(in.end, Mul(in.direction, handle)), Sub(out.start, Mul(out.direction, handle)),
                  out.start, t);
}
bool Signalized(const RoadJunction &j)
{
    const auto &net = RoadNetwork::Get();
    int roads = 0, last = -1;
    for (int lane : j.outgoing)
    {
        if (net.lanes[lane].road != last)
        {
            ++roads;
            last = net.lanes[lane].road;
        }
    }
    return roads >= 3;
}
} // namespace

// ============================================================================
// RoadNetwork — split every crossing so geometry and vehicle routes agree
// ============================================================================
const RoadNetwork &RoadNetwork::Get()
{
    static const RoadNetwork network;
    return network;
}
RoadNetwork::RoadNetwork()
{
    struct Source
    {
        Vector2 a, b;
        int lanes;
        float speed, heightA, heightB;
    };
    std::vector<Source> sources;
    auto add = [&](Vector2 a, Vector2 b, int lanes, float speed, float heightA = 0, float heightB = 0) {
        sources.push_back({a, b, lanes, speed, heightA, heightB});
    };
    // Streets are the primary plan: one collector with spaced T junctions, then local access loops.
    // No automatic nearby-node welding: a short link must be redesigned, never folded into a mega-junction.
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[city].center;
        auto p = [center](float x, float y) { return Add(center, {x, y}); };
        int collector = city <= 1 || city == 4 ? 4 : 2;
        add(p(-90, -17), p(60, -17), collector, 6);
        add(p(-32, -17), p(-32, 32), 2, 3.5f);
        add(p(30, -17), p(30, 32), 2, 3.5f);
        add(p(-32, 32), p(30, 32), 2, 3.5f);
        add(p(-32, 10), p(30, 10), 2, 3);
        add(p(-12, 10), p(-12, 32), 2, 3);
    }
    // Four independent regional portals preserve the scattered city positions without funneling all
    // traffic through one intersection. Narrow two-lane links remain subordinate to local collectors.
    auto cinder = Overworld::Cities()[1].center;
    add({60, -17}, {128, -17}, 2, 8);
    add({128, -17}, {180, -17}, 2, 8);
    add({180, -17}, Add(cinder, {-90, -17}), 2, 8);
    add({128, -17}, Overworld::kMineEntrance, 2, 4);
    auto south = Overworld::Cities()[2].center;
    add({30, 32}, {30, 100}, 2, 5);
    add({30, 100}, {48, 103.5f}, 2, 5);
    add({30, 100}, {-40, 150}, 2, 8);
    add({-40, 150}, Add(south, {-90, -17}), 2, 8);
    auto west = Overworld::Cities()[3].center;
    add({-90, -17}, {-155, -70}, 2, 8);
    add({-155, -70}, Add(west, {60, -17}), 2, 8);
    auto north = Overworld::Cities()[4].center;
    add({-60, -17}, {-60, -75}, 2, 5);
    add({-60, -75}, {48, -103.5f}, 2, 7);
    add({48, -103.5f}, Add(north, {-90, -17}), 2, 8);
    // Eight-metre clearance over the eastern collector. The 110 m ramps keep the usable driving grade below
    // 9%. The crossing at (48,-17) has no graph node: traffic above and below is independent.
    add({48, -103.5f}, {48, -45}, 2, 6, 0, 4.2f);
    add({48, -45}, {48, 45}, 2, 7, 4.2f, 4.2f);
    add({48, 45}, {48, 103.5f}, 2, 6, 4.2f, 0);
    auto halo = Overworld::Cities()[0].center;
    for (int city = 1; city < Overworld::kCityCount; ++city)
    {
        auto c = Overworld::Cities()[city].center;
        auto airDirection = Unit(Sub(c, halo));
        Vector2 airNormal = {-airDirection.y, airDirection.x};
        auto hub = Add(halo, {0, 8});
        airCorridors.push_back({Sub(hub, Mul(airDirection, 28)),
                                Add(Add(hub, Mul(airDirection, 30)), Mul(airNormal, 12)), Add(c, {12, 8}),
                                8.f + city * 3.f});
    }
    std::vector<std::vector<float>> cuts(sources.size(), {0, 1});
    for (size_t i = 0; i < sources.size(); ++i)
    {
        for (size_t j = i + 1; j < sources.size(); ++j)
        {
            auto r = Sub(sources[i].b, sources[i].a), s = Sub(sources[j].b, sources[j].a);
            float denominator = Cross(r, s);
            if (std::fabs(denominator) < .001f)
            {
                continue;
            }
            auto delta = Sub(sources[j].a, sources[i].a);
            float t = Cross(delta, s) / denominator, u = Cross(delta, r) / denominator;
            if (t >= -.0001f && t <= 1.0001f && u >= -.0001f && u <= 1.0001f)
            {
                float hi = sources[i].heightA + (sources[i].heightB - sources[i].heightA) * t;
                float hj = sources[j].heightA + (sources[j].heightB - sources[j].heightA) * u;
                if (std::fabs(hi - hj) < .05f)
                {
                    cuts[i].push_back(Game::Clamp(t, 0, 1));
                    cuts[j].push_back(Game::Clamp(u, 0, 1));
                }
            }
        }
    }
    auto node = [&](Vector2 p, float height) {
        for (size_t i = 0; i < junctions.size(); ++i)
        {
            if (Game::Distance(p, junctions[i].position) < .12f &&
                std::fabs(height - junctions[i].altitude) < .05f)
            {
                return int(i);
            }
        }
        junctions.push_back({p, 3, {}, height});
        return int(junctions.size() - 1);
    };
    for (size_t i = 0; i < sources.size(); ++i)
    {
        auto &split = cuts[i];
        std::sort(split.begin(), split.end());
        for (size_t j = 1; j < split.size(); ++j)
        {
            auto a = Add(sources[i].a, Mul(Sub(sources[i].b, sources[i].a), split[j - 1]));
            auto b = Add(sources[i].a, Mul(Sub(sources[i].b, sources[i].a), split[j]));
            if (Game::Distance(a, b) < .2f)
            {
                continue;
            }
            auto height = [&](float t) {
                return sources[i].heightA + (sources[i].heightB - sources[i].heightA) * t;
            };
            int from = node(a, height(split[j - 1])), to = node(b, height(split[j]));
            if (from == to)
            {
                continue;
            }
            roads.push_back({from, to, sources[i].lanes, sources[i].speed});
        }
    }
    std::vector<int> remap(junctions.size(), -1);
    std::vector<RoadJunction> compact;
    for (const auto &r : roads)
    {
        for (int node : {r.from, r.to})
        {
            if (remap[node] < 0)
            {
                remap[node] = int(compact.size());
                compact.push_back(junctions[node]);
            }
        }
    }
    for (auto &r : roads)
    {
        r.from = remap[r.from];
        r.to = remap[r.to];
    }
    junctions.swap(compact);
    TransitLayout::ConfigureJunctions(*this);
    for (size_t i = 0; i < roads.size(); ++i)
    {
        const auto &road = roads[i];
        for (int direction = 0; direction < 2; ++direction)
        {
            int from = direction ? road.to : road.from, to = direction ? road.from : road.to;
            auto d = Unit(Sub(junctions[to].position, junctions[from].position));
            Vector2 right = RightOf(d);
            for (int slot = 0; slot < road.lanes / 2; ++slot)
            {
                auto offset = Mul(right, (slot + .5f) * kLaneWidth);
                auto start = Add(Add(junctions[from].position, Mul(d, junctions[from].radius)), offset);
                auto end = Add(Sub(junctions[to].position, Mul(d, junctions[to].radius)), offset);
                junctions[from].outgoing.push_back(int(lanes.size()));
                lanes.push_back(
                    {int(i), from, to, slot, start, end, d, Game::Distance(start, end), road.speedLimit});
            }
        }
    }
    // Broad phase shared by collision, zoning and pedestrian graph construction.
    auto insert = [&](Vector2 a, Vector2 b, int index, bool junction) {
        for (int x = int(std::floor((std::min)(a.x, b.x) / 16));
             x <= int(std::floor((std::max)(a.x, b.x) / 16)); ++x)
        {
            for (int y = int(std::floor((std::min)(a.y, b.y) / 16));
                 y <= int(std::floor((std::max)(a.y, b.y) / 16)); ++y)
            {
                (junction ? cells[{x, y}].junctions : cells[{x, y}].roads).push_back(index);
            }
        }
    };
    for (size_t i = 0; i < roads.size(); ++i)
    {
        auto a = junctions[roads[i].from].position, b = junctions[roads[i].to].position;
        insert({(std::min)(a.x, b.x) - 8, (std::min)(a.y, b.y) - 8},
               {(std::max)(a.x, b.x) + 8, (std::max)(a.y, b.y) + 8}, int(i), false);
    }
    for (size_t i = 0; i < junctions.size(); ++i)
    {
        auto p = junctions[i].position;
        float r = junctions[i].radius + 12;
        insert({p.x - r, p.y - r}, {p.x + r, p.y + r}, int(i), true);
    }
}
bool RoadNetwork::Contains(Vector2 position, float margin) const
{
    auto roadContains = [&](int index) {
        const auto &road = roads[index];
        auto a = junctions[road.from].position, b = junctions[road.to].position;
        auto direction = Unit(Sub(b, a));
        auto start = Add(a, Mul(direction, junctions[road.from].radius));
        float span = Game::Distance(a, b) - junctions[road.from].radius - junctions[road.to].radius;
        auto offset = Sub(position, start);
        float along = Dot(offset, direction);
        if (RoadElevation(index, along / span) > 1.4f)
        {
            return false; // Keep low ramp mouths reserved; the clear span can be walked below.
        }
        return along >= -margin && along <= span + margin &&
               std::fabs(Cross(offset, direction)) <= road.lanes * kLaneWidth * .5f + margin;
    };
    if (margin <= 2)
    {
        auto found = cells.find({int(std::floor(position.x / 16)), int(std::floor(position.y / 16))});
        if (found == cells.end())
        {
            return false;
        }
        for (int road : found->second.roads)
        {
            if (roadContains(road))
            {
                return true;
            }
        }
        for (int junction : found->second.junctions)
        {
            if (junctions[junction].altitude < .1f &&
                TransitLayout::ContainsJunction(junction, position, margin))
            {
                return true;
            }
        }
    }
    else
    {
        for (size_t i = 0; i < roads.size(); ++i)
        {
            if (roadContains(int(i)))
            {
                return true;
            }
        }
        for (size_t i = 0; i < junctions.size(); ++i)
        {
            if (junctions[i].altitude < .1f && TransitLayout::ContainsJunction(int(i), position, margin))
            {
                return true;
            }
        }
    }
    return false;
}
Vector2 RoadNetwork::LanePosition(int lane, float distance) const
{
    return Add(lanes[lane].start, Mul(lanes[lane].direction, Game::Clamp(distance, 0, lanes[lane].length)));
}
bool RoadNetwork::ContainsFootprint(Vector2 position, float margin) const
{
    for (const auto &road : roads)
    {
        auto a = junctions[road.from].position, b = junctions[road.to].position;
        if (SegmentDistance(position, a, b) <= road.lanes * kLaneWidth * .5f + margin)
        {
            return true;
        }
    }
    for (size_t j = 0; j < junctions.size(); ++j)
    {
        if (TransitLayout::ContainsJunction(int(j), position, margin))
        {
            return true;
        }
    }
    return false;
}
float RoadNetwork::RoadElevation(int index, float fraction) const
{
    const auto &road = roads[index];
    float t = Game::Clamp(fraction, 0, 1);
    const float a = junctions[road.from].altitude, b = junctions[road.to].altitude;
    if (std::fabs(a - b) > .1f)
    {
        // The delivered 110m ramp has 16m parabolic end curves and 78m constant grade.
        // Its actual surface is faceted at 0.5m authoring intervals: interpolate those
        // same heights so wheels cannot sink into the authored vertical curves.
        auto profile = [](float x) {
            constexpr float grade = 8.4f / 94.f;
            return x < 16   ? grade * x * x / 32
                   : x > 94 ? 8.4f - grade * (110 - x) * (110 - x) / 32
                            : grade * (x - 8);
        };
        float x = t * 110, low = std::floor(x * 2) * .5f;
        t = (profile(low) + (profile((std::min)(110.f, low + .5f)) - profile(low)) * (x - low) * 2) / 8.4f;
    }
    return a + (b - a) * t;
}
bool RoadNetwork::ContainsSurface(Vector2 position, float height, float margin) const
{
    for (size_t index = 0; index < roads.size(); ++index)
    {
        const auto &r = roads[index];
        auto a = junctions[r.from].position, b = junctions[r.to].position, d = Unit(Sub(b, a));
        a = Add(a, Mul(d, junctions[r.from].radius));
        b = Sub(b, Mul(d, junctions[r.to].radius));
        float span = Game::Distance(a, b), along = Dot(Sub(position, a), d);
        if (along >= -margin && along <= span + margin &&
            std::fabs(Cross(Sub(position, a), d)) <= r.lanes * kLaneWidth * .5f + margin &&
            std::fabs(RoadElevation(int(index), along / span) - height) < .15f)
        {
            return true;
        }
    }
    for (size_t j = 0; j < junctions.size(); ++j)
    {
        if (std::fabs(junctions[j].altitude - height) < .15f &&
            TransitLayout::ContainsJunction(int(j), position, margin))
        {
            return true;
        }
    }
    return false;
}
float RoadNetwork::DriveElevation(Vector2 position, float previousHeight) const
{
    float result = 0, best = 10000;
    for (size_t index = 0; index < roads.size(); ++index)
    {
        const auto &road = roads[index];
        auto a = junctions[road.from].position, b = junctions[road.to].position, d = Unit(Sub(b, a));
        a = Add(a, Mul(d, junctions[road.from].radius));
        b = Sub(b, Mul(d, junctions[road.to].radius));
        float t = Game::Clamp(Dot(Sub(position, a), Sub(b, a)) / Dot(Sub(b, a), Sub(b, a)), 0, 1);
        float distance = Game::Distance(position, Add(a, Mul(Sub(b, a), t)));
        float height = RoadElevation(int(index), t);
        if (distance > road.lanes * kLaneWidth * .5f + .2f || std::fabs(height - previousHeight) > .6f)
        {
            continue;
        }
        float score = distance + std::fabs(height - previousHeight) * 8;
        if (score < best)
        {
            best = score;
            result = height;
        }
    }
    for (size_t j = 0; j < junctions.size(); ++j)
    {
        if (std::fabs(junctions[j].altitude - previousHeight) < .6f &&
            TransitLayout::ContainsJunction(int(j), position, 0) && best > 1)
        {
            return junctions[j].altitude;
        }
    }
    return result;
}
bool RoadNetwork::CanTurn(int index, TrafficTurn turn) const
{
    const auto &lane = lanes[index];
    bool straight = false, left = false, right = false;
    int arms = 0;
    for (int outIndex : junctions[lane.to].outgoing)
    {
        const auto &out = lanes[outIndex];
        if (out.slot == 0)
        {
            ++arms;
        }
        if (out.road == lane.road)
        {
            continue;
        }
        auto direction = ClassifyTurn(lane.direction, out.direction);
        straight |= direction == TrafficTurn::Straight;
        left |= direction == TrafficTurn::Left;
        right |= direction == TrafficTurn::Right;
    }
    if (arms == 1)
    {
        return turn == TrafficTurn::Left;
    }
    bool exists = turn == TrafficTurn::Straight ? straight : turn == TrafficTurn::Left ? left : right;
    if (!exists)
    {
        return false;
    }
    if (arms == 2 || turn == TrafficTurn::Straight)
    {
        return true;
    }
    int count = roads[lane.road].lanes / 2;
    if (turn == TrafficTurn::Left)
    {
        return lane.slot == 0 || (!straight && (!right || lane.slot < count - 1));
    }
    return lane.slot == count - 1 || (!straight && !left);
}

Vector2 RoadNetwork::RightOf(Vector2 direction)
{
    // The screen's downward Y axis makes this the displayed driver's right.
    return {-direction.y, direction.x};
}
bool RoadNetwork::IsSignalized(int junction) const
{
    return junction >= 0 && junction < int(junctions.size()) && Signalized(junctions[junction]);
}
int RoadNetwork::SignalAxis(int index, int atJunction) const
{
    const auto &lane = lanes[index];
    const auto &junction = junctions[atJunction < 0 ? lane.to : atJunction];
    int reference = junction.outgoing.front();
    for (int candidate : junction.outgoing)
    {
        if (roads[lanes[candidate].road].lanes > roads[lanes[reference].road].lanes)
        {
            reference = candidate;
        }
    }
    return std::fabs(Dot(lane.direction, lanes[reference].direction)) >= .7071f ? 0 : 1;
}

// ============================================================================
// TrafficSystem — simulation is bounded; rendering never advances vehicle state
// ============================================================================
TrafficSystem &Overworld::Traffic()
{
    if (auto cities = Game::Get().SurfaceCities())
    {
        if (auto active = cities->ActiveTraffic())
        {
            return *active;
        }
    }
    static TrafficSystem traffic;
    return traffic;
}
bool TrafficSystem::CoreLane(int index, int city)
{
    const auto &net = RoadNetwork::Get();
    if (city < -1 || city >= Overworld::kCityCount || index < 0 || size_t(index) >= net.lanes.size())
    {
        return false;
    }
    if (city == -1)
    {
        return true;
    }
    const auto &lane = net.lanes[size_t(index)];
    auto center = Overworld::Cities()[size_t(city)].center;
    const auto &a = net.junctions[size_t(lane.from)], &b = net.junctions[size_t(lane.to)];
    return a.altitude == 0 && b.altitude == 0 && Game::Distance(a.position, center) <= 110 &&
           Game::Distance(b.position, center) <= 110;
}
void TrafficSystem::SetCityFilter(int city)
{
    if (city < -1 || city >= Overworld::kCityCount)
    {
        throw std::invalid_argument("Invalid traffic city");
    }
    cityFilter = city;
    Reset();
}
unsigned int TrafficSystem::Random()
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
void TrafficSystem::Reset()
{
    vehicles.clear();
    signals.assign(RoadNetwork::Get().junctions.size(), {});
    randomState = 0x71b39a5u;
    nextId = 0;
    accumulator = spawnTimer = impactCooldown = 0;
    for (size_t i = 0; i < signals.size(); ++i)
    {
        signals[i].phase = static_cast<SignalPhase>(int(i % 4) * 3);
    }
}
bool TrafficSystem::OutsideView(Vector2 p, Vector2 camera, float padding, float altitude)
{
    Vector2 screen = WorldCamera::ToViewport(
        {kCanvasWidth * .5f + (p.x - p.y - camera.x + camera.y) * 36,
         kCanvasHeight * .57f + (p.x + p.y - camera.x - camera.y) * 18 - altitude * 44});
    return screen.x < -padding || screen.x > kCanvasWidth + padding || screen.y < -padding ||
           screen.y > kCanvasHeight + padding;
}
Vector2 TrafficSystem::RenderPosition(size_t index) const
{
    const auto &car = vehicles[index];
    return Add(car.previousPosition, Mul(Sub(car.position, car.previousPosition), accumulator * 30));
}
float TrafficSystem::RenderYaw(size_t index) const
{
    const auto &car = vehicles[index];
    return car.previousYaw + std::remainder(car.yaw - car.previousYaw, 2 * kPi) * accumulator * 30;
}
float TrafficSystem::RenderElevation(size_t index) const
{
    const auto &car = vehicles[index];
    const auto &net = RoadNetwork::Get();
    const auto &lane = net.lanes[car.lane];
    if (car.turnDistance >= 0)
    {
        return net.junctions[lane.to].altitude;
    }
    float fraction = car.distance / (std::max)(.001f, lane.length);
    if (lane.from != net.roads[lane.road].from)
    {
        fraction = 1 - fraction;
    }
    return net.RoadElevation(lane.road, fraction);
}
bool TrafficSystem::DamageVehicle(std::uint64_t id, int damage, bool playerOwned)
{
    auto car =
        std::find_if(vehicles.begin(), vehicles.end(), [=](const TrafficVehicle &v) { return v.id == id; });
    if (car == vehicles.end() || car->health <= 0 || damage <= 0)
    {
        return false;
    }
    car->health = (std::max)(0, car->health - damage);
    car->hitFlash = .15f;
    if (playerOwned && car->occupied && car->health == 0)
    {
        CivicJustice::Report(Offense::VehicleDestruction, car->position);
    }
    if (car->health <= 0)
    {
        car->speed = 0;
        car->reservedFrom = car->reservedTo = -1;
    }
    if (Game::Get().SurfaceTraffic())
    {
        Game::Get().SurfaceTraffic()->SyncDamage(*car);
    }
    return true;
}
void TrafficSystem::StopForImpact(std::uint64_t id)
{
    auto car =
        std::find_if(vehicles.begin(), vehicles.end(), [=](const TrafficVehicle &v) { return v.id == id; });
    if (car == vehicles.end())
    {
        return;
    }
    car->speed = 0;
    car->impactCooldown = (std::max)(car->impactCooldown, .6f);
}
TrafficLight TrafficSystem::Light(int laneIndex, TrafficTurn turn) const
{
    const auto &net = RoadNetwork::Get();
    const auto &lane = net.lanes[laneIndex];
    if (!Allows(laneIndex, turn))
    {
        return TrafficLight::Red;
    }
    if (!Signalized(net.junctions[lane.to]))
    {
        return TrafficLight::Green;
    }
    if (signals.empty())
    {
        return TrafficLight::Red;
    }
    int phase = int(signals[lane.to].phase);
    int axis = net.SignalAxis(laneIndex) * 6;
    int greenPhase = axis + (turn == TrafficTurn::Left ? 3 : 0);
    if (phase == greenPhase)
    {
        return TrafficLight::Green;
    }
    return phase == greenPhase + 1 ? TrafficLight::Amber : TrafficLight::Red;
}
bool TrafficSystem::Allows(int index, TrafficTurn turn) const
{
    const auto &net = RoadNetwork::Get();
    if (cityFilter < 0)
    {
        return net.CanTurn(index, turn);
    }
    if (!CoreLane(index, cityFilter))
    {
        return false;
    }
    const auto &lane = net.lanes[size_t(index)];
    bool straight = false, left = false, right = false;
    int arms = 0;
    for (int outgoing : net.junctions[size_t(lane.to)].outgoing)
    {
        if (!CoreLane(outgoing, cityFilter))
        {
            continue;
        }
        const auto &out = net.lanes[size_t(outgoing)];
        if (out.slot == 0)
        {
            ++arms;
        }
        if (out.road == lane.road)
        {
            continue;
        }
        auto direction = ClassifyTurn(lane.direction, out.direction);
        straight |= direction == TrafficTurn::Straight;
        left |= direction == TrafficTurn::Left;
        right |= direction == TrafficTurn::Right;
    }
    if (arms == 1)
    {
        return turn == TrafficTurn::Left;
    }
    bool exists = turn == TrafficTurn::Straight ? straight : turn == TrafficTurn::Left ? left : right;
    if (!exists)
    {
        return false;
    }
    if (arms == 2 || turn == TrafficTurn::Straight)
    {
        return true;
    }
    int count = net.roads[size_t(lane.road)].lanes / 2;
    return turn == TrafficTurn::Left ? lane.slot == 0 || (!straight && (!right || lane.slot < count - 1))
                                     : lane.slot == count - 1 || (!straight && !left);
}
bool TrafficSystem::HasRightTurnSignal(int lane) const
{
    const auto &net = RoadNetwork::Get();
    return net.IsSignalized(net.lanes[lane].to) && net.roads[net.lanes[lane].road].lanes >= 6;
}
bool TrafficSystem::PedestrianGreen(int junction, int road) const
{
    if (junction < 0 || junction >= int(signals.size()) || signals[junction].phase != SignalPhase::Walk)
    {
        return false;
    }
    const auto &net = RoadNetwork::Get();
    if (!net.IsSignalized(junction))
    {
        return false;
    }
    std::vector<int> arms;
    int width = 0;
    for (int lane : net.junctions[junction].outgoing)
    {
        if (net.lanes[lane].slot != 0)
        {
            continue;
        }
        arms.push_back(lane);
        width = (std::max)(width, net.roads[net.lanes[lane].road].lanes);
    }
    if (road < 0 || width >= 6)
    {
        return true;
    }
    // Local streets serve one crossing per cycle; collectors serve an opposing pair.
    const auto cycle = signals[junction].walkCycle;
    if (width <= 2)
    {
        return net.lanes[arms[cycle % arms.size()]].road == road;
    }
    bool hasAxis[2] = {false, false};
    for (int lane : arms)
    {
        hasAxis[net.SignalAxis(lane, junction)] = true;
    }
    int selected = hasAxis[0] && hasAxis[1] ? int(cycle % 2) : hasAxis[0] ? 0 : 1;
    for (int lane : arms)
    {
        if (net.lanes[lane].road == road)
        {
            return net.SignalAxis(lane, junction) == selected;
        }
    }
    return false;
}
unsigned int TrafficSystem::PedestrianGroups(int junction) const
{
    const auto &net = RoadNetwork::Get();
    unsigned int arms = 0;
    int width = 0;
    bool axes[2] = {false, false};
    for (int lane : net.junctions[junction].outgoing)
    {
        if (net.lanes[lane].slot != 0)
        {
            continue;
        }
        ++arms;
        width = (std::max)(width, net.roads[net.lanes[lane].road].lanes);
        axes[net.SignalAxis(lane, junction)] = true;
    }
    return width >= 6 ? 1 : width <= 2 ? arms : unsigned(axes[0]) + unsigned(axes[1]);
}
int TrafficSystem::CrossingRoad(int junction, Vector2 position) const
{
    const auto &net = RoadNetwork::Get();
    if (junction < 0 || junction >= int(net.junctions.size()))
    {
        return -1;
    }
    int result = -1;
    float best = 1.e9f;
    const auto &j = net.junctions[junction];
    for (int index : j.outgoing)
    {
        const auto &lane = net.lanes[index];
        if (lane.slot != 0)
        {
            continue;
        }
        auto center = Add(j.position, Mul(lane.direction, j.radius + 1.f));
        auto normal = RoadNetwork::RightOf(lane.direction);
        float half = net.roads[lane.road].lanes * RoadNetwork::kLaneWidth * .5f + 1.f;
        float distance =
            SegmentDistance(position, Add(center, Mul(normal, half)), Sub(center, Mul(normal, half)));
        if (distance < best)
        {
            best = distance;
            result = lane.road;
        }
    }
    return result;
}
float TrafficSystem::PhaseDuration(int junction) const
{
    const auto phase = signals.at(size_t(junction)).phase;
    if (phase == SignalPhase::Walk)
    {
        float width = 0;
        const auto &net = RoadNetwork::Get();
        for (int lane : net.junctions[junction].outgoing)
        {
            if (PedestrianGreen(junction, net.lanes[lane].road))
            {
                width = (std::max)(width, net.roads[net.lanes[lane].road].lanes * RoadNetwork::kLaneWidth);
            }
        }
        return (std::max)(8.f, (width + 1.9f) / .5f + 3.f);
    }
    if (phase == SignalPhase::WalkClear)
    {
        return 2;
    }
    return int(phase) % 3 == 0 ? (int(phase) % 6 == 0 ? 10.f : 5.f) : 3.f;
}
bool TrafficSystem::CanStartCrossing(int junction, float travelSeconds, int road) const
{
    return PedestrianGreen(junction, road) &&
           PhaseDuration(junction) - signals[junction].elapsed >= travelSeconds + 1.f;
}
bool TrafficSystem::CrossingOccupied(const GameState &world, int junction) const
{
    if (!world.isBikeMounted)
    {
        const auto &net = RoadNetwork::Get();
        const auto &j = net.junctions[junction];
        for (int index : j.outgoing)
        {
            const auto &lane = net.lanes[index];
            if (lane.slot != 0)
            {
                continue;
            }
            auto center = Add(j.position, Mul(lane.direction, j.radius + 1));
            auto offset = Sub(world.playerPosition, center);
            if (std::fabs(Dot(offset, lane.direction)) <= 1.05f &&
                std::fabs(Cross(offset, lane.direction)) <=
                    net.roads[lane.road].lanes * RoadNetwork::kLaneWidth * .5f + .25f)
            {
                return true;
            }
        }
    }
    return std::any_of(world.residents.begin(), world.residents.end(),
                       [&world, junction, this](const Resident &resident) {
                           if (&resident - world.residents.data() == Overworld::kKainResidentSlot &&
                               game::GameLevels::Chapter().GetState().kainJoined)
                           {
                               return false;
                           }
                           return (cityFilter < 0 || resident.cityIndex == cityFilter) &&
                                  resident.crossingJunction == junction;
                       });
}
int TrafficSystem::NearbyCrossing(Vector2 position) const
{
    const auto &net = RoadNetwork::Get();
    int result = -1;
    float best = 3.2f;
    for (size_t i = 0; i < net.junctions.size(); ++i)
    {
        const auto &j = net.junctions[i];
        if (!Signalized(j))
        {
            continue;
        }
        for (int index : j.outgoing)
        {
            const auto &lane = net.lanes[index];
            auto across = Add(j.position, Mul(lane.direction, j.radius + 1.f));
            Vector2 normal = {-lane.direction.y, lane.direction.x};
            float half = net.roads[lane.road].lanes * RoadNetwork::kLaneWidth * .5f + .7f;
            float distance =
                SegmentDistance(position, Add(across, Mul(normal, half)), Sub(across, Mul(normal, half)));
            if (distance < best)
            {
                best = distance;
                result = int(i);
            }
        }
    }
    return result;
}
bool TrafficSystem::SweptHit(Vector2 from, Vector2 to, float yaw, Vector2 p, float padding)
{
    // Minkowski-expanded vehicle footprint, swept through this fixed simulation step.
    Vector2 direction = {std::cos(yaw), std::sin(yaw)}, side = {-direction.y, direction.x};
    auto a = Sub(p, from), delta = Sub(to, from);
    float minT = 0, maxT = 1;
    for (int axis = 0; axis < 2; ++axis)
    {
        auto basis = axis ? side : direction;
        float extent = (axis ? .58f : 1.45f) + padding;
        float origin = Dot(a, basis), speed = -Dot(delta, basis);
        if (std::fabs(speed) < .00001f)
        {
            if (std::fabs(origin) > extent)
            {
                return false;
            }
            continue;
        }
        float t0 = (-extent - origin) / speed, t1 = (extent - origin) / speed;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        minT = (std::max)(minT, t0);
        maxT = (std::min)(maxT, t1);
        if (minT > maxT)
        {
            return false;
        }
    }
    return true;
}
void TrafficSystem::ChooseExit(TrafficVehicle &car)
{
    const auto &net = RoadNetwork::Get();
    const auto &lane = net.lanes[car.lane];
    std::vector<int> candidates;
    for (int index : net.junctions[lane.to].outgoing)
    {
        if (!CoreLane(index, cityFilter))
        {
            continue;
        }
        const auto &out = net.lanes[index];
        if (out.road == lane.road)
        {
            continue;
        }
        TrafficTurn turn = ClassifyTurn(lane.direction, out.direction);
        if (!Allows(car.lane, turn))
        {
            continue;
        }
        int targetSlot = turn == TrafficTurn::Right && Signalized(net.junctions[lane.to])
                             ? net.roads[out.road].lanes / 2 - 1
                             : (std::min)(lane.slot, net.roads[out.road].lanes / 2 - 1);
        if (out.slot == targetSlot)
        {
            candidates.push_back(index);
        }
    }
    if (candidates.empty())
    {
        // A terminal or a lane drop has a continuous, reserved U-turn; no teleport/wrap.
        for (int index : net.junctions[lane.to].outgoing)
        {
            if (!CoreLane(index, cityFilter))
            {
                continue;
            }
            if (net.lanes[index].road == lane.road && net.lanes[index].slot == 0)
            {
                candidates.push_back(index);
                break;
            }
        }
    }
    car.nextLane = candidates[Random() % candidates.size()];
    const auto &out = net.lanes[car.nextLane];
    car.turn = ClassifyTurn(lane.direction, out.direction);
    car.turnLength = 0;
    auto previous = TurnPoint(car, 0);
    for (int i = 1; i <= 24; ++i)
    {
        auto p = TurnPoint(car, i / 24.f);
        car.turnLength += Game::Distance(p, previous);
        previous = p;
    }
    car.turnLength = (std::max)(.1f, car.turnLength);
}
void TrafficSystem::Populate(const GameState &world)
{
    const auto &net = RoadNetwork::Get();
    float localLength = 0;
    for (const auto &lane : net.lanes)
    {
        if (!CoreLane(int(&lane - net.lanes.data()), cityFilter))
        {
            continue;
        }
        float projection = Dot(Sub(world.playerPosition, lane.start), lane.direction);
        auto nearest =
            net.LanePosition(int(&lane - net.lanes.data()), Game::Clamp(projection, 0, lane.length));
        if (Game::Distance(nearest, world.playerPosition) < 100)
        {
            localLength += (std::min)(lane.length, 120.f);
        }
    }
    const auto desired = (std::min)(kVehicleLimit, size_t((std::max)(12.f, localLength / 22)));
    // Leave queue storage between adjacent junctions; short links are populated only by incoming cars.
    for (int attempt = 0; attempt < 100 && vehicles.size() < desired; ++attempt)
    {
        int laneIndex = int(Random() % net.lanes.size());
        if (!CoreLane(laneIndex, cityFilter))
        {
            continue;
        }
        const auto &lane = net.lanes[laneIndex];
        float projection = Dot(Sub(world.playerPosition, lane.start), lane.direction);
        float distance =
            Game::Clamp(projection + (int(Random() % 181) - 90), 2, (std::max)(2.f, lane.length - 5));
        if (lane.length < 18)
        {
            continue;
        }
        auto p = net.LanePosition(laneIndex, distance);
        float range = Game::Distance(p, world.playerPosition);
        float fraction = distance / lane.length;
        if (lane.from != net.roads[lane.road].from)
        {
            fraction = 1 - fraction;
        }
        float height = net.RoadElevation(lane.road, fraction);
        if (range < 28 || range > 100 || !OutsideView(p, world.cameraPosition, 240, height) ||
            !OutsideView(p, world.playerPosition, 240, height))
        {
            continue;
        }
        bool crowded = false;
        for (const auto &car : vehicles)
        {
            if (Game::Distance(p, car.position) < 7 || (car.reservedTo == laneIndex && distance < 7))
            {
                crowded = true;
                break;
            }
        }
        if (crowded)
        {
            continue;
        }
        TrafficVehicle car;
        car.id = nextId++;
        car.lane = laneIndex;
        car.distance = distance;
        car.position = car.previousPosition = p;
        car.speed = lane.speedLimit * .65f;
        car.yaw = car.previousYaw = std::atan2(lane.direction.y, lane.direction.x);
        ChooseExit(car);
        vehicles.push_back(car);
    }
}
void TrafficSystem::Update(GameState &world, float dt)
{
    if (signals.empty())
    {
        Reset();
    }
    accumulator += Game::Clamp(dt, 0, .1f);
    // 30 Hz decisions; motion follows the lane continuously between decision boundaries.
    constexpr float step = 1.f / 30;
    while (accumulator >= step)
    {
        Step(world, step);
        accumulator -= step;
    }
}
size_t TrafficSystem::AdvanceDormant(float dt)
{
    dt = Game::Clamp(dt, 0, .05f);
    if (dt <= 0)
    {
        return 0;
    }
    accumulator = 0;
    spawnTimer = (std::max)(0.f, spawnTimer - dt);
    impactCooldown = (std::max)(0.f, impactCooldown - dt);
    for (auto &car : vehicles)
    {
        car.previousPosition = car.position;
        car.previousYaw = car.yaw;
        car.impactCooldown = (std::max)(0.f, car.impactCooldown - dt);
        car.hitFlash = (std::max)(0.f, car.hitFlash - dt);
        if (car.health <= 0)
        {
            car.speed = 0;
            car.wreckSeconds += dt;
        }
    }
    auto before = vehicles.size();
    vehicles.erase(std::remove_if(vehicles.begin(), vehicles.end(),
                                  [](const auto &car) { return car.health <= 0 && car.wreckSeconds > 12; }),
                   vehicles.end());
    // Live vehicles, crossing reservations and signal phases stay consistent while hidden.
    // We never replay missed routing/contacts or manufacture traffic on re-entry.
    return before - vehicles.size();
}
void TrafficSystem::Step(GameState &world, float dt)
{
    const auto &net = RoadNetwork::Get();
    impactCooldown = (std::max)(0.f, impactCooldown - dt);
    for (size_t i = 0; i < signals.size(); ++i)
    {
        if (cityFilter >= 0 &&
            Game::Distance(net.junctions[i].position, Overworld::Cities()[size_t(cityFilter)].center) > 110)
        {
            continue;
        }
        auto &signal = signals[i];
        signal.elapsed += dt;
        float duration = PhaseDuration(int(i));
        if (signal.elapsed < duration)
        {
            continue;
        }
        bool occupied = false;
        for (const auto &car : vehicles)
        {
            if (car.reservedFrom >= 0 && net.lanes[car.reservedFrom].to == int(i))
            {
                occupied = true;
                break;
            }
        }
        const bool clearance = signal.phase == SignalPhase::WalkClear || int(signal.phase) % 3 == 2;
        auto next = static_cast<SignalPhase>((int(signal.phase) + 1) % 14);
        for (int skipped = 0; skipped < 4 && int(next) < 12 && int(next) % 3 == 0; ++skipped)
        {
            bool available = false;
            int axis = int(next) / 6;
            bool left = int(next) % 6 == 3;
            for (size_t lane = 0; lane < net.lanes.size(); ++lane)
            {
                if (!CoreLane(int(lane), cityFilter))
                {
                    continue;
                }
                if (net.lanes[lane].to == int(i) && net.SignalAxis(int(lane)) == axis &&
                    (left
                         ? Allows(int(lane), TrafficTurn::Left)
                         : Allows(int(lane), TrafficTurn::Straight) || Allows(int(lane), TrafficTurn::Right)))
                {
                    available = true;
                    break;
                }
            }
            if (available)
            {
                break;
            }
            next = static_cast<SignalPhase>((int(next) + 3) % 14);
        }
        if (clearance && occupied)
        {
            continue;
        }
        if (signal.phase == SignalPhase::WalkClear)
        {
            // Serve every crossing group within this cycle, not one group every several minutes.
            if (++signal.walkCycle < PedestrianGroups(int(i)))
            {
                next = SignalPhase::Walk;
            }
            else
            {
                signal.walkCycle = 0;
            }
        }
        signal.phase = next;
        signal.elapsed = 0;
    }
    // Reservations cover intersecting paths, rather than locking every lane of a junction.
    auto conflicts = [&](int from, int to, const TrafficVehicle &other) {
        if (other.reservedFrom < 0 || net.lanes[other.reservedFrom].to != net.lanes[from].to)
        {
            return false;
        }
        if (to == other.reservedTo)
        {
            return true;
        }
        TrafficVehicle candidate, existing;
        candidate.lane = from;
        candidate.nextLane = to;
        existing.lane = other.reservedFrom;
        existing.nextLane = other.reservedTo;
        for (int a = 0; a < 12; ++a)
        {
            auto p = TurnPoint(candidate, a / 12.f), q = TurnPoint(candidate, (a + 1) / 12.f);
            for (int b = 0; b < 12; ++b)
            {
                auto r = TurnPoint(existing, b / 12.f), t = TurnPoint(existing, (b + 1) / 12.f);
                auto pq = Sub(q, p), rt = Sub(t, r);
                float denom = Cross(pq, rt);
                if (std::fabs(denom) > .00001f)
                {
                    auto delta = Sub(r, p);
                    float u = Cross(delta, rt) / denom, v = Cross(delta, pq) / denom;
                    if (u >= 0 && u <= 1 && v >= 0 && v <= 1)
                    {
                        return true;
                    }
                }
                if ((std::min)({SegmentDistance(p, r, t), SegmentDistance(q, r, t), SegmentDistance(r, p, q),
                                SegmentDistance(t, p, q)}) < 1.35f)
                {
                    return true;
                }
            }
        }
        return false;
    };
    for (auto &car : vehicles)
    {
        if (car.reservedFrom >= 0 && car.lane != car.reservedFrom && car.turnDistance < 0 &&
            car.distance >= 2)
        {
            car.reservedFrom = car.reservedTo = -1;
        }
    }
    for (auto &car : vehicles)
    {
        car.impactCooldown = (std::max)(0.f, car.impactCooldown - dt);
        car.hitFlash = (std::max)(0.f, car.hitFlash - dt);
        if (car.health <= 0)
        {
            car.previousPosition = car.position;
            car.speed = 0;
            car.wreckSeconds += dt;
            car.reservedFrom = car.reservedTo = -1;
            continue;
        }
        car.previousPosition = car.position;
        car.previousYaw = car.yaw;
        const auto &lane = net.lanes[car.lane];
        float target = car.impactCooldown > 0 ? 0 : lane.speedLimit;
        float gap = 10000;
        bool permissiveRight = false;
        bool committed = car.reservedFrom == car.lane;
        if (committed && car.yieldingRight)
        {
            target = (std::min)(target, 1.5f);
        }
        for (const auto &other : vehicles)
        {
            if (car.id == other.id)
            {
                continue;
            }
            if (other.lane == car.lane && other.turnDistance < 0 && other.distance > car.distance)
            {
                gap = (std::min)(gap, other.distance - car.distance - 3.5f);
            }
        }
        if (car.turnDistance < 0)
        {
            bool exitBlocked = false;
            for (const auto &other : vehicles)
            {
                if (other.id != car.id && other.lane == car.nextLane && other.distance < 6.5f)
                {
                    exitBlocked = true;
                    break;
                }
            }
            bool red = !committed && Light(car.lane, car.turn) != TrafficLight::Green;
            if (car.distance >= lane.length - 4.1f && car.speed < .08f)
            {
                car.stoppedSeconds += dt;
            }
            else if (car.distance < lane.length - 4.1f)
            {
                car.stoppedSeconds = 0;
            }
            // Ordinary Korean right turns: stop on red, then creep only after yielding.
            // Six-lane arterials have a dedicated right-turn signal instead.
            permissiveRight =
                red && car.turn == TrafficTurn::Right && !HasRightTurnSignal(car.lane) &&
                car.stoppedSeconds >= .8f && Light(car.lane, TrafficTurn::Right) == TrafficLight::Red &&
                int(signals[lane.to].phase) % 3 == 0 && signals[lane.to].phase != SignalPhase::WalkClear &&
                !PedestrianGreen(lane.to, lane.road) &&
                !PedestrianGreen(lane.to, net.lanes[car.nextLane].road) && !CrossingOccupied(world, lane.to);
            if (permissiveRight)
            {
                for (const auto &other : vehicles)
                {
                    const auto &approach = net.lanes[other.lane];
                    if (other.id != car.id && approach.to == lane.to && other.turnDistance < 0 &&
                        approach.length - other.distance < 18 &&
                        Light(other.lane, other.turn) == TrafficLight::Green)
                    {
                        permissiveRight = false;
                        break;
                    }
                }
            }
            red &= !permissiveRight;
            if (permissiveRight)
            {
                target = (std::min)(target, 1.5f);
            }
            bool reserved = false;
            if (!committed && car.distance >= lane.length - 8)
            {
                for (const auto &other : vehicles)
                {
                    if (other.id != car.id && conflicts(car.lane, car.nextLane, other))
                    {
                        reserved = true;
                        break;
                    }
                }
            }
            if (committed)
            {
                exitBlocked = false;
            }
            if (red || reserved || exitBlocked)
            {
                gap = (std::min)(gap, lane.length - car.distance - 3.6f);
            }
            if (gap < 15)
            {
                target = (std::min)(target, std::sqrt((std::max)(0.f, 2 * 5.f * (gap - .25f))));
            }
            // Legal pedestrian crossings have an exclusive phase. Drivers also yield to a person
            // who remains on the crossing as it closes; a jaywalker in a live lane can be struck.
            if (!committed &&
                (PedestrianGreen(lane.to) ||
                 (signals[lane.to].phase == SignalPhase::WalkClear && CrossingOccupied(world, lane.to)) ||
                 std::any_of(world.residents.begin(), world.residents.end(), [&](const Resident &r) {
                     return (cityFilter < 0 || r.cityIndex == cityFilter) && r.crossingJunction == lane.to;
                 })))
            {
                gap = (std::min)(gap, lane.length - car.distance - 3.6f);
                target = (std::min)(target, (std::max)(0.f, gap * 2));
            }
        }
        else
        {
            target = (std::min)(lane.speedLimit, car.yieldingRight                   ? 1.5f
                                                 : car.turn == TrafficTurn::Straight ? 5.f
                                                                                     : 3.2f);
        }
        if (car.turnDistance < 0 && !committed && car.distance >= lane.length - 4.8f &&
            gap >= lane.length - car.distance - .05f && target > .1f)
        {
            car.reservedFrom = car.lane;
            car.reservedTo = car.nextLane;
            car.yieldingRight = permissiveRight;
        }
        if (car.turn != TrafficTurn::Straight && (car.turnDistance >= 0 || car.distance > lane.length - 7))
        {
            target = (std::min)(target, 4.5f);
        }
        car.speed += Game::Clamp(target - car.speed, -7.f * dt, 3.f * dt);
        float travel = car.speed * dt;
        if (car.turnDistance < 0)
        {
            travel = (std::min)(travel, (std::max)(0.f, gap));
            if (travel < car.speed * dt - .0001f)
            {
                car.speed = travel / dt;
            }
            car.distance += travel;
            if (car.distance >= lane.length)
            {
                car.position = net.LanePosition(car.lane, lane.length);
                if (cityFilter >= 0 && Game::Get().GetState().surfaceExploration &&
                    Game::Get().SurfaceTraffic() && Game::Get().SurfaceTraffic()->Depart(cityFilter, car))
                {
                    continue;
                }
                car.turnDistance = car.distance - lane.length;
            }
            else
            {
                car.position = net.LanePosition(car.lane, car.distance);
            }
        }
        else
        {
            car.turnDistance += travel;
        }
        if (car.turnDistance >= 0)
        {
            float t = Game::Clamp(car.turnDistance / car.turnLength, 0, 1);
            car.position = TurnPoint(car, t);
            auto tangent =
                Sub(TurnPoint(car, (std::min)(1.f, t + .005f)), TurnPoint(car, (std::max)(0.f, t - .005f)));
            car.yaw = MotionMath::Turn(car.yaw, std::atan2(tangent.y, tangent.x), 2.8f * dt);
            if (t >= 1)
            {
                car.lane = car.nextLane;
                car.distance = 0;
                car.turnDistance = -1;
                car.stoppedSeconds = 0;
                car.yieldingRight = false;
                ChooseExit(car);
            }
        }
        else
        {
            car.yaw = MotionMath::Turn(car.yaw, std::atan2(lane.direction.y, lane.direction.x), 2.8f * dt);
        }
    }
    vehicles.erase(
        std::remove_if(vehicles.begin(), vehicles.end(), [](const auto &car) { return car.departed; }),
        vehicles.end());
    // Oriented hulls are a final physical constraint even when routing normally avoids contact.
    for (size_t a = 0; a < vehicles.size(); ++a)
    {
        for (size_t b = a + 1; b < vehicles.size(); ++b)
        {
            auto &first = vehicles[a], &second = vehicles[b];
            if (first.impactCooldown > 0 || second.impactCooldown > 0 ||
                Game::Distance(first.position, second.position) > 4 ||
                std::fabs(RenderElevation(a) - RenderElevation(b)) > .7f)
            {
                continue;
            }
            auto extents = [](const TrafficVehicle &car) {
                auto &mesh = models::ResourceAssets::Get(
                    models::ModelLibrary::Get().vehicles[size_t(car.id % 4)].body.resourceId);
                return Vector2{mesh.physicalSize.x * .5f, mesh.physicalSize.y * .5f};
            };
            auto ha = extents(first), hb = extents(second);
            Vector2 ax = {std::cos(first.yaw), std::sin(first.yaw)}, ay = {-ax.y, ax.x};
            Vector2 bx = {std::cos(second.yaw), std::sin(second.yaw)}, by = {-bx.y, bx.x};
            bool overlap = true;
            for (auto axis : {ax, ay, bx, by})
            {
                float ra = std::fabs(Dot(ax, axis)) * ha.x + std::fabs(Dot(ay, axis)) * ha.y;
                float rb = std::fabs(Dot(bx, axis)) * hb.x + std::fabs(Dot(by, axis)) * hb.y;
                overlap &= std::fabs(Dot(Sub(first.position, second.position), axis)) < ra + rb - .02f;
            }
            if (!overlap)
            {
                continue;
            }
            float speed = std::hypot(first.speed * ax.x - second.speed * bx.x,
                                     first.speed * ax.y - second.speed * bx.y);
            if (speed < .15f)
            {
                continue;
            }
            for (auto *car : {&first, &second})
            {
                car->impactCooldown = .8f;
                car->speed = 0;
                car->position = car->previousPosition;
                if (car->turnDistance >= 0)
                {
                    car->turnDistance = (std::max)(0.f, car->turnDistance - .25f);
                }
                else
                {
                    car->distance = (std::max)(0.f, car->distance - .25f);
                }
                if (speed > 3)
                {
                    DamageVehicle(car->id, int(speed * 6), false);
                }
            }
        }
    }
    BodyCollision::VehicleContacts(dt);

    vehicles.erase(
        std::remove_if(vehicles.begin(), vehicles.end(),
                       [&](const TrafficVehicle &car) {
                           if (car.health <= 0 && (car.wreckSeconds > 60 ||
                                                   (car.wreckSeconds > 12 &&
                                                    OutsideView(car.position, world.cameraPosition, 180))))
                           {
                               return true;
                           }
                           return !car.globalId &&
                                  Game::Distance(car.position, world.playerPosition) > kDespawnDistance &&
                                  Game::Distance(car.position, world.cameraPosition) > kDespawnDistance &&
                                  OutsideView(car.position, world.cameraPosition, 320);
                       }),
        vehicles.end());
    spawnTimer -= dt;
    if (spawnTimer <= 0)
    {
        Populate(world);
        spawnTimer = .5f;
    }
}

// ============================================================================
// AirTraffic — extrapolate beyond route ends until the entire craft is offscreen
// ============================================================================
AirTraffic &Overworld::Flights()
{
    static AirTraffic traffic;
    return traffic;
}
void AirTraffic::Reset()
{
    aircraft.clear();
    nextId = 0;
    randomState = 0x85ab32u;
    spawnTimer = 0;
}
Vector2 AirTraffic::Tangent(int index, float t)
{
    const auto &route = RoadNetwork::Get().airCorridors.at(size_t(index));
    t = Game::Clamp(t, 0, 1);
    return Mul(Add(Mul(Sub(route.control, route.start), 1 - t), Mul(Sub(route.end, route.control), t)), 2);
}
Vector2 AirTraffic::Position(int index, float t)
{
    const auto &route = RoadNetwork::Get().airCorridors.at(size_t(index));
    float clamped = Game::Clamp(t, 0, 1), s = 1 - clamped;
    auto point = Add(Add(Mul(route.start, s * s), Mul(route.control, 2 * s * clamped)),
                     Mul(route.end, clamped * clamped));
    return Add(point, Mul(Tangent(index, clamped), t - clamped));
}
bool AirTraffic::OutsideVisibleFootprint(const AircraftActor &craft, Vector2 camera, float padding,
                                         float groundElevation)
{
    const auto &model = models::ModelLibrary::Aircraft(craft.type, craft.alternate);
    const auto &resource = models::ResourceAssets::Get(model.resourceId);
    models::Transform3D transform;
    transform.yaw = craft.yaw;
    transform.roll = craft.bank;
    transform.position.z = craft.altitude;
    float minX = 1.e20f, minY = 1.e20f, maxX = -1.e20f, maxY = -1.e20f;
    // Include banked hull, moving gear and the renderer's projected, blurred ground shadow.
    for (int corner = 0; corner < 8; ++corner)
    {
        auto p = transform.Apply({corner & 1 ? resource.maximum.x + 1 : resource.minimum.x - 1,
                                  corner & 2 ? resource.maximum.y + 1 : resource.minimum.y - 1,
                                  corner & 4 ? resource.maximum.z + 1 : resource.minimum.z - 1});
        for (int shadow = 0; shadow < 2; ++shadow)
        {
            const float aboveGround = p.z - groundElevation;
            float x = p.x + (shadow ? .38f * aboveGround : 0), y = p.y + (shadow ? .55f * aboveGround : 0),
                  z = shadow ? groundElevation : p.z;
            auto screen = WorldCamera::ToViewport(
                {kCanvasWidth * .5f +
                     (craft.position.x + x - craft.position.y - y - camera.x + camera.y) * 36,
                 kCanvasHeight * .57f +
                     (craft.position.x + x + craft.position.y + y - camera.x - camera.y) * 18 - z * 44});
            minX = (std::min)(minX, screen.x);
            maxX = (std::max)(maxX, screen.x);
            minY = (std::min)(minY, screen.y);
            maxY = (std::max)(maxY, screen.y);
        }
    }
    return maxX < -padding || minX > kCanvasWidth + padding || maxY < -padding ||
           minY > kCanvasHeight + padding;
}
bool AirTraffic::Contains(const AircraftActor &craft, models::Point p)
{
    const auto &mesh =
        models::ResourceAssets::Get(models::ModelLibrary::Aircraft(craft.type, craft.alternate).resourceId);
    float x = p.x - craft.position.x, y = p.y - craft.position.y, z = p.z - craft.altitude;
    float localX = x * std::cos(craft.yaw) + y * std::sin(craft.yaw);
    float localY = -x * std::sin(craft.yaw) + y * std::cos(craft.yaw);
    float unbankedY = localY * std::cos(craft.bank) + z * std::sin(craft.bank);
    float unbankedZ = -localY * std::sin(craft.bank) + z * std::cos(craft.bank);
    return localX >= mesh.minimum.x - .04f && localX <= mesh.maximum.x + .04f &&
           unbankedY >= mesh.minimum.y - .04f && unbankedY <= mesh.maximum.y + .04f &&
           unbankedZ >= mesh.minimum.z - .04f && unbankedZ <= mesh.maximum.z + .04f;
}
bool AirTraffic::DamageAircraft(std::uint64_t id, int damage, bool playerOwned)
{
    auto found =
        std::find_if(aircraft.begin(), aircraft.end(), [=](const AircraftActor &c) { return c.id == id; });
    if (found == aircraft.end() || found->health <= 0 || damage <= 0)
    {
        return false;
    }
    found->health = (std::max)(0, found->health - damage);
    if (found->health == 0)
    {
        found->speed = 0;
        if (playerOwned && found->occupied)
        {
            CivicJustice::Report(Offense::VehicleDestruction, found->position);
        }
    }
    return true;
}
void AirTraffic::Update(const GameState &world, float dt)
{
    dt = Game::Clamp(dt, 0, .05f);
    for (auto &craft : aircraft)
    {
        if (craft.health <= 0)
        {
            craft.wreckSeconds += dt;
            craft.fallSpeed += 4.9f * dt;
            craft.altitude = (std::max)(.2f, craft.altitude - craft.fallSpeed * dt);
            continue;
        }
        auto oldDirection = Unit(Mul(Tangent(craft.route, craft.parameter), float(craft.direction)));
        auto tangent = Tangent(craft.route, craft.parameter);
        craft.parameter +=
            craft.direction * craft.speed * dt / (std::max)(1.f, std::sqrt(Dot(tangent, tangent)));
        craft.position = Position(craft.route, craft.parameter);
        auto direction = Unit(Mul(Tangent(craft.route, craft.parameter), float(craft.direction)));
        craft.yaw = std::atan2(direction.y, direction.x);
        float targetBank = dt > 0 ? Game::Clamp(-Cross(oldDirection, direction) / dt * .25f, -.18f, .18f) : 0;
        craft.bank += (targetBank - craft.bank) * (1 - std::exp(-4 * dt));
    }
    aircraft.erase(std::remove_if(aircraft.begin(), aircraft.end(),
                                  [&](const AircraftActor &craft) {
                                      if (craft.health <= 0 && craft.wreckSeconds > 20 &&
                                          OutsideVisibleFootprint(craft, world.cameraPosition, 80))
                                      {
                                          return true;
                                      }
                                      return Game::Distance(craft.position, world.playerPosition) > 180 &&
                                             Game::Distance(craft.position, world.cameraPosition) > 180 &&
                                             OutsideVisibleFootprint(craft, world.cameraPosition, 340) &&
                                             OutsideVisibleFootprint(craft, world.playerPosition, 340);
                                  }),
                   aircraft.end());
    spawnTimer -= dt;
    if (spawnTimer > 0 || dt <= 0)
    {
        return;
    }
    spawnTimer = 1;
    auto random = [&]() {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return randomState;
    };
    for (int attempt = 0; attempt < 96 && aircraft.size() < kAircraftLimit; ++attempt)
    {
        int route = int(random() % RoadNetwork::Get().airCorridors.size());
        float t = float(random() % 1400) / 1000.f - .2f;
        auto position = Position(route, t);
        float altitude = RoadNetwork::Get().airCorridors[route].altitude;
        bool crowded = std::any_of(aircraft.begin(), aircraft.end(), [&](const AircraftActor &craft) {
            return std::fabs(craft.altitude - altitude) < 2.5f &&
                   Game::Distance(craft.position, position) < 14;
        });
        if (crowded)
        {
            continue;
        }
        AircraftActor craft;
        craft.id = nextId;
        craft.route = route;
        craft.parameter = t;
        // Each corridor is one way; its altitude separates it from the other routes.
        craft.direction = route % 2 ? -1 : 1;
        craft.type = static_cast<models::AircraftType>((craft.id / 2) % size_t(models::AircraftType::Count));
        craft.alternate = craft.id % 2 != 0;
        craft.speed = craft.type == models::AircraftType::Skycrane ? 14.f : 20.f;
        craft.position = position;
        craft.altitude = altitude;
        auto heading = Mul(Tangent(route, t), float(craft.direction));
        craft.yaw = std::atan2(heading.y, heading.x);
        if (Game::Distance(position, world.playerPosition) > 115 ||
            !OutsideVisibleFootprint(craft, world.cameraPosition, 280) ||
            !OutsideVisibleFootprint(craft, world.playerPosition, 280))
        {
            continue;
        }
        ++nextId;
        aircraft.push_back(craft);
    }
}

// ============================================================================
// PlanetTraffic — route geometry is immutable; persistent actors own spherical metres and stable IDs
// ============================================================================
namespace
{
using TG = world::SurfaceGeometry;
world::Vector3d TransitAdd(world::Vector3d a, world::Vector3d b, double scale = 1)
{
    return {a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale};
}
world::SurfacePoint TransitArc(world::SurfacePoint a, world::SurfacePoint b, double t)
{
    double dot = std::clamp(TG::Dot(a.normal, b.normal), -1., 1.);
    double angle = std::acos(dot);
    auto tangent = TransitAdd(b.normal, a.normal, -dot);
    if (TG::Dot(tangent, tangent) < 1.e-16)
    {
        tangent = TG::Frame(a).right;
    }
    tangent = TG::Unit(tangent);
    return {TG::Unit(TransitAdd({a.normal.x * std::cos(angle * t), a.normal.y * std::cos(angle * t),
                                 a.normal.z * std::cos(angle * t)},
                                tangent, std::sin(angle * t))),
            a.altitudeMeters + (b.altitudeMeters - a.altitudeMeters) * t};
}
double TransitNearest(const PlanetTrafficRoute &route, const world::SurfacePoint &point, double radius)
{
    double best = 1.e100, progress = 0;
    for (size_t i = 1; i < route.points.size(); ++i)
    {
        const auto &a = route.points[i - 1], &b = route.points[i];
        const auto middle = route.caps[i - 1];
        const double length = route.distances[i] - route.distances[i - 1];
        if (TG::Dot(middle, point.normal) < route.capDots[i - 1])
        {
            continue;
        }
        const auto plane = TG::Cross(a.normal, b.normal);
        if (TG::Dot(plane, plane) < 1.e-18)
        {
            continue;
        }
        auto n = TG::Unit(plane);
        auto projected = TransitAdd(point.normal, n, -TG::Dot(point.normal, n));
        if (TG::Dot(projected, projected) < 1.e-18)
        {
            continue;
        }
        projected = TG::Unit(projected);
        if (TG::Dot(projected, middle) < 0)
        {
            projected = {-projected.x, -projected.y, -projected.z};
        }
        double fraction =
            std::atan2(TG::Dot(TG::Cross(a.normal, projected), n), TG::Dot(a.normal, projected)) * radius /
            length;
        fraction = std::clamp(fraction, 0., 1.);
        const auto sample = TransitArc(a, b, fraction);
        double distance = TG::Distance(radius, sample, point);
        if (distance < best)
        {
            best = distance;
            progress = route.distances[i - 1] + fraction * length;
        }
    }
    return best <= 800 ? progress : -1;
}
const models::Model &TransitModel(const PlanetTrafficActor &actor)
{
    return actor.airborne ? models::ModelLibrary::Aircraft(
                                models::AircraftType((actor.id / 2) % size_t(models::AircraftType::Count)),
                                actor.id % 2 != 0)
                          : models::ModelLibrary::Get().vehicles[size_t(actor.id % 4)].body;
}
bool TransitHull(const PlanetTrafficActor &actor, const world::SurfacePoint &point, double radius,
                 double padding)
{
    if (TG::Distance(radius, actor.position, point) > 45 + padding)
    {
        return false;
    }
    const auto &mesh = models::ResourceAssets::Get(TransitModel(actor).resourceId);
    auto frame = TG::Frame(actor.position);
    frame.forward = TG::Unit(TG::Cross(actor.position.normal, actor.heading));
    frame.right = TG::Unit(TG::Cross(frame.forward, actor.position.normal));
    const auto local = TG::ToLocal(frame, point, radius);
    return local.x >= mesh.minimum.x * 2 - padding && local.x <= mesh.maximum.x * 2 + padding &&
           local.y >= mesh.minimum.y * 2 - padding && local.y <= mesh.maximum.y * 2 + padding;
}
std::string TransitKey(const world::PlanetDefinition &p)
{
    return world::WorldIdentity::ContentKey(p, "overworld",
                                            "planet_traffic." + std::to_string(PlanetTraffic::Revision));
}
} // namespace
PlanetTraffic::PlanetTraffic(PlanetTrafficState state) : state(std::move(state))
{
}
unsigned PlanetTraffic::Random()
{
    state.randomState ^= state.randomState << 13;
    state.randomState ^= state.randomState >> 17;
    state.randomState ^= state.randomState << 5;
    return state.randomState;
}
void PlanetTraffic::Validate(const PlanetTrafficState &s, const world::PlanetDefinition &p,
                             std::uint64_t digest)
{
    p.Validate();
    auto require = [](bool ok) {
        if (!ok)
        {
            throw std::invalid_argument("행성 교통 기록의 월드/경로/개체 상태가 올바르지 않습니다.");
        }
    };
    if (s.worldKey.empty())
    {
        require(s.actors.empty() && s.manifestDigest == 0 && s.nextId == 1 && s.randomState == 1 &&
                s.spawnSeconds == 0);
        return;
    }
    require(s.worldKey == TransitKey(p) && s.manifestDigest == digest && s.nextId > 0 && s.randomState != 0 &&
            s.actors.size() <= GroundLimit + AirLimit && std::isfinite(s.spawnSeconds) &&
            s.spawnSeconds >= 0 && s.spawnSeconds <= 1);
    std::set<std::uint64_t> ids;
    size_t ground = 0, air = 0;
    for (const auto &actor : s.actors)
    {
        TG::Validate(actor.position);
        const auto magnitude = TG::Dot(actor.heading, actor.heading);
        require(
            actor.id > 0 && actor.id < s.nextId && ids.insert(actor.id).second && !actor.routeId.empty() &&
            actor.routeId.size() <= 512 && (actor.direction == 1 || actor.direction == -1) &&
            actor.health >= 0 && actor.health <= (actor.airborne ? 180 : 140) && std::isfinite(magnitude) &&
            std::fabs(magnitude - 1) < 1.e-5 &&
            std::fabs(TG::Dot(actor.heading, actor.position.normal)) < 1.e-5 &&
            std::isfinite(actor.distanceMeters) && actor.distanceMeters >= -1.e8 &&
            actor.distanceMeters <= 1.e8 && std::isfinite(actor.speedMeters) && actor.speedMeters >= 0 &&
            actor.speedMeters <= 120 && std::isfinite(actor.altitudeMeters) && actor.altitudeMeters >= 0 &&
            actor.altitudeMeters <= 100 && std::isfinite(actor.bank) && std::fabs(actor.bank) <= .35f &&
            std::isfinite(actor.wreckSeconds) && actor.wreckSeconds >= 0 && actor.wreckSeconds <= 1.e6 &&
            std::isfinite(actor.fallSpeed) && actor.fallSpeed >= 0 && actor.fallSpeed <= 1000 &&
            std::isfinite(actor.farSeconds) && actor.farSeconds >= 0 && actor.farSeconds < .6f &&
            actor.city >= -1 && actor.city < Overworld::kCityCount &&
            (actor.city < 0 ? actor.cityVehicle == 0 : !actor.airborne) &&
            (actor.airborne ? (!actor.health || !actor.collisionReady)
                            : (actor.collisionReady && actor.altitudeMeters == 0 && actor.fallSpeed == 0 &&
                               actor.bank == 0)));
        actor.airborne ? ++air : ++ground;
    }
    require(ground <= GroundLimit && air <= AirLimit);
}
void PlanetTraffic::Bind(std::shared_ptr<const world::GeneratedPlanet> input)
{
    if (!input)
    {
        throw std::invalid_argument("Missing traffic world");
    }
    if (planet == input)
    {
        return;
    }
    profiling::Scope prepareTimer(profiling::Timer::PlanetTrafficPrepare);
    const auto &p = input->terrain.Planet();
    const auto digest = input->Digest();
    Validate(state, p, digest);
    auto prepared = std::make_shared<std::vector<PlanetTrafficRoute>>();
    auto add = [&](PlanetTrafficRoute route) {
        route.distances = {0};
        for (size_t i = 1; i < route.points.size(); ++i)
        {
            const double length = TG::Distance(p.radiusMeters, route.points[i - 1], route.points[i]);
            route.lengthMeters += length;
            route.distances.push_back(route.lengthMeters);
            route.caps.push_back(TransitArc(route.points[i - 1], route.points[i], .5).normal);
            route.capDots.push_back(std::cos((length * .5 + 800) / p.radiusMeters));
        }
        if (route.lengthMeters > 10)
        {
            prepared->push_back(std::move(route));
        }
    };
    for (size_t i = 0; i < input->connections.size(); ++i)
    {
        const auto &edge = input->connections[i];
        if (edge.points.size() < 2)
        {
            continue;
        }
        auto id = world::WorldIdentity::ContentKey(p, "overworld", "traffic1.edge." + std::to_string(i));
        if (edge.mode == world::RouteMode::Road)
        {
            add({id + ".road", edge.from, edge.to, false, edge.points});
        }
        PlanetTrafficRoute flight{id + ".air", edge.from, edge.to, true};
        const auto a = edge.points.front(), b = edge.points.back();
        auto middle = TransitArc(a, b, .5);
        auto tangent = TransitAdd(b.normal, a.normal, -TG::Dot(a.normal, b.normal));
        if (TG::Dot(tangent, tangent) < 1.e-16)
        {
            continue;
        }
        auto right = TG::Unit(TG::Cross(TG::Unit(tangent), middle.normal));
        auto frame = TG::Frame(middle);
        double offset = std::clamp(TG::Distance(p.radiusMeters, a, b) * .18, 80., 4000.) * (i % 2 ? 1 : -1);
        auto control = TG::Advance(frame, TG::Dot(right, frame.right) * offset,
                                   TG::Dot(right, frame.forward) * offset, p.radiusMeters)
                           .origin;
        for (unsigned n = 0; n <= 32; ++n)
        {
            double t = n / 32.;
            flight.points.push_back(TransitArc(TransitArc(a, control, t), TransitArc(control, b, t), t));
        }
        add(std::move(flight));
    }
    // The macro planner ends each authored city's roads at its first real approach join.
    // Add two directed lane-centre paths on that existing pavement; never invent a bridge across lots.
    std::array<std::string, Overworld::kCityCount> preparedExits;
    if (p.storyPlanet && input->cityCores)
    {
        const auto &net = RoadNetwork::Get();
        for (int city = 0; city < Overworld::kCityCount; ++city)
        {
            const auto &info = Overworld::Cities()[size_t(city)];
            const auto *core = input->cityCores->Find(info.id);
            if (!core || core->approaches.empty())
            {
                continue;
            }
            const auto &approach = core->approaches.front();
            if (approach.points.size() < 2)
            {
                continue;
            }
            auto node = std::find_if(input->nodes.begin(), input->nodes.end(),
                                     [&](const auto &v) { return v.id == "site_" + std::string(info.id); });
            if (node == input->nodes.end())
            {
                continue;
            }
            const unsigned nodeId = unsigned(node - input->nodes.begin());
            if (std::none_of(prepared->begin(), prepared->end(), [&](const auto &v) {
                    return !v.airborne && (v.from == nodeId || v.to == nodeId);
                }))
            {
                continue;
            }
            const auto &region = input->Region(info.id);
            auto at = [&](world::Vector3d local) {
                auto point = TG::FromLocal(region.frame, local, p.radiusMeters);
                point.altitudeMeters = input->terrain.Height(point);
                return point;
            };
            if (TG::Distance(p.radiusMeters, at(approach.join), node->location.position) > .25)
            {
                continue;
            }
            int outgoing = -1, incoming = -1;
            for (size_t lane = 0; lane < net.lanes.size(); ++lane)
            {
                const auto &v = net.lanes[lane];
                if (!TrafficSystem::CoreLane(int(lane), city) || v.slot != 0)
                {
                    continue;
                }
                if (v.from == int(approach.from))
                {
                    outgoing = int(lane);
                }
                if (v.to == int(approach.from))
                {
                    incoming = int(lane);
                }
            }
            if (outgoing < 0 || incoming < 0)
            {
                continue;
            }
            auto lanePoint = [&](Vector2 v) {
                return at({(v.x - info.center.x) * 2, (v.y - info.center.y) * 2, 0});
            };
            auto build = [&](bool entry, int lane) {
                PlanetTrafficRoute route;
                route.id = world::WorldIdentity::ContentKey(
                    p, info.id, entry ? "traffic1.portal.in" : "traffic1.portal.out");
                route.portalCity = city;
                route.lane = lane;
                route.entry = entry;
                const unsigned marker = unsigned(input->nodes.size()) + unsigned(city * 2);
                route.from = entry ? nodeId : marker;
                route.to = entry ? marker + 1 : nodeId;
                auto points = approach.points;
                if (entry)
                {
                    std::reverse(points.begin(), points.end());
                }
                route.points.push_back(entry ? node->location.position
                                             : lanePoint(net.lanes[size_t(lane)].end));
                auto joinLane = [&] {
                    const auto join = entry ? points.front() : points.back();
                    const auto neighbor = entry ? points[1] : points[points.size() - 2];
                    const double dx = neighbor.x - join.x, dy = neighbor.y - join.y,
                                 length = std::hypot(dx, dy);
                    if (length < .001)
                    {
                        return at(join);
                    }
                    const double reach = (std::min)(24., length * .5);
                    return at({join.x + dx / length * reach + (entry ? 1 : -1) * dy / length * 1.5,
                               join.y + dy / length * reach - (entry ? 1 : -1) * dx / length * 1.5, 0});
                };
                if (entry)
                {
                    route.points.push_back(joinLane());
                }
                for (size_t i = 0; i < points.size(); ++i)
                {
                    if ((entry && i == 0) || (!entry && i + 1 == points.size()))
                    {
                        continue;
                    }
                    auto a = points[i == 0 ? 0 : i - 1], b = points[i + 1 < points.size() ? i + 1 : i];
                    const double dx = b.x - a.x, dy = b.y - a.y, length = std::hypot(dx, dy);
                    auto point = points[i];
                    if (length > .001)
                    {
                        point.x += dy / length * 1.5;
                        point.y -= dx / length * 1.5;
                    }
                    route.points.push_back(at(point));
                }
                if (!entry)
                {
                    route.points.push_back(joinLane());
                }
                route.points.push_back(entry ? lanePoint(net.lanes[size_t(lane)].start)
                                             : node->location.position);
                // Remove zero-length joints before distance/cap preparation.
                route.points.erase(std::unique(route.points.begin(), route.points.end(),
                                               [&](const auto &a, const auto &b) {
                                                   return TG::Distance(p.radiusMeters, a, b) < .01;
                                               }),
                                   route.points.end());
                if (!entry)
                {
                    preparedExits[size_t(city)] = route.id;
                }
                add(std::move(route));
            };
            build(false, incoming);
            build(true, outgoing);
        }
    }
    std::map<std::string, size_t, std::less<>> index;
    std::map<unsigned, std::vector<size_t>> adjacency;
    for (size_t i = 0; i < prepared->size(); ++i)
    {
        index.emplace((*prepared)[i].id, i);
        if (!(*prepared)[i].airborne)
        {
            adjacency[(*prepared)[i].from].push_back(i);
            adjacency[(*prepared)[i].to].push_back(i);
        }
    }
    for (const auto &actor : state.actors)
    {
        const auto found = index.find(actor.routeId);
        if (found == index.end())
        {
            throw std::invalid_argument("저장된 행성 교통 경로가 존재하지 않습니다.");
        }
        const auto &route = (*prepared)[found->second];
        if (actor.airborne != route.airborne || (route.portalCity >= 0 && actor.direction != 1) ||
            (actor.city >= 0 && (route.portalCity != actor.city || !route.entry)) ||
            actor.distanceMeters >
                route.lengthMeters + (actor.airborne ? TG::Circumference(p.radiusMeters) * 4 : 0) ||
            (!actor.airborne && actor.distanceMeters < 0) ||
            (actor.airborne && actor.distanceMeters < -TG::Circumference(p.radiusMeters) * 4))
        {
            throw std::invalid_argument("저장된 교통 경로 진행 값이 올바르지 않습니다.");
        }
    }
    PlanetTraffic probe;
    probe.planet = input;
    for (const auto &actor : state.actors)
    {
        if (actor.city >= 0)
        {
            continue; // Its checked city projection owns the interior path.
        }
        const auto &route = prepared->at(index.at(actor.routeId));
        auto expected = probe.Position(route, actor.distanceMeters, actor.direction);
        expected.altitudeMeters =
            (std::max)(::world::PlanetTerrain::SeaLevelMeters, input->terrain.Height(expected)) +
            actor.altitudeMeters;
        if (TG::Distance(p.radiusMeters, actor.position, expected) > .25 ||
            std::fabs(actor.position.altitudeMeters - expected.altitudeMeters) > .25)
        {
            throw std::invalid_argument("교통 개체 위치가 저장된 경로 진행 값과 다릅니다.");
        }
    }
    planet = std::move(input);
    routes = std::move(prepared);
    routeIndex = std::move(index);
    groundConnections = std::move(adjacency);
    exits = std::move(preparedExits);
    if (state.worldKey.empty())
    {
        state.worldKey = TransitKey(p);
        state.manifestDigest = digest;
        state.randomState = unsigned(world::WorldIdentity::StreamSeed(p, "overworld", "planet_traffic")) | 1u;
    }
}
const PlanetTrafficRoute *PlanetTraffic::Find(std::string_view id) const
{
    auto found = routeIndex.find(id);
    return found == routeIndex.end() ? nullptr : &routes->at(found->second);
}
world::SurfacePoint PlanetTraffic::Position(const PlanetTrafficRoute &route, double distance,
                                            int direction) const
{
    if (!planet || route.points.size() < 2 || route.distances.size() != route.points.size() ||
        route.lengthMeters <= 0 || !std::isfinite(distance) || std::fabs(distance) > 1.e8 ||
        (direction != 1 && direction != -1))
    {
        throw std::invalid_argument("Invalid spherical traffic route sample");
    }
    const double clamped = std::clamp(distance, 0., route.lengthMeters);
    auto upper = std::upper_bound(route.distances.begin(), route.distances.end(), clamped);
    size_t i = std::clamp(size_t(upper - route.distances.begin()), size_t(1), route.points.size() - 1);
    double segment = route.distances[i] - route.distances[i - 1];
    auto point = TransitArc(route.points[i - 1], route.points[i],
                            segment > 0 ? (clamped - route.distances[i - 1]) / segment : 0);
    const double radius = planet->terrain.Planet().radiusMeters;
    auto frame = TG::Frame(point);
    auto tangent = TransitAdd(route.points[i].normal, route.points[i - 1].normal,
                              -TG::Dot(route.points[i - 1].normal, route.points[i].normal));
    if (TG::Dot(tangent, tangent) < 1.e-18)
    {
        return point;
    }
    tangent = TG::Unit(TransitAdd(tangent, point.normal, -TG::Dot(tangent, point.normal)));
    if (route.airborne && distance != clamped)
    {
        point = TG::Advance(frame, TG::Dot(tangent, frame.right) * (distance - clamped),
                            TG::Dot(tangent, frame.forward) * (distance - clamped), radius)
                    .origin;
    }
    else if (!route.airborne && route.portalCity < 0)
    {
        auto right = TG::Unit(TG::Cross(tangent, point.normal));
        const double lane =
            1.5 * direction * std::clamp((std::min)(clamped, route.lengthMeters - clamped) / 20., 0., 1.);
        point = TG::Advance(frame, TG::Dot(right, frame.right) * lane, TG::Dot(right, frame.forward) * lane,
                            radius)
                    .origin;
    }
    return point;
}
bool PlanetTraffic::InsideCore(const world::SurfacePoint &point) const
{
    if (!planet->cityCores)
    {
        return false;
    }
    for (const auto &core : planet->cityCores->cities)
    {
        if (TG::Distance(planet->terrain.Planet().radiusMeters, point,
                         planet->Region(core.regionId).center.position) < core.radiusMeters + 30)
        {
            return true;
        }
    }
    return false;
}
bool PlanetTraffic::Blocked(const world::SurfacePoint &point, double clearance) const
{
    if (!std::isfinite(clearance) || clearance < 0 || clearance > 8)
    {
        throw std::invalid_argument("Invalid traffic clearance");
    }
    if (!planet)
    {
        return false;
    }
    for (const auto &actor : state.actors)
    {
        if (actor.city < 0 && actor.collisionReady &&
            (!actor.airborne || (actor.health <= 0 && actor.altitudeMeters < 1)) &&
            TransitHull(actor, point, planet->terrain.Planet().radiusMeters, clearance))
        {
            return true;
        }
    }
    return false;
}
bool PlanetTraffic::Damage(std::uint64_t id, int amount, bool playerOwned)
{
    const auto found = std::find_if(state.actors.begin(), state.actors.end(),
                                    [&](const auto &actor) { return actor.id == id; });
    if (found == state.actors.end() || found->health <= 0 || amount <= 0)
    {
        return false;
    }
    if (found->city >= 0)
    {
        auto *cities = Game::Get().SurfaceCities();
        if (!cities)
        {
            return false;
        }
        bool hit = false;
        cities->Observe(Game::Get().GetState(), *Game::Get().Surface(), found->city, [&] {
            hit = cities->Traffic(found->city).DamageVehicle(found->cityVehicle, amount, playerOwned);
        });
        return hit;
    }
    found->health = (std::max)(0, found->health - amount);
    if (found->health == 0)
    {
        found->speedMeters = 0;
        found->farSeconds = 0;
        if (playerOwned && found->occupied)
        {
            PlanetSecurity::Report(Offense::VehicleDestruction, found->position);
        }
    }
    return true;
}
bool PlanetTraffic::Contains(const PlanetTrafficActor &actor, const world::SurfacePoint &point,
                             double padding) const
{
    if (!planet || actor.city >= 0 ||
        !TransitHull(actor, point, planet->terrain.Planet().radiusMeters, padding))
    {
        return false;
    }
    const auto &mesh = models::ResourceAssets::Get(TransitModel(actor).resourceId);
    return point.altitudeMeters >= actor.position.altitudeMeters + mesh.minimum.z * 2 - padding &&
           point.altitudeMeters <= actor.position.altitudeMeters + mesh.maximum.z * 2 + padding;
}
bool PlanetTraffic::Outside(const PlanetTrafficActor &actor, const GameState &world,
                            const world::SurfaceSession &session, float padding) const
{
    const double radius = session.Planet().radiusMeters;
    const auto camera = session.PointAt(world.cameraPosition.x * 2, world.cameraPosition.y * 2);
    if (TG::Distance(radius, actor.position, session.Pose().origin) > 2000 &&
        TG::Distance(radius, actor.position, camera) > 2000)
    {
        return true;
    }
    auto base = actor.position;
    base.altitudeMeters -= actor.altitudeMeters;
    const auto local = session.Local(base);
    Vector2 position{float(local.x / 2), float(local.y / 2)};
    auto viewCamera = [&](Vector2 camera) {
        float offset = world.surfaceCameraHeight * 44 / 36;
        return Vector2{camera.x - offset, camera.y - offset};
    };
    if (actor.airborne)
    {
        AircraftActor projected;
        projected.id = actor.id;
        projected.type = models::AircraftType((actor.id / 2) % size_t(models::AircraftType::Count));
        projected.alternate = actor.id % 2 != 0;
        projected.position = position;
        projected.altitude = float((local.z + actor.altitudeMeters) / 2);
        projected.yaw = float(std::atan2(TG::Dot(actor.heading, session.Origin().forward),
                                         TG::Dot(actor.heading, session.Origin().right)));
        projected.bank = actor.bank;
        return AirTraffic::OutsideVisibleFootprint(projected, viewCamera(world.cameraPosition), padding,
                                                   float(local.z / 2)) &&
               AirTraffic::OutsideVisibleFootprint(projected, viewCamera(world.playerPosition), padding,
                                                   float(local.z / 2));
    }
    return TrafficSystem::OutsideView(position, viewCamera(world.cameraPosition), padding,
                                      float(local.z / 2)) &&
           TrafficSystem::OutsideView(position, viewCamera(world.playerPosition), padding,
                                      float(local.z / 2));
}
std::vector<PlanetTrafficLink> PlanetTraffic::Links() const
{
    std::vector<PlanetTrafficLink> links;
    for (const auto &actor : state.actors)
    {
        if (actor.city >= 0)
        {
            links.push_back({actor.id, actor.cityVehicle, actor.city});
        }
    }
    return links;
}
void PlanetTraffic::SyncDamage(const TrafficVehicle &car)
{
    if (!car.globalId)
    {
        return;
    }
    auto actor = std::find_if(state.actors.begin(), state.actors.end(),
                              [&](const auto &v) { return v.id == car.globalId; });
    if (actor != state.actors.end() && actor->city >= 0)
    {
        actor->health = car.health;
        actor->occupied = car.occupied;
        actor->speedMeters = car.speed * 2;
        actor->wreckSeconds = car.wreckSeconds;
    }
}
void PlanetTraffic::SyncCity(int city, const TrafficSystem &traffic, const world::SurfaceSession &session)
{
    auto &g = Game::Get();
    if (!g.SurfaceCities() || !session.Ready() || city < 0 || city >= Overworld::kCityCount ||
        std::none_of(state.actors.begin(), state.actors.end(), [&](const auto &v) { return v.city == city; }))
    {
        return;
    }
    const auto &frame =
        session.Stream().Snapshot()->planet->Region(Overworld::Cities()[size_t(city)].id).frame;
    state.actors.erase(
        std::remove_if(state.actors.begin(), state.actors.end(),
                       [&](auto &actor) {
                           if (actor.city != city)
                           {
                               return false;
                           }
                           auto car = std::find_if(traffic.Vehicles().begin(), traffic.Vehicles().end(),
                                                   [&](const auto &v) { return v.globalId == actor.id; });
                           if (car == traffic.Vehicles().end())
                           {
                               return true; // The city retired the real wreck, not a second copy.
                           }
                           actor.cityVehicle = car->id;
                           actor.position = *g.SurfaceCities()->Point(session, city, car->position);
                           auto h = world::Vector3d{
                               frame.right.x * std::cos(car->yaw) + frame.forward.x * std::sin(car->yaw),
                               frame.right.y * std::cos(car->yaw) + frame.forward.y * std::sin(car->yaw),
                               frame.right.z * std::cos(car->yaw) + frame.forward.z * std::sin(car->yaw)};
                           actor.heading = TG::Unit(
                               TransitAdd(h, actor.position.normal, -TG::Dot(h, actor.position.normal)));
                           actor.health = car->health;
                           actor.occupied = car->occupied;
                           actor.speedMeters = car->speed * 2;
                           actor.wreckSeconds = car->wreckSeconds;
                           actor.farSeconds = 0;
                           return false;
                       }),
        state.actors.end());
}
bool PlanetTraffic::Depart(int city, TrafficVehicle &car)
{
    if (!planet || city < 0 || city >= Overworld::kCityCount || car.health <= 0 || car.turnDistance >= 0)
    {
        return false;
    }
    const auto *exit = Find(exits[size_t(city)]);
    if (!exit || exit->lane != car.lane)
    {
        return false;
    }
    const auto &lane = RoadNetwork::Get().lanes[size_t(car.lane)];
    if (car.distance < lane.length - .001f)
    {
        return false;
    }
    auto actor = std::find_if(state.actors.begin(), state.actors.end(),
                              [&](const auto &v) { return car.globalId && v.id == car.globalId; });
    if (car.globalId && (actor == state.actors.end() || actor->city != city || actor->cityVehicle != car.id))
    {
        return false;
    }
    if (!car.globalId && (std::count_if(state.actors.begin(), state.actors.end(),
                                        [](const auto &v) { return !v.airborne; }) >= GroundLimit ||
                          state.nextId > std::numeric_limits<std::uint64_t>::max() - 4))
    {
        return false;
    }
    auto point = Position(*exit, 0, 1);
    point.altitudeMeters = planet->terrain.Height(point);
    // Do not merge into a queued portal vehicle or a blocked approach.
    const auto *session = Game::Get().Surface();
    if (!session || !session->LandingClear(point, .8))
    {
        return false;
    }
    for (const auto &other : state.actors)
    {
        if (other.id != car.globalId && other.city < 0 && !other.airborne &&
            TG::Distance(planet->terrain.Planet().radiusMeters, point, other.position) < 7)
        {
            return false;
        }
    }
    if (!car.globalId)
    {
        const auto id = state.nextId + (car.id % 4 + 4 - state.nextId % 4) % 4;
        state.nextId = id + 1;
        PlanetTrafficActor created;
        created.id = id;
        state.actors.push_back(created);
        actor = std::prev(state.actors.end());
    }
    actor->city = -1;
    actor->cityVehicle = 0;
    actor->routeId = exit->id;
    actor->distanceMeters = 0;
    actor->direction = 1;
    actor->position = point;
    actor->speedMeters = car.speed * 2;
    actor->farSeconds = 0;
    actor->health = car.health;
    actor->occupied = car.occupied;
    actor->wreckSeconds = car.wreckSeconds;
    auto ahead = Position(*exit, .25, 1);
    actor->heading = TG::Unit(TransitAdd(ahead.normal, point.normal, -TG::Dot(ahead.normal, point.normal)));
    car.departed = true;
    profiling::Count(profiling::Counter::TrafficHandoffs);
    return true;
}
bool PlanetTraffic::Arrive(PlanetTrafficActor &actor, const PlanetTrafficRoute &route,
                           const world::SurfaceSession &session)
{
    auto *cities = Game::Get().SurfaceCities();
    if (!cities || route.portalCity < 0 || !route.entry || actor.health <= 0)
    {
        return false;
    }
    auto &traffic = cities->Traffic(route.portalCity);
    if (traffic.vehicles.size() >= TrafficSystem::kVehicleLimit ||
        traffic.nextId > std::numeric_limits<std::uint64_t>::max() - 4 ||
        std::count_if(state.actors.begin(), state.actors.end(),
                      [&](const auto &v) { return v.city == route.portalCity; }) >= CityLimit)
    {
        return false;
    }
    const auto &lane = RoadNetwork::Get().lanes[size_t(route.lane)];
    for (const auto &car : traffic.vehicles)
    {
        if (Game::Distance(car.position, lane.start) < 4 ||
            (car.reservedTo == route.lane && car.turnDistance >= 0))
        {
            return false;
        }
    }
    auto point = *cities->Point(session, route.portalCity, lane.start);
    if (!session.LandingClear(point, .8) ||
        cities->OccupiedAt(Game::Get().GetState(), session, point, route.portalCity) ||
        (CivicJustice::IsExterior() &&
         TG::Distance(session.Planet().radiusMeters, point, session.Pose().origin) < 3) ||
        (Game::Get().SurfaceParty() && Game::Get().SurfaceParty()->Blocked(point, .8)) ||
        PlanetSecurity::OfficerBlocked(point, .8))
    {
        return false;
    }
    TrafficVehicle car;
    car.id = traffic.nextId + (actor.id % 4 + 4 - traffic.nextId % 4) % 4;
    car.globalId = actor.id;
    car.lane = route.lane;
    car.distance = 0;
    car.position = car.previousPosition = lane.start;
    car.yaw = car.previousYaw = std::atan2(lane.direction.y, lane.direction.x);
    car.speed = (std::min)(float(actor.speedMeters / 2), lane.speedLimit);
    car.health = actor.health;
    car.occupied = actor.occupied;
    car.wreckSeconds = actor.wreckSeconds;
    traffic.ChooseExit(car); // Prepare before either ownership record changes.
    traffic.vehicles.push_back(car);
    traffic.nextId = car.id + 1;
    actor.city = route.portalCity;
    actor.cityVehicle = car.id;
    actor.position = point;
    actor.distanceMeters = route.lengthMeters;
    actor.farSeconds = 0;
    actor.speedMeters = car.speed * 2;
    const auto &frame =
        session.Stream().Snapshot()->planet->Region(Overworld::Cities()[size_t(route.portalCity)].id).frame;
    const world::Vector3d heading{frame.right.x * std::cos(car.yaw) + frame.forward.x * std::sin(car.yaw),
                                  frame.right.y * std::cos(car.yaw) + frame.forward.y * std::sin(car.yaw),
                                  frame.right.z * std::cos(car.yaw) + frame.forward.z * std::sin(car.yaw)};
    actor.heading = TG::Unit(TransitAdd(heading, point.normal, -TG::Dot(heading, point.normal)));
    profiling::Count(profiling::Counter::TrafficHandoffs);
    return true;
}
void PlanetTraffic::Advance(PlanetTrafficActor &actor, float seconds, const GameState &world,
                            const world::SurfaceSession &session, bool exterior)
{
    const auto *route = Find(actor.routeId);
    const auto *originalRoute = route;
    if (!route)
    {
        return;
    }
    const double radius = session.Planet().radiusMeters;
    if (actor.health <= 0)
    {
        if (actor.airborne)
        {
            actor.altitudeMeters =
                (std::max)(0., actor.altitudeMeters - actor.fallSpeed * seconds - 4.9 * seconds * seconds);
            actor.fallSpeed = (std::min)(1000.f, actor.fallSpeed + 9.8f * seconds);
            actor.position.altitudeMeters =
                (std::max)(::world::PlanetTerrain::SeaLevelMeters, planet->terrain.Height(actor.position)) +
                actor.altitudeMeters;
        }
        return;
    }
    const double edge = (std::min)(actor.distanceMeters, route->lengthMeters - actor.distanceMeters);
    double target = actor.airborne ? 40. + actor.id % 4 * 6
                                   : (std::min)(12. + actor.id % 4 * 2, (std::max)(3., 4 + edge * .6));
    if (route->portalCity >= 0)
    {
        target = (std::min)(target, 6.);
    }
    double nodeTravel = std::numeric_limits<double>::infinity();
    if (!actor.airborne && edge < 30)
    {
        const unsigned node = actor.direction > 0 ? route->to : route->from;
        const bool portalNode = std::any_of(routes->begin(), routes->end(), [&](const auto &v) {
            return v.portalCity >= 0 && v.entry && v.from == node;
        });
        if (portalNode)
        {
            const double ownEnd =
                actor.direction > 0 ? route->lengthMeters - actor.distanceMeters : actor.distanceMeters;
            for (const auto &other : state.actors)
            {
                if (other.id == actor.id || other.airborne || other.city >= 0)
                {
                    continue;
                }
                const auto *r = Find(other.routeId);
                if (!r)
                {
                    continue;
                }
                const unsigned end = other.direction > 0 ? r->to : r->from,
                               start = other.direction > 0 ? r->from : r->to;
                const double remaining =
                    other.direction > 0 ? r->lengthMeters - other.distanceMeters : other.distanceMeters;
                const double traveled =
                    other.direction > 0 ? other.distanceMeters : r->lengthMeters - other.distanceMeters;
                const bool crossing = start == node && traveled >= 0 && traveled < 24;
                const bool ahead =
                    end == node && remaining >= 0 && remaining < 30 &&
                    (remaining < ownEnd - .1 || (std::abs(remaining - ownEnd) <= .1 && other.id < actor.id));
                if (crossing || ahead)
                {
                    nodeTravel = (std::max)(0., ownEnd - 24);
                    target = (std::min)(target, nodeTravel * 1.5);
                    break;
                }
            }
        }
    }
    if (!actor.airborne)
    {
        for (const auto &other : state.actors)
        {
            if (other.id == actor.id || other.city >= 0 || other.airborne || other.routeId != actor.routeId ||
                other.direction != actor.direction)
            {
                continue;
            }
            double gap = (other.distanceMeters - actor.distanceMeters) * actor.direction;
            if (gap > 0 && gap < 24)
            {
                target = (std::min)(target, (std::max)(0., (gap - 8) * 1.3));
            }
        }
    }
    actor.speedMeters += std::clamp(target - actor.speedMeters, -12. * seconds, 5. * seconds);
    double progress =
        actor.distanceMeters + (std::min)(actor.speedMeters * seconds, nodeTravel) * actor.direction;
    if (nodeTravel < actor.speedMeters * seconds)
    {
        actor.speedMeters = 0;
    }
    int direction = actor.direction;
    if (route->entry && route->portalCity >= 0 && progress >= route->lengthMeters)
    {
        if (!Arrive(actor, *route, session))
        {
            actor.speedMeters = 0;
        }
        return;
    }

    if (!actor.airborne && (progress < 0 || progress > route->lengthMeters))
    {
        const unsigned node = actor.direction > 0 ? route->to : route->from;
        const PlanetTrafficRoute *next = nullptr;
        const auto adjacent = groundConnections.find(node);
        if (adjacent != groundConnections.end())
        {
            for (size_t index : adjacent->second)
            {
                const auto &candidate = routes->at(index);
                if (candidate.portalCity >= 0 &&
                    (candidate.from != node ||
                     (route->portalCity >= 0 && candidate.portalCity == route->portalCity)))
                {
                    continue;
                }
                if (candidate.id != route->id && (!next || Random() % 2 == 0))
                {
                    next = &candidate;
                }
            }
        }
        if (!next)
        {
            progress = std::clamp(progress, 0., route->lengthMeters);
            actor.speedMeters = 0;
        }
        else
        {
            const double excess = actor.direction > 0 ? progress - route->lengthMeters : -progress;

            direction = next->from == node ? 1 : -1;
            route = next;
            progress = direction > 0 ? excess : route->lengthMeters - excess;
        }
    }
    if (actor.airborne)
    {
        const double circumference = TG::Circumference(radius);
        if (progress > route->lengthMeters + circumference * 2)
        {
            progress = route->lengthMeters + std::fmod(progress - route->lengthMeters, circumference);
        }
        else if (progress < -circumference * 2)
        {
            progress = -std::fmod(-progress, circumference);
        }
    }
    auto next = Position(*route, progress, direction);
    next.altitudeMeters = (std::max)(::world::PlanetTerrain::SeaLevelMeters, planet->terrain.Height(next)) +
                          actor.altitudeMeters;
    if (!actor.airborne)
    {
        if ((route->portalCity < 0 && InsideCore(next)) ||
            (TG::Distance(radius, next, session.Pose().origin) < DetailedMeters &&
             !session.LandingClear(next, .8)))
        {
            actor.speedMeters = 0;
            return;
        }
        if (Game::Get().SurfaceCities() &&
            Game::Get().SurfaceCities()->OccupiedAt(world, session, next, route->portalCity))
        {
            actor.speedMeters = 0;
            return;
        }
        for (const auto &other : state.actors)
        {
            if (other.id != actor.id && other.city < 0 && !other.airborne &&
                TransitHull(other, next, radius, .9))
            {
                actor.speedMeters = 0;
                return;
            }
        }
        bool playerContact = false;
        if (exterior && TG::Distance(radius, actor.position, session.Pose().origin) < 45)
        {
            const unsigned steps =
                (std::max)(1u, unsigned(std::ceil(TG::Distance(radius, actor.position, next) / .4)));
            auto contact = actor;
            for (unsigned i = 0; i <= steps && !playerContact; ++i)
            {
                contact.position = TransitArc(actor.position, next, double(i) / steps);
                playerContact =
                    TransitHull(contact, session.Pose().origin, radius, world.isBikeMounted ? 1.4 : .5);
            }
        }
        bool npcContact = false;
        if (exterior && TG::Distance(radius, actor.position, session.Pose().origin) < 150)
        {
            auto hit = [&](const ::world::SurfacePoint &point) {
                if (std::abs(point.altitudeMeters - actor.position.altitudeMeters) > 1.8 ||
                    TG::Distance(radius, actor.position, point) > 45)
                {
                    return false;
                }
                const unsigned count =
                    (std::max)(1u, unsigned(std::ceil(TG::Distance(radius, actor.position, next) / .4)));
                auto swept = actor;
                for (unsigned n = 0; n <= count; ++n)
                {
                    swept.position = TransitArc(actor.position, next, double(n) / count);
                    if (TransitHull(swept, point, radius, .5))
                    {
                        return true;
                    }
                }
                return false;
            };
            if (auto *party = Game::Get().SurfaceParty())
            {
                for (size_t i = 0; i < party->State().members.size(); ++i)
                {
                    const auto &m = party->State().members[i];
                    if (m.joined && m.place != CompanionPlace::Mine && m.combat.health > 0 && hit(m.position))
                    {
                        if (actor.speedMeters > 2)
                        {
                            party->Damage(CompanionId(i), int(8 + actor.speedMeters * 2), false);
                        }
                        npcContact = true;
                    }
                }
            }
            for (const auto &record : PlanetSecurity::State().officers)
            {
                auto &officers = CivicJustice::State().officers;
                auto o = std::find_if(officers.begin(), officers.end(),
                                      [&](const auto &v) { return v.id == record.id; });
                if (o != officers.end() && o->health > 0 && hit(record.position))
                {
                    if (actor.speedMeters > 2 && o->impact.invulnerability <= 0)
                    {
                        CivicCombat::DamageOfficer(size_t(o - officers.begin()),
                                                   int(8 + actor.speedMeters * 2), {}, false);
                    }
                    npcContact = true;
                }
            }
        }
        if (npcContact)
        {
            actor.speedMeters = 0;
            return;
        }
        if (playerContact)
        {
            if (actor.speedMeters > 2)
            {
                auto local = session.Local(actor.position);
                Vector2 impulse = Unit(Sub(world.playerPosition, {float(local.x / 2), float(local.y / 2)}));
                CivicCombat::DamagePlayer(int(8 + actor.speedMeters * 2), Mul(impulse, 3));
            }
            actor.speedMeters = 0;
            return;
        }
    }
    auto ahead = Position(*route, progress + direction * .25, direction);
    auto tangent = TransitAdd(ahead.normal, next.normal, -TG::Dot(ahead.normal, next.normal));
    if (TG::Dot(tangent, tangent) > 1.e-18)
    {
        tangent = TG::Unit(tangent);
        if (actor.airborne)
        {
            double turn = TG::Dot(TG::Cross(actor.heading, tangent), next.normal);
            actor.bank += float(std::clamp(-turn / (std::max)(.001f, seconds) * .2, -.25, .25) - actor.bank) *
                          (1 - std::exp(-4 * seconds));
        }
        const auto frame = TG::Frame(next);
        const double oldYaw =
            std::atan2(TG::Dot(actor.heading, frame.forward), TG::Dot(actor.heading, frame.right));
        const double targetYaw = std::atan2(TG::Dot(tangent, frame.forward), TG::Dot(tangent, frame.right));
        const double yaw =
            MotionMath::Turn(float(oldYaw), float(targetYaw), (actor.airborne ? 1.5f : 2.2f) * seconds);
        actor.heading = TG::Unit(TransitAdd(
            {frame.right.x * std::cos(yaw), frame.right.y * std::cos(yaw), frame.right.z * std::cos(yaw)},
            frame.forward, std::sin(yaw)));
    }
    actor.distanceMeters = progress;
    actor.position = next;
    if (route != originalRoute)
    {
        actor.routeId = route->id;
    }
    actor.direction = direction;
}
void PlanetTraffic::Populate(const GameState &world, const world::SurfaceSession &session)
{
    struct Candidate
    {
        size_t route;
        double distance;
    };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < routes->size(); ++i)
    {
        if (routes->at(i).portalCity >= 0)
        {
            continue; // Portals receive real cars; they never spawn copies.
        }
        auto near = TransitNearest((*routes)[i], session.Pose().origin, session.Planet().radiusMeters);
        if (near >= 0)
        {
            candidates.push_back({i, near});
        }
    }
    if (candidates.empty())
    {
        return;
    }
    size_t air = std::count_if(state.actors.begin(), state.actors.end(),
                               [](const auto &actor) { return actor.airborne; });
    size_t ground = state.actors.size() - air;
    for (unsigned attempt = 0; attempt < 24 && (ground < GroundLimit || air < AirLimit); ++attempt)
    {
        const auto &candidate = candidates[Random() % candidates.size()];
        const auto &route = routes->at(candidate.route);
        if (route.airborne ? air >= AirLimit : ground >= GroundLimit)
        {
            continue;
        }
        PlanetTrafficActor actor;
        actor.id = state.nextId;
        actor.routeId = route.id;
        actor.airborne = route.airborne;
        actor.collisionReady = !actor.airborne;
        actor.direction = Random() % 2 ? 1 : -1;
        actor.distanceMeters = candidate.distance + (Random() % 2 ? 1 : -1) * (220. + Random() % 220);
        if (actor.distanceMeters < 20 || actor.distanceMeters > route.lengthMeters - 20)
        {
            continue;
        }
        actor.health = actor.airborne ? 180 : 140;
        actor.altitudeMeters = actor.airborne ? 88 + actor.id % 3 * 6 : 0;
        actor.position = Position(route, actor.distanceMeters, actor.direction);
        actor.position.altitudeMeters =
            (std::max)(::world::PlanetTerrain::SeaLevelMeters, planet->terrain.Height(actor.position)) +
            actor.altitudeMeters;
        const double distance =
            TG::Distance(session.Planet().radiusMeters, actor.position, session.Pose().origin);
        if (distance < 180 || distance > 700 ||
            (!actor.airborne && (InsideCore(actor.position) || !session.LandingClear(actor.position, 1))))
        {
            continue;
        }
        auto ahead = Position(route, actor.distanceMeters + actor.direction * .25, actor.direction);
        actor.heading = TG::Unit(
            TransitAdd(ahead.normal, actor.position.normal, -TG::Dot(ahead.normal, actor.position.normal)));
        actor.speedMeters = actor.airborne ? 40 : 10;
        if (!Outside(actor, world, session, 320))
        {
            continue;
        }
        if (std::any_of(state.actors.begin(), state.actors.end(), [&](const auto &other) {
                return other.airborne == actor.airborne &&
                       std::fabs(other.altitudeMeters - actor.altitudeMeters) < 8 &&
                       TG::Distance(session.Planet().radiusMeters, other.position, actor.position) < 20;
            }))
        {
            continue;
        }
        if (state.nextId == std::numeric_limits<std::uint64_t>::max())
        {
            return;
        }
        ++state.nextId;
        state.actors.push_back(std::move(actor));
        profiling::Count(profiling::Counter::PlanetTrafficSpawned);
        route.airborne ? ++air : ++ground;
    }
}
void PlanetTraffic::Update(GameState &world, const world::SurfaceSession &session, float dt, bool exterior)
{
    if (!session.Ready())
    {
        return;
    }
    Bind(session.Stream().Snapshot()->planet);
    profiling::Scope timer(profiling::Timer::PlanetTraffic);
    dt = Game::Clamp(dt, 0, .05f);
    if (dt <= 0 || world.isPaused || world.mapVisible || world.weaponWheel.visible || world.journalVisible ||
        world.dialogueHistoryVisible || Dialogue::LocksWorld() || HospitalRecovery::Active())
    {
        return;
    }
    for (auto &actor : state.actors)
    {
        if (actor.city >= 0)
        {
            continue;
        }
        const bool detailed = exterior && TG::Distance(session.Planet().radiusMeters, actor.position,
                                                       session.Pose().origin) < DetailedMeters;
        if (actor.health <= 0)
        {
            actor.wreckSeconds += dt;
        }
        actor.farSeconds += dt;
        if (detailed || actor.farSeconds >= .5f)
        {
            float seconds = actor.farSeconds;
            actor.farSeconds = 0;
            Advance(actor, seconds, world, session, exterior);
            profiling::Count(detailed ? profiling::Counter::PlanetTrafficDetailedTicks
                                      : profiling::Counter::PlanetTrafficFarTicks);
        }
        // Never create a solid fallen hull around the player.
        if (actor.airborne && actor.health <= 0 && actor.altitudeMeters < 1 && !actor.collisionReady &&
            !TransitHull(actor, session.Pose().origin, session.Planet().radiusMeters, .7))
        {
            actor.collisionReady = true;
        }
    }
    const auto previousCount = state.actors.size();
    state.actors.erase(std::remove_if(state.actors.begin(), state.actors.end(),
                                      [&](const auto &actor) {
                                          if (actor.city >= 0)
                                          {
                                              return false;
                                          }
                                          if (actor.health <= 0 && actor.wreckSeconds > 20 &&
                                              (!exterior || Outside(actor, world, session, 80)))
                                          {
                                              return true;
                                          }
                                          return TG::Distance(session.Planet().radiusMeters, actor.position,
                                                              session.Pose().origin) > RetireMeters &&
                                                 (!exterior || Outside(actor, world, session, 400));
                                      }),
                       state.actors.end());
    profiling::Count(profiling::Counter::PlanetTrafficRetired, previousCount - state.actors.size());
    state.spawnSeconds = (std::max)(0.f, state.spawnSeconds - dt);
    if (exterior && state.spawnSeconds <= 0)
    {
        state.spawnSeconds = .75f;
        Populate(world, session);
    }
}

// ============================================================================
// WorldScenery — placement, collision and rendering share the imported bounds
// ============================================================================
WorldScenery &Overworld::Scenery()
{
    static WorldScenery scenery;
    return scenery;
}
bool WorldScenery::IsBlocked(Vector2 p, float clearance) const
{
    const float padding = (std::max)(0.f, clearance);
    for (int x = int(std::floor((p.x - padding) / 8)); x <= int(std::floor((p.x + padding) / 8)); ++x)
    {
        for (int y = int(std::floor((p.y - padding) / 8)); y <= int(std::floor((p.y + padding) / 8)); ++y)
        {
            auto cell = solidCells.find({x, y});
            if (cell == solidCells.end())
            {
                continue;
            }
            for (auto index : cell->second)
            {
                const auto &prop = props[index];
                if (p.x > prop.minimum.x - clearance && p.x < prop.maximum.x + clearance &&
                    p.y > prop.minimum.y - clearance && p.y < prop.maximum.y + clearance)
                {
                    return true;
                }
            }
        }
    }
    return false;
}
bool WorldScenery::Place(const GameState &world, const models::Model &model, models::SceneryType type,
                         Vector2 position, float yaw, bool solid)
{
    const auto &mesh = models::ResourceAssets::Get(model.resourceId);
    SceneryProp prop;
    prop.model = &model;
    prop.type = type;
    prop.position = position;
    prop.yaw = yaw;
    prop.solid = solid;
    prop.minimum = {1.e9f, 1.e9f};
    prop.maximum = {-1.e9f, -1.e9f};
    for (float x : {mesh.minimum.x, mesh.maximum.x})
    {
        for (float y : {mesh.minimum.y, mesh.maximum.y})
        {
            Vector2 p = {position.x + x * std::cos(yaw) - y * std::sin(yaw),
                         position.y + x * std::sin(yaw) + y * std::cos(yaw)};
            prop.minimum.x = (std::min)(prop.minimum.x, p.x);
            prop.minimum.y = (std::min)(prop.minimum.y, p.y);
            prop.maximum.x = (std::max)(prop.maximum.x, p.x);
            prop.maximum.y = (std::max)(prop.maximum.y, p.y);
        }
    }
    if (prop.minimum.x < kWorldMinX + 1 || prop.maximum.x > kWorldMaxX - 1 ||
        prop.minimum.y < kWorldMinY + 1 || prop.maximum.y > kWorldMaxY - 1)
    {
        return false;
    }
    Building footprint{prop.minimum.x, prop.minimum.y, prop.maximum.x - prop.minimum.x,
                       prop.maximum.y - prop.minimum.y, 0};
    if (CityLayout::OverlapsQuestSpace(footprint, .7f) ||
        CityPlan::OverlapsAccess(prop.minimum, prop.maximum))
    {
        return false;
    }
    auto overlap = [&](Vector2 minimum, Vector2 maximum, float padding) {
        return prop.minimum.x < maximum.x + padding && prop.maximum.x > minimum.x - padding &&
               prop.minimum.y < maximum.y + padding && prop.maximum.y > minimum.y - padding;
    };
    for (const auto &b : world.buildings)
    {
        if (overlap({b.x, b.y}, {b.x + b.width, b.y + b.depth}, .55f))
        {
            return false;
        }
    }
    for (const auto &other : props)
    {
        if (other.solid && overlap(other.minimum, other.maximum, .6f))
        {
            return false;
        }
    }
    if (solid && TransitLayout::OverlapsPavement(prop.minimum, prop.maximum, .35f))
    {
        return false;
    }
    for (const auto &person : world.residents)
    {
        if (overlap(person.position, person.position, 1.1f))
        {
            return false;
        }
    }
    for (float x = prop.minimum.x - .4f; x <= prop.maximum.x + .4f; x += .4f)
    {
        for (float y = prop.minimum.y - .4f; y <= prop.maximum.y + .4f; y += .4f)
        {
            if (RoadNetwork::Get().Contains({x, y}, .6f))
            {
                return false;
            }
        }
    }
    if (type == models::SceneryType::LandingPad || type == models::SceneryType::Sidewalk)
    {
        prop.elevation = .03f - mesh.maximum.z;
    }
    props.push_back(prop);
    if (prop.solid)
    {
        for (int x = int(std::floor(prop.minimum.x / 8)); x <= int(std::floor(prop.maximum.x / 8)); ++x)
        {
            for (int y = int(std::floor(prop.minimum.y / 8)); y <= int(std::floor(prop.maximum.y / 8)); ++y)
            {
                solidCells[{x, y}].push_back(props.size() - 1);
            }
        }
    }
    return true;
}
std::uint64_t WorldScenery::Revision() const
{
    return revision;
}
void WorldScenery::Generate(const GameState &world)
{
    ++revision;
    profiling::Scope startupTimer(profiling::Timer::ResourceInitialize);
    using S = models::SceneryType;
    props.clear();
    solidCells.clear();
    auto place = [&](S type, Vector2 p, float yaw = 0) {
        return Place(world, models::ModelLibrary::Scenery(type), type, p, yaw,
                     type != S::LandingPad && type != S::Sidewalk);
    };
    constexpr S streets[] = {S::Bench,      S::Planter,    S::Bollard,     S::TransitShelter,
                             S::Wayfinding, S::ChargePost, S::Streetlight, S::CrashBarrier};
    constexpr S industry[] = {S::Transformer, S::CargoContainer, S::HVAC,     S::PipeRack,
                              S::CommRelay,   S::SolarArray,     S::WaterTank};
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[city].center;
        // Deterministic candidates try open setbacks first, then the outer service apron.
        for (int index = 0; index < 26; ++index)
        {
            S type = index < 18 ? streets[index % std::size(streets)]
                                : industry[(index - 18) % std::size(industry)];
            for (int attempt = 0; attempt < 48; ++attempt)
            {
                float x = -27.f + float((index * 7 + attempt * 11) % 66);
                float y = -10.f + float((index * 11 + attempt * 7) % 34);
                if (attempt >= 24)
                {
                    x = 42.f + float(attempt % 6) * 2;
                    y = -25.f + float((index * 7 + attempt) % 48);
                }
                if (place(type, Add(center, {x, y}), index % 2 ? kPi * .5f : 0))
                {
                    break;
                }
            }
        }
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            Vector2 pad = Add(center, {54.f + float(attempt / 3) * 13, 11.f + float(attempt % 3) * 12});
            if (!place(S::LandingPad, pad))
            {
                continue;
            }
            auto type = static_cast<models::AircraftType>(city % int(models::AircraftType::Count));
            Place(world, models::ModelLibrary::Aircraft(type, city % 2 != 0), S::Count, pad, -.35f, true);
            place(S::AirfieldBeacon, Add(pad, {4.6f, 4.6f}));
            place(S::AirfieldBeacon, Add(pad, {-4.6f, 4.6f}));
            place(S::TransitShelter, Add(pad, {6.5f, 0}));
            break;
        }
        for (int i = 0; i < 8; ++i)
        {
            for (int attempt = 0; attempt < 12; ++attempt)
            {
                if (place(i % 2 ? S::PrismBloom : S::SailTree,
                          Add(center,
                              {-35.f + float((i * 13 + attempt * 7) % 78), 35.f + float(attempt % 4) * 3})))
                {
                    break;
                }
            }
        }
    }
    for (auto type : {S::FieldShelter, S::SurveyTripod, S::MiningConveyor, S::CargoContainer, S::Transformer})
    {
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            if (place(type, {137.f + float(attempt % 4) * 3, 12.f + float(attempt / 4) * 4}))
            {
                break;
            }
        }
    }
}

const std::array<CityInfo, Overworld::kCityCount> &Overworld::Cities()
{
    static const std::array<CityInfo, kCityCount> cities = [] {
        std::array<CityInfo, kCityCount> result = {
            {{"halo_07",
              "헤일로",
              {0, 0},
              "익숙한 일상 속 불안",
              "요즘 관문 쪽 수신기에 잡음이 많대요. 정비가 밀렸나 봐요."},
             {"cinder",
              "신더",
              {0, 0},
              "분주한 공업 도시",
              "광산 교대조가 아직 안 돌아왔대요. 길이 막힌 건 아닌지 모르겠네요."},
             {"veil",
              "베일",
              {0, 0},
              "고요한 관측 도시",
              "관측소에서 별자리 기록을 다시 맞춘다더군요. 어제 것과 안 맞는대요."},
             {"lumen",
              "루멘",
              {0, 0},
              "빛으로 버티는 피난 도시",
              "밤길엔 불 켜진 길로 다니세요. 저도 요즘은 돌아가요."},
             {"echo",
              "에코",
              {0, 0},
              "끊어진 통신을 기다리는 도시",
              "수신기가 지난 교신을 자꾸 되풀이해요. 새 연락인 줄 알고 매번 들여다보게 되네요."}}};
        unsigned int seed = 0x458a91u;
        for (int i = 1; i < kCityCount; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            float angle = .25f + (i - 1) * 1.57f + float(seed % 350) / 1000.f;
            float radius = 260.f + float((seed >> 12) % 85);
            result[i].center = {std::round(std::cos(angle) * radius), std::round(std::sin(angle) * radius)};
        }
        return result;
    }();
    return cities;
}

const std::vector<world::StoryAnchor> &Overworld::StoryAnchors()
{
    static const std::vector<world::StoryAnchor> anchors = [] {
        const auto &adapter = world::LegacyWorldAdapter::ErmaExterior();
        auto anchor = [&](const char *id, const char *region, Vector2 p) {
            return adapter.Anchor(id, region, {double(p.x), double(p.y), 0});
        };
        return std::vector<world::StoryAnchor>{anchor("home_entrance", "halo_07", kHomeDoor),
                                               anchor("ria_home", "halo_07", kLiaPosition),
                                               anchor("observatory", "halo_07", kObservatoryBuildingPosition),
                                               anchor("city_receiver", "halo_07", kCityReceiverPosition),
                                               anchor("mine_entrance", "mq00_mine", kMineEntrance)};
    }();
    return anchors;
}
const world::GeneratedPlanet &Overworld::PlanetPlan()
{
    static const auto plan = world::PlanetGenerator::Erma(StoryAnchors()).Generate();
    return plan;
}

int Overworld::CityAt(Vector2 position)
{
    for (int i = 0; i < kCityCount; ++i)
    {
        if (Game::Distance(position, Cities()[i].center) <= 45)
        {
            return i;
        }
    }
    return -1;
}

bool Overworld::IsRoad(int x, int y)
{
    return RoadNetwork::Get().Contains({float(x) + .5f, float(y) + .5f});
}

models::Palette Overworld::BikePalette()
{
    models::Palette palette;
    palette.body = {.21f, .32f, .39f, 1};
    palette.accent = {.91f, .63f, .27f, 1};
    palette.emission = {.28f, .86f, 1, 1};
    return palette;
}

void Overworld::Populate(GameState &state)
{
    Traffic().Reset();
    Flights().Reset();
    // Fix the street hierarchy before allocating any ordinary lots. Only the two story landmarks
    // have authored footprints; every other building is a setback from a real local street frontage.
    const auto &library = models::ModelLibrary::Get();
    Building home = state.buildings.front();
    Building observatory =
        *std::find_if(state.buildings.begin(), state.buildings.end(),
                      [](const Building &b) { return b.type == models::BuildingType::Observatory; });
    auto physical = [&](Building &building) {
        const auto &resource =
            models::ResourceAssets::Get(library.buildings[size_t(building.type)].resourceId);
        building.width = resource.physicalSize.x;
        building.depth = resource.physicalSize.y;
        building.height = resource.physicalSize.z / resource.maximum.z * 44.f;
    };
    physical(home);
    physical(observatory);
    state.buildings = {home, observatory};
    const auto &network = RoadNetwork::Get();
    unsigned lot = 0;
    for (int city = 0; city < kCityCount; ++city)
    {
        auto center = Cities()[city].center;
        for (const auto &road : network.roads)
        {
            const auto &a = network.junctions[road.from], &b = network.junctions[road.to];
            if (a.altitude > .1f || b.altitude > .1f)
            {
                continue;
            }
            auto d = Unit(Sub(b.position, a.position)), normal = RoadNetwork::RightOf(d);
            float length = Game::Distance(a.position, b.position);
            for (float along = a.radius + 8; along < length - b.radius - 6; along += 12)
            {
                auto anchor = Add(a.position, Mul(d, along));
                if (std::fabs(anchor.x - center.x) > 50 || std::fabs(anchor.y - center.y) > 43)
                {
                    continue;
                }
                for (float side : {-1.f, 1.f})
                {
                    Building building = {};
                    ++lot;
                    unsigned count = unsigned(models::BuildingType::Count);
                    unsigned type = (lot + city) % 4;
                    if (count > 5 && lot % 5 == 0 && Game::Distance(anchor, center) > 24)
                    {
                        type = 5 + (lot / 5 + city) % (count - 5);
                    }
                    building.type = models::BuildingType(type);
                    physical(building);
                    float extent = std::fabs(normal.x) * building.width * .5f +
                                   std::fabs(normal.y) * building.depth * .5f;
                    auto position = Add(anchor, Mul(normal, side * (road.lanes * .75f + 2.2f + extent)));
                    building.x = position.x - building.width * .5f;
                    building.y = position.y - building.depth * .5f;
                    building.palette.body = {.32f + .055f * (lot % 4), .38f + .04f * (city % 3),
                                             .43f + .035f * (lot % 5), 1};
                    building.palette.accent = {.32f, .7f, .78f, 1};
                    building.palette.emission = {.25f, .75f, .85f, 1};
                    state.buildings.push_back(building);
                }
            }
        }
    }
    CityLayout::ValidateBuildings(state.buildings);
    CityPlan::Rebuild(state);
    // Lia + Mara + eighteen residents = twenty Halo citizens. Kain replaces a reserved citizen.
    constexpr Vector2 positions[] = {{-5, 3}, {-1, 3},   {5, 3},    {8, -3},  {-11, 3}, {10, 6},  {-8, 8},
                                     {2, 8},  {-10, -7}, {11, -3},  {11, -5}, {-12, 7}, {-6, -7}, {-2, -7},
                                     {4, 8},  {8, 8},    {-13, -3}, {12, 8},  {-3, 3},  {0, 3}};
    constexpr const char *roles[] = {"배송 기사", "교대 근무자", "정비공", "경비원",    "학생",
                                     "상인",      "주민",        "관측원", "통신 기사", "탐사원"};
    constexpr const char *smallTalk[] = {
        "다음 도시까지 배송이 남았어요. 오늘은 길이 덜 막혔으면 좋겠네요.",
        "이제 교대하러 가요. 돌아올 때도 저 가게가 열려 있으면 좋겠는데.",
        "부품은 남아 있는데 손이 모자라네요. 다들 고장 나기 전엔 안 가져오거든요.",
        "횡단보도 신호가 바뀌면 건너세요. 조용해도 화물차가 금방 와요.",
        "관측소 견학을 신청했어요. 관문이 멈추기 전에는 어땠는지 궁금해서요.",
        "물건은 운송편이 오는 대로 채워요. 필요한 게 있으면 미리 말해 주세요.",
        "해 질 때 도시 밖 풀밭을 보셨어요? 바람 부는 쪽부터 빛이 나요.",
        "기록 하나가 다르면 오차일 수 있죠. 그래서 어제 것부터 다시 보고 있어요.",
        "응답이 왔다고 연결된 건 아니에요. 발신 기록까지 확인해야죠.",
        "돌아올 길에 표식을 남겨 두세요. 여기선 익숙한 풍경도 꽤 비슷해 보여요."};
    constexpr models::CharacterType appearances[] = {
        models::CharacterType::Pilot,    models::CharacterType::Civilian,   models::CharacterType::Engineer,
        models::CharacterType::Guard,    models::CharacterType::Civilian,   models::CharacterType::Civilian,
        models::CharacterType::Civilian, models::CharacterType::Researcher, models::CharacterType::Engineer,
        models::CharacterType::Explorer};
    state.residents.clear();
    for (int cityIndex = 0; cityIndex < kCityCount; ++cityIndex)
    {
        const auto &city = Cities()[cityIndex];
        int count = ResidentPopulation(cityIndex);
        for (int i = 0; i < count; ++i)
        {
            Vector2 position = {city.center.x + positions[i].x, city.center.y + positions[i].y};
            // Place every resident on a genuinely reachable outdoor footprint.
            auto blocked = [&state](Vector2 p) {
                bool building =
                    std::any_of(state.buildings.begin(), state.buildings.end(), [p](const Building &b) {
                        return p.x > b.x - .3f && p.x < b.x + b.width + .3f && p.y > b.y - .3f &&
                               p.y < b.y + b.depth + .3f;
                    });
                return building || RoadNetwork::Get().Contains(p, .7f) || CityLayout::IsQuestSpace(p, .5f) ||
                       std::any_of(state.residents.begin(), state.residents.end(),
                                   [p](const Resident &r) { return Game::Distance(p, r.position) < .8f; });
            };
            for (int attempt = 0; attempt < 200 && blocked(position); ++attempt)
            {
                position = {city.center.x - 24 + float((attempt + i * 7) % 49),
                            city.center.y + 3.5f + float(attempt / 49) * 1.2f};
            }
            Resident resident;
            resident.position = position;
            resident.name = std::string(city.name) + " " + roles[i % 10] + " " + std::to_string(i + 1);
            resident.dialogue = resident.name + " / " + (i % 3 == 0 ? city.rumor : smallTalk[i % 10]);
            resident.type = appearances[i % 10];
            resident.coat = {.3f + .08f * (i % 4), .36f + .06f * (cityIndex % 4), .46f + .04f * (i % 5), 1};
            resident.gender = i % 2 ? models::Gender::Female : models::Gender::Male;
            resident.hair = models::HairStyle(i % int(models::HairStyle::Count));
            resident.cityIndex = cityIndex;
            resident.profession = static_cast<ResidentProfession>(i % 10);
            state.residents.push_back(resident);
        }
    }
    Scenery().Generate(state);
    CityResidents::Get().Reset(state);
}

void Overworld::UpdateBike(GameState &state, float deltaSeconds)
{
    if (!state.surfaceExploration)
    {
        BodyCollision::RecoverBike();
    }
    Vector2 previous = state.playerPosition;
    float oldYaw = state.bikeYaw;
    constexpr float response = MotionMath::kResponse;
    if (std::hypot(state.bikeVelocity.x, state.bikeVelocity.y) < .001f && std::fabs(state.bikeSpeed) > .01f)
    {
        state.bikeVelocity = {std::cos(oldYaw) * state.bikeSpeed, std::sin(oldYaw) * state.bikeSpeed};
    }
    float steering = float(state.pressedKeys['d']) - float(state.pressedKeys['a']);
    float targetYawSpeed = steering * (state.bikeSpeed < -.05f ? -1 : 1) * 1.8f;
    state.bikeYawSpeed += Game::Clamp(targetYawSpeed - state.bikeYawSpeed, -9.f * response * deltaSeconds,
                                      9.f * response * deltaSeconds);
    state.bikeYaw = std::remainder(state.bikeYaw + state.bikeYawSpeed * deltaSeconds, 2 * kPi);
    bool forward = state.pressedKeys['w'] && !state.pressedKeys['s'];
    bool reverse = state.pressedKeys['s'] && !state.pressedKeys['w'];
    float targetSpeed = forward   ? (state.pressedKeys[' '] ? kBikeBoostSpeed : kBikeCruiseSpeed)
                        : reverse ? -kBikeReverseSpeed
                                  : 0;
    bool braking =
        state.bikeSpeed * targetSpeed < 0 || (targetSpeed == 0 && std::fabs(state.bikeSpeed) > .01f);
    float acceleration = braking ? 24.f : reverse ? 4.f : 18.f;
    auto blocked = [&](Vector2 p) {
        if (state.surfaceExploration)
        {
            auto session = Game::Get().Surface();
            if (!session)
            {
                return true;
            }
            float c = std::cos(state.bikeYaw), s = std::sin(state.bikeYaw);
            for (float front : {-.65f, .65f})
            {
                for (float side : {-.22f, .22f})
                {
                    if (Game::Get().IsPositionBlocked(
                            {p.x + c * front - s * side, p.y + s * front + c * side}))
                    {
                        return true;
                    }
                }
            }
            return false;
        }
        float h = RoadNetwork::Get().DriveElevation(p, state.bikeElevation);
        if (state.bikeElevation > .5f && h < state.bikeElevation - .6f)
        {
            return true;
        }
        return BodyCollision::BikeBlocked(p, state.bikeYaw, h, false);
    };
    if (blocked(previous))
    {
        float proposedYaw = state.bikeYaw;
        state.bikeYaw = oldYaw;
        if (blocked(previous))
        {
            state.bikeYaw = proposedYaw;
        }
        state.bikeYawSpeed = 0;
    }
    Vector2 forwardAxis = {std::cos(state.bikeYaw), std::sin(state.bikeYaw)};
    Vector2 sideAxis = {-forwardAxis.y, forwardAxis.x};
    float longitudinal = state.bikeVelocity.x * forwardAxis.x + state.bikeVelocity.y * forwardAxis.y;
    float lateral = state.bikeVelocity.x * sideAxis.x + state.bikeVelocity.y * sideAxis.y;
    longitudinal += Game::Clamp(targetSpeed - longitudinal, -acceleration * response * deltaSeconds,
                                acceleration * response * deltaSeconds);
    // Retain a short lateral drift after steering, then regain traction automatically.
    lateral *= std::exp(-(braking ? 10.f : 5.5f) * response * deltaSeconds);
    state.bikeVelocity = {forwardAxis.x * longitudinal + sideAxis.x * lateral,
                          forwardAxis.y * longitudinal + sideAxis.y * lateral};
    float speed = std::hypot(state.bikeVelocity.x, state.bikeVelocity.y);
    float limit = (std::max)(std::fabs(state.bikeSpeed), std::fabs(targetSpeed));
    if (speed > limit && speed > .001f)
    {
        state.bikeVelocity.x *= limit / speed;
        state.bikeVelocity.y *= limit / speed;
    }
    Vector2 requestedDelta = {state.bikeVelocity.x * deltaSeconds, state.bikeVelocity.y * deltaSeconds};
    Vector2 desired = {previous.x + requestedDelta.x, previous.y + requestedDelta.y};
    state.playerPosition = BodyCollision::Slide(previous, desired, blocked);
    float actual = Game::Distance(previous, state.playerPosition);
    state.bikeVelocity = MotionMath::ContactVelocity(
        state.bikeVelocity, requestedDelta,
        {state.playerPosition.x - previous.x, state.playerPosition.y - previous.y});
    speed = std::hypot(state.bikeVelocity.x, state.bikeVelocity.y);
    state.bikeSpeed = (longitudinal < -.001f ? -1.f : 1.f) * speed;
    if (!state.surfaceExploration)
    {
        state.bikeElevation = RoadNetwork::Get().DriveElevation(state.playerPosition, state.bikeElevation);
    }
    state.bikePosition = state.playerPosition;
    state.facingYaw = state.bikeYaw;
    state.movementSpeed = deltaSeconds > 0 ? actual / deltaSeconds : 0;
    state.movementAmount = Game::Clamp(state.movementSpeed / kBikeBoostSpeed, 0, 1);
}

void Overworld::ToggleBike(GameState &state)
{
    if (Dialogue::Active() || state.journalVisible || state.dialogueHistoryVisible ||
        HospitalRecovery::Active())
    {
        return;
    }
    if (state.isBikeMounted)
    {
        if (!state.surfaceExploration && state.bikeElevation > .3f)
        {
            Game::Get().ShowNotice("바이크 / 고가도로에서는 하차할 수 없습니다. 진출 경사로로 이동하세요.");
            return;
        }
        if (std::fabs(state.bikeSpeed) > 1)
        {
            Game::Get().ShowNotice("바이크 / 방향키를 놓고 정차한 뒤 E로 내리세요.");
            return;
        }
        for (Vector2 offset : {Vector2{1.2f, 0}, {-1.2f, 0}, {0, 1.2f}, {0, -1.2f}})
        {
            Vector2 position = {state.bikePosition.x + offset.x, state.bikePosition.y + offset.y};
            bool safe = state.surfaceExploration && Game::Get().Surface()
                            ? !Game::Get().Surface()->Blocked(position.x * 2, position.y * 2, false)
                            : !Game::Get().IsPositionBlocked(position);
            if (safe)
            {
                state.playerPosition = position;
                state.isBikeMounted = false;
                state.mouseFiring = state.mouseAiming = false;
                state.StopMotion();
                Game::Get().ShowNotice("바이크 / 하차했습니다. 주차 위치는 유지됩니다.");
                return;
            }
        }
        Game::Get().ShowNotice("바이크 / 내릴 공간이 부족합니다. 넓은 곳으로 이동하세요.");
        return;
    }
    state.isBikeMounted = true;
    state.mouseFiring = state.mouseAiming = false;
    state.playerPosition = state.bikePosition;
    state.StopMotion();
    Game::Get().ShowNotice(
        state.surfaceExploration
            ? "바이크 / W 가속 · S 제동/후진 · A/D 조향 · Space 부스트 · 정차 후 E 하차"
            : "바이크 / W 가속 · S 제동 · A/D 조향 · 정차 후 E 하차. 교차로의 홀로그램 신호를 확인하세요.");
}
// ============================================================================
// PlanetCities — city-local simulation is never rebased or recreated on a revisit
// ============================================================================
PlanetCities::PlanetCities()
{
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        traffic[size_t(city)].SetCityFilter(city);
    }
}
TrafficSystem *PlanetCities::ActiveTraffic()
{
    const int city = observedCity >= 0 ? observedCity : activeCity;
    return city >= 0 ? &traffic[size_t(city)] : nullptr;
}
world::CityCorePlan PlanetCities::Describe(const GameState &state)
{
    world::CityCorePlan plan;
    const auto &net = RoadNetwork::Get();
    const auto &library = models::ModelLibrary::Get();
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto center = Overworld::Cities()[size_t(city)].center;
        world::CityCore core;
        core.regionId = Overworld::Cities()[size_t(city)].id;
        auto palette = [](const models::Palette &p) {
            return world::SurfaceStructure::Palette{p.body, p.accent, p.emission};
        };
        for (size_t i = 0; i < state.buildings.size(); ++i)
        {
            const auto &b = state.buildings[i];
            const auto &mesh = models::ResourceAssets::Get(library.buildings[size_t(b.type)].resourceId);
            Vector2 position{b.x + (mesh.minimum.x + mesh.maximum.x) * b.width * .5f,
                             b.y + (mesh.minimum.y + mesh.maximum.y) * b.depth * .5f};
            if (Game::Distance(position, center) > 100)
            {
                continue;
            }
            world::CoreLot lot;
            lot.id = "building_" + std::to_string(i);
            lot.variant = std::uint16_t(b.type);
            lot.positionMeters = {(position.x - center.x) * 2, (position.y - center.y) * 2,
                                  mesh.minimum.z * b.height / 22};
            lot.sizeMeters = {(mesh.maximum.x - mesh.minimum.x) * b.width * 2,
                              (mesh.maximum.y - mesh.minimum.y) * b.depth * 2,
                              (mesh.maximum.z - mesh.minimum.z) * b.height / 22};
            lot.palette = palette(b.palette);
            core.lots.push_back(std::move(lot));
        }
        for (size_t i = 0; i < net.roads.size(); ++i)
        {
            const auto &road = net.roads[i];
            int lane = -1;
            for (int index : net.junctions[size_t(road.from)].outgoing)
            {
                if (net.lanes[size_t(index)].road == int(i))
                {
                    lane = index;
                    break;
                }
            }
            if (lane < 0 || !TrafficSystem::CoreLane(lane, city))
            {
                continue;
            }
            auto a = Sub(net.junctions[size_t(road.from)].position, center),
                 b = Sub(net.junctions[size_t(road.to)].position, center);
            core.roads.push_back({std::uint32_t(road.from),
                                  std::uint32_t(road.to),
                                  {a.x * 2, a.y * 2, 0},
                                  {b.x * 2, b.y * 2, 0},
                                  unsigned(road.lanes)});
        }
        auto model = [&](std::string id, world::ContentForm form, std::uint16_t variant,
                         const models::Model &asset, Vector2 position, float yaw, bool solid,
                         float elevation = 0) {
            if (!asset.resourceId || Game::Distance(position, center) > 100)
            {
                return;
            }
            const auto &mesh = models::ResourceAssets::Get(asset.resourceId);
            float x = (mesh.minimum.x + mesh.maximum.x) * .5f, y = (mesh.minimum.y + mesh.maximum.y) * .5f;
            world::CoreLot lot;
            lot.id = std::move(id);
            lot.form = form;
            lot.variant = variant;
            lot.use = world::ContentUse::Services;
            lot.positionMeters = {(position.x - center.x + x * std::cos(yaw) - y * std::sin(yaw)) * 2,
                                  (position.y - center.y + x * std::sin(yaw) + y * std::cos(yaw)) * 2,
                                  (mesh.minimum.z + elevation) * 2};
            lot.sizeMeters = {(mesh.maximum.x - mesh.minimum.x) * 2, (mesh.maximum.y - mesh.minimum.y) * 2,
                              (mesh.maximum.z - mesh.minimum.z) * 2};
            lot.yaw = yaw;
            lot.solid = solid;
            lot.palette = palette({});
            if (lot.sizeMeters.x > .001 && lot.sizeMeters.y > .001 && lot.sizeMeters.z > .001 &&
                std::hypot(lot.positionMeters.x, lot.positionMeters.y) +
                        std::hypot(lot.sizeMeters.x, lot.sizeMeters.y) * .5 <=
                    core.radiusMeters)
            {
                core.lots.push_back(std::move(lot));
            }
        };
        const auto &props = Overworld::Scenery().Props();
        for (size_t i = 0; i < props.size(); ++i)
        {
            const auto &prop = props[i];
            if (!prop.model || prop.elevation > .1f)
            {
                continue;
            }
            if (prop.type == models::SceneryType::Count)
            {
                bool matched = false;
                for (unsigned type = 0; type < unsigned(models::AircraftType::Count) && !matched; ++type)
                {
                    for (unsigned alternate = 0; alternate < 2 && !matched; ++alternate)
                    {
                        if (models::ModelLibrary::Aircraft(models::AircraftType(type), alternate != 0)
                                .resourceId == prop.model->resourceId)
                        {
                            model("parked_aircraft_" + std::to_string(i), world::ContentForm::Aircraft,
                                  std::uint16_t(type * 2 + alternate), *prop.model, prop.position, prop.yaw,
                                  prop.solid, prop.elevation);
                            matched = true;
                        }
                    }
                }
                continue;
            }
            model("prop_" + std::to_string(i), world::ContentForm::Scenery, std::uint16_t(prop.type),
                  *prop.model, prop.position, prop.yaw, prop.solid, prop.elevation);
        }
        if (city == 0)
        {
            model("receiver", world::ContentForm::Console, 0, library.console, kCityReceiverPosition, 0,
                  true);
            model("power", world::ContentForm::Breaker, 0, library.breaker, kRelayPosition, 0, true);
            model("observatory_lift", world::ContentForm::Beacon, 0, library.beacon, kBeaconPosition, 0,
                  true);
        }
        const auto &walkways = CityPlan::Walkways();
        for (size_t i = 0; i < walkways.size(); ++i)
        {
            const auto &walkway = walkways[i];
            if (Game::Distance(walkway.position, center) > 95 || walkway.altitude > .1f)
            {
                continue;
            }
            auto count = core.lots.size();
            model("walkway_" + std::to_string(i), world::ContentForm::TransitMesh, 0,
                  models::ModelLibrary::Transit(walkway.asset), walkway.position, walkway.yaw, false,
                  walkway.altitude);
            if (core.lots.size() > count)
            {
                core.lots.back().assetName = walkway.asset;
            }
        }
        plan.cities.push_back(std::move(core));
    }
    plan.cities.push_back(game::GameLevels::Chapter().Surface().Describe());
    plan.Validate();
    return plan;
}
std::optional<world::SurfacePoint> PlanetCities::Point(const world::SurfaceSession &session, int city,
                                                       Vector2 legacy) const
{
    if (!session.Ready() || city < 0 || city >= Overworld::kCityCount)
    {
        return std::nullopt;
    }
    const auto plan = session.Stream().Snapshot()->planet;
    if (!plan->cityCores || !plan->cityCores->Find(Overworld::Cities()[size_t(city)].id))
    {
        return std::nullopt;
    }
    const auto &region = plan->Region(Overworld::Cities()[size_t(city)].id);
    auto center = Overworld::Cities()[size_t(city)].center;
    auto point = world::SurfaceGeometry::FromLocal(region.frame,
                                                   {(legacy.x - center.x) * 2, (legacy.y - center.y) * 2, 0},
                                                   session.Planet().radiusMeters);
    auto core = plan->cityCores->Find(Overworld::Cities()[size_t(city)].id);
    point.altitudeMeters = Game::Distance(legacy, center) * 2 <= core->radiusMeters
                               ? core->elevationMeters
                               : plan->terrain.Height(point);
    return point;
}
std::optional<Vector2> PlanetCities::Local(const world::SurfaceSession &session, int city,
                                           Vector2 legacy) const
{
    auto point = Point(session, city, legacy);
    if (!point ||
        world::SurfaceGeometry::Distance(session.Planet().radiusMeters, *point, session.Pose().origin) > 1000)
    {
        return std::nullopt;
    }
    auto local = session.Local(*point);
    return Vector2{float(local.x / 2), float(local.y / 2)};
}
float PlanetCities::Yaw(const world::SurfaceSession &session, int city, float yaw) const
{
    auto plan = session.Stream().Snapshot()->planet;
    const auto &region = plan->Region(Overworld::Cities()[size_t(city)].id);
    auto direction = world::SurfaceGeometry::TransformDirection({std::cos(yaw), std::sin(yaw), 0},
                                                                region.frame, session.Origin());
    return float(std::atan2(direction.y, direction.x));
}
// ============================================================================
// CitySimulation — passive time is charged once per play frame, without expensive remote decisions
// ============================================================================
CitySimulationBand CitySimulation::Select(double distance, CitySimulationBand previous, bool exterior)
{
    if (!exterior || !std::isfinite(distance) || distance < 0)
    {
        return CitySimulationBand::Dormant;
    }
    if (distance <= EnterMeters || (previous == CitySimulationBand::Detailed && distance <= LeaveMeters))
    {
        return CitySimulationBand::Detailed;
    }
    return distance <= RetainMeters ? CitySimulationBand::Retained : CitySimulationBand::Dormant;
}
const char *CitySimulation::Name(CitySimulationBand band)
{
    switch (band)
    {
    case CitySimulationBand::Detailed:
        return "detailed";
    case CitySimulationBand::Retained:
        return "retained";
    default:
        return "dormant";
    }
}
void CitySimulation::AdvanceDormant(Resident &resident, float dt)
{
    dt = Game::Clamp(dt, 0, .05f);
    if (dt <= 0)
    {
        return;
    }
    resident.facing.walking = false;
    resident.waitSeconds = (std::max)(0.f, resident.waitSeconds - dt);
    resident.conversationSeconds = (std::max)(0.f, resident.conversationSeconds - dt);
    resident.fearSeconds = (std::max)(0.f, resident.fearSeconds - dt);
    BodyCollision::AgeResident(resident, dt);
    if (resident.health <= 0)
    {
        resident.activity = ResidentActivity::Dead;
        resident.crossingJunction = -1;
        resident.route.clear();
        resident.routeCursor = 0;
    }
    else if ((resident.activity == ResidentActivity::Talking && resident.conversationSeconds <= 0) ||
             (resident.activity == ResidentActivity::Fleeing && resident.fearSeconds <= 0) ||
             (resident.activity == ResidentActivity::Injured && resident.impact.seconds <= 0))
    {
        resident.activity = ResidentActivity::Resting;
    }
    // Preserve living route/crossing occupancy: no teleport to a routine's final waypoint.
}
void PlanetCities::Update(GameState &state, const world::SurfaceSession &session, float dt)
{
    profiling::Scope timer(profiling::Timer::PlanetSimulation);
    activeCity = -1;
    if (!session.Ready())
    {
        return;
    }
    const auto plan = session.Stream().Snapshot()->planet;
    if (!plan->cityCores)
    {
        return;
    }
    const bool exterior = !state.isInsideHome && !state.isInObservatory && !Game::Get().IsSurfaceMine();
    double best = CitySimulation::LeaveMeters + 1;
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto &status = simulation[size_t(city)];
        status.distanceMeters = world::SurfaceGeometry::Distance(
            session.Planet().radiusMeters, session.Pose().origin,
            plan->Region(Overworld::Cities()[size_t(city)].id).center.position);
        status.band = CitySimulation::Select(status.distanceMeters, status.band, exterior);
        if (status.band == CitySimulationBand::Detailed && status.distanceMeters < best)
        {
            best = status.distanceMeters;
            activeCity = city;
        }
    }
    if (dt <= 0 || state.isPaused || state.mapVisible || state.weaponWheel.visible || state.journalVisible ||
        state.dialogueHistoryVisible || Dialogue::LocksWorld() || HospitalRecovery::Active())
    {
        return;
    }
    dt = Game::Clamp(dt, 0, .05f);
    remainingPathBudget = CitySimulation::PathBudget;
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        auto &status = simulation[size_t(city)];
        if (status.band == CitySimulationBand::Detailed)
        {
            Observe(state, session, city, [&] {
                BodyCollision::UpdateCity(dt, city, city == activeCity);
                traffic[size_t(city)].Update(state, dt);
                CityResidents::Get().Update(state, dt, city, &remainingPathBudget);
            });
            ++status.detailedTicks;
            profiling::Count(profiling::Counter::CityDetailedTicks);
        }
        else
        {
            status.passiveSeconds += dt;
            auto retired = traffic[size_t(city)].AdvanceDormant(dt);
            profiling::Count(profiling::Counter::CityRetiredWrecks, retired);
            for (auto &resident : state.residents)
            {
                if (resident.cityIndex == city)
                {
                    CitySimulation::AdvanceDormant(resident, dt);
                    profiling::Count(profiling::Counter::CityPassiveResidents);
                }
            }
        }
        if (auto *transit = Game::Get().SurfaceTraffic())
        {
            transit->SyncCity(city, traffic[size_t(city)], session);
        }
    }
}
void PlanetCities::Observe(GameState &state, const world::SurfaceSession &session, int city,
                           const std::function<void()> &operation)
{
    if (observedCity >= 0 || city < 0 || city >= Overworld::kCityCount || !session.Ready())
    {
        throw std::logic_error("Invalid or nested city observation");
    }
    const auto &frame =
        session.Stream().Snapshot()->planet->Region(Overworld::Cities()[size_t(city)].id).frame;
    const auto center = Overworld::Cities()[size_t(city)].center;
    auto point = [&](world::SurfacePoint p) {
        auto local = world::SurfaceGeometry::ToLocal(frame, p, session.Planet().radiusMeters);
        return Vector2{center.x + float(local.x / 2), center.y + float(local.y / 2)};
    };
    auto vector = [&](Vector2 v, const world::TangentFrame &from, const world::TangentFrame &to) {
        auto r = world::SurfaceGeometry::TransformDirection({v.x, v.y, 0}, from, to);
        return Vector2{float(r.x), float(r.y)};
    };
    auto yaw = [&](float angle, const world::TangentFrame &from, const world::TangentFrame &to) {
        auto v = vector({std::cos(angle), std::sin(angle)}, from, to);
        return std::atan2(v.y, v.x);
    };
    auto &impact = CivicJustice::State().playerImpact;
    struct Restore
    {
        GameState &state;
        BodyImpact &impact;
        int &observer;
        Vector2 player, camera, bike, bikeVelocity, velocity, impulse;
        float facing, bikeYaw, bikeElevation;
        ~Restore()
        {
            state.playerPosition = player;
            state.cameraPosition = camera;
            state.bikePosition = bike;
            state.bikeVelocity = bikeVelocity;
            state.playerMotion.velocity = velocity;
            impact.velocity = impulse;
            state.facingYaw = facing;
            state.bikeYaw = bikeYaw;
            state.bikeElevation = bikeElevation;
            observer = -1;
        }
    } restore{state,
              impact,
              observedCity,
              state.playerPosition,
              state.cameraPosition,
              state.bikePosition,
              state.bikeVelocity,
              state.playerMotion.velocity,
              impact.velocity,
              state.facingYaw,
              state.bikeYaw,
              state.bikeElevation};
    const bool playerPresent = city == activeCity;
    observedCity = city;
    state.playerPosition = point(session.Pose().origin);
    const auto entry = state.playerPosition;
    state.cameraPosition = point(session.PointAt(restore.camera.x * 2, restore.camera.y * 2));
    const auto bike = Game::Get().SurfaceBike();
    state.bikePosition =
        bike && world::SurfaceGeometry::Distance(session.Planet().radiusMeters, *bike, frame.origin) < 1000
            ? point(*bike)
            : Vector2{1000000, 1000000};
    state.facingYaw = yaw(restore.facing, session.Origin(), frame);
    state.bikeYaw = yaw(restore.bikeYaw, session.Origin(), frame);
    state.bikeVelocity = vector(restore.bikeVelocity, session.Origin(), frame);
    state.playerMotion.velocity = vector(restore.velocity, session.Origin(), frame);
    impact.velocity = vector(restore.impulse, session.Origin(), frame);
    // City roads are at the core elevation, not at the current floating origin's elevation.
    const auto core =
        session.Stream().Snapshot()->planet->cityCores->Find(Overworld::Cities()[size_t(city)].id);
    state.bikeElevation =
        core ? restore.bikeElevation +
                   float((session.Origin().origin.altitudeMeters - core->elevationMeters) / 2)
             : 0;
    operation();
    if (playerPresent)
    {
        auto delta = vector(Sub(state.playerPosition, entry), frame, session.Origin());
        restore.player = Add(restore.player, delta);
        if (state.isBikeMounted)
        {
            restore.bike = restore.player;
        }
        restore.bikeVelocity = vector(state.bikeVelocity, frame, session.Origin());
        restore.velocity = vector(state.playerMotion.velocity, frame, session.Origin());
        restore.impulse = vector(impact.velocity, frame, session.Origin());
        restore.facing = yaw(state.facingYaw, frame, session.Origin());
        restore.bikeYaw = yaw(state.bikeYaw, frame, session.Origin());
    }
}
InteractionTarget PlanetCities::Nearby(const GameState &state, const world::SurfaceSession &session,
                                       float *nearestDistance) const
{
    InteractionTarget result = InteractionTarget::None;
    float best = 1.35f;
    auto consider = [&](Vector2 point, InteractionTarget target) {
        if (auto local = Local(session, 0, point))
        {
            if (auto distance = Game::Distance(*local, state.playerPosition); distance < best)
            {
                best = distance;
                result = target;
            }
        }
    };
    consider(kHomeDoor, InteractionTarget::HomeEntrance);
    if ((!Game::Get().SurfaceParty() || !Game::Get().SurfaceParty()->Joined(CompanionId::Ria)) &&
        !CivicCombat::LandmarkDown(kLiaPosition))
    {
        consider(kLiaPosition, InteractionTarget::Lia);
    }
    if (!CivicCombat::LandmarkDown(kMaraPosition))
    {
        consider(kMaraPosition, InteractionTarget::Mara);
    }
    if (!Game::Get().SurfaceParty() && game::GameLevels::Chapter().GetState().kainJoined &&
        !CivicCombat::LandmarkDown(kCityKainPosition))
    {
        consider(kCityKainPosition, InteractionTarget::Kain);
    }
    consider(kRelayPosition, InteractionTarget::PowerRelay);
    consider(kBeaconPosition, InteractionTarget::GateBeacon);
    consider(kCityReceiverPosition, InteractionTarget::CityReceiver);
    for (size_t i = 0; i < state.residents.size(); ++i)
    {
        const auto &r = state.residents[i];
        if (r.health <= 0 || r.fearSeconds > 0 ||
            (i == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined))
        {
            continue;
        }
        if (auto local = Local(session, r.cityIndex, r.position))
        {
            if (auto distance = Game::Distance(*local, state.playerPosition); distance < best)
            {
                best = distance;
                result = InteractionTarget::Resident;
            }
        }
    }
    if (nearestDistance)
    {
        *nearestDistance = result == InteractionTarget::None ? 1000000.f : best;
    }
    return result;
}
bool PlanetCities::Occupied(const GameState &state, const world::SurfaceSession &session, Vector2 local,
                            int cityOverride) const
{
    int city = cityOverride >= 0                             ? cityOverride
               : state.isInsideHome || state.isInObservatory ? 0
                                                             : activeCity;
    return OccupiedAt(state, session, session.PointAt(local.x * 2, local.y * 2), city);
}
bool PlanetCities::OccupiedAt(const GameState &state, const world::SurfaceSession &session,
                              const world::SurfacePoint &point, int cityOverride) const
{
    int city = cityOverride;
    if (city < 0 && session.Ready())
    {
        for (int i = 0; i < Overworld::kCityCount; ++i)
        {
            if (world::SurfaceGeometry::Distance(session.Planet().radiusMeters, point,
                                                 session.Stream()
                                                     .Snapshot()
                                                     ->planet->Region(Overworld::Cities()[size_t(i)].id)
                                                     .center.position) < 300)
            {
                city = i;
                break;
            }
        }
    }
    if (city < 0 || city >= Overworld::kCityCount || !session.Ready())
    {
        return false;
    }
    auto plan = session.Stream().Snapshot()->planet;
    const auto &region = plan->Region(Overworld::Cities()[size_t(city)].id);
    if (world::SurfaceGeometry::Distance(session.Planet().radiusMeters, point, region.center.position) > 300)
    {
        return false;
    }
    auto relative = world::SurfaceGeometry::ToLocal(region.frame, point, session.Planet().radiusMeters);
    auto center = Overworld::Cities()[size_t(city)].center;
    Vector2 query{center.x + float(relative.x / 2), center.y + float(relative.y / 2)};
    for (size_t i = 0; i < state.residents.size(); ++i)
    {
        const auto &r = state.residents[i];
        if (r.cityIndex != city || r.health <= 0 ||
            (i == Overworld::kKainResidentSlot && game::GameLevels::Chapter().GetState().kainJoined))
        {
            continue;
        }
        if (Game::Distance(query, r.position) < .36f)
        {
            return true;
        }
    }
    if (city == 0)
    {
        for (auto point : {kLiaPosition, kMaraPosition, kCityKainPosition})
        {
            if (Game::Get().SurfaceParty() &&
                ((point.x == kLiaPosition.x && point.y == kLiaPosition.y &&
                  Game::Get().SurfaceParty()->Joined(CompanionId::Ria)) ||
                 (point.x == kCityKainPosition.x && point.y == kCityKainPosition.y)))
            {
                continue;
            }
            if (point.x == kCityKainPosition.x && point.y == kCityKainPosition.y &&
                !game::GameLevels::Chapter().GetState().kainJoined)
            {
                continue;
            }
            if (!CivicCombat::LandmarkDown(point) && Game::Distance(query, point) < .36f)
            {
                return true;
            }
        }
    }
    for (const auto &car : traffic[size_t(city)].Vehicles())
    {
        if (Game::Distance(query, car.position) > 5)
        {
            continue;
        }
        const auto &mesh = models::ResourceAssets::Get(
            models::ModelLibrary::Get().vehicles[size_t(car.id % 4)].body.resourceId);
        auto delta = Sub(query, car.position);
        float c = std::cos(car.yaw), s = std::sin(car.yaw);
        if (std::fabs(delta.x * c + delta.y * s) < mesh.physicalSize.x * .5f + .16f &&
            std::fabs(-delta.x * s + delta.y * c) < mesh.physicalSize.y * .5f + .16f)
        {
            return true;
        }
    }
    return false;
}
} // namespace tutorial
