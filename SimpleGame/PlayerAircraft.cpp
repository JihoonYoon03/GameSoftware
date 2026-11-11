#include "stdafx.h"
#include "PlayerAircraft.h"
#include "Game.h"
#include "Overworld.h"
#include "Civic.h"
#include "Level.h"
#include "ResourceAssets.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>

namespace tutorial
{
namespace
{
using G = world::SurfaceGeometry;
const models::ResourceMesh &Hull()
{
    return models::ResourceAssets::Get(
        models::ModelLibrary::Aircraft(models::AircraftType::Courier).resourceId);
}
double Approach(double current, double target, double change)
{
    return current + std::clamp(target - current, -change, change);
}
world::SurfacePoint HullSample(const world::SurfaceSession &s, const world::SurfacePoint &p, unsigned i)
{
    const double angle = i * G::Pi / 4;
    const double r = i == 8 ? 0 : PlayerAircraft::RadiusMeters();
    return G::FromLocal(G::Frame(p), {r * std::cos(angle), r * std::sin(angle), 0}, s.Planet().radiusMeters);
}
} // namespace
double PlayerAircraft::RadiusMeters()
{
    const auto &h = Hull();
    return 2 * std::hypot((std::max)(std::abs(h.minimum.x), std::abs(h.maximum.x)),
                          (std::max)(std::abs(h.minimum.y), std::abs(h.maximum.y)));
}
double PlayerAircraft::HeightMeters()
{
    return Hull().maximum.z * 2;
}
void PlayerAircraft::Stop()
{
    velocity = {};
    bank = 0;
    landingRequested = false;
    landingAvailable = false;
    landingCheckSeconds = 0;
}
void PlayerAircraft::SetYaw(float yaw, const world::TangentFrame &origin)
{
    if (position)
    {
        const auto direction =
            G::TransformDirection({std::cos(yaw), std::sin(yaw), 0}, origin, G::Frame(*position));
        const auto frame = G::Frame(*position);
        heading = G::Unit({frame.right.x * direction.x + frame.forward.x * direction.y,
                           frame.right.y * direction.x + frame.forward.y * direction.y,
                           frame.right.z * direction.x + frame.forward.z * direction.y});
    }
}
float PlayerAircraft::Yaw(const world::TangentFrame &frame) const
{
    return float(std::atan2(G::Dot(heading, frame.forward), G::Dot(heading, frame.right)));
}
bool PlayerAircraft::Contains(const world::SurfaceSession &session, const world::SurfacePoint &point,
                              double padding) const
{
    if (!position ||
        G::Distance(session.Planet().radiusMeters, *position, point) > RadiusMeters() + padding + 1)
    {
        return false;
    }
    auto frame = G::Frame(*position);
    const auto local = G::ToLocal(frame, point, session.Planet().radiusMeters);
    const float yaw = Yaw(frame);
    const double x = local.x * std::cos(yaw) + local.y * std::sin(yaw);
    const double y = -local.x * std::sin(yaw) + local.y * std::cos(yaw);
    const auto &h = Hull();
    const double z = point.altitudeMeters - position->altitudeMeters;
    return x >= h.minimum.x * 2 - padding && x <= h.maximum.x * 2 + padding &&
           y >= h.minimum.y * 2 - padding && y <= h.maximum.y * 2 + padding &&
           z >= h.minimum.z * 2 - padding && z <= h.maximum.z * 2 + padding;
}
bool PlayerAircraftController::Occupied(const Game &g, const world::SurfacePoint &point)
{
    profiling::Count(profiling::Counter::AircraftHullQueries);
    const auto &s = *g.surface;
    // Air traffic and ground vehicles use their real height; no infinite vertical collision columns.
    if (g.surfaceTraffic)
    {
        for (const auto &actor : g.surfaceTraffic->State().actors)
        {
            if (!actor.collisionReady && !actor.airborne)
            {
                continue;
            }
            const auto &model =
                actor.airborne
                    ? models::ModelLibrary::Aircraft(
                          models::AircraftType((actor.id / 2) % size_t(models::AircraftType::Count)),
                          actor.id % 2 != 0)
                    : models::ModelLibrary::Get().vehicles[size_t(actor.id % 4)].body;
            const auto &mesh = models::ResourceAssets::Get(model.resourceId);
            const double base = actor.position.altitudeMeters + mesh.minimum.z * 2;
            const double height = (mesh.maximum.z - mesh.minimum.z) * 2;
            const double radius =
                2 * std::hypot((std::max)(std::abs(mesh.minimum.x), std::abs(mesh.maximum.x)),
                               (std::max)(std::abs(mesh.minimum.y), std::abs(mesh.maximum.y)));
            if (point.altitudeMeters < base + height &&
                point.altitudeMeters + PlayerAircraft::HeightMeters() > base &&
                G::Distance(s.Planet().radiusMeters, actor.position, point) <
                    PlayerAircraft::RadiusMeters() + radius)
            {
                return true;
            }
        }
    }
    const double ground = s.Stream().Contact(point).heightMeters;
    if (point.altitudeMeters < ground + 3)
    {
        const double clearance = PlayerAircraft::RadiusMeters() + .4;
        if ((g.surfaceParty && g.surfaceParty->Blocked(point, clearance)) ||
            (g.surfaceCommunity && g.surfaceCommunity->Blocked(point, clearance)) ||
            PlanetSecurity::OfficerBlocked(point, clearance))
        {
            return true;
        }
        if (g.surfaceCities)
        {
            for (const auto &resident : g.state.residents)
            {
                const auto p = g.surfaceCities->Point(s, resident.cityIndex, resident.position);
                if (resident.health > 0 && p && G::Distance(s.Planet().radiusMeters, *p, point) < clearance)
                {
                    return true;
                }
            }
        }
    }
    for (unsigned i = 0; i <= 8; ++i)
    {
        auto p = HullSample(s, point, i);
        const double ground = s.Stream().Contact(p).heightMeters;
        if (point.altitudeMeters > ground + 3)
        {
            continue;
        }
        p.altitudeMeters = ground;
        if (!g.SurfaceLandingClear(p, -1, false, true) || PlanetSecurity::OfficerBlocked(p, .4))
        {
            return true;
        }
    }
    return false;
}
std::optional<double> PlayerAircraftController::LandingHeight(const Game &g, const world::SurfacePoint &point)
{
    double low = 1e30, high = -1e30;
    for (unsigned i = 0; i <= 8; ++i)
    {
        auto p = HullSample(*g.surface, point, i);
        const auto contact = g.surface->Stream().Contact(p);
        p.altitudeMeters = contact.heightMeters;
        if (!contact.land || contact.slope > .15 || !g.SurfaceLandingClear(p, -1, false, true))
        {
            return std::nullopt;
        }
        low = (std::min)(low, contact.heightMeters);
        high = (std::max)(high, contact.heightMeters);
    }
    auto p = point;
    p.altitudeMeters = high + .08;
    if (high - low > .35 ||
        !g.surface->AirClear(p, PlayerAircraft::RadiusMeters(), PlayerAircraft::HeightMeters()) ||
        Occupied(g, p))
    {
        return std::nullopt;
    }
    return high + .08;
}
bool PlayerAircraftController::Nearby(const Game &g)
{
    const auto &a = g.state.aircraft;
    return g.surface && g.surface->Ready() && a.position && a.health > 0 && a.grounded &&
           std::abs(a.position->altitudeMeters - g.surface->Pose().origin.altitudeMeters) < 1.3 &&
           G::Distance(g.surface->Planet().radiusMeters, *a.position, g.surface->Pose().origin) <
               PlayerAircraft::RadiusMeters() + 1.5;
}
bool PlayerAircraftController::CanLand(const Game &g)
{
    const auto &a = g.state.aircraft;
    if (!g.surface || !g.surface->Ready() || !a.position || !a.mounted || a.grounded || a.health <= 0 ||
        std::hypot(a.velocity.x, a.velocity.y) > 2)
    {
        return false;
    }
    const auto landing = LandingHeight(g, *a.position);
    if (!landing)
    {
        return false;
    }
    // Cover the entire descent with overlapping vertical hulls so bridges and overhangs
    // cannot offer a landing prompt for ground that the craft cannot reach.
    const double height = PlayerAircraft::HeightMeters();
    const double step = (std::max)(.1, 16 - height);
    for (double altitude = *landing; altitude <= a.position->altitudeMeters; altitude += step)
    {
        auto base = *a.position;
        base.altitudeMeters = altitude;
        if (!g.surface->AirClear(base, PlayerAircraft::RadiusMeters(),
                                 (std::min)(16., a.position->altitudeMeters - altitude + height)))
        {
            return false;
        }
    }
    return true;
}
bool PlayerAircraftController::Exit(Game &g)
{
    auto &a = g.state.aircraft;
    if (!a.position || !a.grounded)
    {
        return false;
    }
    for (unsigned i = 0; i < 8; ++i)
    {
        auto p = G::FromLocal(G::Frame(*a.position),
                              {(PlayerAircraft::RadiusMeters() + .8) * std::cos(i * G::Pi / 4),
                               (PlayerAircraft::RadiusMeters() + .8) * std::sin(i * G::Pi / 4), 0},
                              g.surface->Planet().radiusMeters);
        p.altitudeMeters = g.surface->Stream().Contact(p).heightMeters;
        if (std::abs(p.altitudeMeters - a.position->altitudeMeters) > .7 || !g.SurfaceLandingClear(p, -1) ||
            PlanetSecurity::OfficerBlocked(p, .32))
        {
            continue;
        }
        const auto local = g.surface->Local(p);
        const auto center = g.surface->Local(*a.position);
        bool clear = true;
        for (unsigned step = 1; step <= 16; ++step)
        {
            const double t = double(step) / 16;
            auto probe =
                g.surface->PointAt(center.x + (local.x - center.x) * t, center.y + (local.y - center.y) * t);
            clear = clear && g.SurfaceLandingClear(probe, -1, false, true) &&
                    !PlanetSecurity::OfficerBlocked(probe, .32);
        }
        if (!clear || !g.surface->RestorePose(G::Frame(p), g.surface->Origin(), false))
        {
            continue;
        }
        a.mounted = false;
        g.state.StopMotion();
        g.state.playerPosition = {float(local.x / 2), float(local.y / 2)};
        profiling::Count(profiling::Counter::AircraftExits);
        return true;
    }
    return false;
}
void PlayerAircraftController::Interact(Game &g)
{
    auto &a = g.state.aircraft;
    if (a.mounted)
    {
        if (!a.grounded)
        {
            if (a.landingRequested)
            {
                a.landingRequested = false;
                g.ShowNotice("착륙을 취소했습니다.");
            }
            else if (CanLand(g))
            {
                a.landingRequested = true;
                g.ShowNotice("착륙합니다. E로 취소할 수 있습니다.");
            }
            a.landingAvailable = false;
            a.landingCheckSeconds = 0;
        }
        else if (!Exit(g))
        {
            g.ShowNotice("기체 주변의 하차 공간이 막혀 있습니다.");
        }
        return;
    }
    if (g.state.Mounted() || !Nearby(g))
    {
        return;
    }
    // Validate every part of the approach; native hull entry itself is intentional.
    const auto start = g.surface->Pose().origin;
    const auto offset = g.surface->Local(*a.position);
    const auto from = g.surface->Local(start);
    for (unsigned i = 1; i <= 16; ++i)
    {
        const double t = double(i) / 16;
        auto p = g.surface->PointAt(from.x + (offset.x - from.x) * t, from.y + (offset.y - from.y) * t);
        if (!g.SurfaceLandingClear(p, -1, false, true))
        {
            g.ShowNotice("기체로 접근하는 통로가 막혀 있습니다.");
            return;
        }
    }
    if (!g.surface->RestorePose(G::Frame(*a.position), g.surface->Origin(), false))
    {
        return;
    }
    g.state.StopMotion();
    a.mounted = true;
    g.state.facingYaw = a.Yaw(g.surface->Origin());
    g.state.playerPosition = {float(offset.x / 2), float(offset.y / 2)};
    g.state.mouseAiming = g.state.mouseFiring = false;
    profiling::Count(profiling::Counter::AircraftBoardings);
    g.ShowNotice("공용 비행체 탑승 · Space 상승 · C 하강 · W/S 이동 · A/D 조향 · X 제동 · 착륙 가능 시 E");
}
void PlayerAircraftController::Drive(Game &g, float dt)
{
    profiling::Scope timer(profiling::Timer::PlayerAircraft);
    auto &a = g.state.aircraft;
    if (!a.mounted || a.health <= 0 || !a.position)
    {
        return;
    }
    const auto &keys = g.state.pressedKeys;
    if (a.grounded)
    {
        if (!keys[' '])
        {
            return;
        }
        a.grounded = false;
    }
    const double steering = double(keys['d']) - double(keys['a']);
    const float yaw = a.Yaw(g.surface->Origin()) + float(steering * 1.25 * dt);
    auto landing = (a.landingRequested || keys['c']) ? LandingHeight(g, *a.position) : std::nullopt;
    if (a.landingRequested && !landing)
    {
        a.landingRequested = false;
        a.landingAvailable = false;
        a.velocity.z = 0;
    }
    const double target = a.landingRequested || keys['x'] || keys['w'] == keys['s'] ? 0 : keys['w'] ? 36 : -8;
    const double along = Approach(a.velocity.x * std::cos(yaw) + a.velocity.y * std::sin(yaw), target,
                                  (keys['x'] || a.landingRequested ? 24 : 12) * dt);
    if (std::abs(along) > 2)
    {
        a.landingAvailable = false;
    }
    a.velocity.x = along * std::cos(yaw);
    a.velocity.y = along * std::sin(yaw);
    double vertical = (double(keys[' ']) - double(keys['c'])) * 9;
    if (a.landingRequested)
    {
        vertical = landing && std::abs(along) < .5 ? -5 : 0;
    }
    if (a.position->altitudeMeters - g.surface->Stream().Contact(*a.position).heightMeters >=
            PlayerAircraft::MaxHeightMeters &&
        vertical > 0)
    {
        vertical = 0;
    }
    a.velocity.z = Approach(a.velocity.z, vertical, 18 * dt);
    if (landing && a.velocity.z < 0)
    {
        a.velocity.z =
            (std::max)(a.velocity.z, (*landing - a.position->altitudeMeters) / (std::max)(.001f, dt));
    }
    if (!g.surface->Fly({a.velocity.x * dt, a.velocity.y * dt, a.velocity.z * dt},
                        PlayerAircraft::RadiusMeters(), PlayerAircraft::HeightMeters(),
                        [&](const auto &p) { return Occupied(g, p); }))
    {
        if (a.landingRequested)
        {
            a.landingRequested = false;
            a.landingAvailable = false;
            g.ShowNotice("착륙 경로가 막혀 착륙을 중단했습니다.");
        }
        const double speed = std::sqrt(G::Dot(a.velocity, a.velocity));
        if (speed > 6)
        {
            a.health = (std::max)(0, a.health - int((speed - 6) * 2));
        }
        a.velocity = {};
        profiling::Count(profiling::Counter::AircraftContacts);
    }
    a.position = g.surface->Pose().origin;
    a.SetYaw(yaw, g.surface->Origin());
    a.bank += (float(-steering * std::min(1., std::abs(along) / 20) * .2) - a.bank) * (1 - std::exp(-6 * dt));
    g.state.facingYaw = yaw;
    g.state.playerMotion.mobility.Update(dt, {}, yaw, false, false, false, false);
    if (landing && std::abs(along) < .5 && (landing = LandingHeight(g, *a.position)) &&
        std::abs(a.position->altitudeMeters - *landing) < .15)
    {
        a.grounded = true;
        a.Stop();
        profiling::Count(profiling::Counter::AircraftLandings);
        g.ShowNotice("착륙했습니다. E로 하차하거나 Space로 이륙하세요.");
    }
}
void PlayerAircraftController::Update(Game &g, float dt)
{
    profiling::Scope timer(profiling::Timer::PlayerAircraft);
    auto &a = g.state.aircraft;
    if (a.position)
    {
        if (a.health > 0)
        {
            a.landingCheckSeconds -= dt;
            if (!a.mounted || a.grounded || std::hypot(a.velocity.x, a.velocity.y) > 2)
            {
                a.landingAvailable = false;
                a.landingCheckSeconds = 0;
            }
            else if (a.landingCheckSeconds <= 0)
            {
                a.landingAvailable = CanLand(g);
                a.landingCheckSeconds = .2f;
            }
            return;
        }
        a.Stop();
        if (!a.grounded)
        {
            a.fallSpeed += 12 * dt;
            const double drop = a.fallSpeed * dt;
            auto next = *a.position;
            next.altitudeMeters -= drop;
            if (a.mounted)
            {
                a.grounded = !g.surface->Fly({0, 0, -drop}, PlayerAircraft::RadiusMeters(),
                                             PlayerAircraft::HeightMeters());
                a.position = g.surface->Pose().origin;
            }
            else
            {
                const unsigned steps = (std::max)(1u, unsigned(std::ceil(drop / .4)));
                for (unsigned i = 0; i < steps; ++i)
                {
                    next = *a.position;
                    next.altitudeMeters -= drop / steps;
                    if (!g.surface->AirClear(next, PlayerAircraft::RadiusMeters(),
                                             PlayerAircraft::HeightMeters()))
                    {
                        a.grounded = true;
                        break;
                    }
                    a.position = next;
                }
            }
            if (!a.grounded)
            {
                return;
            }
            profiling::Count(profiling::Counter::AircraftCrashes);
        }
        if (a.mounted)
        {
            if (!Exit(g))
            {
                a.mounted = false;
                game::GameLevels::Mine().GetState().player.health = 0;
                HospitalRecovery::Begin();
            }
            return;
        }
        a.wreckSeconds += dt;
        const bool hidden =
            G::Distance(g.surface->Planet().radiusMeters, *a.position, g.surface->Pose().origin) > 300;
        if (VehicleWreck::ShouldRetire(a.wreckSeconds, hidden, a.grounded, VehicleWreck::WorldCleanupSeconds))
        {
            a = {};
            a.spawnRetry = 3;
        }
        return;
    }
    if (g.state.isInsideHome || g.state.isInObservatory || g.IsSurfaceMine() || (a.spawnRetry -= dt) > 0)
    {
        return;
    }
    a.spawnRetry = 1;
    const auto plan = g.surface->Stream().Snapshot()->planet;
    const auto &region = plan->Region(g.IsStoryWorld() ? "halo_07" : "landing_zone");
    const auto anchor = g.IsStoryWorld() && g.surfaceCities
                            ? g.surfaceCities->Point(*g.surface, 0, kHomeDoor)
                            : std::optional<world::SurfacePoint>(region.access.position);
    if (!anchor || G::Distance(g.surface->Planet().radiusMeters, *anchor, g.surface->Pose().origin) > 100)
    {
        return;
    }
    for (double distance : {12., 20., 28., 36., 44., 52., 60.})
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            auto candidate =
                G::FromLocal(G::Frame(*anchor),
                             {distance * std::cos(i * G::Pi / 4), distance * std::sin(i * G::Pi / 4), 0},
                             g.surface->Planet().radiusMeters);
            if (G::Distance(g.surface->Planet().radiusMeters, candidate, g.surface->Pose().origin) <
                PlayerAircraft::RadiusMeters() + 1)
            {
                continue;
            }
            if (auto height = LandingHeight(g, candidate))
            {
                candidate.altitudeMeters = *height;
                a.position = candidate;
                a.SetYaw(0, g.surface->Origin());
                return;
            }
        }
    }
}
} // namespace tutorial
