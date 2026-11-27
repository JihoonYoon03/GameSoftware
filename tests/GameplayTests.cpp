#include "stdafx.h"
#include "Level.h"
#include "Story.h"
#include "StoryArc.h"
#include "StoryJournal.h"
#include "WorldEncounters.h"
#include "FieldMinigame.h"
#include "Hud.h"
#include "Civic.h"
#include "CityFacilities.h"
#include "FacilityApproaches.h"
#include "CityInfrastructure.h"
#include "Drawing.h"
#include "Monitoring/LocalMonitorServer.h"
#include "Monitoring/Monitor.h"
#include "Overworld.h"
#include "Profiler.h"
#include "ResidentAI.h"
#include "Scene.h"
#include "Save.h"
#include "ResourceAssets.h"
#include "TransitLayout.h"
#include "RenderAssets.h"
#include <algorithm>
#include <set>
#include <cassert>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <stdexcept>

// Link the production core; tests exercise CPU rules without creating a window or GL context.
namespace
{
using namespace game;
MineState &mineState = GameLevels::Mine().GetState();
using tutorial::Vector2;
void ReadDialogue();
void WaitForCheckpointLoad();
tutorial::PlanetTrafficActor TrafficFixtureActor(const tutorial::PlanetTraffic &traffic,
                                                 const world::GeneratedPlanet &plan,
                                                 const tutorial::PlanetTrafficRoute &route, double progress,
                                                 std::uint64_t id = 1)
{
    tutorial::PlanetTrafficActor actor;
    actor.id = id;
    actor.routeId = route.id;
    actor.airborne = route.airborne;
    actor.collisionReady = !actor.airborne;
    actor.health = actor.airborne ? 180 : 140;
    actor.distanceMeters = progress;
    actor.speedMeters = 12;
    actor.altitudeMeters = actor.airborne ? 40 : 0;
    actor.position = traffic.Position(route, progress, 1);
    actor.position.altitudeMeters =
        (std::max)(world::PlanetTerrain::SeaLevelMeters, plan.terrain.Height(actor.position)) +
        actor.altitudeMeters;
    actor.heading = world::SurfaceGeometry::Frame(actor.position).right;
    return actor;
}
void InstallTrafficFixture(int damage = 0)
{
    auto &g = tutorial::Game::Get();
    auto plan = g.Surface()->Stream().Snapshot()->planet;
    auto traffic = g.SurfaceTraffic();
    assert(traffic && !traffic->Routes().empty());
    const auto route = std::find_if(traffic->Routes().begin(), traffic->Routes().end(),
                                    [](const auto &r) { return r.airborne; });
    assert(route != traffic->Routes().end());
    auto state = traffic->State();
    state.actors = {TrafficFixtureActor(*traffic, *plan, *route, route->lengthMeters * .5)};
    state.nextId = 2;
    state.spawnSeconds = .75f;
    tutorial::PlanetTraffic replacement(state);
    replacement.Bind(plan);
    *traffic = std::move(replacement);
    if (damage)
    {
        assert(traffic->Damage(1, damage));
    }
}
void TestPlanetTraffic()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    auto p = *world::PlanetProfiles::Find("seira");
    const auto center = G::FromDegrees(0, 179.99);
    const auto frame = G::Frame(center);
    // Minimal transit-only manifest isolates spherical route rules from story generation.
    auto plan = std::make_shared<world::GeneratedPlanet>(
        world::PlanetTerrain(p, {{"traffic_pad", center, 12000, 1000, 0}}, center));
    auto a = G::Advance(frame, -5000, 0, p.radiusMeters).origin,
         b = G::Advance(frame, 5000, 0, p.radiusMeters).origin,
         c = G::Advance(frame, 5000, 5000, p.radiusMeters).origin;
    auto location = [&](const char *id, world::SurfacePoint point) {
        world::WorldLocation l;
        l.planetId = p.id;
        l.planetSeed = p.seed;
        l.generatorVersion = p.generatorVersion;
        l.regionId = id;
        l.position = point;
        return l;
    };
    plan->nodes = {{"a", "a", location("a", a), false},
                   {"b", "b", location("b", b), false},
                   {"c", "c", location("c", c), false}};
    plan->connections = {{0, 1, world::RouteMode::Road, {a, b}, G::Distance(p.radiusMeters, a, b)},
                         {1, 2, world::RouteMode::Road, {b, c}, G::Distance(p.radiusMeters, b, c)}};
    PlanetTraffic prototype;
    prototype.Bind(plan);
    assert(prototype.Routes().size() == 4);
    const auto &road = prototype.Routes().front(), &air = prototype.Routes()[1];
    assert(!road.airborne && air.airborne && air.id != road.id);
    auto right = prototype.Position(road, road.lengthMeters * .5, 1),
         left = prototype.Position(road, road.lengthMeters * .5, -1);
    assert(std::fabs(G::Distance(p.radiusMeters, right, left) - 3) < .001);
    assert(G::ToLocal(frame, right, p.radiusMeters).y > 1.4 &&
           G::ToLocal(frame, left, p.radiusMeters).y < -1.4);
    assert(G::Distance(p.radiusMeters, prototype.Position(air, air.lengthMeters * .5, 1), center) > 50);
    assert(G::Distance(p.radiusMeters, prototype.Position(air, air.lengthMeters + 20, 1),
                       prototype.Position(air, air.lengthMeters + 20 + G::Circumference(p.radiusMeters), 1)) <
           .001);
    assert(std::fabs(G::ToDegrees(right)[1]) > 179.9);
    auto rejected = [](const auto &operation) {
        bool caught = false;
        try
        {
            operation();
        }
        catch (const std::exception &)
        {
            caught = true;
        }
        assert(caught);
    };
    auto record = prototype.State();
    record.nextId = 2;
    record.spawnSeconds = 1;
    record.actors = {TrafficFixtureActor(prototype, *plan, road, road.lengthMeters * .5)};
    PlanetTraffic::Validate(record, p, plan->Digest());
    auto legacyLanes = record;
    legacyLanes.actors.front().position = left; // The former forward lane was the new reverse lane.
    legacyLanes.actors.front().position.altitudeMeters = plan->terrain.Height(left);
    legacyLanes.actors.front().health = 73;
    auto reverseActor = legacyLanes.actors.front();
    reverseActor.id = 2;
    reverseActor.direction = -1;
    reverseActor.position = right;
    reverseActor.position.altitudeMeters = plan->terrain.Height(right);
    legacyLanes.actors.push_back(reverseActor);
    legacyLanes.nextId = 3;
    PlanetTraffic::MigrateLanes(legacyLanes, plan);
    assert(legacyLanes.actors.front().id == 1 && legacyLanes.actors.front().health == 73 &&
           G::Distance(p.radiusMeters, legacyLanes.actors.front().position, right) < .01);
    assert(legacyLanes.actors.back().id == 2 && legacyLanes.actors.back().direction == -1 &&
           G::Distance(p.radiusMeters, legacyLanes.actors.back().position, left) < .01);
    PlanetTraffic::MigrateLanes(legacyLanes, plan);
    assert(G::Distance(p.radiusMeters, legacyLanes.actors.front().position, right) < .01);
    assert(RoadNetwork::RightOf({1, 0}).y > 0); // Authored and spherical road frames must agree.

    auto bad = record;
    bad.actors.push_back(bad.actors.front());
    rejected([&] { PlanetTraffic::Validate(bad, p, plan->Digest()); });
    bad = record;
    bad.worldKey += "wrong";
    rejected([&] { PlanetTraffic::Validate(bad, p, plan->Digest()); });
    bad = record;
    bad.actors.front().routeId = "unknown";
    rejected([&] {
        PlanetTraffic candidate(bad);
        candidate.Bind(plan);
    });
    bad = record;
    bad.actors.front().position =
        G::Advance(G::Frame(bad.actors.front().position), 30, 0, p.radiusMeters).origin;
    rejected([&] {
        PlanetTraffic candidate(bad);
        candidate.Bind(plan);
    });
    world::SurfaceSession session(p, center);
    session.Start(plan);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!session.Ready() && std::chrono::steady_clock::now() < deadline)
    {
        session.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(session.Ready());
    GameState observation;
    observation.isInsideHome = false;
    auto visibleAir = TrafficFixtureActor(prototype, *plan, air, air.lengthMeters * .5);
    auto base = visibleAir.position;
    base.altitudeMeters -= visibleAir.altitudeMeters;
    auto anchor = session.Local(base);
    GameState elevated = observation;
    elevated.playerPosition =
        elevated.cameraPosition = {float(anchor.x / 2 + .38 * visibleAir.altitudeMeters / 2),
                                   float(anchor.y / 2 + .55 * visibleAir.altitudeMeters / 2)};
    const bool hidden = prototype.Outside(visibleAir, elevated, session, 0);
    assert(!hidden);
    visibleAir.position.altitudeMeters += 20;
    elevated.surfaceCameraHeight = 10;
    assert(prototype.Outside(visibleAir, elevated, session, 0) ==
           hidden); // Raising ground and camera preserves the projected hull/shadow.

    PlanetTraffic moving(record);
    moving.Bind(plan);
    const auto initial = moving.State().actors.front().distanceMeters;
    for (int i = 0; i < 9; ++i)
    {
        moving.Update(observation, session, .05f, false);
    }
    assert(moving.State().actors.front().distanceMeters == initial);
    moving.Update(observation, session, .05f, false);
    assert(moving.State().actors.front().distanceMeters > initial + 1 &&
           moving.State().actors.front().distanceMeters < initial + 20);
    auto paused = moving.State();
    observation.isPaused = true;
    for (int i = 0; i < 20; ++i)
    {
        moving.Update(observation, session, .05f, false);
    }
    assert(moving.State().actors.front().distanceMeters == paused.actors.front().distanceMeters &&
           moving.State().actors.front().farSeconds == paused.actors.front().farSeconds);
    observation.isPaused = false;
    record.actors = {TrafficFixtureActor(prototype, *plan, road, 3000)};
    PlanetTraffic retained(record);
    retained.Bind(plan);
    retained.Update(observation, session, .05f, false);
    assert(retained.State().actors.size() == 1); // Offscreen traffic is retained for kilometres.
    record.actors = {TrafficFixtureActor(prototype, *plan, road, 500)};
    PlanetTraffic distant(record);
    distant.Bind(plan);
    const auto local = session.Local(record.actors.front().position);
    observation.cameraPosition = {float(local.x / 2), float(local.y / 2)};
    observation.surfaceCameraHeight = float(local.z / 2);
    assert(!distant.Outside(record.actors.front(), observation, session, 80));
    const auto visibleProgress = distant.State().actors.front().distanceMeters;
    distant.Update(observation, session, .05f, true);
    assert(distant.State().actors.size() == 1 &&
           distant.State().actors.front().distanceMeters > visibleProgress);
    // Hidden actors remain real and advance at 2Hz, even while the exterior is active.
    record.actors = {TrafficFixtureActor(prototype, *plan, road, road.lengthMeters * .5 + 500)};
    PlanetTraffic hiddenTraffic(record);
    hiddenTraffic.Bind(plan);
    observation.cameraPosition = {};
    observation.surfaceCameraHeight = 0;
    const auto hiddenProgress = hiddenTraffic.State().actors.front().distanceMeters;
    for (int i = 0; i < 9; ++i)
    {
        hiddenTraffic.Update(observation, session, .05f, true);
    }
    assert(hiddenTraffic.State().actors.front().distanceMeters == hiddenProgress);
    hiddenTraffic.Update(observation, session, .05f, true);
    assert(hiddenTraffic.State().actors.front().distanceMeters > hiddenProgress &&
           hiddenTraffic.State().actors.front().id == 1);
    observation.cameraPosition = {};
    distant.Update(observation, session, .05f, false);
    assert(distant.State().actors.empty() && distant.State().nextId == 2);
    record.actors = {TrafficFixtureActor(prototype, *plan, air, air.lengthMeters * .25)};
    PlanetTraffic wreck(record);
    wreck.Bind(plan);
    assert(wreck.Damage(1, 999) && !wreck.Damage(1, 5));
    for (int i = 0; i < 120; ++i)
    {
        wreck.Update(observation, session, .05f, false);
    }
    assert(wreck.State().actors.size() == 1 && wreck.State().actors.front().altitudeMeters == 0 &&
           wreck.State().actors.front().fallSpeed == 0 && wreck.State().actors.front().collisionReady &&
           !wreck.State().actors.front().occupied);
    const auto &hull = models::ResourceAssets::Get(
        models::ModelLibrary::Aircraft(models::AircraftType::Courier, true).resourceId);
    const auto wreckCenter =
        G::Advance(G::Frame(wreck.State().actors.front().position), hull.minimum.x + hull.maximum.x,
                   hull.minimum.y + hull.maximum.y, p.radiusMeters)
            .origin;
    assert(wreck.Blocked(wreckCenter, .32));
    for (int i = 0; i < 300; ++i)
    {
        wreck.Update(observation, session, .05f, false);
    }
    assert(wreck.State().actors.empty());
    PlanetTraffic spawned;
    spawned.Bind(plan);
    spawned.Update(observation, session, .05f, true);
    assert(!spawned.State().actors.empty() &&
           spawned.State().actors.size() <= PlanetTraffic::GroundLimit + PlanetTraffic::AirLimit);
    for (const auto &actor : spawned.State().actors)
    {
        assert(spawned.Outside(actor, observation, session, 320));
    }
    PlanetTraffic::Validate(spawned.State(), p, plan->Digest());
    auto copy = spawned.State();
    PlanetTraffic restored(copy);
    restored.Bind(plan);
    assert(restored.State().worldKey == copy.worldKey && restored.State().nextId == copy.nextId &&
           restored.State().actors.size() == copy.actors.size());
    session.Stop();
}
void TestCitySimulationPolicy()
{
    using namespace tutorial;
    using B = CitySimulationBand;
    assert(CitySimulation::Select(420, B::Dormant, true) == B::Detailed);
    assert(CitySimulation::Select(460, B::Detailed, true) == B::Detailed);
    assert(CitySimulation::Select(460, B::Dormant, true) == B::Retained);
    assert(CitySimulation::Select(501, B::Detailed, true) == B::Retained);
    assert(CitySimulation::Select(901, B::Detailed, true) == B::Dormant);
    assert(CitySimulation::Select(10, B::Detailed, false) == B::Dormant);
    assert(CitySimulation::Select(std::numeric_limits<double>::quiet_NaN(), B::Detailed, true) == B::Dormant);
    Resident resident;
    resident.position = {7, 8};
    resident.route = {{{8, 8}, 3}};
    resident.crossingJunction = 3;
    resident.facing.walking = true;
    resident.waitSeconds = resident.conversationSeconds = resident.fearSeconds = 1;
    resident.impact = {{3, 0}, .6f, .7f, BodyResponse::Knockdown};
    for (int i = 0; i < 30; ++i)
    {
        CitySimulation::AdvanceDormant(resident, .05f);
    }
    assert(resident.health == 100 && resident.position.x == 7 && resident.position.y == 8 &&
           resident.route.size() == 1 && resident.crossingJunction == 3 && resident.waitSeconds == 0 &&
           resident.conversationSeconds == 0 && resident.fearSeconds == 0 && resident.impact.seconds == 0 &&
           resident.impact.invulnerability == 0 && resident.impact.response == BodyResponse::None &&
           !resident.facing.walking);
    resident.health = 0;
    resident.impact.response = BodyResponse::Dead;
    CitySimulation::AdvanceDormant(resident, .05f);
    assert(resident.activity == ResidentActivity::Dead && resident.route.empty() &&
           resident.crossingJunction == -1 && resident.corpseSeconds > 0 &&
           resident.impact.response == BodyResponse::Dead);
    TrafficSystem hidden;
    hidden.SetCityFilter(0);
    auto &cars = const_cast<std::vector<TrafficVehicle> &>(hidden.Vehicles());
    TrafficVehicle living, wreck;
    living.id = 12;
    living.position = {2, 3};
    living.previousPosition = {1, 3};
    living.speed = 4;
    living.reservedFrom = 0;
    living.reservedTo = 1;
    wreck.id = 13;
    wreck.health = 0;
    wreck.wreckSeconds = 11.99f;
    cars = {living, wreck};
    const auto phase = hidden.Signals().front().phase;
    const auto elapsed = hidden.Signals().front().elapsed;
    assert(hidden.AdvanceDormant(.05f) == 1 && hidden.Vehicles().size() == 1);
    assert(cars.front().id == 12 && cars.front().position.x == 2 && cars.front().position.y == 3 &&
           cars.front().previousPosition.x == 2 && cars.front().reservedFrom == 0 &&
           cars.front().reservedTo == 1 && hidden.Signals().front().phase == phase &&
           hidden.Signals().front().elapsed == elapsed);
    hidden.AdvanceDormant(.05f);
    assert(cars.size() == 1); // No remote population/spawn burst and no replay of live movement.
}
void TestPlanetCityRuntime()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(g.EnterTestStage(TestStage::SurfaceSurvey));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!g.Surface()->Ready() && std::chrono::steady_clock::now() < deadline)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(g.Surface()->Ready());
    g.Update(0);
    auto cities = g.SurfaceCities();
    auto plan = g.Surface()->Stream().Snapshot()->planet;
    assert(cities && cities->ActiveCity() == 0 && cities->Status(0).band == CitySimulationBand::Detailed);
    w.bikeVelocity = {1.2f, -.8f};
    w.playerMotion.velocity = {-.3f, .4f};
    w.facingYaw = .31f;
    w.bikeYaw = -1.7f;
    const auto before = w;
    bool rejected = false;
    try
    {
        cities->Observe(w, *g.Surface(), 0, [&] {
            assert(cities->ObservedCity() == 0 && cities->PlayerObserved() &&
                   &Overworld::Traffic() == &cities->Traffic(0));
            assert(SaveSystem::Restriction() != nullptr);
            cities->Observe(w, *g.Surface(), 1, [] {});
        });
    }
    catch (const std::logic_error &)
    {
        rejected = true;
    }
    assert(rejected && !cities->Observing() &&
           Game::Distance(w.playerPosition, before.playerPosition) < 1.e-6 &&
           Game::Distance(w.cameraPosition, before.cameraPosition) < 1.e-6 &&
           Game::Distance(w.bikeVelocity, before.bikeVelocity) < 1.e-6 && w.facingYaw == before.facingYaw &&
           w.bikeYaw == before.bikeYaw);
    cities->Observe(w, *g.Surface(), 1, [&] {
        assert(cities->ObservedCity() == 1 && !cities->PlayerObserved() &&
               &Overworld::Traffic() == &cities->Traffic(1));
    });
    assert(!cities->Observing() && Game::Distance(w.playerPosition, before.playerPosition) < 1.e-6);
    w.StopMotion();
    auto &resident = w.residents.front();
    resident.impact = {{3, 0}, .6f, .7f, BodyResponse::Knockdown};
    resident.fearSeconds = 1;
    const auto residentPosition = resident.position;
    assert(g.RelocateSurfaceSurvey(plan->Region("cinder").access.position));
    g.Update(0);
    const auto oldTicks = cities->Status(0).detailedTicks;
    const auto oldPassive = cities->Status(0).passiveSeconds;
    const float playTime = w.playTimeSeconds;
    w.isPaused = true;
    for (int i = 0; i < 30; ++i)
    {
        g.Update(.05f);
    }
    assert(cities->Status(0).passiveSeconds == oldPassive && resident.impact.seconds == .6f &&
           w.playTimeSeconds == playTime);
    w.isPaused = false;
    for (int i = 0; i < 30; ++i)
    {
        g.Update(.05f);
    }
    assert(cities->Status(0).detailedTicks == oldTicks && cities->Status(0).passiveSeconds > oldPassive + 1 &&
           resident.impact.seconds == 0 && resident.fearSeconds == 0 &&
           Game::Distance(resident.position, residentPosition) < 1.e-6 &&
           cities->RemainingPathBudget() >= 0 && cities->RemainingPathBudget() <= CitySimulation::PathBudget);
    auto snapshot = monitoring::WorldSnapshot::Build(20, "surface-simulation");
    assert(snapshot.find("city_simulation") != std::string::npos &&
           snapshot.find("remaining_path_budget") != std::string::npos);
    assert(g.RelocateSurfaceSurvey(plan->Region("halo_07").access.position));
    g.Update(0);
    auto &cars = const_cast<std::vector<TrafficVehicle> &>(cities->Traffic(0).Vehicles());
    cars.clear();
    const auto playerLegacy = Overworld::Cities()[0].center;
    int lane = -1;
    const auto &net = RoadNetwork::Get();
    for (size_t i = 0; i < net.lanes.size(); ++i)
    {
        if (TrafficSystem::CoreLane(int(i), 0))
        {
            lane = int(i);
            break;
        }
    }
    assert(lane >= 0);
    TrafficVehicle car;
    car.id = 0;
    car.lane = lane;
    car.distance = net.lanes[size_t(lane)].length * .5f;
    car.position = car.previousPosition = playerLegacy;
    car.speed = 5;
    car.yaw = 0;
    cars.push_back(car);
    auto phantom =
        std::find_if(w.residents.begin(), w.residents.end(), [](const auto &p) { return p.cityIndex == 1; });
    assert(phantom != w.residents.end());
    phantom->position = playerLegacy;
    const auto phantomHealth = phantom->health;
    auto &stats = mineState.player;
    stats.health = stats.maxHealth;
    CivicJustice::State().playerGrace = 0;
    CivicJustice::State().playerImpact = {};
    cities->Observe(w, *g.Surface(), 0, [&] { BodyCollision::VehicleContacts(.03f); });
    assert(stats.health < stats.maxHealth && phantom->health == phantomHealth && !cities->Observing());
    const int health = stats.health;
    cities->Observe(w, *g.Surface(), 0, [&] { BodyCollision::VehicleContacts(.03f); });
    assert(stats.health == health); // Same collision window must not drain health each substep.
    cars.clear();
    assert(g.RelocateSurfaceSurvey(plan->Region("halo_07").access.position));
    g.Update(0);
    for (auto &npc : w.residents)
    {
        npc.health = 0;
    }
    const auto pose = g.Surface()->Pose().origin;
    CivicJustice::State().playerImpact = {{3, 0}, .6f, .7f, BodyResponse::Stagger};
    CivicJustice::State().playerGrace = .1f;
    const auto corrections = profiling::ReadCounter(profiling::Counter::CityContactCorrections);
    g.Update(.05f);
    const auto local = g.Surface()->Local(g.Surface()->Pose().origin);
    assert(G::Distance(g.Surface()->Planet().radiusMeters, pose, g.Surface()->Pose().origin) > .05 &&
           Game::Distance(w.playerPosition, {float(local.x / 2), float(local.y / 2)}) < 1.e-5 &&
           profiling::ReadCounter(profiling::Counter::CityContactCorrections) > corrections);
    for (int i = 0; i < 4; ++i)
    {
        g.Update(.05f);
    }
    assert(CivicJustice::State().playerGrace == 0);
    g.Reset();
}
void TestPlanetCityMigration()
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(g.EnterTestStage(TestStage::SurfaceSurvey));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!g.Surface()->Ready() && std::chrono::steady_clock::now() < deadline)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(g.Surface()->Ready() && g.SurfaceCities());
    auto plan = g.Surface()->Stream().Snapshot()->planet;
    assert(plan->cityCores && plan->cityCores->cities.size() == Overworld::kCityCount + 1 &&
           plan->cityCores->Find("frontier"));
    for (const auto &anchor : Overworld::StoryAnchors())
    {
        assert(std::fabs(plan->terrain.Height(anchor.location.position) -
                         anchor.location.position.altitudeMeters) < .001);
    }
    auto bike = g.SurfaceBike();
    auto authoredBike = world::LegacyWorldAdapter::ErmaExterior()
                            .Locate("halo_07", {kJetBikePosition.x, kJetBikePosition.y, 0})
                            .position;
    assert(bike &&
           world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, *bike, authoredBike) < 1.e-5);
    for (int i = 0; i < 120; ++i)
    {
        g.Update(.05f);
    }
    assert(g.SurfaceCities()->ActiveCity() == 0);
    w.residents.front().health = 43;
    auto savedResident = w.residents.front();
    auto savedCars = g.SurfaceCities()->Traffic(0).Vehicles();
    auto savedSignals = g.SurfaceCities()->Traffic(0).Signals();
    auto epoch = g.Surface()->Stream().Snapshot()->statistics.epoch;
    assert(g.RelocateSurfaceSurvey(plan->Region("cinder").access.position));
    for (int i = 0; i < 60; ++i)
    {
        g.Update(.05f);
    }
    assert(g.SurfaceCities()->ActiveCity() == 1 &&
           g.Surface()->Stream().Snapshot()->statistics.epoch == epoch);
    assert(w.residents.front().name == savedResident.name && w.residents.front().health == 43 &&
           Game::Distance(w.residents.front().position, savedResident.position) < 1.e-6);
    assert(g.RelocateSurfaceSurvey(plan->Region("halo_07").access.position));
    g.Update(0);
    assert(g.SurfaceCities()->ActiveCity() == 0);
    const auto &cars = g.SurfaceCities()->Traffic(0).Vehicles();
    assert(cars.size() == savedCars.size());
    for (size_t i = 0; i < cars.size(); ++i)
    {
        assert(cars[i].id == savedCars[i].id && cars[i].health == savedCars[i].health &&
               Game::Distance(cars[i].position, savedCars[i].position) < 1.e-6);
    }
    assert(g.SurfaceCities()->Traffic(0).Signals().front().elapsed == savedSignals.front().elapsed);
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        for (const auto &car : g.SurfaceCities()->Traffic(city).Vehicles())
        {
            assert(TrafficSystem::CoreLane(car.lane, city));
        }
    }
    auto mainStage = w.mainQuestStage;
    auto mineStage = mineState.quest.GetStage();
    auto press = [&](unsigned char key) {
        g.KeyDown(key, 0, 0);
        g.KeyUp(key, 0, 0);
    };
    auto placeAtPortal = [&](Vector2 legacy) {
        auto point = g.SurfaceCities()->Point(*g.Surface(), 0, legacy);
        assert(point);
        assert(g.RelocateSurfaceSurvey(*point));
        // Probe the interaction contract at its exact portal, independently of swept walking contact.
        auto local = g.SurfaceCities()->Local(*g.Surface(), 0, legacy);
        assert(local);
        w.playerPosition = *local;
    };
    placeAtPortal(kHomeDoor);
    auto parked = g.SurfaceBike();
    assert(g.FindNearbyInteraction() == InteractionTarget::HomeEntrance);
    press('e');
    assert(w.isInsideHome && SceneManager::Current() == SceneKind::Interior);
    auto room = g.PlanetLocation();
    assert(room && room->space == world::SpaceKind::Interior);
    room->Validate(g.Surface()->Planet());
    w.playerPosition = {2.4f, 2.4f};
    press('e');
    assert(!w.isInsideHome && g.PlanetLocation()->space == world::SpaceKind::Surface);
    assert(parked && g.SurfaceBike() &&
           world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, *parked, *g.SurfaceBike()) <
               1.e-6);
    auto parkedLocal = g.Surface()->Local(*g.SurfaceBike());
    assert(Game::Distance(w.bikePosition, {float(parkedLocal.x / 2), float(parkedLocal.y / 2)}) < 1.e-5f);
    placeAtPortal(kBeaconPosition);
    press('e');
    assert(w.isInObservatory && SceneManager::Current() == SceneKind::Observatory);
    g.PlanetLocation()->Validate(g.Surface()->Planet());
    w.playerPosition = kObservatoryStairs;
    press('e');
    assert(!w.isInObservatory && !w.isInsideHome && SceneManager::Current() == SceneKind::Planet);
    assert(w.mainQuestStage == mainStage && mineState.quest.GetStage() == mineStage &&
           w.residents.front().health == 43);
    assert(!g.EnterSurfaceMine()); // A remote interaction cannot teleport into a field site.
    auto &chapter = GameLevels::Chapter();
    auto &field = chapter.GetState();
    assert(field.generated && field.survivors.size() == 5 && field.scrappers.size() == 3 && !field.active);
    auto riaHealth = field.ria.combat.health;
    field.ria.combat.health = 0;
    auto fieldEpoch = g.Surface()->Stream().Snapshot()->statistics.epoch;
    assert(g.RelocateSurfaceSurvey(chapter.Surface().Point({-2, 20.5f})));
    auto outside = chapter.Surface().Local(w.playerPosition);
    assert(outside.x < 0 && SceneManager::Current() == SceneKind::Planet);
    g.KeyDown('d', 0, 0);
    g.KeyDown('s', 0, 0);
    for (int i = 0; i < 60; ++i)
    {
        g.Update(.05f);
    }
    g.KeyUp('d', 0, 0);
    g.KeyUp('s', 0, 0);
    w.StopMotion();
    assert(chapter.Surface().Local(w.playerPosition).x > 0 && SceneManager::Current() == SceneKind::Planet);
    assert(g.Surface()->Stream().Snapshot()->statistics.epoch == fieldEpoch && !w.isBikeMounted);
    assert(g.RelocateSurfaceSurvey(chapter.Surface().Point({-2, 20.5f})));
    w.bikeYaw = chapter.Surface().Yaw(0);
    press('e');
    assert(w.isBikeMounted);
    g.KeyDown('w', 0, 0);
    for (int i = 0; i < 300 && chapter.Surface().Local(w.playerPosition).x < 50; ++i)
    {
        g.Update(.05f);
    }
    g.KeyUp('w', 0, 0);
    w.StopMotion();
    assert(w.isBikeMounted && chapter.Surface().Local(w.playerPosition).x > kFrontierWidth &&
           SceneManager::Current() == SceneKind::Planet);
    field.ria.combat.health = riaHealth;
    assert(g.RelocateSurfaceSurvey(chapter.Surface().Point({11.5f, 10.9f})));
    w.isBikeMounted = false;
    assert(g.FindNearbyInteraction() == InteractionTarget::FrontierSite);
    auto globalPosition = w.playerPosition, cursor = w.cursorPosition;
    press('e');
    ReadDialogue();
    assert(field.generatorInspected && Game::Distance(globalPosition, w.playerPosition) < 1.e-5f &&
           Game::Distance(cursor, w.cursorPosition) < 1.e-5f);
    assert(g.RelocateSurfaceSurvey(chapter.Surface().Point({18.5f, 20.5f})));
    globalPosition = w.playerPosition;
    mineState.loadout.Select(combat::WeaponSlot::Secondary);
    w.facingYaw = chapter.Surface().Yaw(0);
    w.mouseAiming = false;
    mineState.pistol.Reset();
    field.bullets.clear();
    chapter.Attack();
    assert(!field.bullets.empty() && field.bullets.back().shot.direction.x > .98f);
    assert(Game::Distance(globalPosition, w.playerPosition) < 1.e-5f);
    field.scrappers.front().combat.health = 37;
    auto savedScrapper = field.scrappers.front().position;
    auto fieldTileCount = field.tiles.size();
    assert(g.RelocateSurfaceSurvey(plan->Region("halo_07").access.position));
    for (int i = 0; i < 10; ++i)
    {
        g.Update(.05f);
    }
    assert(!field.active && field.bullets.empty() && field.generatorInspected &&
           field.scrappers.front().combat.health == 37);
    assert(Game::Distance(savedScrapper, field.scrappers.front().position) < 1.e-6f);
    auto accepted = field.unlocked;
    auto questBefore = mineState.quest;
    field.unlocked = false;
    mineState.quest = MineQuest{};
    assert(g.RelocateSurfaceSurvey(plan->Region("frontier").access.position));
    g.Update(0);
    assert(field.active && !field.unlocked && mineState.quest.GetStage() == MineQuestStage::ShowRecord &&
           !mineState.quest.IsRiaJoined());
    assert(field.tiles.size() == fieldTileCount && field.survivors.size() == 5 &&
           field.scrappers.front().combat.health == 37);
    assert(g.Surface()->Stream().Snapshot()->statistics.epoch == fieldEpoch);
    field.unlocked = accepted;
    mineState.quest = questBefore;
    assert(g.RelocateSurfaceSurvey(plan->Region("halo_07").access.position));
    w.mapVisible = true;
    auto mapEpoch = g.Surface()->Stream().Snapshot()->statistics.epoch;
    Vector2 pivot{530, 430};
    auto target = MapHud::MapToWorld(pivot, true);
    assert(MapHud::Zoom(3, pivot));
    assert(Game::Distance(target, MapHud::MapToWorld(pivot, true)) < .01f);
    assert(MapHud::HandleClick(pivot) && w.planetDestination);
    auto destination = *w.planetDestination;
    MapHud::Pan({90, -45});
    assert(Game::Distance(MapHud::WorldToMap(target, true), pivot) > 20);
    MapHud::ResetView(true);
    assert(w.planetMapPan.x == 0 && w.planetMapPan.y == 0);
    assert(g.Surface()->Stream().Snapshot()->statistics.epoch == mapEpoch);
    w.mapVisible = false;
    assert(g.RelocateSurfaceSurvey(plan->Region("mq00_mine").access.position));
    auto mineReturn = g.Surface()->Pose().origin;
    auto mineBike = *g.SurfaceBike();
    mineState.quest = MineQuest{}; // Geography must work before a story is accepted.
    assert(g.FindNearbyInteraction() == InteractionTarget::MineEntrance);
    press('e');
    assert(g.IsSurfaceMine() && mineState.active && SceneManager::Current() == SceneKind::Mine);
    assert(mineState.quest.GetStage() == MineQuestStage::ShowRecord && !mineState.quest.IsRiaJoined());
    auto underground = g.PlanetLocation();
    assert(underground && underground->regionId == "mq00_mine" &&
           underground->space == world::SpaceKind::Subsurface && underground->layer == -1);
    underground->Validate(g.Surface()->Planet());
    assert(std::fabs(underground->position.altitudeMeters -
                     plan->Region("mq00_mine").center.position.altitudeMeters) < .001);
    auto revision = mineState.mapRevision;
    auto tiles = mineState.tiles;
    mineState.enemies.front().health = 7;
    mineState.survivors.front().combat.health = 61;
    mineState.rescueGroups.front().rescued = true;
    mineState.drops.push_back({{20.5f, 20.5f}, ItemKind::Salvage, 975});
    GameLevels::Mine().Map().Reveal({35.5f, 41.5f});
    auto explored = mineState.explored;
    w.mapVisible = true;
    auto beforeEnemy = mineState.enemies.front().position;
    g.Update(.05f);
    assert(Game::Distance(beforeEnemy, mineState.enemies.front().position) < 1.e-6);
    assert(!g.RelocateSurfaceSurvey(plan->Region("halo_07").access.position));
    w.mapVisible = false;
    w.playerPosition = mineState.entry;
    press('e');
    assert(!g.IsSurfaceMine() && !mineState.active && SceneManager::Current() == SceneKind::Planet);
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, mineReturn,
                                            g.Surface()->Pose().origin) <= 8.01);
    for (int i = 0; i < 20; ++i)
    {
        g.Update(.05f);
    }
    assert(mineState.mapRevision == revision && mineState.enemies.front().health == 7 &&
           mineState.survivors.front().combat.health == 61);
    assert(mineState.explored == explored && mineState.rescueGroups.front().rescued &&
           mineState.drops.back().actorId == 975);
    press('e');
    assert(g.IsSurfaceMine() && mineState.mapRevision == revision && mineState.tiles == tiles &&
           mineState.explored == explored);
    assert(mineState.quest.GetStage() == MineQuestStage::ShowRecord && !mineState.quest.IsRiaJoined());
    assert(w.planetDestination &&
           world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, destination,
                                            *w.planetDestination) < 1.e-6);
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, mineBike, *g.SurfaceBike()) <
           1.e-6);
    mineState.player.health = 0;
    for (int i = 0; i < 60; ++i)
    {
        g.Update(.05f);
    }
    assert(!g.IsSurfaceMine() && !mineState.active && mineState.player.health == mineState.player.maxHealth);
    assert(w.ui.screen == UiScreen::Medical && g.PlanetLocation()->space == world::SpaceKind::Surface);
    assert(mineState.mapRevision == revision && mineState.quest.GetStage() == MineQuestStage::ShowRecord);
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, mineBike, *g.SurfaceBike()) <
           1.e-6);
    g.Reset();
    assert(!g.SurfaceCities() && !g.Surface());
}
void TestFacilityAccessContracts()
{
    using namespace tutorial;
    std::set<std::string> ids;
    int interiors = 0;
    for (const auto &facility : CityFacilities::All())
    {
        assert(ids.insert(facility.id).second && CityFacilities::Find(facility.target) == &facility);
        interiors += facility.hasInterior;
        if (facility.hasInterior)
        {
            assert(CityFacilities::ExitAt(facility.exterior) == &facility);
        }
    }
    assert(interiors == 2 && !CityFacilities::Find(InteractionTarget::None));
    auto door = *CityFacilities::Find(InteractionTarget::HomeEntrance);
    assert(Game::Distance(door.exterior, kHomeDoor) == 0);
    assert(Game::Distance(CityFacilities::Find(InteractionTarget::GateBeacon)->exterior, kBeaconPosition) ==
           0);
    door.exterior = {};
    auto clear = [](Vector2) { return true; };
    unsigned visited = 0;
    auto find = [&](const CityFacilities::ClearPoint &geometry, const CityFacilities::ClearPoint &occupancy) {
        auto point = CityFacilities::FindExit(door, geometry, occupancy, &visited);
        assert(visited <= 289);
        return point;
    };
    auto original = find(clear, clear);
    assert(original && Game::Distance(*original, {}) == 0 && visited == 1);
    auto occupied = find(clear, [](Vector2 p) { return std::hypot(p.x, p.y) > .4f; });
    assert(occupied && occupied->y > 0 && visited > 1);
    assert(!find(clear, [](Vector2 p) { return p.y < -.1f; }));
    assert(!find(clear, [](Vector2 p) { return std::hypot(p.x, p.y) > 4.1f; }));
    assert(!find([](Vector2) { return false; }, clear) && visited == 0);
    // A free point across an unbroken wall is not a usable exit, even within the old search radius.
    assert(!find([](Vector2 p) { return std::fabs(p.x - 1) > .21f; }, [](Vector2 p) { return p.x > 1.5f; }));
    auto around = find([](Vector2 p) { return std::fabs(p.x - 1) > .21f || p.y > 1.2f; },
                       [](Vector2 p) { return p.x > 1.5f; });
    assert(around && around->x > 1.5f);
    assert(!find([](Vector2 p) { return p.y > .1f; }, clear)); // A blocked residential doorway stays closed.
    door.role = FacilityRole::Observation;
    auto control = find([](Vector2 p) { return std::hypot(p.x, p.y) > .3f; }, clear);
    assert(control && Game::Distance(*control, CityFacilities::StandingPoint(door)) < .001f);
    door.role = FacilityRole::Residence;
    door.outward = {1, 0};
    assert(!find(clear, [](Vector2 p) { return p.x < -.1f; }));
    assert(find(clear, [](Vector2 p) { return p.x > .4f; }));
    door.outward = {};
    assert(!find(clear, clear) && visited == 0);
    door.outward = {0, 1};
    door.hasInterior = false;
    assert(!find(clear, clear));
    door.hasInterior = true;
    door.exterior.x = std::numeric_limits<float>::quiet_NaN();
    assert(!find(clear, clear));
}

void TestFacilityApproachContracts()
{
    using namespace tutorial;
    auto clear = [](Vector2) { return true; };
    const std::vector<WalkwaySegment> sidewalks = {{{4, -3}, {4, 3}}};
    unsigned visited = 0;
    auto connect = [&](const std::vector<WalkwaySegment> &target,
                       const CityFacilities::ClearPoint &geometry) {
        auto path = FacilityApproaches::Connect({}, target, geometry, &visited);
        assert(visited <= 4225);
        if (!path.empty())
        {
            assert(Game::Distance(path.front(), {}) < .001f);
            for (size_t i = 1; i < path.size(); ++i)
            {
                // Independently sample the resulting corridor, including simplified bends and final joins.
                const int samples = (std::max)(1, int(std::ceil(Game::Distance(path[i - 1], path[i]) * 32)));
                for (int step = 0; step <= samples; ++step)
                {
                    const float t = float(step) / samples;
                    Vector2 p{path[i - 1].x + (path[i].x - path[i - 1].x) * t,
                              path[i - 1].y + (path[i].y - path[i - 1].y) * t};
                    assert(geometry(p) && std::hypot(p.x, p.y) <= 16.001f);
                }
            }
        }
        return path;
    };
    auto direct = connect(sidewalks, clear);
    assert(!direct.empty() && std::fabs(direct.back().x - 4) < .001f && std::fabs(direct.back().y) <= 3);
    auto again = connect(sidewalks, clear);
    assert(direct.size() == again.size());
    for (size_t i = 0; i < direct.size(); ++i)
    {
        assert(Game::Distance(direct[i], again[i]) == 0);
    }
    // A nearby endpoint beyond a solid wall must not be accepted as connected.
    assert(connect(sidewalks, [](Vector2 p) { return std::fabs(p.x - 2) > .2f; }).empty());
    auto around = connect(sidewalks, [](Vector2 p) { return std::fabs(p.x - 2) > .2f || p.y > 2; });
    assert(!around.empty());
    bool detoured = false;
    for (auto p : around)
    {
        detoured |= p.y > 2;
    }
    assert(detoured && std::fabs(around.back().x - 4) < .001f);
    assert(connect(sidewalks, [](Vector2) { return false; }).empty() && visited == 0);
    assert(connect({}, clear).empty() && visited == 0);
    assert(connect({{{17, -1}, {17, 1}}}, clear).empty());
    // Exact projection onto an oblique sidewalk, rather than stopping near its endpoint.
    auto diagonal = connect({{{4, 2}, {6, 4}}}, clear);
    assert(!diagonal.empty() && std::fabs(diagonal.back().x - diagonal.back().y - 2) < .001f);
    auto onSidewalk = connect({{{-1, 0}, {1, 0}}}, clear);
    assert(onSidewalk.size() == 1 && visited == 1);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(FacilityApproaches::Connect({nan, 0}, sidewalks, clear, &visited).empty() && visited == 0);
    assert(connect({{{nan, 0}, {1, 0}}}, clear).empty() && visited == 0);
    assert(FacilityApproaches::Connect({}, sidewalks, {}, &visited).empty() && visited == 0);
}

void TestCityInfrastructureContracts(const tutorial::GameState &state)
{
    using namespace tutorial;
    std::set<std::string> ids;
    for (size_t city = 0; city < CityInfrastructure::Profiles().size(); ++city)
    {
        const auto &profile = CityInfrastructure::Profiles()[city];
        assert(std::string(profile.regionId) == Overworld::Cities()[city].id);
        assert(world::WorldCatalog::Region(profile.regionId).tier == Overworld::Cities()[city].tier);
        for (const auto &role : profile.roles)
        {
            assert(ids.insert(std::string(profile.regionId) + "/" + role.key).second && role.label[0]);
            assert(role.assets.front() != models::SceneryType::Count);
        }
    }
    SceneryProp rotated;
    rotated.minimum = {-3, 7};
    rotated.maximum = {1, 9};
    for (auto pad : CityInfrastructure::Pads(rotated))
    {
        // All inspection pads stay outside the delivered, already-rotated footprint with walking clearance.
        assert(pad.x <= rotated.minimum.x - .64f || pad.x >= rotated.maximum.x + .64f ||
               pad.y <= rotated.minimum.y - .64f || pad.y >= rotated.maximum.y + .64f);
    }
    CityWalkwayPlan empty;
    assert(!CityWalkways::Contains(empty, {}));
    assert(!CityWalkways::Contains(empty, {std::numeric_limits<float>::quiet_NaN(), 0}));
    const auto digest = PlanetCities::Describe(state).Digest();
    const auto baseCount = CityPlan::Walkways().size();
    const auto *live = CityInfrastructure::Get().modules.data();
    auto plan = CityInfrastructure::Prepare(state, FacilityApproaches::Get());
    assert(plan.prepared && CityInfrastructure::Get().modules.data() == live);
    assert(plan.modules.size() == plan.moduleCities.size());
    assert(PlanetCities::Describe(state).Digest() == digest && CityPlan::Walkways().size() == baseCount);
    auto connected = CityWalkways::BaseSegments();
    for (const auto &facility : FacilityApproaches::Get().facilities)
    {
        for (size_t j = 1; j < facility.path.size(); ++j)
        {
            connected.push_back({facility.path[j - 1], facility.path[j]});
        }
    }
    const auto &props = Overworld::Scenery().Props();
    const auto clear = CityWalkways::StaticClearance(state);
    std::set<size_t> selected;
    std::array<unsigned, 5> connectedCities = {};
    for (size_t i = 0; i < plan.sites.size(); ++i)
    {
        const auto &site = plan.sites[i];
        const auto &role = CityInfrastructure::Profiles()[i / 3].roles[i % 3];
        assert(site.city == int(i / 3) && site.attempts <= 4 && site.visitedNodes <= 4 * 4225);
        if (site.access == InfrastructureAccess::Missing)
        {
            assert(site.path.empty() && site.sourceIndex == (std::numeric_limits<size_t>::max)());
            continue;
        }
        assert(site.sourceIndex < props.size());
        const auto &prop = props[site.sourceIndex];
        assert(std::find(role.assets.begin(), role.assets.end(), prop.type) != role.assets.end());
        const auto &mesh = models::ResourceAssets::Get(prop.model->resourceId);
        assert(site.asset == mesh.name && Game::Distance(site.equipment, prop.position) == 0);
        assert(std::fabs(site.nativeSizeMeters.x - (mesh.maximum.x - mesh.minimum.x) * 2) < .001f &&
               std::fabs(site.nativeSizeMeters.y - (mesh.maximum.y - mesh.minimum.y) * 2) < .001f &&
               std::fabs(site.nativeSizeMeters.z - (mesh.maximum.z - mesh.minimum.z) * 2) < .001f);
        if (site.access != InfrastructureAccess::Connected)
        {
            assert(site.path.empty());
            continue;
        }
        ++connectedCities[i / 3];
        assert(site.pathLengthMeters >= 0 && site.pathLengthMeters <= 64);
        assert(selected.insert(site.sourceIndex).second && !site.path.empty());
        assert(Game::Distance(site.path.front(), site.standing) == 0 &&
               CityWalkways::Contains(plan, site.standing));
        assert(std::any_of(connected.begin(), connected.end(), [&](auto segment) {
            return Game::Distance(site.path.back(), CityWalkways::Project(site.path.back(), segment)) < .001f;
        }));
        for (size_t j = 1; j < site.path.size(); ++j)
        {
            const auto a = site.path[j - 1], b = site.path[j];
            const int samples = (std::max)(1, int(std::ceil(Game::Distance(a, b) * 16)));
            for (int sample = 0; sample <= samples; ++sample)
            {
                const float t = float(sample) / samples;
                assert(clear({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}));
            }
            connected.push_back({a, b});
        }
    }
    for (auto count : connectedCities)
    {
        assert(count > 0); // An entire city's profile must not silently produce only diagnostic failures.
    }
    for (auto city : plan.moduleCities)
    {
        assert(city >= 0 && city < Overworld::kCityCount);
    }
}

void TestFacilityExitIntegration()
{
    using namespace tutorial;
    auto &g = Game::Get();
    g.StartNewGame();
    auto &state = g.GetState();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!g.Surface()->Ready() && std::chrono::steady_clock::now() < deadline)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(g.Surface()->Ready() && state.isInsideHome);
    TestCityInfrastructureContracts(state);
    const auto coreDigest = PlanetCities::Describe(state).Digest();
    const auto baseWalkways = CityPlan::Walkways().size();
    const auto *publishedModules = FacilityApproaches::Get().modules.data();
    auto prepared = FacilityApproaches::Prepare(state);
    assert(FacilityApproaches::Get().modules.data() == publishedModules);
    assert(PlanetCities::Describe(state).Digest() == coreDigest &&
           CityPlan::Walkways().size() == baseWalkways);
    for (size_t i = 0; i < prepared.facilities.size(); ++i)
    {
        const auto &facility = CityFacilities::All()[i];
        const auto &approach = prepared.facilities[i];
        assert(approach.target == facility.target && !approach.path.empty());
        const auto entrance = facility.role == FacilityRole::Residence
                                  ? facility.exterior
                                  : CityFacilities::StandingPoint(facility);
        assert(Game::Distance(approach.path.front(), entrance) < .001f);
        assert(FacilityApproaches::Connected(facility.target) && FacilityApproaches::Contains(entrance));
        bool joined = CityPlan::IsWalkway(approach.path.back());
        for (size_t earlier = 0; earlier < i && !joined; ++earlier)
        {
            const auto &previous = prepared.facilities[earlier].path;
            for (size_t j = 1; j < previous.size(); ++j)
            {
                // Reuse the public connection query for an exact point on an earlier proven route.
                auto connection = FacilityApproaches::Connect(
                    approach.path.back(), {{previous[j - 1], previous[j]}}, [](Vector2) { return true; });
                joined |= connection.size() == 1;
            }
        }
        assert(joined);
        for (auto p : approach.path)
        {
            assert(!RoadNetwork::Get().Contains(p, .4f) && !Overworld::Scenery().IsBlocked(p, .4f));
        }
    }
    auto originalResidents = state.residents;
    assert(!state.residents.empty());
    auto &traveller = state.residents.front();
    traveller.route = {{traveller.position, -1}, {{traveller.position.x + 1, traveller.position.y}, 0}};
    traveller.routeCursor = 1;
    traveller.randomState = 917923;
    traveller.waitSeconds = 1.75f;
    traveller.activity = ResidentActivity::WaitingSignal;
    const auto residentsBefore = state.residents;
    const auto navigation = CityResidents::Get().NavigationFingerprint();
    FacilityApproaches::Publish({});
    CityResidents::Get().RefreshSidewalks();
    FacilityApproaches::Publish(std::move(prepared));
    CityResidents::Get().RefreshSidewalks();
    assert(CityResidents::Get().NavigationFingerprint() == navigation);
    for (size_t i = 0; i < state.residents.size(); ++i)
    {
        const auto &before = residentsBefore[i], &after = state.residents[i];
        assert(Game::Distance(before.position, after.position) == 0 &&
               before.route.size() == after.route.size() && before.routeCursor == after.routeCursor &&
               before.randomState == after.randomState && before.activity == after.activity &&
               before.waitSeconds == after.waitSeconds);
        for (size_t j = 0; j < before.route.size(); ++j)
        {
            assert(Game::Distance(before.route[j].position, after.route[j].position) == 0 &&
                   before.route[j].crossing == after.route[j].crossing);
        }
    }
    state.residents = std::move(originalResidents);
    const auto &door = *CityFacilities::Find(InteractionTarget::HomeEntrance);
    const auto pose = g.Surface()->Pose().origin;
    const auto bike = g.SurfaceBike();
    const auto epoch = g.Surface()->Stream().Snapshot()->statistics.epoch;
    const auto stage = state.mainQuestStage;
    const int credits = mineState.player.credits, salvage = mineState.player.salvage;
    auto residents = std::move(state.residents);
    // Occupy every candidate pad without replacing terrain or enabling a quest fixture.
    for (int x = -8; x <= 8; ++x)
    {
        for (int y = 0; y <= 8; ++y)
        {
            Resident person;
            person.position = {door.exterior.x + x * .5f, door.exterior.y + y * .5f};
            person.cityIndex = 0;
            state.residents.push_back(person);
        }
    }
    state.playerPosition = door.exit;
    assert(g.FindNearbyInteraction() == InteractionTarget::ApartmentExit);
    g.InteractWithNearbyTarget();
    assert(state.isInsideHome && g.Surface()->Stream().Snapshot()->statistics.epoch == epoch);
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, pose,
                                            g.Surface()->Pose().origin) < 1.e-6);
    state.residents = std::move(residents);
    g.InteractWithNearbyTarget();
    assert(!state.isInsideHome && !state.isInObservatory);
    const auto &region = g.Surface()->Stream().Snapshot()->planet->Region("halo_07");
    auto local = world::SurfaceGeometry::ToLocal(region.frame, g.Surface()->Pose().origin,
                                                 g.Surface()->Planet().radiusMeters);
    auto center = Overworld::Cities()[0].center;
    const Vector2 exit{center.x + float(local.x / 2), center.y + float(local.y / 2)};
    assert(exit.y >= door.exterior.y - .001f && Game::Distance(exit, door.exterior) <= 4.01f);
    assert(!RoadNetwork::Get().Contains(exit, .2f));
    assert(FacilityApproaches::Contains(exit) || CityPlan::IsWalkway(exit));
    assert(g.Surface()->LandingClear(g.Surface()->Pose().origin, .32));
    assert(state.mainQuestStage == stage && mineState.player.credits == credits &&
           mineState.player.salvage == salvage && !mineState.loadout.HasPistol());
    assert(bike && g.SurfaceBike() &&
           world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, *bike, *g.SurfaceBike()) <
               1.e-6);
    assert(g.Surface()->Stream().Snapshot()->statistics.epoch == epoch);
    const auto &lift = *CityFacilities::Find(InteractionTarget::GateBeacon);
    const auto front = g.SurfaceCities()->Point(*g.Surface(), 0, CityFacilities::StandingPoint(lift));
    assert(front && g.RelocateSurfaceSurvey(*front));
    auto savedResidents = std::move(state.residents);
    assert(g.FindNearbyInteraction() == InteractionTarget::GateBeacon);
    g.InteractWithNearbyTarget();
    assert(state.isInObservatory && Game::Distance(state.playerPosition, lift.arrival) < .001f);
    state.playerPosition = lift.exit;
    g.InteractWithNearbyTarget();
    assert(!state.isInObservatory && !state.isInsideHome);
    local = world::SurfaceGeometry::ToLocal(region.frame, g.Surface()->Pose().origin,
                                            g.Surface()->Planet().radiusMeters);
    assert(center.y + float(local.y / 2) >= lift.exterior.y - .001f);
    state.residents = std::move(savedResidents);
    assert(state.mainQuestStage == stage && g.Surface()->Stream().Snapshot()->statistics.epoch == epoch);
    const auto file =
        std::filesystem::path("x64/RevisionChecks") /
        ("facility-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) /
        "access.sav";
    assert(SaveSystem::Save(file));
    publishedModules = FacilityApproaches::Get().modules.data();
    const auto *publishedInfrastructure = CityInfrastructure::Get().modules.data();
    assert(SaveSystem::Load(file));
    SaveSystem::Cancel();
    assert(FacilityApproaches::Get().modules.data() == publishedModules &&
           CityInfrastructure::Get().modules.data() == publishedInfrastructure &&
           CityResidents::Get().NavigationFingerprint() == navigation);
    assert(SaveSystem::Load(file));
    WaitForCheckpointLoad();
    assert(PlanetCities::Describe(state).Digest() == coreDigest);
    assert(CityInfrastructure::Get().prepared);
    for (const auto &facility : CityFacilities::All())
    {
        assert(FacilityApproaches::Connected(facility.target));
    }
    const auto snapshot = monitoring::WorldSnapshot::Build(1, "facility-test");
    assert(snapshot.find("\"city_facilities\"") != std::string::npos &&
           snapshot.find("\"access_status\":\"connected\"") != std::string::npos &&
           snapshot.find("\"city_infrastructure\"") != std::string::npos &&
           snapshot.find("\"building_entrance_contract\":\"awaiting_delivery\"") != std::string::npos);
    g.Reset();
}

void TestStoryDialogueContext()
{
    using namespace tutorial;
    GameState state;
    game::MineState mine;
    game::ChapterOneState chapter;
    auto proposal = [&](MissionId id, const char *channel = "제안 기록") {
        auto lines = StoryDialogue::Proposal(id, state, mine, chapter, channel);
        assert(!lines.empty() && lines.back().id == "offer");
        assert(lines.back().choices.size() == 2 && lines.back().choices[0].id == "accept" &&
               lines.back().choices[1].id == "later");
        for (const auto &line : lines)
        {
            assert((line.channel == channel || (line.speaker == "나" && line.channel == "조사 기록")) &&
                   !line.text.empty());
        }
        assert(lines.back().channel == channel);
        return lines;
    };
    auto signal = proposal(MissionId::Signal, "단말 기록");
    state.gateSignalHeard = true;
    assert(proposal(MissionId::Signal).front().id != signal.front().id);
    assert(state.missions.Status(MissionId::Signal) == MissionStatus::NotAccepted);
    state.hasDescendedObservatory = true;
    auto excavation = proposal(MissionId::Mine);
    mine.powerBypassed = true;
    auto visited = proposal(MissionId::Mine);
    assert(visited.front().id != excavation.front().id);
    mine.signalRecorded = mine.glyphRecorded = mine.coordinatesRecorded = true;
    assert(proposal(MissionId::Mine).front().id != visited.front().id);
    assert(mine.quest.GetStage() == game::MineQuestStage::ShowRecord && !mine.quest.IsRiaJoined());
    assert(!mine.loadout.HasPistol() && !state.isComplete);
    assert(state.missions.Accept(MissionId::Mine, true) && state.missions.Pause(MissionId::Mine));
    const auto resumed = proposal(MissionId::Mine);
    assert(resumed.front().id == "resume" && resumed[1].id == "mine/coordinates");
    assert(state.missions.Status(MissionId::Mine) == MissionStatus::Paused);
    assert(state.missions.Accept(MissionId::Mine, true));
    assert(StoryDialogue::Proposal(MissionId::Mine, state, mine, chapter).empty());
    assert(state.missions.Complete(MissionId::Mine));
    assert(StoryDialogue::Proposal(MissionId::Mine, state, mine, chapter).empty());
    assert(StoryDialogue::Proposal(MissionId::Count, state, mine, chapter).empty());
    assert(StoryDialogue::Proposal(MissionId(-1), state, mine, chapter).empty());

    auto frontier = proposal(MissionId::Frontier);
    chapter.observedRecords = 1;
    assert(proposal(MissionId::Frontier).front().id != frontier.front().id);
    chapter.generatorRestored = chapter.convoyRescued = chapter.ledgerFound = true;
    assert(proposal(MissionId::Frontier).front().id == "frontier/report");
    assert(!chapter.recordsCompared && !chapter.waterwayUnlocked && !chapter.convoyRewardClaimed);
    auto mara = proposal(MissionId::Mara, "현장");
    state.isPowerRestored = true;
    assert(proposal(MissionId::Mara, "현장").front().id != mara.front().id);
    assert(!state.isSideQuestAccepted && !state.isSideQuestComplete);

    // First reads, companion reactions and archive reads use separate history entries without new flags.
    for (auto discovery :
         {MineDiscovery::Power, MineDiscovery::Signal, MineDiscovery::Glyph, MineDiscovery::Coordinates})
    {
        game::MineState facts;
        if (discovery == MineDiscovery::Coordinates)
        {
            auto missing = StoryDialogue::ObserveMine(discovery, facts, true);
            assert(missing.front().id == "requires-records" && missing.front().speaker != "리아");
            assert(!facts.coordinatesRecorded);
            facts.signalRecorded = true;
            assert(StoryDialogue::ObserveMine(discovery, facts, false).front().id == "requires-records");
            facts.glyphRecorded = true;
        }
        const auto solo = StoryDialogue::ObserveMine(discovery, facts, false);
        const auto accompanied = StoryDialogue::ObserveMine(discovery, facts, true);
        assert(solo.size() == 1 && accompanied.size() == 1);
        assert(solo.front().id == "discovery/solo" && solo.front().speaker != "리아");
        assert(accompanied.front().id == "discovery/ria" && accompanied.front().speaker == "리아" &&
               accompanied.front().channel == "무전");
        assert(!facts.coordinatesRecorded && !facts.powerBypassed);
        assert(facts.quest.GetStage() == game::MineQuestStage::ShowRecord && !facts.quest.IsRiaJoined());
        switch (discovery)
        {
        case MineDiscovery::Power:
            facts.powerBypassed = true;
            break;
        case MineDiscovery::Signal:
            facts.signalRecorded = true;
            break;
        case MineDiscovery::Glyph:
            facts.glyphRecorded = true;
            break;
        case MineDiscovery::Coordinates:
            facts.coordinatesRecorded = true;
            break;
        default:
            assert(false);
        }
        for (bool riaPresent : {false, true})
        {
            const auto archive = StoryDialogue::ObserveMine(discovery, facts, riaPresent);
            assert(archive.front().id == "archive" && archive.front().speaker != "리아");
        }
        assert(StoryDialogue::DiscoveryKey(discovery));
    }
    assert(!StoryDialogue::DiscoveryKey(MineDiscovery::Count));
    assert(StoryDialogue::ObserveMine(MineDiscovery(-1), mine, true).empty());
    assert(mine.player.credits == game::PlayerStats{}.credits && mine.player.salvage == 0);
}

void TestStoryFollowUpContracts()
{
    using namespace tutorial;
    game::ChapterOneState chapter;
    GameState state;
    assert(!StoryDialogue::CanFollowUp(CompanionId::Ria, chapter, true));
    chapter.generatorRestored = chapter.portableLightRepaired = true;
    assert(StoryDialogue::FrontierFollowUp(CompanionId::Ria, chapter, false).empty());
    auto rest = StoryDialogue::FrontierFollowUp(CompanionId::Ria, chapter, true);
    assert(rest.size() == 3 && rest.front().speaker == "리아" && rest[1].speaker == "나");
    chapter.ria.combat.health = 0;
    assert(StoryDialogue::FrontierFollowUp(CompanionId::Ria, chapter, true).empty());
    chapter.ria.combat.health = 100;
    assert(!chapter.convoyRewardClaimed && !chapter.recordsCompared && !chapter.waterwayUnlocked);

    chapter.convoyEvacuated = true;
    assert(StoryDialogue::FrontierFollowUp(CompanionId::Kain, chapter, true).empty());
    chapter.kainJoined = true;
    auto empty = StoryDialogue::FrontierFollowUp(CompanionId::Kain, chapter, true);
    assert(empty.front().id == "roll-call/0/0/0");
    chapter.survivors.resize(5);
    for (size_t i = 0; i < chapter.survivors.size(); ++i)
    {
        chapter.survivors[i].arrived = i < 4;
    }
    chapter.survivors.front().combat.health = 0;
    auto count = StoryDialogue::FrontierFollowUp(CompanionId::Kain, chapter, true);
    assert(count.front().id == "roll-call/5/4/1");
    chapter.survivors.back().arrived = true;
    chapter.survivors.front().combat.health = 100;
    auto received = StoryDialogue::FrontierFollowUp(CompanionId::Kain, chapter, true);
    assert(received.front().id == "roll-call/5/5/0" && received[1].text == count[1].text);
    chapter.receiverInspected = true;
    assert(StoryDialogue::FrontierFollowUp(CompanionId::Kain, chapter, true)[1].text != received[1].text);
    chapter.kain.combat.health = 0;
    assert(!StoryDialogue::CanFollowUp(CompanionId::Kain, chapter, true));
    chapter.kain.combat.health = 100;
    assert(StoryDialogue::FrontierFollowUp(CompanionId::Kain, chapter, false).empty());
    assert(StoryDialogue::FrontierFollowUp(CompanionId(255), chapter, true).empty());
    assert(!chapter.convoyRewardClaimed && !chapter.recordsCompared && chapter.survivors.back().arrived);

    assert(StoryDialogue::MaraFollowUp(state, chapter).empty());
    state.isPowerRestored = true;
    assert(StoryDialogue::MaraFollowUp(state, chapter).empty());
    state.isSideQuestComplete = true;
    auto mara = StoryDialogue::MaraFollowUp(state, chapter);
    assert(mara.size() == 4 && mara[2].speaker == "나" && mara[2].id == "power");
    chapter.generatorRestored = false;
    assert(StoryDialogue::MaraFollowUp(state, chapter).size() == 3);
    assert(!state.isSideQuestAccepted && state.isSideQuestComplete);

    for (bool ria : {false, true})
    {
        for (bool kain : {false, true})
        {
            const auto comparison = StoryDialogue::FrontierComparison(ria, kain);
            assert(comparison.size() == 5 && comparison.front().id == "match" &&
                   comparison.back().id == "next");
            assert(std::any_of(comparison.begin(), comparison.end(),
                               [](const auto &line) { return line.speaker == "리아"; }) == ria);
            assert(std::any_of(comparison.begin(), comparison.end(),
                               [](const auto &line) { return line.speaker == "카인"; }) == kain);
            if (ria)
            {
                assert(comparison[2].channel == "무전");
            }
            if (!kain)
            {
                assert(comparison[3].speaker == "수신망" && comparison[3].channel == "단말 기록");
            }
        }
    }
    assert(!chapter.recordsCompared && !chapter.waterwayUnlocked && !chapter.convoyRewardClaimed);
}

void TestStoryJournalContracts()
{
    using namespace tutorial;
    GameState state;
    game::MineState mine;
    game::ChapterOneState chapter;
    state.surfaceExploration = true;
    auto collect = [&] { return StoryJournal::Collect(state, mine, chapter); };
    auto find = [&](const std::string &id) -> std::optional<InvestigationRecord> {
        const auto records = collect();
        std::set<std::string> ids;
        for (const auto &record : records)
        {
            assert(ids.insert(record.id).second && !record.fact.empty());
            assert(record.fact.find("장막") == std::string::npos &&
                   record.fact.find("네레우스") == std::string::npos);
        }
        for (const auto &record : records)
        {
            if (record.id == id)
            {
                return record;
            }
        }
        return std::nullopt;
    };
    assert(collect().empty());
    state.gateSignalHeard = true;
    assert(find("observatory/voice") && !find("mine/route") && !find("mine/glyph"));
    assert(!find("observatory/voice")->unresolved.empty());
    mine.signalRecorded = mine.glyphRecorded = mine.coordinatesRecorded = true;
    assert(find("mine/signal") && find("mine/glyph") && find("mine/coordinates"));
    assert(!find("mine/report") && !mine.quest.IsRiaJoined());
    assert(state.missions.Status(MissionId::Mine) == MissionStatus::NotAccepted);
    assert(state.missions.Accept(MissionId::Mine, true) && state.missions.Pause(MissionId::Mine));
    assert(find("mine/glyph") && state.missions.Status(MissionId::Mine) == MissionStatus::Paused);

    chapter.observedRecords = 1 | 2 | 4 | 8 | 16;
    assert(find("frontier/generator") && find("frontier/radio") && find("frontier/trade"));
    assert(find("frontier/ledger") && find("frontier/detail-ledger"));
    assert(!chapter.generatorInspected && !chapter.ledgerFound && !chapter.recordsCompared);
    const auto before = collect().size();
    const auto generator = find("frontier/generator")->fact;
    chapter.generatorInspected = chapter.receiverInspected = chapter.tradeProofFound = true;
    chapter.ledgerFound = chapter.detailLedgerFound = true;
    assert(collect().size() == before); // Acceptance must not duplicate an already observed record.
    chapter.generatorRestored = true;
    assert(find("frontier/generator")->fact != generator && collect().size() == before);
    chapter.convoyEvacuated = true;
    assert(find("frontier/evacuation") && !find("frontier/kain") && !chapter.convoyRewardClaimed);
    chapter.kainJoined = true;
    assert(find("frontier/kain"));
    chapter.waterwayUnlocked = true;
    assert(!find("frontier/waterway"));
    chapter.recordsCompared = true;
    assert(find("frontier/comparison") && find("frontier/waterway"));
    assert(!find("frontier/waterway")->unresolved.empty());

    while (mine.quest.GetStage() != game::MineQuestStage::Complete)
    {
        const auto stage = mine.quest.GetStage();
        if (stage == game::MineQuestStage::TravelToMine)
        {
            assert(mine.quest.BeginJourney());
        }
        assert(mine.quest.Advance(stage));
    }
    assert(find("mine/report") && find("mine/coordinates")->fact.find("보고한다") == std::string::npos);
    mine.signalRecorded = mine.glyphRecorded = mine.coordinatesRecorded = false;
    assert(!find("mine/glyph")); // Surface facts are not invented from an unrelated progression fixture.
    state.surfaceExploration = false;
    assert(find("mine/glyph") && find("mine/signal") && find("mine/coordinates"));
    assert(mine.player.credits == game::PlayerStats{}.credits && !mine.loadout.HasPistol());

    const auto terminal = StoryDialogue::MineStatus(mine, StoryContact::Receiver);
    assert(terminal.front().id == "reported" && terminal.size() == 1);
    for (const auto &line : terminal)
    {
        assert(line.speaker == "헤일로 수신망" || line.speaker == "수신망");
    }
    game::MineState ongoing;
    while (ongoing.quest.GetStage() != game::MineQuestStage::Complete)
    {
        auto lines = StoryDialogue::MineStatus(ongoing, StoryContact::Receiver);
        assert(!lines.empty());
        for (const auto &line : lines)
        {
            assert(line.speaker != "리아");
        }
        const auto stage = ongoing.quest.GetStage();
        if (stage == game::MineQuestStage::TravelToMine)
        {
            assert(ongoing.quest.BeginJourney());
        }
        assert(ongoing.quest.Advance(stage));
    }
    const auto status = StoryDialogue::FrontierStatus(chapter, StoryContact::Receiver);
    const auto revisit = StoryDialogue::FrontierReturn(chapter);
    assert(status.front().id == "compared" &&
           revisit.back().choices.size() == (chapter.waterwayUnlocked ? 3u : 2u));
    assert(revisit.back().choices[0].id == "revisit" && revisit.back().choices[1].id == "later");
    chapter.recordsCompared = false;
    assert(StoryDialogue::FrontierReturn(chapter).empty());
    assert(StoryDialogue::MineStatus(mine, StoryContact(-1)).empty());
    assert(StoryDialogue::FrontierStatus(chapter, StoryContact(-1)).empty());
    assert(!chapter.convoyRewardClaimed && state.missions.Status(MissionId::Mine) == MissionStatus::Paused);
}

void TestStoryMissionCompletionContracts()
{
    using namespace tutorial;
    using Progress = StoryMissionProgress;
    GameState state;
    game::MineState mine;
    game::ChapterOneState chapter;
    state.surfaceExploration = true;
    const auto signal = MissionId::Signal, excavation = MissionId::Mine;
    const auto frontier = MissionId::Frontier, mara = MissionId::Mara;
    auto ready = [&](MissionId id, bool story = true) {
        return Progress::CanComplete(id, state, mine, chapter, story);
    };
    auto complete = [&](MissionId id, bool story = true) {
        return Progress::Complete(id, state, mine, chapter, story);
    };
    assert(Progress::Restriction(excavation, state, mine, true));
    assert(Progress::Restriction(frontier, state, mine, true));
    assert(Progress::Restriction(mara, state, mine, false));
    assert(!ready(MissionId::Count) && !complete(MissionId(-1)));
    state.gateSignalHeard = true;
    assert(state.missions.Accept(signal, true) && !complete(signal));
    state.hasDescendedObservatory = true;
    assert(!Progress::Restriction(excavation, state, mine, true));
    assert(ready(signal) && state.missions.Status(signal) == MissionStatus::Active);
    assert(state.missions.Pause(signal) && !complete(signal));
    assert(state.gateSignalHeard && state.hasDescendedObservatory);
    assert(state.missions.Accept(signal, true));
    assert(state.missions.Accept(mara, true) && state.missions.Track(mara));
    state.surfaceExploration = false;
    assert(!complete(signal));
    state.surfaceExploration = true;
    assert(!complete(signal, false) && complete(signal) && !complete(signal));
    assert(state.missions.Tracked() == mara && state.missions.Active(mara));
    assert(state.mainQuestStage == MainQuestStage::MountJetBike);

    // Pre-visited mine evidence remains a physical record; only the return report completes the mission.
    mine.powerBypassed = mine.signalRecorded = mine.glyphRecorded = mine.coordinatesRecorded = true;
    assert(!complete(excavation) && !state.isComplete);
    assert(state.missions.Accept(excavation, true) && !complete(excavation));
    while (mine.quest.GetStage() != game::MineQuestStage::ReturnToHalo)
    {
        const auto stage = mine.quest.GetStage();
        if (stage == game::MineQuestStage::TravelToMine)
        {
            assert(mine.quest.BeginJourney());
        }
        assert(mine.quest.Advance(stage));
    }
    assert(ready(excavation) && !state.isComplete);
    assert(state.missions.Pause(excavation) && !complete(excavation));
    assert(mine.coordinatesRecorded && mine.quest.GetStage() == game::MineQuestStage::ReturnToHalo);
    assert(state.missions.Accept(excavation, true) && !complete(excavation, false));
    const int credits = mine.player.credits, salvage = mine.player.salvage;
    assert(complete(excavation) && !complete(excavation));
    assert(state.isComplete && state.mainQuestStage == MainQuestStage::Complete);
    assert(mine.quest.GetStage() == game::MineQuestStage::Complete);
    assert(!Progress::Restriction(frontier, state, mine, true));
    assert(mine.player.credits == credits && mine.player.salvage == salvage && !mine.loadout.HasPistol());
    assert(state.missions.Status(frontier) == MissionStatus::NotAccepted);

    assert(!complete(mara));
    state.isPowerRestored = true;
    assert(ready(mara) && !state.isSideQuestComplete);
    assert(state.missions.Pause(mara) && !complete(mara));
    assert(state.isPowerRestored && !state.isSideQuestComplete);
    assert(state.missions.Accept(mara, true) && complete(mara) && !complete(mara));
    assert(state.isSideQuestComplete);

    // Each of the three existing site facts is required, regardless of discovery order.
    assert(state.missions.Accept(frontier, true));
    chapter.ledgerFound = chapter.convoyRescued = true;
    assert(!complete(frontier) && !chapter.recordsCompared && !chapter.waterwayUnlocked);
    chapter.generatorRestored = true;
    chapter.ledgerFound = false;
    assert(!complete(frontier));
    chapter.ledgerFound = true;
    chapter.convoyRescued = false;
    assert(!complete(frontier));
    chapter.convoyRescued = true;
    assert(ready(frontier) && !chapter.recordsCompared && !chapter.waterwayUnlocked);
    assert(!complete(frontier, false) && !chapter.recordsCompared && !chapter.waterwayUnlocked);
    assert(state.missions.Pause(frontier) && !complete(frontier));
    assert(chapter.generatorRestored && chapter.ledgerFound && chapter.convoyRescued);
    assert(state.missions.Accept(frontier, true) && complete(frontier) && !complete(frontier));
    assert(chapter.recordsCompared && chapter.waterwayUnlocked);
    assert(mine.player.credits == credits && mine.player.salvage == salvage);
}

void TestOpenWorldParticipation()
{
    using namespace tutorial;
    QuestParticipation participation;
    assert(participation.Tracked() == MissionId::Count && !participation.Track(MissionId::Mine));
    assert(!participation.Track(MissionId::Count));
    assert(participation.Accept(MissionId::Mara, true) && participation.Tracked() == MissionId::Mara);
    assert(!participation.Accept(MissionId::Mine, false));
    assert(participation.Accept(MissionId::Mine, true) && !participation.Accept(MissionId::Mine, true));
    assert(participation.Tracked() == MissionId::Mara);
    assert(participation.Track(MissionId::Mine) && participation.Tracked() == MissionId::Mine);
    assert(participation.Pause(MissionId::Mine) && !participation.Complete(MissionId::Mine));
    assert(participation.Tracked() == MissionId::Count && participation.Active(MissionId::Mara));
    assert(participation.Accept(MissionId::Mine, true) && participation.Complete(MissionId::Mine));
    assert(!participation.Accept(MissionId::Mine, true) && !participation.Complete(MissionId::Mine));
    auto &g = Game::Get();
    auto &w = g.GetState();
    g.StartNewGame();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!g.Surface()->Ready() && std::chrono::steady_clock::now() < deadline)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(g.Surface()->Ready() && w.surfaceExploration && w.isInsideHome && !mineState.loadout.HasPistol());
    for (int i = 0; i < int(MissionId::Count); ++i)
    {
        assert(w.missions.Status(MissionId(i)) == MissionStatus::NotAccepted);
    }
    auto read = [&](size_t choice = 0) {
        for (unsigned page = 0; Dialogue::Active() && page < 128; ++page)
        {
            const auto &line = w.dialogue.pages[w.dialogue.page];
            if (!line.choices.empty() && w.dialogue.selection < 0)
            {
                assert(Dialogue::Select(choice));
            }
            assert(Dialogue::Advance());
        }
        assert(!Dialogue::Active());
    };
    auto city = [&](Vector2 p) {
        auto target = g.SurfaceCities()->Point(*g.Surface(), 0, p);
        assert(target);
        assert(g.RelocateSurfaceSurvey(*target));
        w.playerPosition = *g.SurfaceCities()->Local(*g.Surface(), 0, p);
        g.InteractWithNearbyTarget();
    };
    w.playerPosition = {1, -1.2f};
    g.InteractWithNearbyTarget();
    assert(Dialogue::Active() && w.dialogue.pages.front().channel == "단말 기록");
    read(1);
    assert(w.homeMessageRead && w.missions.Status(MissionId::Signal) == MissionStatus::NotAccepted);
    assert(g.MissionRestriction(MissionId::Mine) && !g.MissionEligible(MissionId::Mine));
    assert(w.missions.Accept(MissionId::Mara, true));
    assert(w.missions.Accept(MissionId::Signal, true));
    assert(w.missions.Track(MissionId::Signal) && g.FocusedMission() == MissionId::Signal);
    assert(std::string(g.MissionObjective()).find("전망대") != std::string::npos);
    assert(w.missions.Track(MissionId::Mara) && g.FocusedMission() == MissionId::Mara);
    assert(w.missions.Pause(MissionId::Signal) && w.missions.Pause(MissionId::Mara));
    w.missions = {}; // Restore the freely visited, never-accepted scenario below.
    w.playerPosition = {2.4f, 2.4f};
    g.InteractWithNearbyTarget();
    assert(!w.isInsideHome);
    city(kBeaconPosition);
    assert(w.isInObservatory);
    w.playerPosition = kObservatoryConsole;
    g.InteractWithNearbyTarget();
    read();
    w.playerPosition = kObservatoryStairs;
    g.InteractWithNearbyTarget();
    assert(w.gateSignalHeard && w.hasDescendedObservatory &&
           w.missions.Status(MissionId::Signal) == MissionStatus::NotAccepted);
    g.OfferMission(MissionId::Signal);
    read();
    assert(w.missions.Status(MissionId::Signal) == MissionStatus::Completed);
    auto plan = g.Surface()->Stream().Snapshot()->planet;
    auto epoch = g.Surface()->Stream().Snapshot()->statistics.epoch;
    assert(g.RelocateSurfaceSurvey(plan->Region("mq00_mine").access.position));
    assert(g.EnterSurfaceMine() && !mineState.loadout.HasPistol() && !mineState.quest.IsRiaJoined());
    for (auto point : {kPowerBypass, kRuinSignal, kObserverGlyph, kCoordinateTerminal})
    {
        w.playerPosition = point;
        GameLevels::Mine().Interact();
        assert(Dialogue::Active() && w.dialogue.pages.front().speaker != "리아");
        read();
    }
    w.playerPosition = kRuinSignal;
    GameLevels::Mine().Interact();
    assert(Dialogue::Active() && w.dialogue.pages.front().id.find("/archive/") != std::string::npos);
    read();
    assert(mineState.powerBypassed && mineState.coordinatesRecorded &&
           mineState.quest.GetStage() == MineQuestStage::ShowRecord);
    w.playerPosition = mineState.entry;
    GameLevels::Mine().Interact();
    assert(!g.IsSurfaceMine());
    g.OfferMission(MissionId::Mine);
    assert(Dialogue::Active() && w.dialogue.pages.front().id.find("/mine/coordinates/") != std::string::npos);
    read(1);
    assert(w.missions.Status(MissionId::Mine) == MissionStatus::NotAccepted);
    g.OfferMission(MissionId::Mine);
    read();
    assert(w.missions.Active(MissionId::Mine) &&
           mineState.quest.GetStage() == MineQuestStage::CheckCityRelay);
    city(kLiaPosition);
    read(1);
    assert(mineState.quest.GetStage() == MineQuestStage::TravelToMine && !mineState.quest.IsRiaJoined());
    city(kLiaPosition);
    read();
    assert(mineState.quest.IsRiaJoined() && !mineState.loadout.HasPistol());
    assert(g.RelocateSurfaceSurvey(plan->Region("mq00_mine").access.position));
    g.InteractWithNearbyTarget();
    assert(Dialogue::Active() && !mineState.active);
    read();
    assert(mineState.active && mineState.loadout.HasPistol() &&
           mineState.quest.GetStage() == MineQuestStage::ReturnToHalo);
    g.ManageMission(MissionId::Mine);
    assert(w.missions.Status(MissionId::Mine) == MissionStatus::Paused);
    assert(mineState.powerBypassed && mineState.coordinatesRecorded);
    g.ManageMission(MissionId::Mine);
    read();
    assert(w.missions.Active(MissionId::Mine));
    w.playerPosition = mineState.entry;
    GameLevels::Mine().Interact();
    city(kLiaPosition);
    read();
    assert(w.missions.Status(MissionId::Mine) == MissionStatus::Completed &&
           w.missions.Status(MissionId::Frontier) == MissionStatus::NotAccepted &&
           g.IsGroundVehicleUnlocked());
    auto &chapter = GameLevels::Chapter();
    auto &field = chapter.GetState();
    assert(g.RelocateSurfaceSurvey(chapter.Surface().Point({11.5f, 10.9f})));
    chapter.Interact();
    read();
    assert(field.observedRecords && !field.generatorInspected);
    g.OfferMission(MissionId::Frontier);
    read();
    assert(w.missions.Active(MissionId::Frontier) && field.generatorInspected && !field.generatorRestored);
    g.ManageMission(MissionId::Frontier);
    assert(w.missions.Status(MissionId::Frontier) == MissionStatus::Paused);
    auto salvage = mineState.player.salvage;
    field.convoyEvacuated = true;
    chapter.CommitConvoy();
    assert(mineState.player.salvage == salvage);
    g.ManageMission(MissionId::Frontier);
    read();
    assert(field.convoyRewardClaimed && field.convoyRescued && mineState.player.salvage == salvage + 3);
    chapter.CommitConvoy();
    assert(mineState.player.salvage == salvage + 3);
    city(kRelayPosition);
    assert(w.isPowerRestored && !w.isSideQuestAccepted);
    g.OfferMission(MissionId::Mara);
    read();
    city(kMaraPosition);
    read();
    assert(w.isSideQuestComplete && w.missions.Status(MissionId::Mara) == MissionStatus::Completed);
    g.ManageMission(MissionId::Frontier);
    mineState.player.health = 0;
    for (int i = 0; i < 60; ++i)
    {
        g.Update(.05f);
    }
    assert(w.missions.Status(MissionId::Frontier) == MissionStatus::Paused && field.generatorInspected &&
           field.convoyRewardClaimed);
    assert(g.Surface()->Stream().Snapshot()->statistics.epoch == epoch);
    Hud::CloseMenu();
    assert(!HospitalRecovery::Active());
    g.ManageMission(MissionId::Frontier);
    read();
    field.generatorRestored = field.ledgerFound = true;
    assert(g.CanCompleteMission(MissionId::Frontier));
    chapter.InteractHub();
    assert(Dialogue::Active());
    // An obsolete dialogue callback must not complete a paused mission or consume the only retry.
    assert(w.missions.Pause(MissionId::Frontier));
    read();
    assert(!field.recordsCompared && !field.waterwayUnlocked);
    g.ManageMission(MissionId::Frontier);
    read();
    chapter.InteractHub();
    read();
    assert(field.recordsCompared && field.waterwayUnlocked &&
           w.missions.Status(MissionId::Frontier) == MissionStatus::Completed);
    assert(!g.CompleteMission(MissionId::Frontier) && !g.CanCompleteMission(MissionId::Frontier));
    const auto position = g.Surface()->Pose().origin;
    const auto manualPin = plan->Region("mq00_mine").access.position;
    w.planetDestination = manualPin;
    chapter.InteractHub();
    assert(Dialogue::Active() && w.dialogue.sceneId == "chapter1/return");
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, *w.planetDestination,
                                            manualPin) < 1.e-6);
    read(1);
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, *w.planetDestination,
                                            manualPin) < 1.e-6);
    const int reportSalvage = mineState.player.salvage;
    chapter.InteractHub();
    read();
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, *w.planetDestination,
                                            chapter.Surface().Point(kFrontierEntry)) < 1.e-6);
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, position,
                                            g.Surface()->Pose().origin) < 1.e-6);
    assert(mineState.player.salvage == reportSalvage && field.recordsCompared &&
           g.Surface()->Stream().Snapshot()->statistics.epoch == epoch);
    g.Reset();
}
void TestRelayTravelAndAtlas()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(g.EnterTestStage(TestStage::SurfaceSurvey));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!g.Surface()->Ready() && std::chrono::steady_clock::now() < deadline)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(g.Surface()->Ready());
    const auto plan = g.Surface()->Stream().Snapshot()->planet;
    const auto *source = world::RelayNetwork::Find(*plan, "relay_halo_07_0");
    const auto *target = world::RelayNetwork::Find(*plan, "relay_frontier_0");
    assert(source && target);
    const auto epoch = g.Surface()->Stream().Snapshot()->statistics.epoch;
    const auto stage = mineState.quest.GetStage();
    const auto salvage = mineState.player.salvage;
    auto visit = [&](const world::RelayCandidate &relay) {
        w.mapVisible = false;
        assert(g.RelocateSurfaceSurvey(relay.location.position));
        RelayTravel::Interact();
        assert(w.relayNetwork.Registered(relay.id) && w.mapVisible);
    };
    visit(*target);
    visit(*source);
    const auto start = g.Surface()->Pose().origin, bike = *g.SurfaceBike();
    w.selectedRelay = target->id;
    assert(MapHud::HandleMapKey('p') && w.planetMapRange > g.Surface()->Planet().radiusMeters * 3);
    for (const auto &region : plan->regions)
    {
        auto canvas = MapHud::PlanetToMap(region.access.position);
        if (MapHud::Contains(canvas, true))
        {
            auto restored = MapHud::PlanetAt(canvas);
            assert(restored &&
                   G::Distance(g.Surface()->Planet().radiusMeters, *restored, region.access.position) < 3);
        }
    }
    MapHud::ResetView();
    CivicJustice::State().wanted = 1;
    assert(!RelayTravel::Begin(target->id));
    CivicJustice::State().wanted = 0;
    w.isBikeMounted = true;
    assert(!RelayTravel::Begin(target->id));
    w.isBikeMounted = false;
    assert(!RelayTravel::Begin("unknown_relay"));
    assert(RelayTravel::Begin(target->id) && !RelayTravel::Begin(target->id));
    g.Update(.05f);
    assert(G::Distance(g.Surface()->Planet().radiusMeters, start, g.Surface()->Pose().origin) < 1.e-6);
    g.KeyDown(27, 0, 0);
    g.KeyUp(27, 0, 0);
    assert(!w.relayPreparing &&
           G::Distance(g.Surface()->Planet().radiusMeters, start, g.Surface()->Pose().origin) < 1.e-6);
    assert(RelayTravel::Begin(target->id));
    for (int i = 0; i < 8 && w.relayPreparing; ++i)
    {
        g.Update(.05f);
    }
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (w.relayPreparing && std::chrono::steady_clock::now() < deadline)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!w.relayPreparing && !w.mapVisible &&
           G::Distance(g.Surface()->Planet().radiusMeters, target->location.position,
                       g.Surface()->Pose().origin) <= 8.01);
    assert(G::Distance(g.Surface()->Planet().radiusMeters, bike, *g.SurfaceBike()) < 1.e-6);
    assert(g.Surface()->Stream().Snapshot()->statistics.epoch == epoch &&
           mineState.quest.GetStage() == stage && mineState.player.salvage == salvage);
    w.mapVisible = false;
    assert(g.RelocateSurfaceSurvey(plan->Region("mq00_mine").access.position));
    assert(g.EnterSurfaceMine());
    const auto fog = mineState.explored;
    w.mapVisible = true;
    w.planetMapSurface = false;
    assert(MapHud::HandleMapKey(9) && w.planetMapSurface && MapHud::PlanetAt({488, 400}));
    assert(!RelayTravel::Begin(source->id));
    assert(MapHud::HandleMapKey(9) && !w.planetMapSurface && mineState.explored == fog);
    w.mapVisible = false;
    assert(g.LeaveSurfaceMine());
    visit(*source);
    w.selectedRelay = target->id;
    assert(RelayTravel::Begin(target->id));
    mineState.player.health = 0;
    g.Update(.05f);
    assert(!w.relayPreparing && w.relayNetwork.Registered(source->id) &&
           w.relayNetwork.Registered(target->id));
    g.Reset();
}
void PrepareCheckpointFixture()
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(g.EnterTestStage(TestStage::SurfaceSurvey));
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!g.Surface()->Ready() && std::chrono::steady_clock::now() < until)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(g.Surface()->Ready());
    auto plan = g.Surface()->Stream().Snapshot()->planet;
    auto relay = world::RelayNetwork::Find(*plan, "relay_halo_07_0");
    assert(relay);
    assert(g.RelocateSurfaceSurvey(relay->location.position));
    RelayTravel::Interact();
    w.mapVisible = false;
    assert(w.relayNetwork.Registered(relay->id));
    w.residents.front().name = "저장 검사 주민";
    w.residents.front().health = 64;
    assert(g.RelocateSurfaceSurvey(plan->Region("mq00_mine").access.position));
    assert(g.EnterSurfaceMine());
    mineState.player.credits = 1737;
    // A regional visit must never refill or heal a joined companion.
    assert(g.HasCompanion(CompanionId::Ria, CompanionPlace::Mine));
    mineState.riaCombat.health = 37;
    combat::Shot companionShot;
    assert(mineState.riaCombat.pistol.TryFire(0, companionShot));
    assert(mineState.riaCombat.pistol.Reload());
    mineState.riaCombat.pistol.Update(.42f);
    mineState.player.salvage = 23;
    mineState.enemies.front().health = 7;
    mineState.survivors.front().combat.health = 61;
    mineState.drops.push_back({{20.5f, 20.5f}, ItemKind::Salvage, mineState.nextDropActorId++});
    GameLevels::Mine().Map().Reveal({35.5f, 41.5f});
    assert(mineState.loadout.Select(combat::WeaponSlot::Secondary));
    combat::Shot shot;
    assert(mineState.pistol.TryFire(0, shot));
    assert(mineState.pistol.Reload());
    mineState.pistol.Update(.42f);
    assert(w.missions.Pause(MissionId::Frontier));
    auto &field = GameLevels::Chapter().GetState();
    field.generatorInspected = true;
    field.observedRecords |= 1;
    w.dialogue.history.push_back(
        {"checkpoint/line", "리아", "기록", "다시 불러와도 한글과 완료 기록을 보존한다.", {}});
    w.dialogue.scenes["checkpoint/scene"] = {DialoguePhase::Completed, 1, "accept", ""};
    w.dialogue.events.insert("checkpoint/event");
    w.playerGender = models::Gender::Female;
    w.playerHair = models::HairStyle::Bun;
    w.planetMapRange = 64000;
    w.mapRegisteredRelaysOnly = true;
    CivicJustice::State().infamy = 17;
    CivicJustice::State().heat = 200;
    CivicJustice::State().wanted = 2;
    PlanetSecurity::State().incident = PlanetSecurity::State().search = g.Surface()->Pose().origin;
}
void WaitForCheckpointLoad()
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(35);
    while (tutorial::SaveSystem::Loading() && std::chrono::steady_clock::now() < until)
    {
        tutorial::Game::Get().Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!tutorial::SaveSystem::Loading());
}
void VerifyCheckpointFixture(int credits = 1737)
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(g.Surface() && g.Surface()->Ready() &&
           g.Surface()->Planet().SameWorld(world::WorldCatalog::Erma()));
    assert(g.IsSurfaceMine() && mineState.active &&
           g.PlanetLocation()->space == world::SpaceKind::Subsurface);
    assert(mineState.player.credits == credits && mineState.player.salvage == 23 &&
           mineState.enemies.front().health == 7 && mineState.survivors.front().combat.health == 61 &&
           mineState.drops.back().actorId + 1 == mineState.nextDropActorId);
    assert(mineState.pistol.GetRounds() == 11 && mineState.pistol.IsReloading() &&
           std::fabs(mineState.pistol.GetReloadProgress() - .3f) < .0001f);
    assert(g.HasCompanion(CompanionId::Ria, CompanionPlace::Mine) && mineState.riaCombat.health == 37 &&
           mineState.riaCombat.pistol.GetRounds() == 11 && mineState.riaCombat.pistol.IsReloading() &&
           std::fabs(mineState.riaCombat.pistol.GetReloadProgress() - .3f) < .0001f);
    assert(w.missions.Status(MissionId::Frontier) == MissionStatus::Paused &&
           GameLevels::Chapter().GetState().generatorInspected);
    assert(w.relayNetwork.Registered("relay_halo_07_0") && w.playerGender == models::Gender::Female &&
           w.playerHair == models::HairStyle::Bun);
    assert(w.residents.front().name == "저장 검사 주민" && w.residents.front().health == 64);
    assert(w.dialogue.events.count("checkpoint/event") &&
           w.dialogue.scenes.at("checkpoint/scene").choice == "accept" &&
           w.dialogue.history.back().text == "다시 불러와도 한글과 완료 기록을 보존한다.");
    assert(w.planetMapRange == 64000 && w.mapRegisteredRelaysOnly && !w.relayPreparing && !w.mouseAiming &&
           !w.mouseFiring);
    assert(CivicJustice::State().infamy == 17 && CivicJustice::State().wanted == 2 &&
           CivicJustice::State().heat == 200);
}
void TestAircraftFlightContracts()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    PrepareCheckpointFixture();
    auto &g = Game::Get();
    const auto plan = g.Surface()->Stream().Snapshot()->planet;
    auto start = g.Surface()->Pose().origin;
    start.altitudeMeters = plan->terrain.Height(start) + 512;
    world::SurfaceSession session(g.Surface()->Planet(), start);
    session.Start(plan);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!session.Ready() && std::chrono::steady_clock::now() < until)
    {
        session.Update({}, std::nullopt, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(session.Ready());
    session.Update({}, std::nullopt, true);
    assert(std::abs(session.Pose().origin.altitudeMeters - start.altitudeMeters) < .0001);
    assert(session.Fly({3, 0, 2}, PlayerAircraft::RadiusMeters(), PlayerAircraft::HeightMeters()));
    assert(G::Distance(session.Planet().radiusMeters, start, session.Pose().origin) > 2.9);
    assert(std::abs(session.Pose().origin.altitudeMeters - start.altitudeMeters - 2) < .0001);
    const double ceiling = session.Pose().origin.altitudeMeters + .8;
    assert(!session.Fly({0, 0, 10}, PlayerAircraft::RadiusMeters(), PlayerAircraft::HeightMeters(),
                        [&](const auto &p) { return p.altitudeMeters > ceiling; }));
    assert(session.Pose().origin.altitudeMeters <= ceiling + .0001);
    PlayerAircraft aircraft;
    aircraft.position = session.Pose().origin;
    aircraft.SetYaw(.7f, session.Origin());
    auto inside = *aircraft.position;
    inside.altitudeMeters += PlayerAircraft::HeightMeters() / 2;
    assert(aircraft.Contains(session, inside));
    inside.altitudeMeters += PlayerAircraft::HeightMeters() + 1;
    assert(!aircraft.Contains(session, inside));
    assert(session.Fly({0, 0, 65}, PlayerAircraft::RadiusMeters(), PlayerAircraft::HeightMeters()));
    const auto global = session.Pose().origin;
    assert(session.Rebase());
    assert(G::Distance(session.Planet().radiusMeters, global, session.Origin().origin) < .0001);
    assert(!session.Fly({0, 0, -1000}, PlayerAircraft::RadiusMeters(), PlayerAircraft::HeightMeters()));
    assert(session.Pose().origin.altitudeMeters >=
           session.Stream().Contact(session.Pose().origin).heightMeters - .05);
    auto &w = g.GetState();
    const auto previousCamera = w.cameraPosition;
    const float previousCameraHeight = w.surfaceCameraHeight;
    w.cameraPosition = {};
    w.surfaceCameraHeight =
        float((g.Surface()->ContactAt(0, 0).heightMeters - g.Surface()->Origin().origin.altitudeMeters) / 2);
    const auto groundView = g.SurfaceTerrainView();
    w.surfaceCameraHeight += 60; // Raise the camera by 120 metres.
    const auto elevatedView = g.SurfaceTerrainView();
    assert(G::Distance(g.Surface()->Planet().radiusMeters, groundView.center, elevatedView.center) > 100);
    const auto localGround = g.Surface()->Local(elevatedView.center);
    const auto projectedGround =
        Drawing::WorldToScreen(float(localGround.x / 2), float(localGround.y / 2), float(localGround.z * 22));
    assert(std::abs(projectedGround.x - kCanvasWidth * .5f) < 32 &&
           std::abs(projectedGround.y - kCanvasHeight * .57f) < 32);
    w.cameraPosition = previousCamera;
    w.surfaceCameraHeight = previousCameraHeight;
    auto &a = g.GetState().aircraft;
    a.position = g.Surface()->Pose().origin;
    a.mounted = true;
    a.grounded = false;
    a.velocity = {3, 0, 0};
    assert(!PlayerAircraftController::CanLand(g));
    PlayerAircraftController::Interact(g);
    assert(!a.landingRequested); // A fast craft cannot queue a landing for later.
    a = {};
    session.Stop();
    g.Reset();
}
void WriteLegacyCheckpoint(const std::filesystem::path &current, const std::filesystem::path &legacy,
                           unsigned version = 1)
{
    std::ifstream stream(current, std::ios::binary);
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>()};
    auto number = [&](size_t offset) {
        std::uint64_t result = 0;
        assert(offset + 8 <= bytes.size());
        for (unsigned i = 0; i < 8; ++i)
        {
            result |= std::uint64_t(bytes[offset + i]) << (8 * i);
        }
        return result;
    };
    const auto schema = number(8);
    assert(schema >= 8 && schema <= 16 &&
           ((version >= 1 && version <= 8) || version == 11 || version == 12 || version == 13 || version == 14 ||
            version == 15));
    const size_t schema15End =
        schema >= 16 ? bytes.size() - 8 - size_t(number(bytes.size() - 8)) : bytes.size();
    const size_t schema14End =
        schema >= 15 ? schema15End - 8 - size_t(number(schema15End - 8)) : schema15End;
    const size_t schema13End =
        schema >= 14 ? schema14End - 8 - size_t(number(schema14End - 8)) : schema14End;
    const size_t schema12End =
        schema >= 13 ? schema13End - 8 - size_t(number(schema13End - 8)) : schema13End;
    const size_t schema8End =
        schema >= 9 ? schema12End - 8 - size_t(number(schema12End - 8)) : schema12End;
    const size_t trackingStart = schema8End - 8 - size_t(number(schema8End - 8));
    const size_t communityStart = trackingStart - 8 - size_t(number(trackingStart - 8));
    const size_t linkStart = communityStart - 8 - size_t(number(communityStart - 8));
    const size_t securityStart = linkStart - 8 - size_t(number(linkStart - 8));
    const size_t partyStart = securityStart - 8 - size_t(number(securityStart - 8));
    const size_t payloadEnd = version == 8   ? schema8End
                              : version == 7 ? trackingStart
                              : version == 6 ? communityStart
                              : version == 5 ? linkStart
                              : version == 4 ? securityStart
                              : version == 3 ? partyStart
                                             : partyStart - 8 - size_t(number(partyStart - 8));
    size_t offset = 40;
    const auto activeLength = number(offset);
    offset += 8 + size_t(activeLength);
    assert(number(offset) == 1);
    offset += 8;
    const auto keyLength = number(offset);
    offset += 8 + size_t(keyLength);
    assert(offset < bytes.size());
    std::vector<unsigned char> previous;
    size_t aircraftBytes = 0;
    if (version == 11)
    {
        assert(schema >= 12);
        // This fixture has exactly one world. The final schema-12 field is its aircraft record:
        // optional flag, SurfacePoint (4 doubles), heading (3), health (8), age (4), grounded flag.
        aircraftBytes = bytes[schema12End - 9] == 0 ? 1 : 1 + 4 * 8 + 3 * 8 + 8 + 4 + 1;
        previous.assign(bytes.begin(), bytes.begin() + schema12End);
        previous.erase(previous.end() - 8 - aircraftBytes, previous.end() - 8);
    }
    else if (version == 12)
    {
        assert(schema >= 13);
        previous.assign(bytes.begin(), bytes.begin() + schema12End);
    }
    else if (version == 13)
    {
        assert(schema >= 14);
        previous.assign(bytes.begin(), bytes.begin() + schema13End);
    }
    else if (version == 14)
    {
        assert(schema >= 15);
        previous.assign(bytes.begin(), bytes.begin() + schema14End);
    }
    else if (version == 15)
    {
        assert(schema >= 16);
        previous.assign(bytes.begin(), bytes.begin() + schema15End);
    }
    else
    {
        previous.assign(bytes.begin(), bytes.begin() + 40);
        previous.insert(previous.end(), bytes.begin() + (version == 1 ? offset : 40),
                        bytes.begin() + payloadEnd);
    }
    auto put = [&](size_t at, std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i)
        {
            previous[at + i] = static_cast<unsigned char>(value >> (8 * i));
        }
    };
    std::uint64_t hash = 14695981039346656037ull;
    if (version == 11)
    {
        put(previous.size() - 8, number(schema12End - 8) - aircraftBytes);
    }
    for (size_t i = 40; i < previous.size(); ++i)
    {
        hash ^= previous[i];
        hash *= 1099511628211ull;
    }
    put(8, version);
    put(24, previous.size() - 40);
    put(32, hash);
    std::ofstream output(legacy, std::ios::binary);
    output.write(reinterpret_cast<const char *>(previous.data()), std::streamsize(previous.size()));
    assert(output.good());
}
void TestActivityObjectives()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    PrepareCheckpointFixture();
    auto &g = Game::Get();
    auto &w = g.GetState();
    const auto fog = mineState.explored;
    const auto credits = mineState.player.credits;
    w.missions = {};
    assert(w.missions.Accept(MissionId::Mara, true));
    assert(ActivityJournal::TrackMission(MissionId::Mara));
    auto goal = ActivityJournal::Current();
    assert(goal.mission == MissionId::Mara && goal.mineTarget &&
           Game::Distance(*goal.mineTarget, mineState.entry) < .001f);
    assert(goal.action.find("출구") != std::string::npos);
    assert(!ActivityJournal::TrackCommunity("unaccepted_region") &&
           !ActivityJournal::TrackMission(MissionId::Count));
    assert(w.missions.Accept(MissionId::Mine, true) && ActivityJournal::TrackMission(MissionId::Mine));
    mineState.quest = MineQuest{};
    assert(mineState.quest.Advance(MineQuestStage::ShowRecord));
    assert(mineState.quest.Advance(MineQuestStage::CheckCityRelay));
    assert(mineState.quest.BeginJourney());
    assert(mineState.quest.Advance(MineQuestStage::TravelToMine));
    for (auto target : {kPowerBypass, kRuinSignal, kObserverGlyph, kCoordinateTerminal})
    {
        const auto stage = mineState.quest.GetStage();
        goal = ActivityJournal::Current();
        assert(goal.mineTarget && Game::Distance(*goal.mineTarget, target) < .001f);
        assert(mineState.quest.GetStage() == stage && mineState.player.credits == credits &&
               mineState.explored == fog);
        assert(mineState.quest.Advance(stage));
    }
    goal = ActivityJournal::Current();
    assert(goal.mineTarget && Game::Distance(*goal.mineTarget, mineState.entry) < .001f);
    w.mapDestinations[size_t(MapRegion::Mine)] = {true, {12.5f, 11.5f}};
    w.planetDestination = g.Surface()->Pose().origin;
    const auto manual = *w.planetDestination;
    assert(MapHud::FocusTrackedObjective() && w.mapVisible && !w.planetMapSurface);
    const auto centered = MapHud::WorldToMap(*goal.mineTarget, true);
    assert(Game::Distance(centered, {489, 400}) < .01f);
    assert(w.mapDestinations[size_t(MapRegion::Mine)].active &&
           Game::Distance(w.mapDestinations[size_t(MapRegion::Mine)].position, {12.5f, 11.5f}) < .001f &&
           G::Distance(g.Surface()->Planet().radiusMeters, manual, *w.planetDestination) < .001 &&
           mineState.explored == fog);
    w.mapVisible = false;
    assert(g.LeaveSurfaceMine());
    assert(ActivityJournal::TrackMission(MissionId::Mara));
    for (bool restored : {false, true})
    {
        w.isPowerRestored = restored;
        goal = ActivityJournal::Current();
        const auto expected =
            g.SurfaceCities()->Point(*g.Surface(), 0, restored ? kMaraPosition : kRelayPosition);
        assert(expected && goal.surfaceTarget &&
               G::Distance(g.Surface()->Planet().radiusMeters, *expected, *goal.surfaceTarget) < .001 &&
               !goal.mineTarget);
    }
    const auto statuses = w.missions.Status(MissionId::Mara);
    w.journalVisible = true;
    w.mapVisible = false;
    w.ui = {};
    const auto previousTracking = ActivityJournal::Current().mission;
    ActivityJournalUi::Select(int(MissionId::Mine));
    assert(w.ui.activityMission == MissionId::Mine && ActivityJournal::Current().mission == previousTracking);
    const auto activityControls = Hud::Controls();
    for (size_t i = 0; i < activityControls.size(); ++i)
    {
        const auto &a = activityControls[i];
        assert(a.position.x >= 64 && a.position.y >= 40 && a.position.x + a.size.x <= 1216 &&
               a.position.y + a.size.y <= 760);
        for (size_t j = i + 1; j < activityControls.size(); ++j)
        {
            const auto &b = activityControls[j];
            assert(a.position.x + a.size.x <= b.position.x || b.position.x + b.size.x <= a.position.x ||
                   a.position.y + a.size.y <= b.position.y || b.position.y + b.size.y <= a.position.y);
        }
    }
    assert(std::any_of(activityControls.begin(), activityControls.end(), [](const UiControl &c) {
        return c.action == UiAction::TrackMission && c.choice == int(MissionId::Mine) && c.enabled;
    }));
    const auto beforePage = w.journalActivityPage;
    ActivityJournalUi::TurnPage(1);
    assert(w.journalActivityPage == beforePage && w.missions.Status(MissionId::Mara) == statuses &&
           ActivityJournal::Current().mission == previousTracking && mineState.player.credits == credits);
    assert(Hud::HandleKey('r') && w.ui.screen == UiScreen::None); // Reading cannot trigger restart.
    w.pressedKeys['w'] = true;
    assert(Hud::HandleKey('w') && !w.pressedKeys['w']);
    ActivityJournalUi::TurnPage(-1);
    assert(w.ui.activityDetailPage == 0);
    w.journalVisible = true;
    assert(MapHud::FocusTrackedObjective() && !w.journalVisible && w.mapVisible);
    const auto onMap = MapHud::PlanetToMap(*goal.surfaceTarget, true);
    assert(Game::Distance(onMap, {488, 400}) < .01f && w.missions.Status(MissionId::Mara) == statuses);
    assert(G::Distance(g.Surface()->Planet().radiusMeters, manual, *w.planetDestination) < .001);
    g.Reset();
}

void TestCommunityTaskTransactions()
{
    using namespace tutorial;
    using Progress = CommunityTaskProgress;
    CommunitySite site;
    int credits = 500;
    assert(!Progress::Measure(site) && !Progress::Cancel(site));
    assert(Progress::Submit(site, credits) == Progress::SubmitResult::NotReady && credits == 500);
    assert(Progress::Accept(site) && !Progress::Accept(site) && Progress::IsActive(site));
    assert(Progress::Cancel(site) && !Progress::Cancel(site) && !Progress::IsActive(site));
    assert(Progress::Accept(site) && Progress::Measure(site) && !Progress::Measure(site));
    assert(!Progress::Cancel(site)); // A measured record is never discarded by an obsolete cancel callback.
    credits = game::PlayerStats::kMaximumCredits;
    assert(Progress::Submit(site, credits) == Progress::SubmitResult::WalletFull &&
           site.task == Progress::Measured && credits == game::PlayerStats::kMaximumCredits);
    credits = game::PlayerStats::kMaximumCredits - Progress::RewardCredits + 1;
    const auto full = credits;
    assert(Progress::Submit(site, credits) == Progress::SubmitResult::WalletFull && credits == full);
    credits = -1;
    assert(Progress::Submit(site, credits) == Progress::SubmitResult::NotReady && credits == -1);
    credits = game::PlayerStats::kMaximumCredits - Progress::RewardCredits;
    assert(Progress::Submit(site, credits) == Progress::SubmitResult::Granted &&
           site.task == Progress::Paid && credits == game::PlayerStats::kMaximumCredits);
    assert(Progress::Submit(site, credits) == Progress::SubmitResult::AlreadyPaid &&
           credits == game::PlayerStats::kMaximumCredits && !Progress::IsActive(site));
    assert(!Progress::Accept(site) && !Progress::Cancel(site) && !Progress::Measure(site));
    site.task = 999;
    assert(Progress::Submit(site, credits) == Progress::SubmitResult::NotReady && !Progress::IsActive(site));
}

void TestActivityTrackingContracts()
{
    using namespace tutorial;
    const auto planet = world::WorldCatalog::Erma();
    world::RelayNetwork key;
    key.Bind(planet);
    PlanetCommunityState community;
    community.worldKey = key.WorldKey();
    // Only identity/status fields participate in this metadata contract; geometry is validated separately.
    for (const auto *id : {"site_z", "site_a", "site_c", "site_b"})
    {
        CommunitySite site;
        site.regionId = id;
        site.task = 1;
        community.sites.push_back(site);
    }
    GameState state;
    assert(state.missions.Accept(MissionId::Mara, true));
    assert(state.missions.Accept(MissionId::Frontier, true));
    ActivityTrackingState chosen{MissionId::Mara, key.WorldKey(), "site_z"};
    ActivityJournal::RestoreTracking(state, chosen, community, planet);
    auto saved = ActivityJournal::CaptureTracking(state, community, planet);
    assert(saved.mission == MissionId::Mara && saved.communityRegion == "site_z" &&
           saved.communityWorldKey == key.WorldKey());
    assert(state.journalLocalActivities && state.journalActivityPage == 1);
    assert(state.missions.Active(MissionId::Mara) && state.missions.Active(MissionId::Frontier));
    const auto original = state;
    chosen.communityWorldKey = "foreign_planet";
    ActivityJournal::RestoreTracking(state, chosen, community, planet);
    assert(state.trackedCommunityRegion.empty() && state.missions.Tracked() == MissionId::Mara);
    assert(state.missions.Active(MissionId::Mara) && state.missions.Active(MissionId::Frontier));
    chosen.communityWorldKey = key.WorldKey();
    for (unsigned status : {0u, 3u})
    {
        community.sites.front().task = status;
        ActivityJournal::RestoreTracking(state, chosen, community, planet);
        assert(state.trackedCommunityRegion.empty() && community.sites.front().task == status);
    }
    community.sites.front().task = 2;
    ActivityJournal::RestoreTracking(state, chosen, community, planet);
    assert(state.trackedCommunityRegion == "site_z" && community.sites.front().task == 2);
    assert(state.missions.Pause(MissionId::Mara));
    ActivityJournal::RestoreTracking(state, chosen, community, planet);
    assert(state.missions.Tracked() == MissionId::Count &&
           state.missions.Status(MissionId::Mara) == MissionStatus::Paused);
    chosen.communityRegion = "missing_site";
    chosen.mission = MissionId(999);
    ActivityJournal::RestoreTracking(state, chosen, community, planet);
    assert(state.trackedCommunityRegion.empty() && state.missions.Tracked() == MissionId::Count);
    assert(ActivityJournal::CaptureTracking(original, community, world::PlanetProfiles::All()[1])
               .communityRegion.empty());
    assert(ActivityJournal::CaptureTracking(original, community, world::PlanetProfiles::All()[1]).mission ==
           MissionId::Count);
}
void TestActivityTrackingPersistence()
{
    using namespace tutorial;
    PrepareCheckpointFixture();
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(w.missions.Accept(MissionId::Mara, true) && w.missions.Accept(MissionId::Frontier, true));
    w.isSideQuestAccepted = true;
    assert(ActivityJournal::TrackMission(MissionId::Mara));
    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("tracking-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / "selected.sav", legacy = folder / "previous7.sav",
               legacy11 = folder / "previous11.sav";
    const auto parked = g.Surface()->Pose().origin;
    w.car.position = parked;
    w.car.SetYaw(.6f, g.Surface()->Origin());
    w.car.health = 71;
    const auto savedHeading = w.car.heading;
    const bool savedOk = SaveSystem::Save(file);
    if (!savedOk)
    {
        std::cerr << "SaveSystem::Save failed: " << SaveSystem::Message() << "\n";
    }
    assert(savedOk && SaveSystem::CheckFile(file));
    assert(w.missions.Tracked() == MissionId::Mara);
    WriteLegacyCheckpoint(file, legacy, 7);
    WriteLegacyCheckpoint(file, legacy11, 11);
    assert(SaveSystem::CheckFile(legacy11));
    assert(SaveSystem::CheckFile(legacy) && w.missions.Tracked() == MissionId::Mara);
    assert(ActivityJournal::TrackMission(MissionId::Frontier));
    assert(SaveSystem::Load(file));
    SaveSystem::Cancel();
    assert(w.missions.Tracked() == MissionId::Frontier);
    assert(SaveSystem::Load(file));
    WaitForCheckpointLoad();
    assert(w.missions.Tracked() == MissionId::Mara && ActivityJournal::Current().mission == MissionId::Mara);
    assert(w.car.position && !w.car.mounted && w.car.speed == 0 && w.car.health == 71);
    assert(world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, parked, *w.car.position) <
           .001);
    assert(world::SurfaceGeometry::Dot(savedHeading, w.car.heading) > .9999);
    assert(SaveSystem::Load(legacy11));
    WaitForCheckpointLoad();
    assert(w.car.position && w.car.health == 71 && !w.aircraft.position);
    assert(w.missions.Active(MissionId::Frontier) && w.missions.Active(MissionId::Mara));
    assert(SaveSystem::Load(legacy));
    WaitForCheckpointLoad();
    assert(w.missions.Tracked() == MissionId::Count && w.missions.Active(MissionId::Mara) &&
           w.missions.Active(MissionId::Frontier) && w.trackedCommunityRegion.empty());
    assert(!w.car.position);
    g.Reset();
    std::filesystem::remove(file);
    std::filesystem::remove(legacy);
    std::filesystem::remove(legacy11);
    std::filesystem::remove(folder);
}

void TestWorldCheckpoints()
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &w = g.GetState();
    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("checkpoint-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / std::filesystem::u8path("월드.sav"), bad = folder / "incompatible.sav";
    PrepareCheckpointFixture();
    const auto fog = mineState.explored;
    const auto bike = *g.SurfaceBike();
    // Ria and Mara are separate landmarks: the ordinary resident vector contains 98, not 100.
    assert(Overworld::ResidentPopulation() == 98 && w.residents.size() == 98);
    for (int city = 0; city < Overworld::kCityCount; ++city)
    {
        assert(std::count_if(w.residents.begin(), w.residents.end(), [&](const auto &r) {
                   return r.cityIndex == city;
               }) == Overworld::ResidentPopulation(city));
    }
    const bool savedOk = SaveSystem::Save(file);
    if (!savedOk)
    {
        std::cerr << "SaveSystem::Save failed: " << SaveSystem::Message() << "\n";
    }
    assert(savedOk && SaveSystem::CheckFile(file));
    const auto legacy = folder / "schema1.sav";
    WriteLegacyCheckpoint(file, legacy);
    assert(SaveSystem::CheckFile(legacy));
    const auto legacy2 = folder / "schema2.sav";
    WriteLegacyCheckpoint(file, legacy2, 2);
    assert(SaveSystem::CheckFile(legacy2));
    const auto legacy3 = folder / "schema3.sav";
    WriteLegacyCheckpoint(file, legacy3, 3);
    assert(SaveSystem::CheckFile(legacy3));
    const auto legacy4 = folder / "schema4.sav";
    WriteLegacyCheckpoint(file, legacy4, 4);
    assert(SaveSystem::CheckFile(legacy4));
    const auto legacy5 = folder / "schema5.sav";
    WriteLegacyCheckpoint(file, legacy5, 5);
    assert(SaveSystem::CheckFile(legacy5));
    const auto legacy6 = folder / "version6.sav";
    WriteLegacyCheckpoint(file, legacy6, 6);
    assert(SaveSystem::Load(legacy6));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture();
    const auto legacy7 = folder / "version7.sav";
    WriteLegacyCheckpoint(file, legacy7, 7);
    assert(SaveSystem::Load(legacy7));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture();
    assert(w.missions.Tracked() == MissionId::Count && w.trackedCommunityRegion.empty());
    const auto legacy8 = folder / "version8.sav";
    WriteLegacyCheckpoint(file, legacy8, 8);
    assert(SaveSystem::CheckFile(legacy8));
    const auto malformed = folder / "tracking-footer.sav";
    const auto malformedCommunity = folder / "community-footer.sav";
    auto breakFooter = [&](const auto &source, const auto &destination) {
        std::ifstream input(source, std::ios::binary);
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),
                                         std::istreambuf_iterator<char>()};
        std::fill(bytes.end() - 8, bytes.end(), 0);
        std::uint64_t hash = 14695981039346656037ull;
        for (size_t i = 40; i < bytes.size(); ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        for (unsigned i = 0; i < 8; ++i)
        {
            bytes[32 + i] = static_cast<unsigned char>(hash >> (i * 8));
        }
        std::ofstream output(destination, std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    };
    breakFooter(legacy8, malformed);
    breakFooter(legacy7, malformedCommunity);
    assert(!SaveSystem::CheckFile(malformed) && !SaveSystem::CheckFile(malformedCommunity));
    const auto creditsBefore = mineState.player.credits;
    assert(!SaveSystem::Load(malformed) && !SaveSystem::Loading() &&
           mineState.player.credits == creditsBefore);
    mineState.player.credits = 2701;
    assert(SaveSystem::Save(file) &&
           std::filesystem::exists(std::filesystem::path(file.wstring() + L".bak")));
    g.Reset();
    w.isPowerRestored = true;
    const bool generated = GameLevels::Chapter().GetState().generated;
    assert(SaveSystem::Load(file) && SaveSystem::Loading());
    SaveSystem::Cancel();
    assert(!SaveSystem::Loading() && !g.Surface() && w.isPowerRestored &&
           GameLevels::Chapter().GetState().generated == generated);
    assert(SaveSystem::Load(file) && !SaveSystem::Load(file));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture(2701);
    assert(mineState.explored == fog && world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters,
                                                                         bike, *g.SurfaceBike()) < 1.e-6);
    assert(
        Dialogue::Start("checkpoint/incomplete", {{"line", "리아", "기록", "대화를 끝내고 저장한다.", {}}}));
    assert(!SaveSystem::Save(file));
    ReadDialogue();
    // Corruption falls back to the previous valid checkpoint without rewriting the damaged primary.
    {
        std::fstream stream(file, std::ios::binary | std::ios::in | std::ios::out);
        stream.put('X');
        assert(stream.good());
    }
    assert(!SaveSystem::CheckFile(file));
    assert(SaveSystem::Load(file));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture();
    assert(!SaveSystem::CheckFile(file));
    // A mismatching schema with no backup never changes the live world.
    std::filesystem::copy_file(std::filesystem::path(file.wstring() + L".bak"), bad);
    {
        std::fstream stream(bad, std::ios::binary | std::ios::in | std::ios::out);
        stream.seekp(8);
        stream.put(char(99));
        assert(stream.good());
    }
    const auto position = g.Surface()->Pose().origin;
    mineState.player.credits = 4444;
    assert(!SaveSystem::Load(bad) && !SaveSystem::Loading() && mineState.player.credits == 4444 &&
           world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, position,
                                            g.Surface()->Pose().origin) < 1.e-6);
    assert(SaveSystem::Save(bad));
    const auto opaqueBackup = std::filesystem::path(bad.wstring() + L".bak");
    {
        std::ifstream stream(opaqueBackup, std::ios::binary);
        stream.seekg(8);
        assert(stream.get() == 99);
    }
    assert(g.LeaveSurfaceMine());
    CivicJustice::State().wanted = 0;
    CivicJustice::State().heat = 0;
    const auto plan = g.Surface()->Stream().Snapshot()->planet;
    const auto *from = world::RelayNetwork::Find(*plan, "relay_halo_07_0"),
               *to = world::RelayNetwork::Find(*plan, "relay_frontier_0");
    assert(from && to);
    auto registerAt = [&](const world::RelayCandidate &relay) {
        w.mapVisible = false;
        assert(g.RelocateSurfaceSurvey(relay.location.position));
        RelayTravel::Interact();
    };
    registerAt(*to);
    registerAt(*from);
    const auto departure = g.Surface()->Pose().origin;
    const auto transit = folder / "transit.sav";
    assert(RelayTravel::Begin(to->id) && SaveSystem::Save(transit));
    RelayTravel::Cancel();
    assert(SaveSystem::Load(transit));
    WaitForCheckpointLoad();
    assert(!w.relayPreparing && !w.mapVisible &&
           world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, departure,
                                            g.Surface()->Pose().origin) < 1.e-6);
    assert(SaveSystem::Load(legacy5));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture();
    assert(SaveSystem::Load(legacy4));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture();
    assert(SaveSystem::Load(legacy3));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture();
    assert(SaveSystem::Load(legacy2));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture();
    assert(SaveSystem::Load(legacy));
    WaitForCheckpointLoad();
    VerifyCheckpointFixture();
    g.Reset();
    std::filesystem::remove(legacy);
    std::filesystem::remove(legacy2);
    std::filesystem::remove(legacy3);
    std::filesystem::remove(legacy4);
    std::filesystem::remove(legacy5);
    std::filesystem::remove(legacy6);
    std::filesystem::remove(legacy7);
    std::filesystem::remove(legacy8);
    std::filesystem::remove(malformedCommunity);
    std::filesystem::remove(malformed);
    std::filesystem::remove(file);
    std::filesystem::remove(std::filesystem::path(file.wstring() + L".bak"));
    std::filesystem::remove(bad);
    std::filesystem::remove(opaqueBackup);
    std::filesystem::remove(transit);
    std::filesystem::remove(folder);
}

void TestCityTrafficHandoff()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    PrepareCheckpointFixture();
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(g.LeaveSurfaceMine());
    auto plan = g.Surface()->Stream().Snapshot()->planet;
    auto *transit = g.SurfaceTraffic();
    auto portal = std::find_if(transit->Routes().begin(), transit->Routes().end(),
                               [](const auto &r) { return r.portalCity >= 0 && r.entry; });
    if (portal == transit->Routes().end())
    {
        // All 5 historic cities (including halo_07) now run on unified Rev 5 procedural urban streets/traffic.
        assert(plan->cityCores);
        for (int i = 0; i < Overworld::kCityCount; ++i)
        {
            const auto *core = plan->cityCores->Find(Overworld::Cities()[size_t(i)].id);
            assert(core && core->roads.empty());
        }
        assert(!transit->Routes().empty());
        return;
    }
    const auto entry = *portal;
    auto exitIterator = std::find_if(transit->Routes().begin(), transit->Routes().end(), [&](const auto &r) {
        return r.portalCity == entry.portalCity && !r.entry;
    });
    assert(exitIterator != transit->Routes().end());
    const auto exit = *exitIterator;
    const int city = entry.portalCity;
    const auto &net = RoadNetwork::Get();
    assert(g.RelocateSurfaceSurvey(entry.points.front()));
    g.Update(0);
    CivicJustice::State().wanted = 0;
    CivicJustice::State().heat = 0;
    auto state = transit->State();
    auto actor = TrafficFixtureActor(*transit, *plan, entry, entry.lengthMeters - .01, 37);
    actor.health = 91;
    state.actors = {actor};
    state.nextId = 38;
    state.spawnSeconds = .75f;
    *transit = PlanetTraffic(state);
    transit->Bind(plan);
    transit->Update(w, *g.Surface(), .05f, true);
    assert(transit->State().actors.front().city == city && transit->Links().size() == 1);
    auto *traffic = &g.SurfaceCities()->Traffic(city);
    auto findCar = [&]() {
        return std::find_if(traffic->Vehicles().begin(), traffic->Vehicles().end(),
                            [](const auto &v) { return v.globalId == 37; });
    };
    auto car = findCar();
    assert(car != traffic->Vehicles().end() && car->health == 91 && car->id % 4 == 37 % 4);
    const auto position = *g.SurfaceCities()->Point(*g.Surface(), city, car->position);
    assert(G::Distance(g.Surface()->Planet().radiusMeters, position,
                       transit->State().actors.front().position) < .01);
    const auto before = car->position;
    g.SurfaceCities()->Observe(w, *g.Surface(), city, [&] { traffic->Update(w, .1f); });
    transit->SyncCity(city, *traffic, *g.Surface());
    car = findCar();
    assert(car != traffic->Vehicles().end() && Game::Distance(before, car->position) > .0001f);
    assert(transit->Damage(37, 5, true) && findCar()->health == 86 &&
           transit->State().actors.front().health == 86 && CivicJustice::State().heat == 0);
    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("traffic-links-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / "city-owned.sav";
    assert(SaveSystem::Save(file) && SaveSystem::Load(file));
    WaitForCheckpointLoad();
    transit = g.SurfaceTraffic();
    traffic = &g.SurfaceCities()->Traffic(city);
    assert(transit->Links().size() == 1 && findCar() != traffic->Vehicles().end() &&
           findCar()->health == 86 && transit->State().actors.front().city == city);
    // Simulate the same physical actor reaching the egress lane, without manufacturing another car.
    auto &cars = const_cast<std::vector<TrafficVehicle> &>(traffic->Vehicles());
    cars.erase(std::remove_if(cars.begin(), cars.end(), [](const auto &v) { return v.globalId != 37; }),
               cars.end());
    auto &leaving = cars.front();
    leaving.lane = exit.lane;
    leaving.distance = net.lanes[size_t(exit.lane)].length;
    leaving.position = net.lanes[size_t(exit.lane)].end;
    leaving.turnDistance = -1;
    const auto departing = *g.SurfaceCities()->Point(*g.Surface(), city, leaving.position);
    assert(transit->Depart(city, leaving) && leaving.departed);
    cars.clear(); // TrafficSystem::Step removes this relinquished projection before contacts/rendering.
    transit->SyncCity(city, *traffic, *g.Surface());
    const auto &roadActor = transit->State().actors.front();
    assert(roadActor.id == 37 && roadActor.city == -1 && roadActor.health == 86 && transit->Links().empty() &&
           roadActor.routeId == exit.id &&
           G::Distance(g.Surface()->Planet().radiusMeters, departing, roadActor.position) < .01);
    // A blocked receiving lane keeps the actor at its last safe point, rather than teleporting onto a car.
    state = transit->State();
    actor = TrafficFixtureActor(*transit, *plan, entry, entry.lengthMeters - .01, 37);
    actor.health = 86;
    state.actors = {actor};
    state.spawnSeconds = .75f;
    *transit = PlanetTraffic(state);
    transit->Bind(g.Surface()->Stream().Snapshot()->planet);
    TrafficVehicle blocker;
    blocker.id = 0;
    blocker.lane = entry.lane;
    blocker.position = net.lanes[size_t(entry.lane)].start;
    cars.push_back(blocker);
    const auto waiting = transit->State().actors.front().position;
    transit->Update(w, *g.Surface(), .05f, true);
    assert(transit->State().actors.front().city == -1 && transit->Links().empty() &&
           G::Distance(g.Surface()->Planet().radiusMeters, waiting,
                       transit->State().actors.front().position) < .001);
    cars.clear();
    transit->Update(w, *g.Surface(), .05f, true);
    assert(transit->State().actors.front().city == city && findCar() != traffic->Vehicles().end());
    assert(transit->Damage(37, 999, true) && findCar()->health == 0 && CivicJustice::State().heat == 50);
    assert(!transit->Damage(37, 999, true) && CivicJustice::State().heat == 50);
    const auto json = monitoring::WorldSnapshot::Build(1, "handoff-test");
    assert(json.find("owner_city") != std::string::npos && json.find("city_owned") != std::string::npos);
    g.Reset();
    std::filesystem::remove(file);
    std::filesystem::remove(folder);
}

void TestPlanetCommunity()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    PrepareCheckpointFixture();
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(g.LeaveSurfaceMine());
    CivicJustice::State().wanted = 0;
    CivicJustice::State().heat = 0;
    CivicJustice::State().infamy = 0;
    const auto plan = g.Surface()->Stream().Snapshot()->planet;
    auto *community = g.SurfaceCommunity();
    assert(community);
    unsigned attempts = 0;
    for (const auto &region : plan->regions)
    {
        if (region.center.space != world::SpaceKind::Surface ||
            (region.definition.kind != world::RegionKind::Settlement &&
             region.definition.kind != world::RegionKind::Outpost) ||
            (plan->cityCores && plan->cityCores->Find(region.definition.id)))
        {
            continue;
        }
        if (++attempts > 12)
        {
            break;
        }
        assert(g.RelocateSurfaceSurvey(region.access.position));
        g.Update(0);
        for (int i = 0; i < 110 && community->State().sites.empty(); ++i)
        {
            community->Update(.05f, true);
        }
        if (!community->State().sites.empty())
        {
            break;
        }
    }
    assert(!community->State().sites.empty());
    const auto initial = community->State();
    const auto regionId = initial.sites.front().regionId;
    const auto contact = initial.sites.front().contact, target = initial.sites.front().target;
    const auto citizen = initial.sites.front().residents.back().id;
    const auto mainStage = mineState.quest.GetStage();
    PlanetCommunity::Validate(initial, g.Surface()->Planet());
    PlanetCommunity copied(initial);
    copied.Bind(plan);
    auto passing = initial;
    bool sidestep = false;
    for (auto &person : passing.sites.front().residents)
    {
        const auto frame = G::Frame(person.position);
        const auto axis = G::ToLocal(frame, person.path[1], g.Surface()->Planet().radiusMeters);
        const double length = std::hypot(axis.x, axis.y);
        if (length < .001)
        {
            continue;
        }
        for (double offset : {-.2, .2})
        {
            auto candidate = G::Advance(frame, -axis.y / length * offset, axis.x / length * offset,
                                        g.Surface()->Planet().radiusMeters)
                                 .origin;
            candidate.altitudeMeters = plan->terrain.Height(candidate);
            if (g.Surface()->LandingClear(candidate, .35))
            {
                person.position = candidate;
                sidestep = true;
                break;
            }
        }
        if (sidestep)
        {
            break;
        }
    }
    assert(sidestep);
    PlanetCommunity passingRestored(passing);
    passingRestored.Bind(plan); // A physical sidestep must survive validation and a later visit.
    auto rejected = initial;
    rejected.sites.push_back(rejected.sites.front());
    bool caught = false;
    try
    {
        PlanetCommunity::Validate(rejected, g.Surface()->Planet());
    }
    catch (const std::invalid_argument &)
    {
        caught = true;
    }
    assert(caught);
    const auto personPosition = community->State().sites.front().residents.back().position;
    const auto nearby = G::Advance(G::Frame(personPosition), 0, 4, g.Surface()->Planet().radiusMeters).origin;
    assert(g.RelocateSurfaceSurvey(nearby));
    // Isolate one real saved community actor; the sweep must hit even when both endpoints miss.
    auto collisionFixture = initial;
    collisionFixture.sites.resize(1);
    for (auto &person : collisionFixture.sites.front().residents)
    {
        person.health = 0;
    }
    collisionFixture.sites.front().residents.back().health = 100;
    collisionFixture.sites.front().residents.back().hitFlash = 0;
    *community = PlanetCommunity(collisionFixture);
    community->Bind(plan);
    const auto personLocal = g.Surface()->Local(personPosition);
    auto from = g.Surface()->PointAt(personLocal.x - 6, personLocal.y);
    auto to = g.Surface()->PointAt(personLocal.x + 6, personLocal.y);
    from.altitudeMeters = to.altitudeMeters = personPosition.altitudeMeters;
    w.isBikeMounted = true;
    w.bikeYaw = 0;
    w.bikeSpeed = 12;
    w.bikeVelocity = {12, 0};
    PlanetSecurity::Project();
    const auto stopped = BodyCollision::SurfaceVehicleContacts(from, to, 0, 12);
    assert(stopped.x < personLocal.x / 2 - .5 &&
           community->State().sites.front().residents.back().health < 100);
    assert(CivicJustice::State().wanted == 0 && CivicJustice::State().heat == 0);
    assert(!community->Blocked(personPosition, .32)); // Lethal impact leaves no invisible corpse wall.
    *community = PlanetCommunity(collisionFixture);
    community->Bind(plan);
    assert(community->Blocked(personPosition, .32) && !community->Blocked(personPosition, .32, false));
    assert(community->Blocked(contact, .32, false)); // A terminal remains solid for a mounted rider.
    from.altitudeMeters += 10;
    to.altitudeMeters += 10;
    const auto overpass = BodyCollision::SurfaceVehicleContacts(from, to, 0, 12);
    assert(std::abs(overpass.x - float(g.Surface()->Local(to).x / 2)) < .001f);
    assert(community->State().sites.front().residents.back().health == 100);
    const auto escape = BodyCollision::SurfaceVehicleContacts(
        personPosition, g.Surface()->PointAt(personLocal.x - 6, personLocal.y), 0, .65f);
    assert(escape.x < personLocal.x / 2 - 2.9 &&
           community->State().sites.front().residents.back().health == 100);
    w.isBikeMounted = false;
    w.bikeSpeed = 0;
    w.bikeVelocity = {};
    auto visionFixture = initial;
    for (auto &site : visionFixture.sites)
    {
        for (auto &person : site.residents)
        {
            person.perceptionCooldown = 0;
        }
    }
    *community = PlanetCommunity(visionFixture);
    community->Bind(plan);
    // Put the witness in a known, unobstructed viewing fixture rather than relying on omniscient infamy.
    const auto viewer = G::Advance(G::Frame(personPosition), 0, 1, g.Surface()->Planet().radiusMeters).origin;
    assert(g.RelocateSurfaceSurvey(viewer));
    assert(CivicJustice::ClearLine(PlanetSecurity::Local(personPosition),
                                   PlanetSecurity::Local(g.Surface()->Pose().origin)));
    CivicJustice::State().infamy = 40;
    community->Update(.05f, true);
    assert(community->State().sites.front().residents.back().fearSeconds > 0);
    assert(community->State().sites.front().residents.back().threat.Visible());
    const auto rememberedThreat = *community->State().sites.front().residents.back().lastThreat;
    CivicJustice::State().infamy = 0;
    const auto position = community->State().sites.front().residents.back().position;
    w.isPaused = true;
    community->Update(.05f, true);
    w.isPaused = false;
    assert(G::Distance(g.Surface()->Planet().radiusMeters, position,
                       community->State().sites.front().residents.back().position) < .0001);
    community->Update(.05f, false);
    const auto &witness = community->State().sites.front().residents.back();
    assert(witness.threat.Known() && !witness.threat.Visible() &&
           G::Distance(g.Surface()->Planet().radiusMeters, *witness.lastThreat, rememberedThreat) < .0001);
    for (int frame = 0; frame < 122; ++frame)
    {
        community->Update(.05f, false);
    }
    assert(!community->State().sites.front().residents.back().threat.Known());
    auto read = [&](size_t choice) {
        int pages = 0;
        while (Dialogue::Active())
        {
            assert(++pages < 200);
            if (!w.dialogue.pages[w.dialogue.page].choices.empty())
            {
                assert(Dialogue::Select(choice));
            }
            assert(Dialogue::Advance());
        }
    };
    assert(g.RelocateSurfaceSurvey(contact));
    assert(community->Nearby() == InteractionTarget::Community);
    community->Interact();
    read(1); // Discovering a settlement never accepts work automatically.
    assert(community->State().sites.front().task == 0);
    assert(!ActivityJournal::TrackCommunity(regionId)); // Seeing an offer does not accept it remotely.
    community->Interact();
    read(0);
    assert(community->State().sites.front().task == 1 && !community->Journal().empty());
    assert(ActivityJournal::TrackCommunity(regionId) && Game::Get().FocusedMission() == MissionId::Count);
    const auto activityKey = w.trackedCommunityWorldKey;
    w.trackedCommunityWorldKey = "different_world";
    assert(!ActivityJournal::TrackedCommunity());
    ActivityJournal::Current(); // A query must not repair or mutate a foreign selection.
    assert(w.trackedCommunityWorldKey == "different_world");
    ActivityJournal::Refresh();
    assert(w.trackedCommunityRegion.empty() && w.trackedCommunityWorldKey.empty());
    assert(ActivityJournal::TrackCommunity(regionId) && w.trackedCommunityWorldKey == activityKey);
    auto objective = ActivityJournal::Current();
    assert(objective.communityRegion == regionId && objective.surfaceTarget &&
           G::Distance(g.Surface()->Planet().radiusMeters, *objective.surfaceTarget, target) < .001);
    community->Interact();
    read(1);
    assert(community->State().sites.front().task == 0 && !ActivityJournal::TrackedCommunity() &&
           w.trackedCommunityRegion.empty());
    community->Interact();
    read(0);
    assert(ActivityJournal::TrackCommunity(regionId));
    w.journalVisible = true;
    w.journalLocalActivities = true;
    auto controls = Hud::Controls();
    const auto selected = std::find_if(controls.begin(), controls.end(),
                                       [](const auto &c) { return c.action == UiAction::TrackCommunity; });
    assert(selected != controls.end() && selected->enabled);
    for (size_t i = 0; i < controls.size(); ++i)
    {
        for (size_t j = i + 1; j < controls.size(); ++j)
        {
            const auto &a = controls[i], &b = controls[j];
            assert(a.position.x + a.size.x <= b.position.x || b.position.x + b.size.x <= a.position.x ||
                   a.position.y + a.size.y <= b.position.y || b.position.y + b.size.y <= a.position.y);
        }
    }
    assert(selected->label.find('\n') != std::string::npos &&
           selected->label.find("\\n") == std::string::npos);
    const auto show = std::find_if(controls.begin(), controls.end(),
                                   [](const auto &c) { return c.action == UiAction::ViewActivity; });
    assert(show != controls.end() && show->enabled);
    w.cursorInsideViewport = true;
    w.cursorPosition = {show->position.x + show->size.x * .5f, show->position.y + show->size.y * .5f};
    assert(Hud::HandlePointer(0, true) && Hud::HandlePointer(0, false));
    assert(w.mapVisible && !w.journalVisible && ActivityJournal::TrackedCommunity());
    w.mapVisible = false;
    const int credits = mineState.player.credits;
    assert(g.RelocateSurfaceSurvey(target));
    community->Interact();
    assert(community->State().sites.front().task == 2 && mineState.player.credits == credits);
    objective = ActivityJournal::Current();
    assert(objective.surfaceTarget &&
           G::Distance(g.Surface()->Planet().radiusMeters, *objective.surfaceTarget, contact) < .001 &&
           objective.action.find("제출") != std::string::npos);
    assert(mineState.quest.GetStage() == mainStage);
    const auto activityJson = monitoring::WorldSnapshot::Build(1, "tracked-activity-test");
    assert(activityJson.find("\"tracked_activity\"") != std::string::npos &&
           activityJson.find("\"community_region\":\"" + regionId + "\"") != std::string::npos);
    {
        const auto focusFolder =
            std::filesystem::path("x64/RevisionChecks") /
            ("community-focus-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto focusFile = focusFolder / "selected.sav";
        const auto worldKey = w.trackedCommunityWorldKey;
        assert(SaveSystem::Save(focusFile) && SaveSystem::CheckFile(focusFile));
        w.trackedCommunityRegion.clear();
        w.trackedCommunityWorldKey.clear();
        assert(SaveSystem::Load(focusFile));
        WaitForCheckpointLoad();
        community = g.SurfaceCommunity();
        assert(community && community->State().sites.front().task == 2 &&
               w.trackedCommunityRegion == regionId && w.trackedCommunityWorldKey == worldKey &&
               w.journalLocalActivities && ActivityJournal::TrackedCommunity());
        objective = ActivityJournal::Current();
        assert(objective.surfaceTarget &&
               G::Distance(g.Surface()->Planet().radiusMeters, *objective.surfaceTarget, contact) < .001 &&
               mineState.player.credits == credits);
        std::filesystem::remove(focusFile);
        std::filesystem::remove(focusFolder);
    }
    assert(g.RelocateSurfaceSurvey(contact));
    mineState.player.credits = game::PlayerStats::kMaximumCredits;
    community->Interact();
    assert(community->State().sites.front().task == CommunityTaskProgress::Measured &&
           mineState.player.credits == game::PlayerStats::kMaximumCredits &&
           ActivityJournal::TrackedCommunity());
    assert(w.noticeText.find("정산 보류") != std::string::npos &&
           w.noticeText.find("소지 한도") != std::string::npos);
    mineState.player.credits = credits;
    community->Interact();
    assert(community->State().sites.front().task == 3 && mineState.player.credits == credits + 100 &&
           mineState.quest.GetStage() == mainStage);
    assert(!ActivityJournal::TrackedCommunity() && w.trackedCommunityRegion.empty() &&
           !ActivityJournal::TrackCommunity(regionId));
    community->Interact();
    read(0);
    assert(mineState.player.credits == credits + 100);
    assert(community->Damage(citizen, 7, false) && CivicJustice::State().heat == 0);
    assert(community->Damage(citizen, 5, true) && CivicJustice::State().heat > 0);
    CivicJustice::State().wanted = 0;
    CivicJustice::State().heat = 0;
    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("community-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / "residents.sav";
    assert(SaveSystem::Save(file) && SaveSystem::Load(file));
    WaitForCheckpointLoad();
    community = g.SurfaceCommunity();
    assert(community && community->State().sites.front().regionId == regionId &&
           community->State().sites.front().task == 3 &&
           community->State().sites.front().residents.back().health == 88 &&
           community->State().sites.front().residents.back().id == citizen &&
           mineState.player.credits == credits + 100);
    const auto json = monitoring::WorldSnapshot::Build(1, "community-test");
    assert(json.find("task_state") != std::string::npos && json.find("npc_community") != std::string::npos);
    g.Reset();
    std::filesystem::remove(file);
    std::filesystem::remove(folder);
}

void TestPlanetSecurity()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    PrepareCheckpointFixture();
    auto &g = Game::Get();
    auto &w = g.GetState();
    auto &law = CivicJustice::State();
    assert(g.LeaveSurfaceMine());
    auto plan = g.Surface()->Stream().Snapshot()->planet;
    assert(g.RelocateSurfaceSurvey(plan->Region("halo_07").access.position));
    g.Update(0);
    law.wanted = 0;
    law.heat = 0;
    law.identified = false;
    const float reputation = law.infamy;
    PlanetSecurity::Report(Offense::Discharge, g.Surface()->Pose().origin);
    PlanetSecurity::Report(Offense::RecklessDriving, g.Surface()->Pose().origin);
    PlanetSecurity::Report(Offense::VehicleDamage, g.Surface()->Pose().origin);
    assert(law.wanted == 0 && law.heat == 0 && law.infamy == reputation);
    PlanetSecurity::Report(Offense::Assault, g.Surface()->Pose().origin);
    assert(law.wanted == 1 && !law.identified && law.pendingInfamy > 0 && law.infamy == reputation);
    for (int i = 0; i < 10 && law.officers.empty(); ++i)
    {
        PlanetSecurity::Spawn();
    }
    assert(!law.officers.empty() && law.responseStarted && !PlanetSecurity::State().officers.empty());
    assert(law.officers.front().threat.Known() && !law.officers.front().threat.Visible() &&
           law.officers.front().threat.Remaining() <= ai::PerceptionSettings::kReportMemorySeconds &&
           law.officers.front().health == SecurityResponse::ForTier(1, mineState.player.level).health);
    PlanetSecurity::Project();
    const auto officerId = law.officers.front().id;
    const auto policePosition = PlanetSecurity::State().officers.front().position;
    const auto oldOrigin = g.Surface()->Origin();
    const float elevation =
        PlanetSecurity::Elevation(g.Surface()->Pose().origin) + models::WorldScale::aimHeightPixels / 44;
    law.bullets.push_back({w.playerPosition, {1, 0}, 34, 30, 18, true, false, elevation});
    const auto shotPosition =
        G::FromLocal(oldOrigin, {w.playerPosition.x * 2, w.playerPosition.y * 2, elevation * 2},
                     g.Surface()->Planet().radiusMeters);
    auto destination = G::Advance(g.Surface()->Pose(), 260, 0, g.Surface()->Planet().radiusMeters).origin;
    assert(g.RelocateSurfaceSurvey(destination));
    PlanetSecurity::Project();
    assert(law.officers.front().id == officerId &&
           G::Distance(g.Surface()->Planet().radiusMeters, policePosition,
                       PlanetSecurity::State().officers.front().position) < .001);
    assert(!law.bullets.empty());
    const auto &bullet = law.bullets.front();
    auto restoredShot = g.Surface()->PointAt(bullet.position.x * 2, bullet.position.y * 2);
    restoredShot.altitudeMeters = g.Surface()->Origin().origin.altitudeMeters + bullet.elevation * 2;
    assert(G::Distance(g.Surface()->Planet().radiusMeters, shotPosition, restoredShot) < .01 &&
           std::abs(shotPosition.altitudeMeters - restoredShot.altitudeMeters) < .01);
    law.initialUnseenSeconds = 19.99f;
    CivicJustice::Update(.05f);
    assert(law.wanted == 0 && law.heat == 0 && law.pendingInfamy == 0 && law.infamy == reputation);
    law.bullets.clear();
    law.officers.clear();
    PlanetSecurity::Capture();
    PlanetSecurity::Report(Offense::Assault, g.Surface()->Pose().origin);
    for (int i = 0; i < 14; ++i)
    {
        PlanetSecurity::Report(Offense::Assault, g.Surface()->Pose().origin);
    }
    assert(law.wanted == 1); // Heat alone cannot skip the elapsed pursuit requirement.
    law.pursuitSeconds = 25;
    PlanetSecurity::Report(Offense::Assault, g.Surface()->Pose().origin);
    assert(law.wanted == 2);
    InstallTrafficFixture();
    auto *traffic = g.SurfaceTraffic();
    float heat = law.heat;
    assert(traffic->Damage(1, 1, true) && law.heat == heat);
    assert(traffic->Damage(1, 999, true) && law.heat == heat + 50);
    assert(!traffic->Damage(1, 999, true) && law.heat == heat + 50);
    // Empty vehicles and damage caused by other actors do not charge the player.
    InstallTrafficFixture();
    auto vehicleState = traffic->State();
    vehicleState.actors.front().occupied = false;
    *traffic = PlanetTraffic(vehicleState);
    traffic->Bind(plan);
    heat = law.heat;
    assert(traffic->Damage(1, 999, true) && law.heat == heat);
    // Persist the response as physical actors, and reject mismatched actor IDs without mutating it.
    PlanetSecurity::State().search = g.Surface()->Pose().origin;
    for (int i = 0; i < 10 && law.officers.empty(); ++i)
    {
        PlanetSecurity::Spawn();
    }
    assert(!law.officers.empty());
    PlanetSecurity::Project();
    PlanetSecurity::Capture();
    const auto records = PlanetSecurity::State();
    auto bad = records;
    bad.officers.front().id = law.nextOfficer + 10;
    bool rejected = false;
    try
    {
        PlanetSecurity::Validate(bad, law, g.Surface()->Planet());
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    assert(rejected);
    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("security-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / "security.sav";
    const float savedHeat = law.heat;
    assert(SaveSystem::Save(file) && SaveSystem::Load(file));
    WaitForCheckpointLoad();
    assert(law.heat == savedHeat &&
           PlanetSecurity::State().officers.front().id == records.officers.front().id &&
           G::Distance(g.Surface()->Planet().radiusMeters, PlanetSecurity::State().officers.front().position,
                       records.officers.front().position) < .001);
    const auto json = monitoring::WorldSnapshot::Build(1, "security-test");
    assert(json.find("planet_officers") != std::string::npos &&
           json.find("jurisdiction") != std::string::npos);
    const auto pursuit = law.pursuitSeconds;
    w.isPaused = true;
    g.Update(.05f);
    assert(law.pursuitSeconds == pursuit);
    w.isPaused = false;
    g.Reset();
    std::filesystem::remove(file);
    std::filesystem::remove(folder);
}

void TestPlanetParty()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    PrepareCheckpointFixture();
    auto &g = Game::Get();
    auto &field = GameLevels::Chapter().GetState();
    auto plan = g.Surface()->Stream().Snapshot()->planet;
    auto *party = g.SurfaceParty();
    party->Capture();
    auto seed = party->State();
    field.kainJoined = true;
    seed.members[1] = seed.members[0];
    seed.members[1].combat.health = 46;
    seed.members[1].position =
        G::FromLocal(plan->Region("mq00_mine").frame, {4, -2, 0}, g.Surface()->Planet().radiusMeters);
    *party = PlanetParty(seed);
    party->Bind(plan);
    party->Publish();
    assert(g.HasCompanion(CompanionId::Kain, CompanionPlace::Mine) && mineState.kain.combat.health == 46);
    assert(!g.HasCompanion(CompanionId::Kain, CompanionPlace::Frontier));
    // Non-owning projections cannot heal, move or refill either physical actor.
    field.ria.combat.health = 99;
    field.kain.combat.health = 98;
    party->Capture();
    party->Update(0);
    assert(party->State().members[0].combat.health == 37 && party->State().members[1].combat.health == 46);
    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("party-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / "party.sav";
    assert(SaveSystem::Save(file) && SaveSystem::Load(file));
    WaitForCheckpointLoad();
    party = g.SurfaceParty();
    assert(g.HasCompanion(CompanionId::Ria, CompanionPlace::Mine) &&
           g.HasCompanion(CompanionId::Kain, CompanionPlace::Mine));
    assert(mineState.riaCombat.health == 37 && mineState.kain.combat.health == 46 &&
           mineState.kain.combat.pistol.GetRounds() == 11);
    // A downed actor deeper inside the mine stays there when the player leaves or relocates.
    seed = party->State();
    seed.members[0].position =
        G::FromLocal(plan->Region("mq00_mine").frame,
                     {(kPowerBypass.x - mineState.entry.x) * 2, (kPowerBypass.y - mineState.entry.y) * 2, 0},
                     g.Surface()->Planet().radiusMeters);
    seed.members[0].combat.health = 0;
    seed.members[0].combat.downedSeconds = 12;
    *party = PlanetParty(seed);
    party->Bind(plan);
    party->Publish();
    const auto downed = party->State().members[0].position;
    assert(g.LeaveSurfaceMine());
    party->Update(.05f);
    assert(g.RelocateSurfaceSurvey(plan->Region("halo_07").access.position));
    assert(party->Owns(CompanionId::Ria, CompanionPlace::Mine) &&
           G::Distance(g.Surface()->Planet().radiusMeters, downed, party->State().members[0].position) <
               .001);
    assert(!party->NearbyTravelers()[0]);
    // On the surface, only a nearby healthy companion joins a deliberate transport.
    seed = party->State();
    seed.members[0].place = CompanionPlace::Surface;
    seed.members[0].position =
        G::Advance(g.Surface()->Pose(), 4, 0, g.Surface()->Planet().radiusMeters).origin;
    seed.members[0].combat.health = 37;
    seed.members[0].combat.downedSeconds = 0;
    seed.members[1].place = CompanionPlace::Surface;
    seed.members[1].position = downed;
    *party = PlanetParty(seed);
    party->Bind(plan);
    const auto travelers = party->NearbyTravelers();
    assert(travelers[0] && !travelers[1]);
    auto &aircraft = g.GetState().aircraft;
    aircraft.position = party->State().members[0].position;
    aircraft.mounted = true;
    assert(!party->Blocked(*aircraft.position, .32, 1));
    const auto beforeSeparation = party->State().members[0].position;
    party->Update(.05f);
    assert(G::Distance(g.Surface()->Planet().radiusMeters, beforeSeparation,
                       party->State().members[0].position) <= .401);
    const auto waiting = party->State().members[0].position;
    aircraft.position = G::Advance(g.Surface()->Pose(), 100, 0, g.Surface()->Planet().radiusMeters).origin;
    aircraft.position->altitudeMeters += 20;
    party->Update(.05f);
    assert(G::Distance(g.Surface()->Planet().radiusMeters, waiting, party->State().members[0].position) <
           .001);
    aircraft = {};
    const auto farKain = party->State().members[1].position;
    assert(g.RelocateSurfaceSurvey(plan->Region("frontier").access.position));
    assert(G::Distance(g.Surface()->Planet().radiusMeters, g.Surface()->Pose().origin,
                       party->State().members[0].position) <= 8.1);
    assert(G::Distance(g.Surface()->Planet().radiusMeters, farKain, party->State().members[1].position) <
           .001);
    assert(party->State().members[0].combat.health == 37 &&
           party->State().members[0].combat.pistol.GetRounds() == 11);
    const auto monitor = monitoring::WorldSnapshot::Build(1, "party-test");
    assert(monitor.find("halo/ria") == std::string::npos &&
           monitor.find("frontier/kain") == std::string::npos &&
           monitor.find("planet_party") != std::string::npos);
    // Corrupt ownership and cross-planet records are rejected before changing the live game.
    auto invalid = party->State();
    invalid.members[0].place = CompanionPlace(255);
    bool rejected = false;
    try
    {
        PlanetParty::Validate(invalid, g.Surface()->Planet());
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    assert(rejected);
    CivicJustice::State().wanted = 0;
    CivicJustice::State().heat = 0;
    assert(SaveSystem::SwitchPlanet("seira"));
    WaitForCheckpointLoad();
    assert(!g.SurfaceParty()->Joined(CompanionId::Ria) && !g.SurfaceParty()->Joined(CompanionId::Kain));
    assert(SaveSystem::SwitchPlanet("erma"));
    WaitForCheckpointLoad();
    assert(g.SurfaceParty()->Joined(CompanionId::Ria) && g.SurfaceParty()->Joined(CompanionId::Kain));
    g.Reset();
    std::filesystem::remove(file);
    std::filesystem::remove(folder);
}

void TestPlanetProfileVisits()
{
    using namespace tutorial;
    using G = world::SurfaceGeometry;
    auto &g = Game::Get();
    auto &w = g.GetState();
    PrepareCheckpointFixture();
    assert(g.LeaveSurfaceMine());
    assert(w.missions.Accept(MissionId::Mara, true));
    w.isSideQuestAccepted = true;
    assert(ActivityJournal::TrackMission(MissionId::Mara));
    const auto ermaBike = *g.SurfaceBike();
    mineState.player.credits = 7000;
    CivicJustice::State().wanted = 0;
    CivicJustice::State().heat = 0;
    assert(!SaveSystem::SwitchPlanet("unknown_profile"));
    assert(SaveSystem::SwitchPlanet("seira"));
    SaveSystem::Cancel();
    assert(g.IsStoryWorld() && w.residents.front().health == 64 && mineState.player.credits == 7000 &&
           w.missions.Tracked() == MissionId::Mara);
    assert(SaveSystem::SwitchPlanet("seira"));
    assert(!SaveSystem::SwitchPlanet("orren"));
    WaitForCheckpointLoad();
    assert(g.Surface()->Planet().id == "seira" && !g.IsStoryWorld() && !g.SurfaceCities());
    assert(!CityInfrastructure::Get().prepared && CityInfrastructure::Get().modules.empty());
    assert(w.missions.Tracked() == MissionId::Count && w.trackedCommunityRegion.empty());
    assert(w.residents.empty() && w.buildings.empty() && !mineState.generated &&
           !GameLevels::Chapter().GetState().generated && !g.EnterSurfaceMine());
    assert(!g.MissionEligible(MissionId::Signal) && !g.MissionEligible(MissionId::Mine));
    assert(w.dialogue.scenes.empty() && w.dialogue.history.empty() && !mineState.quest.IsRiaJoined());
    assert(mineState.player.credits == 7000 && mineState.loadout.HasPistol() &&
           CivicJustice::State().infamy == 17);
    InstallTrafficFixture(17);
    const auto seiraTrafficKey = g.SurfaceTraffic()->State().worldKey;
    const auto plan = g.Surface()->Stream().Snapshot()->planet;
    assert(plan->anchors.empty() && !plan->cityCores &&
           plan->Region("landing_zone").definition.planetId == "seira");
    const auto &relay = plan->relays.front();
    assert(g.RelocateSurfaceSurvey(relay.location.position));
    RelayTravel::Interact();
    w.mapVisible = false;
    assert(w.relayNetwork.Registered(relay.id));
    w.planetDestination = plan->Region("high_city").access.position;
    w.planetMapRange = 9900;
    const auto seiraPose = g.Surface()->Pose().origin, seiraBike = *g.SurfaceBike();
    mineState.player.credits = 7300;
    CivicJustice::State().infamy = 23;
    w.pressedKeys['w'] = true;
    w.mouseAiming = true;
    assert(SaveSystem::SwitchPlanet("orren"));
    WaitForCheckpointLoad();
    assert(g.Surface()->Planet().id == "orren" && w.relayNetwork.Registrations().empty() &&
           !w.planetDestination && mineState.player.credits == 7300 && CivicJustice::State().infamy == 23);
    assert(g.SurfaceTraffic()->State().worldKey != seiraTrafficKey &&
           g.SurfaceTraffic()->State().actors.empty());
    InstallTrafficFixture(10);
    assert(SaveSystem::SwitchPlanet("seira"));
    WaitForCheckpointLoad();
    assert(g.SurfaceTraffic()->State().worldKey == seiraTrafficKey &&
           g.SurfaceTraffic()->State().actors.front().id == 1 &&
           g.SurfaceTraffic()->State().actors.front().health == 163);
    assert(g.Surface()->Planet().id == "seira" && w.relayNetwork.Registered(relay.id) &&
           w.planetDestination && w.planetMapRange == 9900 && !w.pressedKeys['w'] && !w.mouseAiming &&
           G::Distance(g.Surface()->Planet().radiusMeters, seiraPose, g.Surface()->Pose().origin) < 1.e-6 &&
           G::Distance(g.Surface()->Planet().radiusMeters, seiraBike, *g.SurfaceBike()) < 1.e-6);
    auto monitor = monitoring::WorldSnapshot::Build(101, "profile-test");
    assert(monitor.find("halo/ria") == std::string::npos && monitor.find("halo/mara") == std::string::npos);
    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("profiles-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / "campaign.sav";
    const bool savedOk = SaveSystem::Save(file);
    if (!savedOk)
    {
        std::cerr << "SaveSystem::Save failed: " << SaveSystem::Message() << "\n";
    }
    assert(savedOk && SaveSystem::CheckFile(file));
    g.Reset();
    assert(SaveSystem::Load(file));
    WaitForCheckpointLoad();
    assert(g.Surface()->Planet().id == "seira" && w.relayNetwork.Registered(relay.id));
    assert(g.SurfaceTraffic()->State().worldKey == seiraTrafficKey &&
           g.SurfaceTraffic()->State().actors.front().health == 163);
    mineState.player.credits = 8100;
    assert(SaveSystem::SwitchPlanet("erma"));
    WaitForCheckpointLoad();
    assert(CityInfrastructure::Get().prepared);
    assert(w.missions.Tracked() == MissionId::Mara && ActivityJournal::Current().mission == MissionId::Mara);
    assert(g.IsStoryWorld() && w.residents.size() == 98 && w.residents.front().health == 64 &&
           w.residents.front().name == "저장 검사 주민" && w.relayNetwork.Registered("relay_halo_07_0") &&
           GameLevels::Chapter().GetState().generatorInspected && mineState.generated &&
           mineState.enemies.front().health == 7 && mineState.player.credits == 8100 &&
           CivicJustice::State().infamy == 23 &&
           G::Distance(g.Surface()->Planet().radiusMeters, ermaBike, *g.SurfaceBike()) < 1.e-6);
    assert(SaveSystem::SwitchPlanet("orren"));
    WaitForCheckpointLoad();
    assert(g.Surface()->Planet().id == "orren" && mineState.player.credits == 8100);
    assert(g.SurfaceTraffic()->State().actors.front().id == 1 &&
           g.SurfaceTraffic()->State().actors.front().health == 170);
    g.Reset();
    std::filesystem::remove(file);
    std::filesystem::remove(folder);
}
void TestMapZoomAndSelection()
{
    using namespace tutorial;
    auto &g = Game::Get();
    g.Reset();
    auto &w = g.GetState();
    w.isInsideHome = false;
    w.mapVisible = true;
    for (auto region : {MapRegion::City, MapRegion::Mine, MapRegion::Frontier})
    {
        mineState.active = region == MapRegion::Mine;
        GameLevels::Chapter().GetState().active = region == MapRegion::Frontier;
        assert(MapHud::Region() == region);
        Vector2 cursor{510, 350};
        auto before = MapHud::MapToWorld(cursor, true);
        auto mini = MapHud::WorldToMap(w.playerPosition, false);
        assert(MapHud::Zoom(3, cursor));
        assert(Game::Distance(before, MapHud::MapToWorld(cursor, true)) < .002f);
        assert(Game::Distance(mini, MapHud::WorldToMap(w.playerPosition, false)) < .001f);
        MapHud::Pan({70, 35});
        assert(Game::Distance(MapHud::WorldToMap(before, true), cursor) > 30);
        auto point = MapHud::MapToWorld({400, 300}, true);
        assert(Game::Distance(MapHud::WorldToMap(point, true), {400, 300}) < .001f);
        MapHud::ResetView(true);
        auto centered = MapHud::WorldToMap(w.playerPosition, true);
        assert(Game::Distance(centered, {489, 400}) < 5);
        MapHud::ResetView();
        assert(w.mapViews[size_t(region)].zoom == 1);
    }
    w.mapVisible = false;
    assert(!MapHud::Zoom(1, {500, 400}));
    g.Reset();
}
void TestFrameScheduleAndTextCache()
{
    tutorial::FrameSchedule schedule;
    double now = 0;
    for (int frame = 0; frame < 6000; ++frame)
    {
        const double end = now + 4;
        auto delay = schedule.NextDelay(now, end);
        assert(delay >= 1 && delay <= 17);
        now = end + delay;
    }
    assert(std::fabs(now - 100000) <= 1); // Update work must not accumulate on top of 60 Hz.
    auto late = schedule.NextDelay(now + 500, now + 504);
    assert(late >= 12 && late <= 13);
    assert(schedule.NextDelay(now + 517, now + 550) == 1);

    const std::string original = "한글 안내 ABC 123 · 긴 대사도 보존합니다.\n두 번째 줄입니다.";
    auto expected = tutorial::Dialogue::Wrap(original, 135, 19);
    const int width = RenderAssets::MeasureUtf8(original, 19);
    for (int i = 0; i < 600; ++i)
    {
        tutorial::Dialogue::Wrap(original + std::to_string(i), 80 + i % 10, 16);
    }
    assert(tutorial::Dialogue::Wrap(original, 135, 19) == expected);
    assert(RenderAssets::MeasureUtf8(original, 19) == width);
    assert(tutorial::Dialogue::Wrap("", 135, 19) == std::vector<std::string>{""});
}

void ReadDialogue()
{
    auto &d = tutorial::Game::Get().GetState().dialogue;
    int pages = 0;
    while (tutorial::Dialogue::Active())
    {
        assert(++pages < 200);
        if (!d.pages[d.page].choices.empty())
        {
            assert(tutorial::Dialogue::Select(0));
        }
        assert(tutorial::Dialogue::Advance());
    }
}
void ReadyMine(unsigned int seed = 13)
{
    GameLevels::Mine().Reset();
    mineState.loadout.GrantPistol();
    mineState.loadout.Select(combat::WeaponSlot::Secondary);
    assert(mineState.quest.Advance(MineQuestStage::ShowRecord));
    assert(mineState.quest.Advance(MineQuestStage::CheckCityRelay));
    assert(mineState.quest.BeginJourney());
    assert(mineState.quest.Advance(MineQuestStage::TravelToMine));
    tutorial::Game::Get().GetState().playerPosition = kMineEntry;
    tutorial::Game::Get().GetState().cameraPosition = kMineEntry;
    GameLevels::Mine().Map().Generate(seed);
    mineState.active = true;
    mineState.invulnerability = 1000;
}
void TestStageSelectionAndLoadout()
{
    using namespace tutorial;
    using combat::WeaponSlot;
    auto &g = Game::Get();
    auto &w = g.GetState();
    auto press = [&](unsigned char key) {
        g.KeyDown(key, 0, 0);
        g.KeyUp(key, 0, 0);
    };
    g.Reset();
    g.Resize(1280, 800);
    assert(!mineState.loadout.HasPistol() && mineState.loadout.Selected() == WeaponSlot::Unarmed);
    assert(!mineState.loadout.Select(WeaponSlot::Primary));
    assert(!mineState.loadout.Select(WeaponSlot::Secondary));
    for (int i = 0; i < int(TestStage::Count); ++i)
    {
        Hud::OpenMenu(i % 2 ? UiScreen::Pause : UiScreen::Title);
        press('s');
        press(13);
        assert(w.ui.screen == UiScreen::Stages);
        auto controls = Hud::Controls();
        assert(controls.size() == size_t(TestStage::Count) + 1);
        for (int j = 0; j < i; ++j)
        {
            press('s');
        }
        press(13);
        assert(w.ui.screen == UiScreen::None && !w.isPaused && !w.weaponWheel.visible);
        assert(w.surfaceExploration && g.Surface() != nullptr);
        assert(mineState.loadout.Selected() == WeaponSlot::Unarmed);
        assert(mineState.loadout.HasPistol() == (i >= int(TestStage::WeaponUnlocked)));
        assert(mineState.active == (i == int(TestStage::Mine)));
        assert(g.Mode() != WorldMode::LegacyFrontier &&
               GameLevels::Chapter().IsActive() == (i == int(TestStage::ChapterOne)));
        if (i == int(TestStage::Prologue))
        {
            assert(w.isInsideHome && w.mainQuestStage == MainQuestStage::ReadHomeTerminal &&
                   g.Mode() == WorldMode::SurfaceInterior);
        }
        if (i == int(TestStage::Observatory))
        {
            assert(w.isInObservatory && !w.gateSignalHeard && g.Mode() == WorldMode::SurfaceObservatory);
        }
        if (i >= int(TestStage::BikeUnlocked))
        {
            assert(w.gateSignalHeard && w.hasDescendedObservatory);
        }
        if (i == int(TestStage::BikeUnlocked))
        {
            assert(mineState.quest.GetStage() == MineQuestStage::TravelToMine &&
                   mineState.quest.IsRiaJoined() && w.isBikeMounted &&
                   g.Mode() == WorldMode::SurfaceExterior);
        }
        if (i == int(TestStage::WeaponUnlocked))
        {
            const auto &mineAccess = g.Surface()->Stream().Snapshot()->planet->Region("mq00_mine").access.position;
            assert(!w.isBikeMounted && !mineState.active &&
                   mineState.quest.GetStage() == MineQuestStage::TravelToMine &&
                   world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, g.Surface()->Pose().origin,
                                                    mineAccess) < 6.0);
        }
        if (i == int(TestStage::Mine))
        {
            assert(g.IsSurfaceMine() && g.Mode() == WorldMode::SurfaceMine &&
                   mineState.quest.IsRiaJoined() &&
                   mineState.quest.GetStage() == MineQuestStage::BypassPower);
        }
        if (i == int(TestStage::ChapterOne))
        {
            const auto &frontierAccess = g.Surface()->Stream().Snapshot()->planet->Region("frontier").access.position;
            assert(g.Mode() == WorldMode::SurfaceExterior &&
                   mineState.quest.GetStage() == MineQuestStage::Complete && w.isComplete &&
                   GameLevels::Chapter().IsAvailable() &&
                   world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters, g.Surface()->Pose().origin,
                                                    frontierAccess) < 8.0);
        }
    }
    assert(!g.EnterTestStage(TestStage::Count) && w.surfaceExploration);
    // The normal narrative gives the pistol once, only after reading Ria's entrance handover on Erma surface.
    assert(g.EnterTestStage(TestStage::BikeUnlocked));
    w.isBikeMounted = false;
    assert(g.RelocateSurfaceSurvey(g.Surface()->Stream().Snapshot()->planet->Region("mq00_mine").access.position));
    press('e');
    assert(Dialogue::Active() && !mineState.loadout.HasPistol() && !mineState.active);
    ReadDialogue();
    assert(mineState.active && g.IsSurfaceMine() && mineState.loadout.HasPistol() &&
           !mineState.loadout.IsPistolEquipped());
    assert(!mineState.loadout.GrantPistol());
    auto choose = [&](Vector2 offset) {
        g.MouseMotion(640, 400);
        g.KeyDown('q', 0, 0);
        assert(w.weaponWheel.visible && !w.mouseFiring);
        g.MouseMotion(int(640 + offset.x), int(400 + offset.y));
        g.KeyUp('q', 0, 0);
        assert(!w.weaponWheel.visible && !w.mouseFiring);
    };
    int kits = mineState.player.recoveryKits;
    mineState.player.health = 20;
    choose({140, 70});
    assert(mineState.loadout.IsPistolEquipped() && mineState.player.recoveryKits == kits);
    int rounds = mineState.pistol.GetRounds();
    GameLevels::Mine().Combat().Attack();
    assert(mineState.pistol.GetRounds() == rounds - 1);
    choose({-140, 70});
    assert(mineState.loadout.Selected() == WeaponSlot::Unarmed);
    GameLevels::Mine().Combat().Reload();
    assert(!mineState.pistol.IsReloading());
    GameLevels::Mine().Combat().Attack();
    assert(mineState.pistol.GetRounds() == rounds - 1 && mineState.meleeSeconds > 0);
    choose({0, -150}); // Empty primary cannot replace the currently selected hands.
    assert(mineState.loadout.Selected() == WeaponSlot::Unarmed);
    g.MouseMotion(640, 400);
    g.KeyDown('q', 0, 0);
    g.KeyDown('q', 0, 0); // Auto-repeat cannot consume a kit or reopen the wheel.
    g.MouseMotion(780, 470);
    GameLevels::Mine().Combat().Attack();
    g.MouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 780, 470);
    assert(!w.mouseFiring && mineState.pistol.GetRounds() == rounds - 1);
    press(27); // Cancel preserves the prior weapon; Q auto-repeat cannot reopen it.
    g.KeyDown('q', 0, 0);
    assert(!w.weaponWheel.visible);
    g.KeyUp('q', 0, 0);
    assert(!w.weaponWheel.visible && !mineState.loadout.IsPistolEquipped());
    choose({140, 70});
    g.MouseMotion(640, 400);
    g.KeyDown('q', 0, 0);
    g.MouseMotion(500, 470);
    g.MouseEntry(GLFW_FALSE);
    g.KeyUp('q', 0, 0);
    assert(mineState.loadout.IsPistolEquipped());
    Hud::OpenMenu(UiScreen::Pause);
    assert(!WeaponWheel::Open());
    press('q');
    assert(w.ui.screen == UiScreen::Pause);
    Hud::CloseMenu();
    w.mapVisible = true;
    assert(!WeaponWheel::Open());
    w.mapVisible = false;
    press('g');
    assert(mineState.player.recoveryKits == kits - 1 && mineState.player.health > 20);
    GameLevels::Mine().Leave();
    w.playerPosition = Overworld::kMineEntrance;
    press('e');
    assert(mineState.active && !Dialogue::Active() && mineState.pistol.GetRounds() == rounds - 1);
    // Mouse deltas and release also work through the window's letterbox transform.
    g.Resize(1920, 1080);
    g.MouseMotion(960, 540);
    g.KeyDown('q', 0, 0);
    g.MouseMotion(771, 634);
    g.KeyUp('q', 0, 0);
    assert(mineState.loadout.Selected() == WeaponSlot::Unarmed);
    g.Resize(1280, 800);
    g.Reset();
    w.isInsideHome = false;
    CivicCombat::Fire();
    assert(!mineState.loadout.HasPistol() && mineState.pistol.GetRounds() == 12 &&
           CivicJustice::State().meleeSeconds > 0);
    g.Reset();
}
void TestUiMenusAndTransactions()
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &w = g.GetState();
    g.Reset();
    auto press = [&](unsigned char key) {
        g.KeyDown(key, 0, 0);
        g.KeyUp(key, 0, 0);
    };
    Hud::OpenMenu(UiScreen::Title);
    assert(w.isPaused && !w.mouseFiring);
    auto title = Hud::Controls();
    assert(title.front().action == UiAction::Start && title.size() == 6);
    Hud::OpenMenu(UiScreen::Options);
    auto slider = Hud::Controls().front();
    // Fit a 1280x800 canvas into a 1920x1080 window; input must share the same inverse transform.
    g.Resize(1920, 1080);
    auto click = [&](Vector2 p) {
        float scale = 1.35f;
        int x = int(96 + p.x * scale), y = int(p.y * scale);
        g.MouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, x, y);
        g.MouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, x, y);
    };
    click({slider.position.x + 16, slider.position.y + 60});
    assert(std::fabs(w.uiPreferences.background - .8f) < .001f && !w.mouseFiring);
    click({slider.position.x + slider.size.x - 16, slider.position.y + 60});
    assert(std::fabs(w.uiPreferences.background - 1) < .001f);
    for (int i = 0; i < 30; ++i)
    {
        press('a');
    }
    assert(std::fabs(w.uiPreferences.background - .8f) < .001f);
    press('s');
    press('s'); // Content layer has a separate readability floor.
    for (int i = 0; i < 30; ++i)
    {
        press('a');
    }
    assert(std::fabs(w.uiPreferences.content - .85f) < .001f);
    g.Reset();
    assert(w.ui.screen == UiScreen::None && !w.isPaused && w.uiPreferences.content == .85f);
    w.uiPreferences = {};
    g.Resize(1280, 800);
    // Cancel a destructive action; entering the confirmation is not a reset.
    w.isSideQuestAccepted = true;
    Hud::OpenMenu(UiScreen::Pause);
    press('r');
    assert(w.ui.screen == UiScreen::Confirm && w.isSideQuestAccepted);
    press('s');
    press(13);
    assert(w.ui.screen == UiScreen::Pause && w.isSideQuestAccepted);
    press(27);
    assert(!w.isPaused && w.ui.screen == UiScreen::None);
    // A cancelled/disabled exchange consumes no goods, and valid purchases use live reputation pricing.
    auto &level = game::GameLevels::Chapter();
    level.Enter(true);
    level.GetState().convoyRescued = true;
    w.playerPosition = {24.5f, 20.5f};
    auto &stats = game::GameLevels::Mine().GetState().player;
    CivicJustice::State().infamy = 80;
    stats.salvage = 0;
    stats.recoveryKits = 1;
    Hud::OpenMenu(UiScreen::Supply);
    assert(!Hud::Controls().front().enabled);
    press(13);
    assert(stats.salvage == 0 && stats.recoveryKits == 1);
    stats.salvage = 100;
    int price = CivicJustice::ShopPrice(2);
    press('e');
    assert(stats.salvage == 100 - price && stats.recoveryKits == 2);
    g.KeyDown('e', 0, 0); // A fresh press buys once; OS repeat cannot buy again.
    g.KeyDown('e', 0, 0);
    assert(stats.salvage == 100 - price * 2 && stats.recoveryKits == 3);
    g.KeyUp('e', 0, 0);
    press(27);
    assert(w.ui.screen == UiScreen::None);
    g.Reset();
}
void TestDialogueProgress()
{
    using namespace tutorial;
    auto &game = Game::Get();
    auto &world = game.GetState();
    game.Reset();
    game.KeyDown('j', 0, 0);
    assert(world.journalVisible && Dialogue::LocksWorld());
    auto journal = Dialogue::Journal();
    for (const auto &entry : journal)
    {
        assert(entry.find("문양:") == std::string::npos && entry.find("N-0/17") == std::string::npos);
    }
    game.KeyUp('j', 0, 0);
    game.KeyDown('j', 0, 0);
    game.KeyUp('j', 0, 0);
    const std::string korean = "긴 한글 문장과 English words 123을 실제 글꼴 폭에 맞춰 누락 없이 읽는다.";
    auto rows = Dialogue::Wrap(korean, 135);
    std::string joined;
    for (const auto &row : rows)
    {
        assert(RenderAssets::MeasureUtf8(row, 19) <= 135);
        joined += row;
    }
    auto compact = [](std::string text) {
        text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
        return text;
    };
    assert(compact(joined) == compact(korean));
    const std::string completeNotice =
        korean + korean + korean + korean + " 마지막 문장도 누락 없이 표시한다. 원문 문장부호 … 는 유지한다.";
    const auto noticePages = Hud::TextPages(completeNotice, 294, 16, 4);
    assert(noticePages.size() > 1);
    std::string restored;
    for (const auto &noticePage : noticePages)
    {
        assert(!noticePage.empty() && noticePage.size() <= 4);
        for (const auto &line : noticePage)
        {
            assert(RenderAssets::MeasureUtf8(line, 16) <= 294);
            restored += line;
        }
    }
    assert(compact(restored) == compact(completeNotice));
    const auto objectivePages = Hud::TextPages(completeNotice, 420, 18, 2);
    restored.clear();
    for (const auto &objectivePage : objectivePages)
    {
        assert(objectivePage.size() <= 2);
        for (const auto &line : objectivePage)
        {
            restored += line;
        }
    }
    assert(compact(restored) == compact(completeNotice));
    game.ShowNotice(completeNotice);
    assert(world.noticeDurationSeconds == 6.f * noticePages.size());
    assert(world.noticeSecondsRemaining == world.noticeDurationSeconds);
    int completions = 0;
    auto scene = [&]() {
        return Dialogue::Start("test/choice",
                               {{"first", "검증 화자", "단말 기록", korean + korean + korean + korean, {}},
                                {"choice",
                                 "리아",
                                 "현장",
                                 "출발 여부를 선택해 줘.",
                                 {{"depart", "출발한다"}, {"prepare", "아직 준비한다"}}}},
                               [&](const std::string &choice) {
                                   assert(choice == "prepare");
                                   ++completions;
                               });
    };
    assert(scene() && Dialogue::LocksWorld());
    size_t page = world.dialogue.page;
    world.mouseFiring = true;
    game.KeyDown('e', 0, 0);
    assert(world.dialogue.page == page + 1);
    game.KeyDown('e', 0, 0);
    assert(world.dialogue.page == page + 1); // Held E consumes exactly one page.
    game.KeyUp('e', 0, 0);
    while (world.dialogue.pages[world.dialogue.page].choices.empty())
    {
        assert(Dialogue::Advance());
    }
    assert(!Dialogue::Advance() && completions == 0);
    assert(Dialogue::Select(1) && completions == 0);
    auto savedPage = world.dialogue.page;
    game.ShowNotice("재장전 / 전투 알림은 대사 본문을 덮지 않는다.");
    assert(world.dialogue.page == savedPage && world.dialogue.selection == 1);
    game.KeyDown(27, 0, 0);
    assert(!Dialogue::Advance() && !world.dialogue.scenes.at("test/choice").choice.size());
    game.KeyDown(27, 0, 0);
    HospitalRecovery::Begin();
    assert(world.dialogue.suspended && !Dialogue::Active() && !Dialogue::Resume());
    for (int step = 0; step < 80; ++step)
    {
        HospitalRecovery::Update(.05f);
    }
    assert(!HospitalRecovery::Active());
    assert(world.ui.screen == UiScreen::Medical);
    game.KeyDown(13, 0, 0);
    game.KeyUp(13, 0, 0);
    game.KeyDown('j', 0, 0);
    game.KeyUp('j', 0, 0);
    game.KeyDown(13, 0, 0);
    assert(Dialogue::Active());
    assert(world.dialogue.page == savedPage && world.dialogue.selection == 1);
    game.KeyDown(13, 0, 0); // Resume must retain the Enter latch even when movement keys are cleared.
    assert(Dialogue::Active() && world.dialogue.page == savedPage && completions == 0);
    game.KeyUp(13, 0, 0);
    assert(Dialogue::Advance() && completions == 1);
    assert(world.dialogue.scenes.at("test/choice").choice == "prepare");
    assert(scene());
    while (world.dialogue.pages[world.dialogue.page].choices.empty())
    {
        assert(Dialogue::Advance());
    }
    assert(Dialogue::Select(1) && Dialogue::Advance() && completions == 1);
    auto history = world.dialogue.history.size();
    assert(history > 1);
    world.isInsideHome = false;
    CivicJustice::State().wanted = 1;
    assert(Dialogue::Start("test/radio", {{"radio", "리아", "무전", "안전 지점으로 이동해.", {}}}));
    assert(Dialogue::Danger() && !Dialogue::LocksWorld() && CivicCombat::Available());
    game.KeyDown('d', 0, 0);
    assert(world.pressedKeys['d']);
    ReadDialogue();
    assert(world.dialogue.history.size() == history + 1);
    game.Reset();
    world.isInsideHome = false;
    world.isInObservatory = true;
    world.playerPosition = kObservatoryConsole;
    game.InteractWithNearbyTarget();
    assert(world.gateSignalHeard && world.dialogue.pages.size() == 3);
    assert(world.dialogue.pages[0].speaker != world.dialogue.pages[1].speaker &&
           world.dialogue.pages[1].speaker != world.dialogue.pages[2].speaker);
    ReadDialogue();
    game.Reset();
}
void TestNpcFacing()
{
    tutorial::MovementFacing facing;
    for (Vector2 delta : {Vector2{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, -1}})
    {
        facing.Update({0, 0}, delta);
        assert(facing.walking);
        assert(std::fabs(facing.yaw - std::atan2(delta.y, delta.x)) < .00001f);
        float lastYaw = facing.yaw;
        facing.Update(delta, delta);
        assert(!facing.walking && facing.yaw == lastYaw);
    }
    ReadyMine();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.entry = {9.5f, 10.5f};
    tutorial::Game::Get().GetState().playerPosition = {12.5f, 12.5f};
    mineState.survivors.clear();
    RescueNpc npc{100, 0, {8.5f, 9.5f}};
    npc.evacuating = true;
    npc.route = {{9.5f, 9.5f}, {9.5f, 10.5f}};
    mineState.survivors.push_back(npc);
    mineState.riaPosition = {8.5f, 7.5f};
    mineState.riaRoute = {{9.5f, 7.5f}, {9.5f, 8.5f}};
    mineState.riaNavigationCooldown = 100;
    GameLevels::Mine().Rescue().Update(.1f);
    assert(mineState.survivors.front().facing.walking);
    assert(std::fabs(mineState.survivors.front().facing.yaw) < .0001f);
    assert(mineState.riaFacing.walking && std::fabs(mineState.riaFacing.yaw) < .0001f);
    for (int i = 0; i < 12; ++i)
    {
        GameLevels::Mine().Rescue().Update(.1f);
    }
    const auto &rescued = mineState.survivors.front();
    assert(!rescued.facing.walking && std::fabs(rescued.facing.yaw - tutorial::kPi / 2) < .0001f);
    assert(!mineState.riaFacing.walking && std::fabs(mineState.riaFacing.yaw - tutorial::kPi / 2) < .0001f);
}
void AimAt(Vector2 target)
{
    auto camera = tutorial::Game::Get().GetState().cameraPosition;
    tutorial::Game::Get().GetState().cursorPosition = tutorial::WorldCamera::ToViewport(
        {tutorial::kCanvasWidth * .5f + (target.x - target.y - camera.x + camera.y) * 36,
         tutorial::kCanvasHeight * .57f + (target.x + target.y - camera.x - camera.y) * 18 -
             models::WorldScale::aimHeightPixels});
    auto &w = tutorial::Game::Get().GetState();
    w.mouseAiming = true;
    auto direction = game::MineLevel::GetAimDirection(game::GameLevels::AimAngle());
    w.facingYaw = std::atan2(direction.y, direction.x);
}
void Step(float seconds)
{
    for (int i = 0; i < int(std::ceil(seconds / .025f)); ++i)
    {
        GameLevels::Mine().Update(.025f);
    }
}
void TestLevelLifecycle()
{
    GameLevels::Reset();
    assert(GameLevels::Active() == nullptr);
    ReadyMine();
    assert(GameLevels::Active() == &GameLevels::Mine());
    assert(GameLevels::Active()->UsesWalkingControls());
}
void TestWeapon()
{
    static_assert(std::is_base_of<combat::Weapon, combat::Firearm>::value);
    static_assert(std::is_base_of<combat::Firearm, combat::Pistol>::value);
    combat::Pistol gun;
    combat::Weapon &weapon = gun;
    combat::Shot shot;
    assert(!weapon.Reload());
    assert(weapon.TryFire(.5f, shot));
    assert(std::fabs(shot.angle - .5f) <= .0081f);
    assert(gun.GetRounds() == 11);
    assert(!weapon.TryFire(.5f, shot));
    float initial = gun.GetSpread();
    for (int i = 1; i < 12; ++i)
    {
        weapon.Update(.2f);
        assert(weapon.TryFire(.5f, shot));
    }
    assert(gun.GetRounds() == 0 && gun.GetSpread() > initial);
    weapon.Update(.2f);
    assert(!weapon.TryFire(.5f, shot) && gun.IsReloading());
    assert(!weapon.TryFire(.5f, shot) && !weapon.Reload());
    weapon.Update(1.f);
    assert(gun.IsReloading() && gun.GetRounds() == 0);
    weapon.Update(.5f);
    assert(!gun.IsReloading() && gun.GetRounds() == 12);
    assert(weapon.TryFire(.5f, shot));
    assert(weapon.Reload());
    weapon.Update(2.f);
    assert(gun.GetRounds() == 12 && gun.GetSpread() <= .0081f);
    assert(!weapon.TryFire(std::numeric_limits<float>::quiet_NaN(), shot));
    weapon.Update(-1);
    assert(gun.GetRounds() == 12);
}
void TestQuest()
{
    MineQuest quest;
    assert(!quest.BeginJourney());
    assert(!quest.Advance(MineQuestStage::ReturnToHalo));
    assert(quest.Advance(MineQuestStage::ShowRecord));
    assert(!quest.Advance(MineQuestStage::ShowRecord));
    assert(quest.Advance(MineQuestStage::CheckCityRelay));
    assert(!quest.Advance(MineQuestStage::TravelToMine));
    assert(quest.BeginJourney() && quest.IsRiaJoined());
    assert(quest.Advance(MineQuestStage::TravelToMine));
    assert(!quest.IsPowerBypassed());
    assert(quest.Advance(MineQuestStage::BypassPower));
    assert(quest.IsPowerBypassed());
    assert(quest.Advance(MineQuestStage::InvestigateSignal));
    assert(quest.Advance(MineQuestStage::InvestigateGlyph));
    assert(quest.Advance(MineQuestStage::CollectCoordinates));
    assert(quest.AreCoordinatesFound());
    assert(quest.Advance(MineQuestStage::ReturnToHalo));
    assert(!quest.Advance(MineQuestStage::Complete));
}
void TestMapAndGate()
{
    static_assert(kMapSize * kMapSize == 4 * 64 * 64);
    std::vector<Tile> previous;
    for (unsigned seed = 0; seed < 30; ++seed)
    {
        ReadyMine(seed);
        if (!previous.empty())
        {
            assert(previous != mineState.tiles);
        }
        previous = mineState.tiles;
        GameLevels::Mine().Map().Generate(seed);
        assert(previous == mineState.tiles); // A reported seed reproduces the exact layout.
        assert(!GameLevels::Mine().Map().FindRoute(mineState.entry, kPowerBypass).empty());
        assert(GameLevels::Mine().Map().FindRoute(mineState.entry, kRuinSignal).empty());
        assert(!GameLevels::Mine().Map().IsWalkable(kMineGateX, kMineGateY));
        assert(mineState.survivors.size() == 12 && mineState.rescueGroups.size() == 4);
        assert(mineState.enemies.size() == kEnemyPopulation);
        assert(mineState.quest.Advance(MineQuestStage::BypassPower));
        assert(GameLevels::Mine().Map().IsWalkable(kMineGateX, kMineGateY));
        assert(!GameLevels::Mine().Map().FindRoute(mineState.entry, kRuinSignal).empty());
        assert(!GameLevels::Mine().Map().FindRoute(mineState.entry, kObserverGlyph).empty());
        assert(!GameLevels::Mine().Map().FindRoute(mineState.entry, kCoordinateTerminal).empty());
        for (const auto &npc : mineState.survivors)
        {
            assert(!GameLevels::Mine().Map().IsBlocked(npc.position));
            assert(!GameLevels::Mine().Map().FindRoute(npc.position, mineState.entry).empty());
        }
        tutorial::Game::Get().GetState().playerPosition = mineState.entry;
        GameLevels::Mine().Map().UpdateNavigation();
        for (int cell : mineState.reachableCells)
        {
            assert(mineState.navigationDistances[cell] >= 0);
        }
        int junctions = 0;
        for (int cell : mineState.reachableCells)
        {
            int x = cell % kMapSize, y = cell / kMapSize;
            int exits = 0;
            for (Vector2 d : {Vector2{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
            {
                exits += GameLevels::Mine().Map().IsWalkable(x + int(d.x), y + int(d.y)) ? 1 : 0;
            }
            junctions += exits >= 3 ? 1 : 0;
        }
        assert(junctions > 100 && mineState.reachableCells.size() > 3000);
    }
}
void TestConnectedOverworld()
{
    auto &game = tutorial::Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    std::array<int, tutorial::Overworld::kCityCount> population = {2, 0, 0, 0, 0};
    for (const auto &resident : world.residents)
    {
        ++population[resident.cityIndex];
        assert(!game.IsPositionBlocked(resident.position));
    }
    for (int count : population)
    {
        assert(count == tutorial::Overworld::kPopulationPerCity);
    }
    const auto &cities = tutorial::Overworld::Cities();
    const auto &network = tutorial::RoadNetwork::Get();
    std::vector<bool> reached(network.junctions.size(), false);
    std::vector<int> pending = {0};
    reached[0] = true;
    for (size_t i = 0; i < pending.size(); ++i)
    {
        for (int lane : network.junctions[pending[i]].outgoing)
        {
            int next = network.lanes[lane].to;
            if (!reached[next])
            {
                reached[next] = true;
                pending.push_back(next);
            }
        }
    }
    assert(std::all_of(reached.begin(), reached.end(), [](bool b) { return b; }));
    // City positions occupy different sectors around Halo, with reproducible organic offsets.
    assert(cities[1].center.x > 0 && cities[2].center.y > 0 && cities[3].center.x < 0 &&
           cities[4].center.y < 0);
    for (const auto &city : cities)
    {
        assert(city.center.x > tutorial::kWorldMinX + 50 && city.center.x < tutorial::kWorldMaxX - 50);
    }
    for (float yaw : {0.f, tutorial::kPi * .5f})
    {
        world.bikePosition = {70, 50};
        world.bikeYaw = yaw;
        tutorial::Overworld::ToggleBike(world);
        assert(world.isBikeMounted && GameLevels::Active() == nullptr);
        world.pressedKeys['w'] = true;
        for (int frame = 0; frame < 120; ++frame)
        {
            tutorial::Overworld::UpdateBike(world, 1.f / 60);
        }
        assert(tutorial::Game::Distance(world.playerPosition, Vector2{70, 50}) > 12);
        assert(tutorial::Game::Distance(world.playerPosition, Vector2{70, 50}) < 15);
        assert(std::fabs(world.bikeSpeed - tutorial::Overworld::kBikeCruiseSpeed) < .001f);
        world.pressedKeys['w'] = false;
        for (int frame = 0; frame < 30; ++frame)
        {
            tutorial::Overworld::UpdateBike(world, 1.f / 60);
        }
        auto parked = world.bikePosition;
        tutorial::Overworld::ToggleBike(world);
        assert(!world.isBikeMounted && world.bikeSpeed == 0);
        assert(tutorial::Game::Distance(parked, world.bikePosition) < .001f);
    }
    world.bikePosition = tutorial::Overworld::kMineEntrance;
    world.playerPosition = {world.bikePosition.x + 1.2f, world.bikePosition.y};
    assert(game.FindNearbyInteraction() == tutorial::InteractionTarget::MineEntrance);
    auto snapshot = monitoring::WorldSnapshot::Build(1, "overworld-test");
    for (const auto &city : cities)
    {
        assert(snapshot.find(std::string("\"id\":\"") + city.id + "\"") != std::string::npos);
    }
    game.Reset();
}
void TestCityQuestAccess()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    bool home = false, observatory = false;
    for (const auto &building : world.buildings)
    {
        assert(!CityLayout::OverlapsQuestSpace(building));
        home |= building.type == models::BuildingType::Habitat &&
                Game::Distance({building.x, building.y}, kHomeBuildingPosition) < .001f;
        observatory |= building.type == models::BuildingType::Observatory &&
                       Game::Distance({building.x, building.y}, kObservatoryBuildingPosition) < .001f;
    }
    assert(home && observatory);
    for (const auto &resident : world.residents)
    {
        assert(!CityLayout::IsQuestSpace(resident.position, .5f));
    }
    auto clear = [&](Vector2 position) {
        for (float dx : {-.65f, 0.f, .65f})
        {
            for (float dy : {-.65f, 0.f, .65f})
            {
                assert(!game.IsPositionBlocked({position.x + dx, position.y + dy}));
            }
        }
    };
    for (Vector2 point : {kHomeDoor, kLiaPosition, kMaraPosition, kRelayPosition, kBeaconPosition,
                          kCityReceiverPosition, kCityKainPosition, kJetBikePosition})
    {
        clear(point);
        assert(CityLayout::IsQuestSpace(point));
        assert(!RoadNetwork::Get().Contains(point));
    }
    // Verify the actual approach paths, rather than only testing individual spawn centers.
    const Vector2 route[] = {kHomeDoor,        kLiaPosition, kMaraPosition,         kRelayPosition,
                             kBeaconPosition,  kLiaPosition, kCityReceiverPosition, {kJetBikePosition.x, -3},
                             kJetBikePosition, kBikeRoadExit};
    for (size_t i = 1; i < std::size(route); ++i)
    {
        int steps = int(std::ceil(Game::Distance(route[i - 1], route[i]) / .1f));
        for (int step = 0; step <= steps; ++step)
        {
            float t = float(step) / steps;
            clear({route[i - 1].x + (route[i].x - route[i - 1].x) * t,
                   route[i - 1].y + (route[i].y - route[i - 1].y) * t});
        }
    }
    game.Reset();
    // Follow the production interaction sequence through the relocated outdoor targets.
    world.playerPosition = {1, -1.2f};
    game.InteractWithNearbyTarget();
    ReadDialogue();
    world.playerPosition = {2.4f, 2.4f};
    game.InteractWithNearbyTarget();
    ReadDialogue();
    assert(!world.isInsideHome && Game::Distance(world.playerPosition, kHomeDoor) < .001f);
    auto interact = [&](Vector2 position, InteractionTarget target) {
        world.playerPosition = position;
        assert(game.FindNearbyInteraction() == target);
        game.InteractWithNearbyTarget();
        ReadDialogue();
    };
    interact(kLiaPosition, InteractionTarget::Lia);
    interact(kMaraPosition, InteractionTarget::Mara);
    interact(kRelayPosition, InteractionTarget::PowerRelay);
    interact(kMaraPosition, InteractionTarget::Mara);
    assert(world.isSideQuestComplete);
    interact(kBeaconPosition, InteractionTarget::GateBeacon);
    assert(world.isInObservatory);
    interact(kObservatoryConsole, InteractionTarget::ObservatorySignal);
    interact(kObservatoryStairs, InteractionTarget::ObservatoryExit);
    assert(!world.isInObservatory && Game::Distance(world.playerPosition, kBeaconPosition) < .001f);
    interact(kLiaPosition, InteractionTarget::Lia);
    assert(mineState.quest.GetStage() == MineQuestStage::TravelToMine);
    assert(Game::Distance(world.bikePosition, kJetBikePosition) < .001f && world.bikeYaw == kJetBikeYaw);
    interact({kJetBikePosition.x + 1.2f, kJetBikePosition.y}, InteractionTarget::JetBike);
    assert(world.isBikeMounted);
    world.pressedKeys['w'] = true;
    for (int frame = 0; frame < 120 && world.playerPosition.y > kBikeRoadExit.y; ++frame)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(world.playerPosition.y <= kBikeRoadExit.y && world.bikeSpeed > 0);
    world.pressedKeys['w'] = false;
    for (int frame = 0; frame < 30; ++frame)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(!game.IsPositionBlocked(world.bikePosition)); // Braking may coast onto the open shoulder.
    auto parked = world.bikePosition;
    game.InteractWithNearbyTarget();
    ReadDialogue();
    assert(!world.isBikeMounted && Game::Distance(parked, world.bikePosition) < .001f);
    assert(!game.IsPositionBlocked(world.playerPosition));
    game.Reset();
}
void TestImportedWorldAndFlights()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    const auto &props = Overworld::Scenery().Props();
    assert(props.size() > 100);
    size_t pads = 0, parked = 0;
    for (const auto &prop : props)
    {
        assert(prop.model && prop.model->resourceId);
        assert(
            !CityLayout::OverlapsQuestSpace({prop.minimum.x, prop.minimum.y, prop.maximum.x - prop.minimum.x,
                                             prop.maximum.y - prop.minimum.y, 0},
                                            .7f));
        pads += prop.type == models::SceneryType::LandingPad;
        parked += prop.type == models::SceneryType::Count;
        if (prop.solid)
        {
            // An open shelter's AABB centre is walkable. Probe an actual low body surface
            // instead of incorrectly requiring its empty centre/canopy to be solid.
            const auto &mesh = models::ResourceAssets::Get(prop.model->resourceId);
            for (const auto &vertex : mesh.packedVertices)
            {
                if (vertex.z > .05f && vertex.z < .9f)
                {
                    Vector2 point = {
                        prop.position.x + vertex.x * std::cos(prop.yaw) - vertex.y * std::sin(prop.yaw),
                        prop.position.y + vertex.x * std::sin(prop.yaw) + vertex.y * std::cos(prop.yaw)};
                    assert(game.IsPositionBlocked(point));
                    break;
                }
            }
        }
    }
    assert(pads == Overworld::kCityCount && parked == pads);
    for (const auto &person : world.residents)
    {
        assert(!game.IsPositionBlocked(person.position));
    }
    for (int route = 0; route < int(RoadNetwork::Get().airCorridors.size()); ++route)
    {
        for (float t : {-.1f, 0.f, .1f, .3f, .5f, .7f, .9f, 1.f, 1.1f})
        {
            auto tangent = AirTraffic::Tangent(route, t);
            assert(std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y) > 30);
            assert(Game::Distance(AirTraffic::Position(route, t - .0001f),
                                  AirTraffic::Position(route, t + .0001f)) < .3f);
        }
    }
    auto &traffic = Overworld::Flights();
    std::unordered_map<std::uint64_t, AircraftActor> previous;
    bool visible = false;
    unsigned int variants = 0;
    for (int frame = 0; frame < 3600; ++frame)
    {
        traffic.Update(world, 1.f / 60);
        assert(traffic.Aircraft().size() <= AirTraffic::kAircraftLimit);
        std::unordered_map<std::uint64_t, AircraftActor> current;
        for (const auto &craft : traffic.Aircraft())
        {
            variants |= 1u << (unsigned(craft.type) * 2 + (craft.alternate ? 1 : 0));
            auto old = previous.find(craft.id);
            if (old == previous.end())
            {
                assert(TrafficSystem::OutsideView(craft.position, world.cameraPosition, 270, craft.altitude));
            }
            else
            {
                assert(Game::Distance(old->second.position, craft.position) < .8f);
            }
            visible |= !TrafficSystem::OutsideView(craft.position, world.cameraPosition, 0, craft.altitude);
            assert(std::fabs(craft.bank) <= .181f);
            current.emplace(craft.id, craft);
        }
        for (const auto &old : previous)
        {
            if (!current.count(old.first))
            {
                assert(Game::Distance(old.second.position, world.playerPosition) > 179);
                assert(TrafficSystem::OutsideView(old.second.position, world.cameraPosition, 330,
                                                  old.second.altitude));
                // The last sampled hull AND shadow must already be outside before despawn.
                assert(AirTraffic::OutsideVisibleFootprint(old.second, world.cameraPosition, 300));
            }
        }
        previous.swap(current);
    }
    assert(visible && variants == 255);
    game.Reset();
}
void TestUrbanTraffic()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    world.cameraPosition = world.playerPosition = {0, 0};
    const auto &network = RoadNetwork::Get();
    bool two = false, four = false, elevated = false, inclined = false;
    for (const auto &road : network.roads)
    {
        two |= road.lanes == 2;
        four |= road.lanes == 4;
        elevated |= network.junctions[road.from].altitude > 1 || network.junctions[road.to].altitude > 1;
        assert(road.lanes <= 4);
        auto a = network.junctions[road.from].position, b = network.junctions[road.to].position;
        inclined |= std::fabs(a.x - b.x) > 2 && std::fabs(a.y - b.y) > 2;
    }
    assert(two && four && elevated && inclined && network.airCorridors.size() == 4);
    assert(!TransitLayout::Get().empty());
    for (const auto &module : TransitLayout::Get())
    {
        assert(models::ModelLibrary::Transit(module.asset).resourceId);
    }
    for (const auto &lane : network.lanes)
    {
        assert(lane.length > 3.5f && std::fabs(Game::Distance(lane.start, lane.end) - lane.length) < .001f);
        auto mid = network.LanePosition(int(&lane - network.lanes.data()), lane.length * .5f);
        assert(network.ContainsSurface(mid, network.RoadElevation(lane.road, .5f)));
        auto origin = network.junctions[lane.from].position;
        auto right = RoadNetwork::RightOf(lane.direction);
        assert((mid.x - origin.x) * right.x + (mid.y - origin.y) * right.y > 0);
        // Test the displayed driving side independently of RightOf's implementation.
        auto screenDirection =
            Vector2{(lane.direction.x - lane.direction.y) * 36, (lane.direction.x + lane.direction.y) * 18};
        auto screenOffset = Vector2{(right.x - right.y) * 36, (right.x + right.y) * 18};
        assert(screenDirection.x * screenOffset.y - screenDirection.y * screenOffset.x > 0);
    }
    assert(!network.Contains(kLiaPosition) && !network.Contains(kMaraPosition));
    assert(TrafficSystem::SweptHit({-5, 0}, {5, 0}, 0, {0, 0}));
    assert(!TrafficSystem::SweptHit({-5, 0}, {5, 0}, 0, {0, 2}));
    assert(TrafficSystem::SweptHit({0, -5}, {0, 5}, kPi * .5f, {0, 0}));
    auto &traffic = Overworld::Traffic();
    std::unordered_map<std::uint64_t, TrafficVehicle> previous;
    bool sawTurn = false, sawStop = false, sawWalk = false, sawCar = false;
    bool sawSingleWalk = false, sawAxisWalk = false, sawRedRight = false;
    for (int frame = 0; frame < 7200; ++frame)
    {
        traffic.Update(world, 1.f / 60);
        assert(traffic.Vehicles().size() <= TrafficSystem::kVehicleLimit);
        std::unordered_map<std::uint64_t, TrafficVehicle> current;
        for (const auto &car : traffic.Vehicles())
        {
            sawCar = true;
            assert(network.ContainsSurface(
                car.position, traffic.RenderElevation(size_t(&car - traffic.Vehicles().data())), .25f));
            auto old = previous.find(car.id);
            if (old == previous.end())
            {
                assert(TrafficSystem::OutsideView(
                    car.position, world.cameraPosition, 230,
                    traffic.RenderElevation(size_t(&car - traffic.Vehicles().data()))));
            }
            else
            {
                assert(Game::Distance(old->second.position, car.position) < .8f);
                if (old->second.lane == car.lane && old->second.reservedFrom != car.lane &&
                    car.reservedFrom == car.lane && network.IsSignalized(network.lanes[car.lane].to))
                {
                    auto light = traffic.Light(car.lane, car.turn);
                    if (car.turn == TrafficTurn::Right && light == TrafficLight::Red)
                    {
                        assert(!traffic.HasRightTurnSignal(car.lane));
                        assert(old->second.stoppedSeconds >= .75f && car.speed <= 1.51f);
                        sawRedRight = true;
                    }
                    else
                    {
                        assert(light == TrafficLight::Green);
                    }
                }
                auto rendered = traffic.RenderPosition(size_t(&car - traffic.Vehicles().data()));
                assert(Game::Distance(rendered, car.position) < .8f);
            }
            sawTurn |= car.turnDistance >= 0 && car.turn != TrafficTurn::Straight;
            sawStop |= car.speed < .05f && traffic.Light(car.lane, car.turn) == TrafficLight::Red;
            current.emplace(car.id, car);
        }
        for (const auto &old : previous)
        {
            if (current.count(old.first) == 0)
            {
                assert(Game::Distance(old.second.position, world.playerPosition) >
                       TrafficSystem::kDespawnDistance - 1);
                assert(TrafficSystem::OutsideView(old.second.position, world.cameraPosition, 300));
            }
        }
        for (size_t j = 0; j < network.junctions.size(); ++j)
        {
            size_t arms = 0;
            for (int lane : network.junctions[j].outgoing)
            {
                arms += network.lanes[lane].slot == 0;
            }
            if (arms < 3 || !traffic.PedestrianGreen(int(j)))
            {
                continue;
            }
            sawWalk = true;
            int greens = 0, width = 0;
            for (int outgoing : network.junctions[j].outgoing)
            {
                if (network.lanes[outgoing].slot != 0)
                {
                    continue;
                }
                int road = network.lanes[outgoing].road;
                width = (std::max)(width, network.roads[road].lanes);
                greens += traffic.PedestrianGreen(int(j), road);
                if (!traffic.PedestrianGreen(int(j), road))
                {
                    assert(!traffic.CanStartCrossing(int(j), 1, road));
                }
            }
            sawSingleWalk |= width == 2 && greens == 1;
            sawAxisWalk |= width == 4 && greens > 0 && greens < int(arms);
            for (size_t lane = 0; lane < network.lanes.size(); ++lane)
            {
                if (network.lanes[lane].to != int(j))
                {
                    continue;
                }
                for (auto turn : {TrafficTurn::Straight, TrafficTurn::Left, TrafficTurn::Right})
                {
                    assert(traffic.Light(int(lane), turn) == TrafficLight::Red);
                }
            }
        }
        previous.swap(current);
    }
    assert(sawCar && sawTurn && sawStop && sawWalk);
    if (!sawSingleWalk || !sawAxisWalk)
    {
        std::cerr << "walk coverage " << sawSingleWalk << " " << sawAxisWalk << std::endl;
        for (size_t j = 0; j < network.junctions.size(); ++j)
        {
            if (network.IsSignalized(int(j)))
            {
                std::cerr << "junction " << j << " at " << network.junctions[j].position.x << ","
                          << network.junctions[j].position.y << " phase " << int(traffic.Signals()[j].phase)
                          << " elapsed " << traffic.Signals()[j].elapsed << std::endl;
            }
        }
    }
    assert(sawSingleWalk && sawAxisWalk);
    // sawWalk above must occur without any key or crossing request.
    // Production impact path, using an approaching moving vehicle rather than a damage mock.
    auto hit =
        std::find_if(traffic.Vehicles().begin(), traffic.Vehicles().end(), [&](const TrafficVehicle &car) {
            return car.speed > 1 && traffic.RenderElevation(size_t(&car - traffic.Vehicles().data())) < .1f;
        });
    assert(hit != traffic.Vehicles().end());
    world.playerPosition = hit->position;
    mineState.player.health = mineState.player.maxHealth;
    traffic.Update(world, .05f);
    assert(mineState.player.health < mineState.player.maxHealth);
    game.Reset();
}
void TestCityPlanAndSignalProgress()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    const auto &net = RoadNetwork::Get();
    assert(CityPlan::Connections().size() == world.buildings.size());
    assert(!CityPlan::Walkways().empty());
    for (const auto &connection : CityPlan::Connections())
    {
        assert(connection.path.size() >= 2);
        for (size_t segment = 1; segment < connection.path.size(); ++segment)
        {
            auto a = connection.path[segment - 1], b = connection.path[segment];
            int samples = int(std::ceil(Game::Distance(a, b) * 6)) + 1;
            for (int step = 0; step <= samples; ++step)
            {
                Vector2 p = {a.x + (b.x - a.x) * step / samples, a.y + (b.y - a.y) * step / samples};
                assert(!net.Contains(p, .1f));
                assert(!Overworld::Scenery().IsBlocked(p, .15f));
                for (const auto &building : world.buildings)
                {
                    assert(!(p.x > building.x - .2f && p.x < building.x + building.width + .2f &&
                             p.y > building.y - .2f && p.y < building.y + building.depth + .2f));
                }
            }
        }
    }
    auto home = std::find_if(world.buildings.begin(), world.buildings.end(), [](const Building &b) {
        return b.x == kHomeBuildingPosition.x && b.y == kHomeBuildingPosition.y;
    });
    assert(home != world.buildings.end());
    assert(world.bikePosition.x > home->x + home->width &&
           world.bikePosition.x < home->x + home->width + 1.6f);
    assert(world.bikePosition.y > home->y - 1 && world.bikePosition.y < home->y + home->depth + 1);
    world.playerPosition = world.cameraPosition = {-425, -425};
    world.residents.clear();
    Resident lingering;
    lingering.crossingJunction = 0;
    while (!net.IsSignalized(lingering.crossingJunction))
    {
        ++lingering.crossingJunction;
    }
    lingering.position = net.junctions[lingering.crossingJunction].position;
    world.residents.push_back(lingering);
    std::vector<int> changes(net.junctions.size(), 0);
    std::vector<SignalPhase> phases;
    for (const auto &signal : Overworld::Traffic().Signals())
    {
        phases.push_back(signal.phase);
    }
    std::vector<bool> crossingServed(net.roads.size() * 2, false);
    for (int frame = 0; frame < 18000; ++frame)
    {
        Overworld::Traffic().Update(world, 1.f / 60);
        for (size_t j = 0; j < phases.size(); ++j)
        {
            if (!net.IsSignalized(int(j)))
            {
                continue;
            }
            if (frame < 7200)
            {
                for (int lane : net.junctions[j].outgoing)
                {
                    if (net.lanes[lane].slot == 0 &&
                        Overworld::Traffic().PedestrianGreen(int(j), net.lanes[lane].road))
                    {
                        int road = net.lanes[lane].road;
                        crossingServed[road * 2 + (net.roads[road].from == int(j) ? 0 : 1)] = true;
                    }
                }
            }
            const auto &signal = Overworld::Traffic().Signals()[j];
            assert(signal.elapsed <= Overworld::Traffic().PhaseDuration(int(j)) + .1f);
            if (signal.phase != phases[j])
            {
                ++changes[j];
                phases[j] = signal.phase;
            }
        }
    }
    for (size_t j = 0; j < changes.size(); ++j)
    {
        if (net.IsSignalized(int(j)))
        {
            assert(changes[j] >= 14);
        }
    }
    for (size_t road = 0; road < net.roads.size(); ++road)
    {
        if (net.IsSignalized(net.roads[road].from))
        {
            assert(crossingServed[road * 2]);
        }
        if (net.IsSignalized(net.roads[road].to))
        {
            assert(crossingServed[road * 2 + 1]);
        }
    }
    game.Reset();
}
void TestVehicleQueueProgress()
{
    using namespace tutorial;
    auto &game = Game::Get();
    auto &traffic = Overworld::Traffic();
    const auto &net = RoadNetwork::Get();
    for (int city : {0, 1})
    {
        game.Reset();
        auto &world = game.GetState();
        world.isInsideHome = false;
        world.residents.clear();
        world.playerPosition = world.cameraPosition = Overworld::Cities()[city].center;
        for (int frame = 0; frame < 9000; ++frame)
        {
            traffic.Update(world, 1.f / 30);
            for (size_t j = 0; j < traffic.Signals().size(); ++j)
            {
                if (net.IsSignalized(int(j)))
                {
                    if (traffic.Signals()[j].elapsed >= traffic.PhaseDuration(int(j)) + 20)
                    {
                        std::cerr << "Held junction " << j << " at " << net.junctions[j].position.x << ","
                                  << net.junctions[j].position.y << " phase "
                                  << int(traffic.Signals()[j].phase) << std::endl;
                        for (const auto &car : traffic.Vehicles())
                        {
                            if (car.reservedFrom >= 0 && net.lanes[car.reservedFrom].to == int(j))
                            {
                                std::cerr << "car " << car.id << " lane " << car.lane << " next "
                                          << car.nextLane << " dist " << car.distance << " length "
                                          << net.lanes[car.lane].length << " speed " << car.speed << " turn "
                                          << car.turnDistance << " reservation " << car.reservedFrom << " -> "
                                          << car.reservedTo << std::endl;
                            }
                        }
                    }
                    assert(traffic.Signals()[j].elapsed < traffic.PhaseDuration(int(j)) + 20);
                }
            }
            for (const auto &car : traffic.Vehicles())
            {
                assert(net.ContainsSurface(
                    car.position, traffic.RenderElevation(size_t(&car - traffic.Vehicles().data())), .25f));
                if (car.speed > .01f || car.turnDistance >= 0 || car.distance < 1 ||
                    car.distance > net.lanes[car.lane].length - 5)
                {
                    continue;
                }
                bool queued = std::any_of(
                    traffic.Vehicles().begin(), traffic.Vehicles().end(), [&](const TrafficVehicle &leader) {
                        return leader.id != car.id && leader.lane == car.lane && leader.turnDistance < 0 &&
                               leader.distance > car.distance && leader.distance - car.distance < 4.1f;
                    });
                assert(queued); // A free approach must keep moving toward its marked stop line.
            }
        }
    }
    game.Reset();
}
void TestCityResidentsAndShadowLifetime()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    world.playerPosition = world.cameraPosition = {0, 0};
    assert(CityResidents::Get().NavigationNodeCount() > 1000);
    const auto initial = world.residents;
    bool moved = false, patrol = false, waiting = false, crossed = false, worked = false;
    for (int frame = 0; frame < 14400; ++frame)
    {
        Overworld::Traffic().Update(world, 1.f / 60);
        CityResidents::Get().Update(world, 1.f / 60);
        for (size_t i = 0; i < world.residents.size(); ++i)
        {
            const auto &resident = world.residents[i];
            moved |= Game::Distance(initial[i].position, resident.position) > 2;
            patrol |= resident.activity == ResidentActivity::Patrolling;
            waiting |= resident.activity == ResidentActivity::WaitingSignal;
            crossed |= resident.activity == ResidentActivity::Crossing;
            worked |= resident.activity == ResidentActivity::Working;
            assert(resident.routeCursor <= resident.route.size());
            assert(!Overworld::Scenery().IsBlocked(resident.position, .2f));
            assert(!CityLayout::IsQuestSpace(resident.position, .2f));
            if (resident.activity == ResidentActivity::Crossing)
            {
                const auto &signal = Overworld::Traffic().Signals()[resident.crossingJunction];
                if (signal.phase != SignalPhase::Walk && signal.phase != SignalPhase::WalkClear)
                {
                    for (const auto &car : Overworld::Traffic().Vehicles())
                    {
                        assert(car.reservedFrom < 0 ||
                               RoadNetwork::Get().lanes[car.reservedFrom].to != resident.crossingJunction);
                    }
                }
            }
            else
            {
                assert(!RoadNetwork::Get().Contains(resident.position, .2f));
            }
        }
    }
    assert(moved && patrol && waiting && crossed && worked);
    // A high hull can be offscreen while its projected shadow remains centered in the view.
    AircraftActor craft;
    craft.altitude = 200;
    craft.position = {-craft.altitude * .38f, -craft.altitude * .55f};
    assert(TrafficSystem::OutsideView(craft.position, {0, 0}, 0, craft.altitude));
    assert(!AirTraffic::OutsideVisibleFootprint(craft, {0, 0}));
    craft.position = {1000, 1000};
    assert(AirTraffic::OutsideVisibleFootprint(craft, {0, 0}));
    game.Reset();
}
void TestAimLightingAndWallShots()
{
    ReadyMine();
    // Combat fixtures are explicit; procedural topology is covered separately by TestMapAndGate.
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.lights.clear();
    for (int y = 0; y < kMapSize; ++y)
    {
        if (!GameLevels::Mine().Map().IsGateCell(kMineGateX, y))
        {
            mineState.tiles[y * kMapSize + kMineGateX] = Tile::Wall;
        }
    }
    mineState.enemies.clear();
    tutorial::Game::Get().GetState().playerPosition = {24.5f, 17.5f};
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    AimAt({24.5f, 23.5f});
    auto direction = MineLevel::GetAimDirection(GameLevels::Mine().GetAimAngle());
    assert(std::fabs(direction.x) < .0001f && direction.y > .99f);
    assert(GameLevels::Mine().Lighting().At({24.5f, 21.5f}) > .3f);
    assert(GameLevels::Mine().Lighting().At({24.5f, 13.5f}) < .01f);
    GameLevels::Mine().Combat().Attack();
    assert(mineState.bullets.size() == 1);
    auto muzzle = models::ModelRig::Muzzle(tutorial::Game::Get().PlayerPose(1));
    // Wall-safe muzzle advance follows the aim ray, before spread is applied.
    float muzzleDistance = std::hypot(muzzle.x, muzzle.y) * models::WorldScale::characterScale;
    assert(std::fabs(mineState.bullets[0].position.x - 24.5f) < .0001f);
    assert(std::fabs(mineState.bullets[0].position.y - 17.5f - muzzleDistance) < .0001f);
    mineState.bullets.clear();
    mineState.pistol.Reset();
    tutorial::Game::Get().GetState().playerPosition = {kMineGateX - 2.5f, kMineGateY + .5f};
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    Vector2 beyondGate = {kMineGateX + 3.5f, kMineGateY + .5f};
    AimAt(beyondGate);
    mineState.enemies.push_back({beyondGate});
    assert(GameLevels::Mine().Lighting().At(beyondGate) == 0);
    GameLevels::Mine().Combat().Attack();
    Step(.3f);
    assert(mineState.enemies[0].health == kEnemyMaxHealth);
    assert(mineState.bullets.empty());
    assert(mineState.quest.Advance(MineQuestStage::BypassPower));
    assert(GameLevels::Mine().Lighting().At(beyondGate) > .1f);
    tutorial::Game::Get().GetState().playerPosition = {20.5f, 10.5f};
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    mineState.enemies = {{{23.f, 10.5f}}};
    mineState.pistol.Reset();
    for (int i = 0; i < 3; ++i)
    {
        AimAt(mineState.enemies[0].position);
        GameLevels::Mine().Combat().Attack();
        Step(.225f);
    }
    assert(mineState.enemies[0].health == 0);
    assert(mineState.kills == 1 && mineState.player.experience == kExperiencePerKill);
    assert(mineState.drops.size() == 1);
}
void TestNpcSupport()
{
    using namespace tutorial;
    using ai::SupportTask;
    auto &game = Game::Get();
    game.Reset();
    ReadyMine();
    auto &world = game.GetState();
    auto &mine = GameLevels::Mine();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.enemies.clear();
    mineState.survivors = {{777, 0, {20.5f, 20.5f}}};
    auto &patient = mineState.survivors.front();
    patient.combat.health = 0;
    patient.combat.downedSeconds = 12;
    mineState.rescueGroups[0].rescued = false;
    mineState.riaPosition = {19.7f, 20.5f};
    world.playerPosition = {20.5f, 23.5f};
    const int credits = mineState.player.credits, experience = mineState.player.experience;
    const auto stage = mineState.quest.GetStage();
    for (int frame = 0; frame < 20; ++frame)
    {
        mine.Combat().UpdateNpcs(.05f);
        mine.Rescue().Update(.05f);
    }
    assert(mineState.riaCombat.support.task == SupportTask::Aid &&
           mineState.riaCombat.support.progressSeconds > .8f && patient.combat.health == 0);
    mineState.enemies = {{{21.5f, 20.5f}}};
    mine.Rescue().Update(.05f);
    assert(mineState.riaCombat.support.blocked && mineState.riaCombat.support.progressSeconds == 0);
    mineState.enemies.clear();
    for (int frame = 0; frame < 62; ++frame)
    {
        mine.Combat().UpdateNpcs(.05f);
        mine.Rescue().Update(.05f);
    }
    assert(patient.combat.health == 50 && patient.combat.downedSeconds == 0 && patient.combat.hitGrace > 2);
    assert(!mineState.rescueGroups[0].rescued && !patient.evacuating);
    assert(mineState.player.credits == credits && mineState.player.experience == experience &&
           mineState.quest.GetStage() == stage);
    patient.combat.health = 0;
    patient.combat.downedSeconds = 12;
    mine.Rescue().Update(.05f);
    assert(mineState.riaCombat.support.task == SupportTask::Aid);
    world.playerPosition = {40.5f, 40.5f};
    mine.Rescue().Update(.05f);
    assert(mineState.riaCombat.support.task == SupportTask::None);

    game.Reset();
    auto &chapter = GameLevels::Chapter();
    chapter.Enter(true);
    ReadDialogue();
    auto &field = chapter.GetState();
    std::fill(field.tiles.begin(), field.tiles.end(), FrontierTile::Grass);
    std::fill(field.surfaceObstacles.begin(), field.surfaceObstacles.end(), 0);
    field.structures.clear();
    field.scrappers.clear();
    field.survivors.clear();
    field.kainJoined = true;
    field.ria.position = {12, 10.5f};
    field.kain.position = {14.5f, 10.5f};
    world.playerPosition = {10.5f, 12.5f};
    const auto beforeCredits = mineState.player.credits;
    const bool generator = field.generatorRestored, convoy = field.convoyRescued;
    // The production party adapter can also be exercised independently of bullets and story interaction.
    FrontierParty party(chapter);
    party.Update(.05f);
    assert(field.ria.combat.support.task == SupportTask::Inspect &&
           field.kain.combat.support.task == SupportTask::Cover);
    const float guardRetry = field.kain.combat.support.retrySeconds;
    for (int frame = 0; frame < 5; ++frame)
    {
        party.Update(.05f);
    }
    assert(field.kain.combat.support.retrySeconds < guardRetry); // Work cover retains its route across ticks.
    for (int frame = 0; frame < 77; ++frame)
    {
        party.Update(.05f);
    }
    assert(field.ria.combat.support.task == SupportTask::Supply);
    assert(field.generatorRestored == generator && field.convoyRescued == convoy && !field.vehicleRepaired &&
           !field.escortStarted && mineState.player.credits == beforeCredits);
    const auto snapshot = monitoring::WorldSnapshot::Build(1, "support-test");
    assert(snapshot.find("\"support\":") != std::string::npos &&
           snapshot.find("\"duration_seconds\":") != std::string::npos);
    world.playerPosition = {5.5f, 30.5f};
    party.Update(.05f);
    assert(field.ria.combat.support.task == SupportTask::None &&
           field.kain.combat.support.task == SupportTask::None);
    game.Reset();
}
void TestMineCooperation()
{
    using namespace tutorial;
    Game::Get().Reset();
    ReadyMine();
    auto &world = Game::Get().GetState();
    auto &level = GameLevels::Mine();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.survivors.clear();
    world.playerPosition = {20.5f, 20.5f};
    mineState.riaPosition = {17.5f, 20.5f};
    mineState.riaCombat.pistol.Reset();
    mineState.enemies = {{{23.5f, 20.5f}}};
    const int rounds = mineState.riaCombat.pistol.GetRounds();
    level.Combat().UpdateNpcs(.05f);
    assert(mineState.riaCombat.teamwork.task == ai::TeamTask::HoldFire &&
           mineState.riaCombat.pistol.GetRounds() == rounds && mineState.bullets.empty());
    world.playerPosition.y += 4;
    level.Combat().UpdateNpcs(.05f);
    assert(mineState.riaCombat.pistol.GetRounds() == rounds - 1 && !mineState.bullets.empty());
    mineState.rescueGroups[0].combatants = true;
    mineState.survivors = {{888, 0, {17.5f, 21.5f}}};
    mineState.enemies = {{{23.5f, 20.5f}}, {{23.5f, 21.5f}}};
    level.Combat().UpdateNpcs(.05f);
    assert(mineState.riaCombat.teamwork.targetId != mineState.survivors.front().combat.teamwork.targetId &&
           mineState.riaCombat.teamwork.targetId > 0 &&
           mineState.survivors.front().combat.teamwork.targetId > 0);

    mineState.bullets.clear();
    mineState.survivors.clear();
    mineState.riaCombat.health = 0;
    world.playerPosition = {25.5f, 20.5f};
    for (int y = 0; y < kMapSize; ++y)
    {
        mineState.tiles[size_t(y * kMapSize + 22)] = Tile::Wall;
    }
    mineState.enemies = {{{23.5f, 20.5f}}, {{20.5f, 20.5f}}, {{13.5f, 20.5f}}};
    level.Combat().UpdateEnemies(.05f);
    assert(mineState.enemies[0].threat.Visible());
    assert(mineState.enemies[1].threat.Known() && !mineState.enemies[1].threat.Visible() &&
           mineState.enemies[1].teamwork.reporterId == 1);
    assert(!mineState.enemies[2].threat.Known());
    const auto reported = mineState.enemies[1].target;
    world.playerPosition = {65.5f, 20.5f};
    for (int frame = 0; frame < 45; ++frame)
    {
        level.Combat().UpdateEnemies(.05f);
        assert(!mineState.enemies[2].threat.Known());
        assert(Game::Distance(reported, mineState.enemies[1].target) < .001f);
    }
    assert(!mineState.enemies[1].threat.Known());
    Game::Get().Reset();
}
void TestMinePerceptionMemory()
{
    using namespace tutorial;
    ReadyMine();
    auto &world = Game::Get().GetState();
    auto &level = GameLevels::Mine();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    for (int y = 0; y < kMapSize; ++y)
    {
        mineState.tiles[size_t(y * kMapSize + 22)] = Tile::Wall;
    }
    mineState.survivors.clear();
    mineState.riaCombat.health = mineState.kain.combat.health = 0;
    mineState.enemies = {{{20.5f, 20.5f}}};
    auto &enemy = mineState.enemies.front();
    enemy.target = {21.5f, 20.5f};
    enemy.threat.Remember(ai::Stimulus::Sight, 4);
    world.playerPosition = {24.5f, 20.5f};
    const int health = mineState.player.health;
    for (int frame = 0; frame < 30; ++frame)
    {
        level.Combat().UpdateEnemies(.05f);
        assert(!enemy.threat.Visible() && Game::Distance(enemy.target, {21.5f, 20.5f}) < .001f);
        assert(mineState.player.health == health);
    }
    for (int frame = 0; frame < 60; ++frame)
    {
        level.Combat().UpdateEnemies(.05f);
    }
    assert(!enemy.threat.Known() && enemy.decision == ai::Decision::Routine);
    const auto stopped = enemy.position;
    level.Combat().UpdateEnemies(.05f);
    assert(Game::Distance(stopped, enemy.position) < .001f);
    world.playerPosition = {21.5f, 20.5f};
    mineState.navigationCooldown = 0;
    level.Combat().UpdateEnemies(.05f);
    assert(enemy.threat.Visible() && enemy.decision == ai::Decision::Engage &&
           Game::Distance(enemy.target, world.playerPosition) < .001f);
    mineState.enemies.clear();
    world.playerPosition = {24.5f, 20.5f};
    for (int i = 0; i < 5; ++i)
    {
        Enemy listener;
        listener.position = {20.5f, 25.5f + i};
        listener.target = world.playerPosition;
        listener.threat.Remember(ai::Stimulus::Sound, 4);
        mineState.enemies.push_back(listener);
    }
    const auto requests = profiling::ReadCounter(profiling::Counter::NpcMemoryPathRequests);
    level.Combat().UpdateEnemies(.05f);
    assert(profiling::ReadCounter(profiling::Counter::NpcMemoryPathRequests) - requests == 2);
    Game::Get().Reset();
}
void TestRescueAndScanner()
{
    ReadyMine();
    mineState.enemies.clear();
    tutorial::Game::Get().GetState().playerPosition = mineState.rescueGroups[0].position;
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    GameLevels::Mine().Rescue().Scan();
    assert(mineState.scannerSeconds > 0 && !mineState.scannerContacts.empty());
    float cooldown = mineState.scannerCooldown;
    GameLevels::Mine().Rescue().Scan();
    assert(mineState.scannerCooldown == cooldown);
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(mineState.rescueGroups[0].rescued && mineState.rescuedPeople == 3);
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(mineState.rescuedPeople == 3);
    for (const auto &npc : mineState.survivors)
    {
        if (npc.group == 0)
        {
            assert(npc.evacuating && !npc.route.empty());
        }
    }
    Step(3.f);
    assert(mineState.scannerSeconds == 0);
    assert(mineState.survivors.size() == 12); // Visible evacuees are never erased early.
    tutorial::Game::Get().GetState().playerPosition = {52.5f, 51.5f};
    tutorial::Game::Get().GetState().cameraPosition = tutorial::Game::Get().GetState().playerPosition;
    Step(3.f);
    assert(mineState.survivors.size() == 9 && mineState.returnedPeople == 3);
}
void TestScannerPropagation()
{
    ReadyMine();
    auto &level = GameLevels::Mine();
    auto &world = tutorial::Game::Get().GetState();
    const Vector2 origin = world.playerPosition;
    mineState.survivors.clear();
    mineState.riaNavigationCooldown = 1000;
    mineState.rescueGroups = {{{origin.x + 15, origin.y}, false},
                              {{origin.x + 30, origin.y}, true},
                              {{origin.x + 43, origin.y}, false}};
    level.Rescue().Scan();
    assert(mineState.scannerRadius == 0 && mineState.scannerContacts.empty());
    level.Rescue().Update(.5f);
    assert(std::fabs(mineState.scannerRadius - 7.5f) < .001f && mineState.scannerContacts.empty());
    level.Rescue().Update(.51f);
    assert(mineState.scannerContacts.size() == 1 && mineState.scannerContacts.front().group == 0);
    assert(std::fabs(mineState.scannerContacts.front().secondsRemaining - 4.99f) < .001f);
    world.playerPosition = {origin.x + 80, origin.y + 80};
    level.Rescue().Scan(); // Cooldown cannot move or restart a wave already in flight.
    assert(tutorial::Game::Distance(mineState.scannerOrigin, origin) < .001f);
    level.Rescue().Update(1.01f);
    assert(std::fabs(mineState.scannerRadius - 30.3f) < .001f && mineState.scannerContacts.size() == 2);
    level.Rescue().Update(1);
    assert(mineState.scannerSeconds == 0 && mineState.scannerRadius == kScannerRange);
    assert(mineState.scannerContacts.size() == 2); // The pulse never detects the 43-unit transmitter.
    level.Rescue().Update(3.1f);
    assert(mineState.scannerContacts.size() == 1 && mineState.scannerContacts.front().group == 1);
    level.Rescue().Update(1);
    assert(mineState.scannerContacts.empty());

    world.playerPosition = origin;
    level.Rescue().Scan();
    level.Rescue().Update(1.1f);
    assert(mineState.scannerContacts.size() == 1);
    mineState.rescueGroups[0].rescued = true;
    level.Rescue().Update(0);
    assert(mineState.scannerContacts.empty());

    // A long frame ages discoveries from their arrival time rather than granting five fresh seconds.
    mineState.scannerCooldown = 0;
    mineState.rescueGroups[0].rescued = false;
    level.Rescue().Scan();
    level.Rescue().Update(6.2f);
    assert(mineState.scannerSeconds == 0 && mineState.scannerContacts.size() == 1);
    assert(mineState.scannerContacts.front().group == 1 &&
           std::fabs(mineState.scannerContacts.front().secondsRemaining - .8f) < .001f);
    level.Rescue().Update(1);
    assert(mineState.scannerContacts.empty());
}
void TestNpcCombat()
{
    ReadyMine();
    auto &level = GameLevels::Mine();
    auto &world = tutorial::Game::Get().GetState();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.survivors.clear();
    mineState.riaPosition = mineState.entry;
    mineState.rescueGroups[0].position = {15.5f, 15.5f};
    mineState.rescueGroups[0].combatants = true;
    mineState.survivors.push_back({100, 0, {15.5f, 15.5f}});
    auto &npc = mineState.survivors.front();
    mineState.enemies = {{{16.f, 15.5f}}};
    int playerHealth = mineState.player.health;
    level.Combat().UpdateEnemies(.05f);
    assert(npc.combat.health == 88 && mineState.player.health == playerHealth);
    assert(mineState.enemies.front().target.x == npc.position.x);
    // A guard returns fire with its own magazine. NPC kills grant no player rewards.
    mineState.enemies = {{{18.5f, 15.5f}}};
    int xp = mineState.player.experience;
    level.Combat().UpdateNpcs(.05f);
    assert(npc.combat.engaged && npc.combat.pistol.GetRounds() == 11);
    assert(!mineState.bullets.empty() && !mineState.bullets.front().playerOwned);
    for (int i = 0; i < 80; ++i)
    {
        level.Combat().UpdateNpcs(.05f);
        level.Combat().Update(.05f);
    }
    assert(mineState.enemies.front().health == 0 && mineState.totalEnemyDeaths == 1);
    assert(mineState.kills == 0 && mineState.player.experience == xp && mineState.drops.empty());
    mineState.enemies = {{{18.5f, 15.5f}, 5000}};
    bool reloaded = false, reloading = false;
    int previous = npc.combat.pistol.GetRounds();
    for (int i = 0; i < 400; ++i)
    {
        level.Combat().UpdateNpcs(.05f);
        level.Combat().Update(.05f);
        int rounds = npc.combat.pistol.GetRounds();
        reloading |= npc.combat.pistol.IsReloading();
        reloaded |= rounds > previous;
        previous = rounds;
    }
    assert(reloading && reloaded && mineState.enemies.front().health < 5000);
    // A wall blocks both guard fire and monster acquisition.
    npc.combat.pistol.Reset();
    npc.combat.cooldown = 0;
    for (int y = 0; y < kMapSize; ++y)
    {
        mineState.tiles[y * kMapSize + 17] = Tile::Wall;
    }
    mineState.bullets.clear();
    level.Combat().UpdateNpcs(.1f);
    assert(!npc.combat.engaged && npc.combat.pistol.GetRounds() == 12);
    int health = npc.combat.health;
    level.Combat().UpdateEnemies(.1f);
    assert(npc.combat.health == health);
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    // Miners retreat and defend at close range without a firearm.
    mineState.rescueGroups[0].combatants = false;
    npc.position = {15.5f, 15.5f};
    mineState.enemies = {{{16.f, 15.5f}}};
    npc.combat.cooldown = 0;
    level.Combat().UpdateNpcs(.1f);
    assert(npc.combat.evading && npc.facing.walking && npc.position.x < 15.5f);
    assert(mineState.enemies.front().health == 32 && npc.combat.pistol.GetRounds() == 12);
    // Downed people stop walking and can recover or receive aid without losing rescue state.
    npc.combat.health = 12;
    npc.combat.hitGrace = 0;
    mineState.enemies = {{{npc.position.x + .2f, npc.position.y}}};
    level.Combat().UpdateEnemies(.1f);
    assert(npc.combat.health == 0 && npc.combat.downedSeconds > 0);
    auto position = npc.position;
    npc.evacuating = true;
    npc.route = {{15.5f, 14.5f}};
    level.Rescue().Update(.1f);
    assert(tutorial::Game::Distance(position, npc.position) < .001f);
    level.Combat().UpdateNpcs(12.1f);
    assert(npc.combat.health == 50 && npc.evacuating);
    npc.combat.health = 0;
    npc.combat.downedSeconds = 12;
    world.playerPosition = npc.position;
    assert(level.Rescue().TryInteract() && npc.combat.health == 50);
    npc.evacuating = false;
    npc.combat.health = 0;
    npc.combat.downedSeconds = 12;
    assert(!mineState.rescueGroups[0].rescued);
    assert(level.Rescue().TryInteract());
    assert(mineState.rescueGroups[0].rescued && npc.evacuating && npc.combat.health == 50 &&
           !npc.route.empty());
    // Player projectiles never turn a rescue NPC into a PK target.
    mineState.enemies.clear();
    mineState.bullets = {{{npc.position.x - 1, npc.position.y}, {1, 0}, 30, 5, 100, true}};
    level.Combat().Update(.1f);
    assert(npc.combat.health == 50);
    // Ria can also be attacked independently of the player.
    world.playerPosition = mineState.entry;
    mineState.survivors.clear();
    mineState.riaPosition = {20.5f, 20.5f};
    mineState.enemies = {{{21.f, 20.5f}}};
    level.Combat().UpdateEnemies(.1f);
    assert(mineState.riaCombat.health == 88);
    mineState.riaPosition = mineState.entry;
    mineState.survivors.push_back({101, 0, {mineState.entry.x + 1, mineState.entry.y}});
    mineState.enemies = {{{mineState.entry.x + 1.5f, mineState.entry.y}}};
    level.Combat().UpdateEnemies(.1f);
    assert(mineState.survivors.front().combat.health == 100 && !mineState.enemies.front().facing.walking);
    // NPC muzzle flashes reveal their source in darkness and still obey walls.
    mineState.lights.clear();
    mineState.tracers.clear();
    mineState.muzzleFlash = 0;
    auto &flashingNpc = mineState.survivors.front();
    flashingNpc.position = {40.5f, 40.5f};
    flashingNpc.combat.muzzleFlash = .05f;
    assert(level.Lighting().At(flashingNpc.position) > .99f);
    assert(level.Lighting().At({42.5f, 40.5f}) > .5f);
    mineState.tiles[40 * kMapSize + 41] = Tile::Wall;
    assert(level.Lighting().At({42.5f, 40.5f}) < .01f);
    flashingNpc.combat.muzzleFlash = 0;
    assert(level.Lighting().At(flashingNpc.position) < .01f);
    mineState.riaPosition = {45.5f, 45.5f};
    mineState.riaCombat.muzzleFlash = .05f;
    assert(level.Lighting().At(mineState.riaPosition) > .99f);
}
void TestMineNavigationProgress()
{
    using namespace tutorial;
    auto &g = Game::Get();
    g.Reset();
    ReadyMine();
    auto &level = GameLevels::Mine();
    auto &w = g.GetState();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.survivors.clear();
    mineState.enemies.clear();
    w.playerPosition = {24.5f, 20.5f};
    mineState.riaPosition = {22.6f, 20.5f};
    mineState.riaRoute = {w.playerPosition};
    mineState.riaNavigationCooldown = 1;
    const auto stopped = mineState.riaPosition;
    level.Rescue().Update(.05f);
    assert(Game::Distance(stopped, mineState.riaPosition) < .001f && mineState.riaRoute.empty());

    mineState.riaPosition = mineState.entry;
    mineState.enemies = {{{20.5f, 20.5f}}, {{20.5f, 21.5f}}};
    for (int frame = 0; frame < 160; ++frame)
    {
        level.Combat().UpdateEnemies(.05f);
        assert(Game::Distance(mineState.enemies[0].position, mineState.enemies[1].position) >= .4f);
        for (const auto &enemy : mineState.enemies)
        {
            assert(!level.Map().IsBlocked(enemy.position));
        }
    }
    mineState.enemies.clear();
    w.playerPosition = mineState.riaPosition = mineState.entry;
    mineState.tiles[20 * kMapSize + 21] = Tile::Wall;
    mineState.survivors = {{101, 0, {20.5f, 20.5f}}};
    auto &npc = mineState.survivors.front();
    npc.evacuating = true;
    npc.route = {{22.5f, 20.5f}};
    for (int frame = 0; frame < 24; ++frame)
    {
        level.Rescue().Update(.05f);
    }
    assert(!npc.route.empty() && Game::Distance(npc.route.back(), mineState.entry) < 1);
    assert(!level.Map().IsBlocked(npc.position));
    g.Reset();
}
void TestForwardFlashlight()
{
    using namespace tutorial;
    auto &g = Game::Get();
    g.EnterTestStage(TestStage::Mine);
    g.Resize(1280, 800);
    auto &w = g.GetState();
    auto &lighting = GameLevels::Mine().Lighting();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.lights.clear();
    mineState.tracers.clear();
    mineState.enemies.clear();
    mineState.survivors.clear();
    mineState.muzzleFlash = mineState.riaCombat.muzzleFlash = mineState.scannerSeconds = 0;
    w.playerPosition = w.cameraPosition = {32.5f, 32.5f};
    auto checkForward = [&]() {
        Vector2 forward = {std::cos(w.facingYaw), std::sin(w.facingYaw)};
        auto direction = MineLevel::GetAimDirection(lighting.FlashlightAngle());
        assert(direction.x * forward.x + direction.y * forward.y > .9999f);
        assert(lighting.At({w.playerPosition.x + forward.x * 6, w.playerPosition.y + forward.y * 6}) > .7f);
        assert(lighting.At({w.playerPosition.x - forward.x * 6, w.playerPosition.y - forward.y * 6}) < .01f);
    };
    // Free cursor motion cannot turn the unarmed actor or its beam.
    w.facingYaw = .4f;
    for (auto cursor : {Vector2{100, 100}, {1180, 700}})
    {
        g.MouseMotion(int(cursor.x), int(cursor.y));
        g.Update(1.f / 60);
        assert(w.facingYaw == .4f && !g.IsAiming());
        checkForward();
    }
    g.KeyDown('a', 0, 0);
    g.Update(1.f / 60);
    g.KeyUp('a', 0, 0);
    assert(std::fabs(std::remainder(w.facingYaw - .4f, 2 * kPi)) > .1f);
    checkForward();
    mineState.loadout.Select(combat::WeaponSlot::Secondary);
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 640, 300);
    for (int i = 0; i < 16; ++i)
    {
        float angle = i * 2 * kPi / 16;
        Vector2 target = {w.playerPosition.x + std::cos(angle) * 4, w.playerPosition.y + std::sin(angle) * 4};
        auto cursor = WorldCamera::ToViewport(
            {kCanvasWidth * .5f + (target.x - target.y - w.cameraPosition.x + w.cameraPosition.y) * 36,
             kCanvasHeight * .57f + (target.x + target.y - w.cameraPosition.x - w.cameraPosition.y) * 18 -
                 models::WorldScale::aimHeightPixels});
        g.MouseMotion(int(std::round(cursor.x)), int(std::round(cursor.y)));
        g.Update(1.f / 60);
        assert(g.IsAiming() && std::cos(w.facingYaw - angle) > .999f);
        checkForward();
    }
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 640, 300);
    float facing = w.facingYaw;
    g.MouseMotion(100, 100);
    g.Update(1.f / 60);
    assert(w.facingYaw == facing && !g.IsAiming());
    checkForward();
    g.Reset();
}
void TestWorldCamera()
{
    using tutorial::WorldCamera;
    for (Vector2 point : {Vector2{0, 0}, {1280, 800}, {640, 456}, {930, 390}})
    {
        auto restored = WorldCamera::FromViewport(WorldCamera::ToViewport(point));
        assert(tutorial::Game::Distance(point, restored) < .001f);
    }
    assert(WorldCamera::zoom > 1 && WorldCamera::zoom < 1.5f);
    ReadyMine();
    auto &world = tutorial::Game::Get().GetState();
    world.cameraPosition = {12, 9};
    for (Vector2 direction : {Vector2{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {1, -1}})
    {
        Vector2 target = {world.playerPosition.x + direction.x * 4, world.playerPosition.y + direction.y * 4};
        AimAt(target);
        auto actual = GameLevels::Mine().GetAimDirection(GameLevels::Mine().GetAimAngle());
        float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        assert(std::fabs(actual.x - direction.x / length) < .001f);
        assert(std::fabs(actual.y - direction.y / length) < .001f);
    }
}
void ChapterInteract(game::ChapterAction action)
{
    auto &level = game::GameLevels::Chapter();
    bool found = false;
    for (auto point : level.Interactions())
    {
        if (point.action == action)
        {
            assert(!level.FindRoute(game::kFrontierEntry, point.position).empty());
            tutorial::Game::Get().GetState().playerPosition = point.position;
            found = true;
            break;
        }
    }
    assert(found);
    ReadDialogue();
    level.Interact();
    if (action == game::ChapterAction::ConvoySupply)
    {
        assert(tutorial::Game::Get().GetState().ui.screen == tutorial::UiScreen::Supply);
        tutorial::Hud::ExchangeSupply();
        tutorial::Hud::CloseMenu();
    }
    ReadDialogue();
}
void CompleteChapterSite(int site, bool peaceful = true)
{
    auto &level = GameLevels::Chapter();
    auto &s = level.GetState();
    if (site == 0)
    {
        ChapterInteract(ChapterAction::Generator);
        assert(s.generatorInspected && !s.generatorRestored);
        ChapterInteract(ChapterAction::IsolateLine);
        assert(s.generatorRestored);
    }
    else if (site == 1)
    {
        ChapterInteract(ChapterAction::Kain);
        assert(s.convoyBriefed && s.survivors.size() == 5);
        // The repeated sixth response is evidence, never an extra rescue actor.
        ChapterInteract(ChapterAction::Receiver);
        assert(s.receiverInspected && s.survivors.size() == 5);
        ChapterInteract(ChapterAction::SurveyOne);
        ChapterInteract(ChapterAction::SurveyTwo);
        ChapterInteract(ChapterAction::SurveyThree);
        ChapterInteract(ChapterAction::Escort);
        assert(s.escortStarted && !s.convoyRescued);
        for (int i = 0; i < 700 && !s.convoyRescued; ++i)
        {
            level.Update(.05f);
        }
        ReadDialogue();
        assert(s.convoyRescued && s.kainJoined);
        for (const auto &person : s.survivors)
        {
            assert(person.arrived && !level.IsBlocked(person.position));
        }
    }
    else
    {
        if (peaceful)
        {
            ChapterInteract(ChapterAction::Scrappers);
            assert(!s.negotiated && !s.hostile);
            ChapterInteract(ChapterAction::TradeProof);
            ChapterInteract(ChapterAction::Scrappers);
            assert(s.negotiated && !s.hostile);
            for (const auto &person : s.scrappers)
            {
                assert(person.combat.health == 60);
            }
        }
        else
        {
            auto &world = tutorial::Game::Get().GetState();
            mineState.loadout.Select(combat::WeaponSlot::Secondary);
            s.invulnerability = 1000;
            for (size_t i = 0; i < s.scrappers.size(); ++i)
            {
                auto target = s.scrappers[i].position;
                bool found = false;
                for (Vector2 delta : {Vector2{2, 0}, {-2, 0}, {0, 2}, {0, -2}})
                {
                    Vector2 p = {target.x + delta.x, target.y + delta.y};
                    if (!level.IsBlocked(p) && level.HasLineOfSight(p, target))
                    {
                        world.playerPosition = world.cameraPosition = p;
                        found = true;
                        break;
                    }
                }
                assert(found);
                world.mouseFiring = true;
                for (int frame = 0; frame < 300 && s.scrappers[i].combat.health > 0; ++frame)
                {
                    AimAt(s.scrappers[i].position);
                    level.Update(.025f);
                }
                world.mouseFiring = false;
                assert(s.scrappers[i].combat.health == 0);
            }
            assert(!s.negotiated);
        }
        ChapterInteract(ChapterAction::Ledger);
        assert(s.ledgerFound);
    }
}
void TestChapterFollowUpInteractions()
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &world = g.GetState();
    auto &level = GameLevels::Chapter();
    g.Reset();
    level.Enter(true);
    auto &chapter = level.GetState();
    chapter.generatorRestored = chapter.portableLightRepaired = true;
    chapter.convoyEvacuated = chapter.convoyRescued = chapter.kainJoined = true;
    chapter.ria.position = {7.5f, 6.5f};
    chapter.kain.position = {8.5f, 7.5f};
    auto find = [&](ChapterAction action) {
        const auto points = level.Interactions();
        return std::any_of(points.begin(), points.end(),
                           [&](const auto &point) { return point.action == action; });
    };
    assert(level.CompanionAvailable(CompanionId::Ria) && find(ChapterAction::RiaTalk));
    assert(find(ChapterAction::KainTalk) && ChapterOneLevel::IsOptionalConversation(ChapterAction::KainTalk));
    assert(!ChapterOneLevel::IsOptionalConversation(ChapterAction::PortableLight));
    chapter.ria.combat.health = 0;
    assert(!level.CompanionAvailable(CompanionId::Ria) && !find(ChapterAction::RiaTalk));
    chapter.ria.combat.health = 100;
    chapter.kain.combat.health = 0;
    assert(!find(ChapterAction::KainTalk));
    chapter.kain.combat.health = 100;
    const int salvage = mineState.player.salvage, credits = mineState.player.credits;
    const auto participation = world.missions.Status(MissionId::Frontier);
    world.playerPosition = chapter.ria.position;
    ChapterInteraction nearest{};
    assert(level.NearestInteraction(nearest) && nearest.action == ChapterAction::RiaTalk);
    level.Interact();
    assert(Dialogue::Active() && world.dialogue.sceneId == "chapter1/followup/ria" &&
           !world.dialogue.completion);
    const auto position = chapter.survivors.front().position;
    level.Update(1);
    assert(Game::Distance(position, chapter.survivors.front().position) == 0);
    ReadDialogue();
    const auto history = world.dialogue.history.size();
    level.Interact();
    ReadDialogue();
    assert(world.dialogue.history.size() == history);

    // Optional rest cannot pause a live firefight or commit any mission/reward state.
    chapter.hostile = true;
    chapter.bullets.push_back({});
    level.Interact();
    assert(!Dialogue::Active());
    chapter.hostile = false;
    chapter.bullets.clear();
    world.playerPosition = chapter.kain.position;
    level.Interact();
    assert(Dialogue::Active() && world.dialogue.sceneId == "chapter1/followup/kain");
    ReadDialogue();
    assert(mineState.player.salvage == salvage && mineState.player.credits == credits);
    assert(world.missions.Status(MissionId::Frontier) == participation && !chapter.recordsCompared &&
           !chapter.waterwayUnlocked && !chapter.convoyRewardClaimed);
    assert(chapter.generatorRestored && chapter.portableLightRepaired && chapter.convoyRescued);

    // The same physical conversations remain after leaving/re-entering; no duplicate joining/payout.
    level.Leave();
    level.Enter();
    assert(find(ChapterAction::RiaTalk) && find(ChapterAction::KainTalk));
    assert(mineState.player.salvage == salvage && mineState.player.credits == credits);
    g.Reset();
}

void TestStoryReturnFlow()
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &level = GameLevels::Chapter();
    g.Reset();
    level.Enter(true);
    auto &chapter = level.GetState();
    chapter.generatorRestored = chapter.convoyRescued = chapter.ledgerFound = true;
    chapter.recordsCompared = chapter.waterwayUnlocked = true;
    level.Leave();
    const int salvage = mineState.player.salvage, credits = mineState.player.credits;
    auto finish = [&](size_t choice) {
        for (unsigned page = 0; Dialogue::Active() && page < 128; ++page)
        {
            auto &d = g.GetState().dialogue;
            if (!d.pages[d.page].choices.empty() && d.selection < 0)
            {
                assert(Dialogue::Select(choice));
            }
            assert(Dialogue::Advance());
        }
        assert(!Dialogue::Active());
    };
    level.InteractHub();
    assert(Dialogue::Active() && !level.IsActive() && g.GetState().dialogue.sceneId == "chapter1/return");
    finish(1);
    assert(!level.IsActive());
    level.InteractHub();
    finish(0);
    assert(level.IsActive() && chapter.recordsCompared && chapter.waterwayUnlocked);
    const auto journal = Dialogue::Journal();
    assert(std::any_of(journal.begin(), journal.end(),
                       [](const auto &line) { return line.find("확인한 사실 /") == 0; }));
    assert(std::any_of(journal.begin(), journal.end(),
                       [](const auto &line) { return line.find("남은 의문 /") == 0; }));
    assert(std::none_of(journal.begin(), journal.end(), [](const auto &line) {
        return line.find("입구로 나와 바이크로") != std::string::npos;
    }));
    assert(mineState.player.salvage == salvage && mineState.player.credits == credits);
    assert(!chapter.convoyRewardClaimed);
    g.Reset();
}

void TestChapterOne()
{
    auto &game = tutorial::Game::Get();
    auto &level = GameLevels::Chapter();
    game.Reset();
    level.Enter();
    assert(!level.IsActive() && !level.IsAvailable());
    ReadyMine();
    for (auto stage : {MineQuestStage::BypassPower, MineQuestStage::InvestigateSignal,
                       MineQuestStage::InvestigateGlyph, MineQuestStage::CollectCoordinates})
    {
        assert(mineState.quest.Advance(stage));
    }
    GameLevels::Mine().Leave();
    assert(!level.IsAvailable() && !level.IsActive());
    game.GetState().playerPosition = tutorial::kLiaPosition;
    game.InteractWithNearbyTarget();
    ReadDialogue();
    assert(level.IsAvailable() && !level.IsActive());
    game.InteractWithNearbyTarget();
    ReadDialogue();
    assert(level.IsActive() && SceneManager::Current() == SceneKind::Frontier);
    level.Leave();
    assert(!level.IsActive() && SceneManager::Current() == SceneKind::Exterior);
    // All six permutations must reach the same chapter conclusion.
    std::array<int, 3> order = {0, 1, 2};
    do
    {
        game.Reset();
        level.Enter(true);
        for (int site : order)
        {
            CompleteChapterSite(site);
        }
        auto &s = level.GetState();
        assert(level.CompletedSites() == 3 && !s.recordsCompared);
        assert(!s.vehicleRepaired && !s.portableLightRepaired && !s.detailLedgerFound);
        level.Leave();
        game.InteractWithNearbyTarget();
        assert(s.comparisonStarted && !s.recordsCompared && !level.IsActive());
        level.Update(3.1f);
        assert(!s.recordsCompared);
        level.Update(500.f);
        assert(!s.recordsCompared && !s.waterwayUnlocked);
        ReadDialogue();
        assert(s.recordsCompared && s.waterwayUnlocked);
        level.Enter();
        assert(level.CompletedSites() == 3 && s.kainJoined && s.recordsCompared);
        auto snapshot = monitoring::WorldSnapshot::Build(1, "chapter-test");
        assert(snapshot.find("\"current_region_id\":\"frontier\"") != std::string::npos);
        assert(snapshot.find("\"main.chapter1_complete\":true") != std::string::npos);
    } while (std::next_permutation(order.begin(), order.end()));
    // Departure requires an explicit choice, and the confirmation key cannot also exit the field.
    game.Reset();
    level.Enter(true);
    level.Leave();
    game.KeyDown('e', 0, 0);
    assert(tutorial::Dialogue::Active() && !level.IsActive());
    game.KeyDown('e', 0, 0);
    assert(!level.IsActive());
    game.KeyUp('e', 0, 0);
    game.KeyDown('1', 0, 0);
    game.KeyUp('1', 0, 0);
    game.KeyDown('e', 0, 0);
    assert(level.IsActive());
    game.KeyDown('e', 0, 0);
    assert(level.IsActive());
    game.KeyUp('e', 0, 0);
    game.KeyDown('e', 0, 0);
    assert(!level.IsActive());
    game.KeyUp('e', 0, 0);
    game.Reset();
    level.Enter(true);
    ChapterInteract(ChapterAction::Kain);
    ChapterInteract(ChapterAction::SurveyOne);
    ChapterInteract(ChapterAction::SurveyTwo);
    ChapterInteract(ChapterAction::SurveyThree);
    ChapterInteract(ChapterAction::Escort);
    level.Update(1);
    auto pausedPosition = level.GetState().survivors.front().position;
    level.Leave();
    level.Update(5);
    assert(tutorial::Game::Distance(pausedPosition, level.GetState().survivors.front().position) < .001f);
    level.Enter();
    for (int frame = 0; frame < 700 && !level.GetState().convoyRescued; ++frame)
    {
        level.Update(.05f);
    }
    assert(level.GetState().convoyRescued);
    for (const auto &person : level.GetState().survivors)
    {
        assert(person.arrived && !person.facing.walking);
    }
    game.Reset();
    level.Enter(true);
    CompleteChapterSite(2, false);
    assert(level.GetState().ledgerFound && !level.GetState().negotiated);
    ChapterInteract(ChapterAction::DetailLedger);
    assert(level.GetState().detailLedgerFound);
    CompleteChapterSite(0);
    ChapterInteract(ChapterAction::PortableLight);
    CompleteChapterSite(1);
    ChapterInteract(ChapterAction::Vehicle);
    int kits = mineState.player.recoveryKits, salvage = mineState.player.salvage;
    assert(salvage >= 2);
    ChapterInteract(ChapterAction::ConvoySupply);
    assert(mineState.player.recoveryKits == kits + 1 && mineState.player.salvage == salvage - 2);
    assert(level.GetState().portableLightRepaired && level.GetState().vehicleRepaired);
    auto &s = level.GetState();
    // No quest or optional outcome is lost on an early return/re-entry.
    level.Leave();
    level.Enter();
    assert(s.detailLedgerFound && s.portableLightRepaired && s.vehicleRepaired);
    game.Reset();
    level.Enter(true);
    // Companions retaliate, take damage and recover without blocking the story.
    s.hostile = true;
    s.ria.position = {31.5f, 31.5f};
    int rounds = s.ria.combat.pistol.GetRounds();
    for (int i = 0; i < 40; ++i)
    {
        level.Update(.05f);
    }
    assert(s.ria.combat.pistol.GetRounds() < rounds && s.ria.combat.health < 100);
    s.hostile = false;
    s.bullets.clear();
    s.ria.combat.health = 0;
    s.ria.combat.downedSeconds = 12;
    level.Update(12.1f);
    assert(s.ria.combat.health == 50);
    // Swept projectiles stop at a facility wall, and friendly shots cannot harm evacuees.
    // Imported proportions can change the footprint; locate an actual foundation edge.
    auto wall = std::find(s.tiles.begin(), s.tiles.end(), FrontierTile::Foundation);
    assert(wall != s.tiles.end());
    const int wallCell = int(wall - s.tiles.begin());
    Vector2 wallApproach = {float(wallCell % kFrontierWidth) + .5f, float(wallCell / kFrontierWidth) - .5f};
    s.bullets = {{{wallApproach, {0, 1}, 100, 20, 100, true}, ChapterTeam::Player}};
    level.Update(.1f);
    assert(s.bullets.empty());
    auto person = s.survivors.front().position;
    s.bullets = {{{{person.x - 1, person.y}, {1, 0}, 100, 20, 100, true}, ChapterTeam::Player}};
    level.Update(.02f);
    assert(s.survivors.front().combat.health == 100);
    mineState.player.health = 0;
    bool generator = s.generatorRestored;
    level.Update(.05f);
    assert(tutorial::HospitalRecovery::Active() && mineState.player.health <= 0);
    for (int i = 0; i < 60; ++i)
    {
        tutorial::HospitalRecovery::Update(.05f);
    }
    assert(!tutorial::HospitalRecovery::Active() && mineState.player.health == mineState.player.maxHealth);
    assert(s.generatorRestored == generator && !s.active);
    const auto &clinic =
        tutorial::CivicJustice::State().hospitals[size_t(tutorial::CivicJustice::State().hospital)];
    assert(tutorial::Game::Distance(game.GetState().playerPosition, clinic.position) < .1f);
    game.Reset();
}
void TestStoryAndReentry()
{
    ReadyMine();
    mineState.enemies.clear();
    tutorial::Game::Get().GetState().playerPosition = kCoordinateTerminal;
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(!mineState.quest.AreCoordinatesFound());
    tutorial::Game::Get().GetState().playerPosition = kPowerBypass;
    GameLevels::Mine().Interact();
    ReadDialogue();
    tutorial::Game::Get().GetState().playerPosition = kRuinSignal;
    GameLevels::Mine().Interact();
    ReadDialogue();
    tutorial::Game::Get().GetState().playerPosition = kObserverGlyph;
    GameLevels::Mine().Interact();
    ReadDialogue();
    tutorial::Game::Get().GetState().playerPosition = kCoordinateTerminal;
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(mineState.quest.GetStage() == MineQuestStage::ReturnToHalo);
    auto revision = mineState.mapRevision;
    mineState.player.salvage = 7;
    tutorial::Game::Get().GetState().playerPosition = mineState.entry;
    GameLevels::Mine().Interact();
    ReadDialogue();
    assert(!GameLevels::Mine().IsActive() && mineState.quest.GetStage() == MineQuestStage::ReturnToHalo);
    auto &world = tutorial::Game::Get().GetState();
    assert(tutorial::Game::Distance(world.playerPosition, world.mineReturnPosition) < .001f);
    world.isBikeMounted = false;
    world.playerPosition = tutorial::kLiaPosition;
    tutorial::Game::Get().InteractWithNearbyTarget();
    ReadDialogue();
    assert(mineState.quest.GetStage() == MineQuestStage::Complete);
    GameLevels::Mine().Enter();
    assert(GameLevels::Mine().IsActive() && mineState.mapRevision == revision &&
           mineState.player.salvage == 7);
    assert(mineState.quest.GetStage() == MineQuestStage::Complete);
}
void TestExplorationAndMelee()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &level = GameLevels::Mine();
    level.Enter(true);
    auto &state = level.GetState();
    auto &world = game.GetState();
    state.tiles.assign(kMapSize * kMapSize, Tile::Floor);
    state.explored.assign(kMapSize * kMapSize, 0);
    for (int y = 0; y < kMapSize; ++y)
    {
        state.tiles[y * kMapSize + 15] = Tile::Wall;
    }
    world.playerPosition = world.cameraPosition = {12.5f, 12.5f};
    level.Map().Reveal(world.playerPosition);
    assert(level.Map().IsExplored({14.5f, 12.5f}));
    assert(level.Map().IsExplored({15.5f, 12.5f}));
    assert(!level.Map().IsExplored({16.5f, 12.5f}));
    assert(!level.Map().IsExplored({90.5f, 90.5f}));
    level.Map().Reveal({20.5f, 12.5f});
    assert(level.Map().IsExplored({12.5f, 12.5f}) && level.Map().IsExplored({20.5f, 12.5f}));
    state.tiles.assign(kMapSize * kMapSize, Tile::Floor);
    state.tiles[13 * kMapSize + 13] = Tile::Wall;
    assert(!level.Map().IsBlocked({12.88f, 12.88f}));
    assert(level.Map().IsBlocked({12.9f, 12.9f}));
    state.tiles[13 * kMapSize + 13] = Tile::Floor;
    auto target = Drawing::WorldToScreen(14, 12.5f, models::WorldScale::aimHeightPixels);
    world.cursorPosition = target;
    auto aim = level.GetAimDirection(level.GetAimAngle());
    state.enemies = {{{world.playerPosition.x + aim.x * .8f, world.playerPosition.y + aim.y * .8f}},
                     {{world.playerPosition.x - aim.x * .8f, world.playerPosition.y - aim.y * .8f}},
                     {{world.playerPosition.x + aim.x * 3.f, world.playerPosition.y + aim.y * 3.f}}};
    int ammunition = state.pistol.GetRounds();
    // V key now triggers mission navigation guidance instead of melee attack.
    game.KeyDown('v', 0, 0);
    game.KeyUp('v', 0, 0);
    assert(state.meleeSeconds == 0 && state.enemies[0].health == kEnemyMaxHealth);
    level.Combat().Melee();
    assert(state.meleeSeconds > 0 && state.enemies[0].health == 10);
    assert(state.enemies[1].health == kEnemyMaxHealth && state.enemies[2].health == kEnemyMaxHealth);
    level.Combat().Melee();
    assert(state.enemies[0].health == 10 && state.pistol.GetRounds() == ammunition);
    level.Combat().Update(.7f);
    level.Combat().Melee();
    assert(state.enemies[0].health == 0 && state.kills == 1 && state.player.experience >= kExperiencePerKill);
    state.meleeSeconds = 0;
    world.playerPosition = world.cameraPosition = {12.9f, 13.2f};
    world.cursorPosition = Drawing::WorldToScreen(13.2f, 12.9f, models::WorldScale::aimHeightPixels);
    state.enemies = {{{13.2f, 12.9f}}};
    state.tiles[13 * kMapSize + 13] = Tile::Wall;
    level.Combat().Melee();
    assert(state.enemies[0].health == kEnemyMaxHealth); // No melee damage around a solid wall corner.
    state.meleeSeconds = 0;
    world.mapVisible = true;
    level.Combat().Melee();
    assert(state.meleeSeconds == 0);
    world.mapVisible = false;
    level.Map().Generate(703);
    assert(!level.Map().IsExplored({90.5f, 90.5f}));
    assert(level.Map().IsExplored(state.entry));
    game.Reset();
}
void TestBikeSpeedsAndSliding()
{
    using namespace tutorial;
    auto &game = Game::Get();
    game.Reset();
    auto &world = game.GetState();
    world.isInsideHome = false;
    world.isBikeMounted = true;
    world.playerPosition = world.bikePosition = {70, 50};
    world.bikeYaw = 0;
    world.pressedKeys['w'] = true;
    for (int i = 0; i < 120; ++i)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(std::fabs(world.bikeSpeed - Overworld::kBikeCruiseSpeed) < .001f);
    world.pressedKeys[' '] = true;
    for (int i = 0; i < 120; ++i)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(std::fabs(world.bikeSpeed - Overworld::kBikeBoostSpeed) < .001f);
    world.pressedKeys[' '] = world.pressedKeys['w'] = false;
    world.pressedKeys['s'] = true;
    for (int i = 0; i < 150; ++i)
    {
        Overworld::UpdateBike(world, 1.f / 60);
    }
    assert(std::fabs(world.bikeSpeed + Overworld::kBikeReverseSpeed) < .001f);
    auto before = world.playerPosition;
    Overworld::UpdateBike(world, .1f);
    assert(world.playerPosition.x < before.x && world.movementSpeed > 0 && world.movementAmount > 0);
    auto straight = BodyCollision::Slide({0, 0}, {2, 1}, [](Vector2 p) { return p.x > .5f; });
    assert(straight.x <= .501f && straight.y > .9f);
    auto skew = BodyCollision::Slide({0, 0}, {2, .5f}, [](Vector2 p) { return p.x + p.y > 1; });
    assert(skew.x + skew.y <= 1.001f && skew.x > 1 && skew.y < 0);
    auto overlap = BodyCollision::Slide({.52f, 0}, {.52f, 1}, [](Vector2 p) { return p.x > .5f; });
    assert(overlap.x <= .501f && overlap.y > .9f);
    game.Reset();
}
void TestAimMovementAndLayers()
{
    using namespace tutorial;
    auto &g = Game::Get();
    g.EnterTestStage(TestStage::Mine);
    g.Resize(1280, 800);
    auto &w = g.GetState();
    std::fill(mineState.tiles.begin(), mineState.tiles.end(), Tile::Floor);
    mineState.enemies.clear();
    mineState.survivors.clear();
    w.playerPosition = w.cameraPosition = {20.5f, 20.5f};
    for (auto slot : {combat::WeaponSlot::Unarmed, combat::WeaponSlot::Secondary})
    {
        mineState.loadout.Select(slot);
        g.MouseMotion(100, 300);
        auto before = w.playerPosition;
        g.KeyDown('d', 0, 0);
        for (int frame = 0; frame < 30; ++frame)
        {
            g.Update(1.f / 60);
        }
        g.KeyUp('d', 0, 0);
        auto delta = Vector2{w.playerPosition.x - before.x, w.playerPosition.y - before.y};
        assert(std::fabs(std::remainder(w.facingYaw - std::atan2(delta.y, delta.x), 2 * kPi)) < .001f);
        float yaw = w.facingYaw;
        g.MouseMotion(1000, 680);
        g.Update(1.f / 60);
        assert(w.facingYaw == yaw && !g.IsAiming());
    }
    mineState.loadout.Select(combat::WeaponSlot::Unarmed);
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 100, 300);
    assert(!g.IsAiming());
    mineState.loadout.Select(combat::WeaponSlot::Secondary);
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 100, 300);
    assert(g.IsAiming());
    g.KeyDown('d', 0, 0);
    auto before = w.playerPosition;
    for (int frame = 0; frame < 30; ++frame)
    {
        g.Update(1.f / 60);
    }
    assert(w.playerPosition.x > before.x && w.playerPosition.y < before.y);
    auto aim = MineLevel::GetAimDirection(GameLevels::AimAngle());
    assert(std::cos(w.facingYaw) * aim.x + std::sin(w.facingYaw) * aim.y > .995f);
    auto pose = g.PlayerPose();
    assert(pose.blended && pose.moveForward < 0 && pose.aimWeight > .99f);
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 100, 300);
    float beforeTurn = w.facingYaw;
    g.Update(1.f / 60);
    assert(!g.IsAiming() && std::fabs(std::remainder(w.facingYaw - beforeTurn, 2 * kPi)) <=
                                12.f * MotionMath::kResponse / 60 + .001f);
    for (int frame = 0; frame < 30; ++frame)
    {
        g.Update(1.f / 60);
    }
    assert(std::fabs(w.facingYaw + kPi / 4) < .001f);
    g.KeyUp('d', 0, 0);
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 100, 300);
    Hud::OpenMenu(UiScreen::Pause);
    assert(!w.mouseAiming && !g.IsAiming());
    Hud::CloseMenu();
    assert(!g.IsAiming());
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 100, 300);
    g.KeyDown('q', 0, 0);
    assert(w.weaponWheel.visible && !w.mouseAiming);
    g.KeyUp('q', 0, 0);
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 100, 300);
    g.MouseEntry(GLFW_FALSE);
    assert(!w.mouseAiming);
    w.cursorInsideViewport = true;
    // Hip fire follows the moving actor; aiming fire follows the cursor. Both use the same pose.
    w.facingYaw = .4f;
    mineState.meleeSeconds = 0;
    mineState.pistol.Reset();
    mineState.bullets.clear();
    GameLevels::Mine().Combat().Attack();
    assert(mineState.bullets.size() == 1);
    auto direction = mineState.bullets.front().direction;
    assert(direction.x * std::cos(.4f) + direction.y * std::sin(.4f) > .99f);
    assert(g.PlayerPose().fireSeconds > 0);
    assert(mineState.pistol.Reload());
    mineState.pistol.Update(.42f);
    assert(std::fabs(g.PlayerPose().reloadProgress - .3f) < .0001f);
    int reloadRounds = mineState.pistol.GetRounds();
    for (int i = 0; i < 30; ++i)
    {
        g.PlayerPose();
    }
    assert(mineState.pistol.GetRounds() == reloadRounds && mineState.pistol.IsReloading());
    mineState.loadout.Select(combat::WeaponSlot::Unarmed);
    assert(g.PlayerPose().reloadProgress < 0);
    mineState.loadout.Select(combat::WeaponSlot::Secondary);
    mineState.pistol.Update(1.f);
    assert(!mineState.pistol.IsReloading() && g.PlayerPose().reloadProgress < 0);
    g.MouseButton(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 100, 300);
    GameLevels::Mine().Leave();
    assert(!w.mouseAiming);
    w.mouseFiring = w.mouseAiming = true;
    Overworld::ToggleBike(w);
    assert(w.isBikeMounted && !w.mouseAiming && !w.mouseFiring);
    g.Reset();
}
} // namespace
void TestResidenceFeatures()
{
    using namespace tutorial;
    auto &g = Game::Get();
    g.Reset();
    auto &w = g.GetState();
    assert(w.isInsideHome);

    // 1. Interior station detection & obstacle walkability checks.
    w.playerPosition = kHomeBedInteraction;
    assert(g.FindNearbyInteraction() == InteractionTarget::HomeBed);
    assert(!g.IsPositionBlocked(kHomeBedInteraction));
    w.playerPosition = kHomeWorkbenchInteraction;
    assert(g.FindNearbyInteraction() == InteractionTarget::HomeWorkbench);
    assert(!g.IsPositionBlocked(kHomeWorkbenchInteraction));
    w.playerPosition = kHomeStorageInteraction;
    assert(g.FindNearbyInteraction() == InteractionTarget::HomeStorage);
    assert(!g.IsPositionBlocked(kHomeStorageInteraction));
    const Vector2 terminalPos = {1.f, -1.2f};
    const Vector2 exitPos = {2.f, 2.f};
    w.playerPosition = terminalPos;
    assert(g.FindNearbyInteraction() == InteractionTarget::HomeTerminal);
    assert(!g.IsPositionBlocked(terminalPos));
    w.playerPosition = exitPos;
    assert(g.FindNearbyInteraction() == InteractionTarget::ApartmentExit);
    assert(!g.IsPositionBlocked(exitPos));
    assert(g.IsPositionBlocked({2.25f, -.15f})); // Storage crate solid footprint
    assert(!g.IsPositionBlocked({0.f, 0.f}) && !g.IsPositionBlocked({1.6f, 1.5f}));

    // 2. Prologue terminal flow preserved on first read; residence terminal opens afterward.
    w.playerPosition = terminalPos;
    assert(!w.homeMessageRead && w.mainQuestStage == MainQuestStage::ReadHomeTerminal);
    g.InteractWithNearbyTarget();
    assert(Dialogue::Active());
    ReadDialogue();
    assert(w.homeMessageRead && w.mainQuestStage == MainQuestStage::MeetLia);
    g.InteractWithNearbyTarget();
    assert(Dialogue::Active());
    ReadDialogue();

    // 3. Bed: heals HP/stamina/companions, reloads firearms, calms wanted <= 2*, refuses 3*+.
    auto &player = mineState.player;
    mineState.loadout.GrantPistol();
    mineState.pistol.Reset();
    combat::Shot shot{};
    assert(mineState.pistol.TryFire(0, shot));
    assert(mineState.pistol.GetRounds() < mineState.pistol.GetMagazineSize());
    player.health = 28;
    w.playerMotion.mobility.RestoreStamina(14.f, .5f);
    mineState.riaCombat.health = 35;
    CivicJustice::State().wanted = 2;
    CivicJustice::State().heat = 140.f;
    assert(g.ResidenceRest(false));
    assert(player.health == player.maxHealth);
    assert(w.playerMotion.mobility.Stamina() == PlayerMobility::MaxStamina);
    assert(mineState.pistol.GetRounds() == mineState.pistol.GetMagazineSize());
    assert(mineState.riaCombat.health >= 100);
    assert(CivicJustice::State().wanted == 0 && CivicJustice::State().heat == 0.f);
    assert(w.residence.restCount == 1);

    player.health = 45;
    CivicJustice::State().wanted = 3;
    CivicJustice::State().heat = 280.f;
    assert(g.ResidenceRest(false));
    assert(player.health == player.maxHealth && CivicJustice::State().wanted == 3 &&
           CivicJustice::State().heat == 280.f && w.residence.restCount == 2);
    CivicJustice::State().wanted = 0;
    CivicJustice::State().heat = 0.f;

    // 4. Workbench: craft kits (salvage first, then credits), armor & weapon tiers 1..3 + cap.
    player.recoveryKits = 0;
    player.salvage = 1;
    player.credits = 100;
    assert(g.ResidenceCraftKit());
    assert(player.salvage == 0 && player.recoveryKits == 1 && player.credits == 100);
    assert(g.ResidenceCraftKit());
    assert(player.credits == 60 && player.recoveryKits == 2);
    player.credits = 10;
    assert(!g.ResidenceCraftKit());

    const int baseMaxHp = player.maxHealth;
    const int baseDef = player.defense;
    const int baseAtk = player.attack;
    player.salvage = 30;
    player.credits = 3000;
    for (int tier = 1; tier <= ResidenceState::kMaxUpgradeTier; ++tier)
    {
        assert(g.ResidenceUpgradeArmor());
        assert(w.residence.armorTier == tier);
        assert(player.maxHealth == baseMaxHp + tier * 20);
        assert(player.defense == baseDef + tier * 2);
    }
    assert(!g.ResidenceUpgradeArmor());
    for (int tier = 1; tier <= ResidenceState::kMaxUpgradeTier; ++tier)
    {
        assert(g.ResidenceUpgradeWeapon());
        assert(w.residence.weaponTier == tier);
        assert(player.attack == baseAtk + tier * 4);
    }
    assert(!g.ResidenceUpgradeWeapon());

    // 5. Storage & Wardrobe: safe deposit/withdraw, hospital-fee isolation, appearance cycling.
    player.credits = 600;
    player.recoveryKits = 3;
    player.salvage = 5;
    assert(g.ResidenceDepositCredits());
    assert(g.ResidenceDepositCredits());
    assert(w.residence.stashCredits == 400 && player.credits == 200);
    assert(g.ResidenceWithdrawCredits());
    assert(w.residence.stashCredits == 200 && player.credits == 400);
    assert(g.ResidenceDepositCredits());
    assert(w.residence.stashCredits == 400 && player.credits == 200);
    assert(g.ResidenceDepositKit());
    assert(w.residence.stashRecoveryKits == 1 && player.recoveryKits == 2);
    assert(g.ResidenceWithdrawKit());
    assert(w.residence.stashRecoveryKits == 0 && player.recoveryKits == 3);
    assert(g.ResidenceDepositKit() && g.ResidenceDepositKit());
    assert(w.residence.stashRecoveryKits == 2 && player.recoveryKits == 1);
    assert(g.ResidenceDepositSalvage());
    assert(w.residence.stashSalvage == 5 && player.salvage == 0);
    assert(g.ResidenceWithdrawSalvage());
    assert(w.residence.stashSalvage == 0 && player.salvage == 5);
    assert(g.ResidenceDepositSalvage());
    assert(w.residence.stashSalvage == 5 && player.salvage == 0);

    // Hospital recovery penalizes carried wallet/salvage only, never residence stash.
    player.credits = 150;
    player.salvage = 2;
    player.health = 0;
    HospitalRecovery::Begin();
    assert(w.residence.stashCredits == 400 && w.residence.stashSalvage == 5);
    while (HospitalRecovery::Active())
    {
        HospitalRecovery::Update(.25f);
    }
    assert(w.residence.stashCredits == 400 && w.residence.stashSalvage == 5);

    const auto startGender = w.playerGender;
    g.ResidenceCycleGender();
    assert(w.playerGender != startGender);
    g.ResidenceCycleGender();
    assert(w.playerGender == startGender);
    const auto startHair = w.playerHair;
    for (unsigned i = 0; i < unsigned(models::HairStyle::Count); ++i)
    {
        g.ResidenceCycleHair();
    }
    assert(w.playerHair == startHair);

    // 6. Surface exploration: remote vehicle repair, vehicle recall, Monitor JSON, Schema 13 save/load.
    assert(g.EnterTestStage(TestStage::SurfaceSurvey));
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!g.Surface()->Ready() && std::chrono::steady_clock::now() < until)
    {
        g.Update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(g.Surface()->Ready());
    w.isInsideHome = true;
    w.playerPosition = kHomeWorkbenchInteraction;
    const auto parked = g.Surface()->Pose().origin;
    w.car.position = parked;
    w.car.SetYaw(.4f, g.Surface()->Origin());
    w.car.health = 20;
    w.car.wreckSeconds = 0;
    w.aircraft.position = parked;
    w.aircraft.heading = w.car.heading;
    w.aircraft.health = 0;
    w.aircraft.wreckSeconds = 3.f;
    w.aircraft.grounded = true;
    w.aircraft.mounted = false;
    mineState.player.salvage = 4;
    assert(g.ResidenceRepairVehicles());
    assert(w.car.health == GroundVehicle::MaxHealth && w.car.wreckSeconds == 0);
    assert(w.aircraft.health == PlayerAircraft::MaxHealth && w.aircraft.wreckSeconds == 0);
    assert(mineState.player.salvage == 3);
    assert(g.ResidenceRecallVehicles());
    assert(w.car.position.has_value());

    w.residence.stashCredits = 600;
    w.residence.stashRecoveryKits = 3;
    w.residence.stashSalvage = 9;
    w.residence.armorTier = 2;
    w.residence.weaponTier = 3;
    w.residence.restCount = 4;
    w.residence.lastRestPlayTime = 123.5f;

    const auto snapshot = monitoring::WorldSnapshot::Build(1, "residence-test");
    assert(snapshot.find("\"residence\":") != std::string::npos);
    assert(snapshot.find("\"stash_credits\":600") != std::string::npos);
    assert(snapshot.find("\"armor_tier\":2") != std::string::npos);
    assert(snapshot.find("\"weapon_tier\":3") != std::string::npos);

    const auto folder =
        std::filesystem::path("x64/RevisionChecks") /
        ("residence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = folder / "residence.sav";
    const auto legacy12 = folder / "schema12.sav";
    const bool savedOk = SaveSystem::Save(file);
    if (!savedOk)
    {
        std::cerr << "SaveSystem::Save failed: " << SaveSystem::Message() << "\n";
    }
    assert(savedOk && SaveSystem::CheckFile(file));
    WriteLegacyCheckpoint(file, legacy12, 12);
    assert(SaveSystem::CheckFile(legacy12));
    assert(SaveSystem::Load(legacy12));
    WaitForCheckpointLoad();
    assert(w.residence.stashCredits == 0 && w.residence.armorTier == 0 && w.residence.weaponTier == 0);
    assert(SaveSystem::Load(file));
    WaitForCheckpointLoad();
    assert(w.residence.stashCredits == 600 && w.residence.stashRecoveryKits == 3 &&
           w.residence.stashSalvage == 9 && w.residence.armorTier == 2 && w.residence.weaponTier == 3 &&
           w.residence.restCount == 4 && std::fabs(w.residence.lastRestPlayTime - 123.5f) < .01f);
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    g.Reset();
}

void TestCq10AndUrbanFacilities()
{
    using namespace tutorial;
    PrepareCheckpointFixture();
    auto &g = Game::Get();
    auto &w = g.GetState();
    assert(g.LeaveSurfaceMine());
    CivicJustice::State().wanted = 0;
    CivicJustice::State().heat = 0;
    CivicJustice::State().infamy = 0;
    const auto plan = g.Surface()->Stream().Snapshot()->planet;
    // 1. All 5 historic cities (including halo_07) use modern Rev 5 procedural cities;
    //    halo_07 retains ONLY core story landmarks (40..65 m, empty roads) while cinder/veil/lumen/echo keep 40m anchors.
    assert(plan->cityCores);
    assert(CityFacilities::All().size() == 3 && !CityFacilities::Find(InteractionTarget::CityReceiver));
    for (int i = 0; i < Overworld::kCityCount; ++i)
    {
        const auto *core = plan->cityCores->Find(Overworld::Cities()[size_t(i)].id);
        assert(core);
        if (i == 0)
        {
            assert(core->radiusMeters >= 40. && core->radiusMeters <= 65. && !core->lots.empty() &&
                   core->roads.empty());
        }
        else
        {
            assert(core->radiusMeters == 40. && core->lots.empty() && core->roads.empty());
        }
    }
    // 1a. Part 1 checks: J remote accept blocked (triggers 3D guidance instead), V key triggers 3D guidance,
    //     and GroundVehicle/Aircraft gating verified on fresh game.
    {
        assert(w.missions.Status(MissionId::Mara) == MissionStatus::NotAccepted);
        w.guidanceSecondsRemaining = 0.f;
        g.ManageMission(MissionId::Mara);
        assert(w.missions.Status(MissionId::Mara) == MissionStatus::NotAccepted && !Dialogue::Active());
        assert(w.guidanceSecondsRemaining > 0.f);
        w.guidanceSecondsRemaining = 0.f;
        g.KeyDown('v', 0, 0);
        g.KeyUp('v', 0, 0);
        assert(w.guidanceSecondsRemaining > 0.f);
        assert(GroundVehicle::MaxHealth == 260);
    }
    // 1b. Phantom-door regression check: street props/consoles/aircraft MUST NEVER classify as enterable facilities.
    {
        world::SurfaceStructure prop;
        prop.id = "core1.prop_0";
        prop.regionId = "halo_07";
        prop.form = world::ContentForm::Scenery;
        prop.use = world::ContentUse::Services;
        prop.sizeMeters = {8., 8., 6.};
        prop.solid = true;
        assert(Game::ClassifyUrbanFacility(prop) == UrbanFacilityKind::None);
        prop.form = world::ContentForm::Console;
        assert(Game::ClassifyUrbanFacility(prop) == UrbanFacilityKind::None);
        prop.form = world::ContentForm::MeasuredBuilding;
        prop.variant = 2; // Halo_Clinic
        assert(Game::ClassifyUrbanFacility(prop) == UrbanFacilityKind::Clinic);
    }
    // 2. CQ-04 C2 Urban Facility Interiors: enter, use stations, Schema 14 save/load, Monitor JSON, exit.
    const auto returnPoint = g.Surface()->Pose().origin;
    ActiveFacilityInterior marketEntry;
    marketEntry.kind = UrbanFacilityKind::Market;
    marketEntry.regionId = "halo_07";
    marketEntry.regionName = "헤일로";
    marketEntry.label = Game::FacilityKindName(UrbanFacilityKind::Market);
    marketEntry.returnPoint = returnPoint;
    marketEntry.returnYaw = 0.5f;
    assert(g.EnterFacilityInterior(marketEntry));
    assert(g.Mode() == WorldMode::SurfaceFacility && g.IsInteriorMode() && w.IsInsideAny());
    auto &player = mineState.player;
    player.credits = 200;
    player.recoveryKits = 1;
    player.salvage = 2;
    assert(g.InteractFacilityStation(InteractionTarget::FacilityServicePrimary)); // Market primary: buy kit (-40 Cr, +1 kit)
    assert(player.credits == 160 && player.recoveryKits == 2);
    assert(g.InteractFacilityStation(InteractionTarget::FacilityServiceSecondary)); // Market secondary: sell salvage (+35 Cr, -1 salvage)
    assert(player.credits == 195 && player.salvage == 1);
    const std::string monitorJson = monitoring::WorldSnapshot::Build(1, "cq10-facility");
    assert(monitorJson.find("\"facility_interior\":{\"active\":true") != std::string::npos);
    assert(monitorJson.find("상점 · 보급소") != std::string::npos);
    // Schema 14 save/load round-trip while inside a facility interior.
    const auto saveFolder =
        std::filesystem::path("x64/RevisionChecks") /
        ("cq10-facility-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto saveFile = saveFolder / "facility.sav";
    const bool savedOk = SaveSystem::Save(saveFile);
    if (!savedOk)
    {
        std::cerr << "CQ10 SaveSystem::Save failed: " << SaveSystem::Message() << "\n";
    }
    assert(savedOk);
    assert(SaveSystem::CheckFile(saveFile));
    assert(g.ExitFacilityInterior());
    assert(g.Mode() == WorldMode::SurfaceExterior && !w.IsInsideAny());
    assert(SaveSystem::Load(saveFile));
    WaitForCheckpointLoad();
    assert(g.Mode() == WorldMode::SurfaceFacility && w.facilityInterior.kind == UrbanFacilityKind::Market &&
           w.facilityInterior.regionId == "halo_07");
    assert(g.ExitFacilityInterior());
    assert(g.Mode() == WorldMode::SurfaceExterior);
    // Verify Clinic & Workshop stations as well.
    ActiveFacilityInterior clinicEntry = marketEntry;
    clinicEntry.kind = UrbanFacilityKind::Clinic;
    clinicEntry.label = Game::FacilityKindName(UrbanFacilityKind::Clinic);
    player.health = 40;
    assert(g.EnterFacilityInterior(clinicEntry));
    assert(g.InteractFacilityStation(InteractionTarget::FacilityServicePrimary)); // Clinic primary: heal (-30 Cr, full HP)
    assert(player.health == player.maxHealth);
    // Verify 16:9 widescreen logical UI width/anchor offsets and live-animated 1st-person FacilityShop.
    {
        const int prevW = w.viewportWidth, prevH = w.viewportHeight;
        w.viewportWidth = 1920;
        w.viewportHeight = 1080;
        assert(std::fabs(WorldCamera::LogicalWidth() - (800.f * 16.f / 9.f)) < 0.05f);
        assert(std::fabs(WorldCamera::CenterOffsetX() - ((WorldCamera::LogicalWidth() - 1280.f) * .5f)) < 0.05f);
        assert(std::fabs(WorldCamera::RightOffsetX() - (WorldCamera::LogicalWidth() - 1280.f)) < 0.05f);
        w.viewportWidth = prevW;
        w.viewportHeight = prevH;
    }
    w.playerPosition = {-.2f, -.9f};
    assert(g.FindNearbyInteraction() == InteractionTarget::FacilityNpc);
    g.InteractWithNearbyTarget();
    assert(w.ui.screen == UiScreen::FacilityShop && w.isPaused);
    assert(w.facilityReactionSeconds > 0.5f);
    {
        const float animBefore = w.animationTimeSeconds;
        const float reactBefore = w.facilityReactionSeconds;
        for (int step = 0; step < 4; ++step)
        {
            g.Update(0.05f);
        }
        assert(w.animationTimeSeconds > animBefore + 0.15f);
        assert(w.facilityReactionSeconds < reactBefore - 0.15f);
    }
    Hud::CloseMenu();
    assert(g.ExitFacilityInterior());
    // 3. CQ-10 Empty-Zone Content & Outlaw Raiders (no wanted heat on self-defense).
    auto *community = g.SurfaceCommunity();
    assert(community);
    bool testedOutlaw = false;
    bool testedNonCityQuest = false;
    for (const auto &region : plan->regions)
    {
        if (region.center.space != world::SpaceKind::Surface ||
            region.definition.kind == world::RegionKind::Settlement ||
            region.definition.id == "frontier" || region.definition.id == "mq00_mine")
        {
            continue;
        }
        if (region.definition.kind == world::RegionKind::Outlaw && !testedOutlaw)
        {
            assert(g.RelocateSurfaceSurvey(region.access.position));
            g.Update(0);
            for (int i = 0; i < 120; ++i)
            {
                community->Update(.05f, true);
                auto it = std::find_if(community->State().sites.begin(), community->State().sites.end(),
                                       [&](const auto &s) { return s.regionId == region.definition.id; });
                if (it != community->State().sites.end())
                {
                    break;
                }
            }
            auto it = std::find_if(community->State().sites.begin(), community->State().sites.end(),
                                   [&](const auto &s) { return s.regionId == region.definition.id; });
            if (it != community->State().sites.end() && it->residents.size() >= 2)
            {
                assert(!it->residents[0].hostile);
                assert(it->residents[1].hostile);
                // Friendly/hostile self-fire protection: enemy-owned bolts (!playerOwned) must not hit hostile raiders.
                auto raiderPt = it->residents[1].position;
                raiderPt.altitudeMeters += .8;
                assert(!community->Hit(raiderPt, 10, false));
                const int salvageBefore = player.salvage;
                const int wantedBefore = CivicJustice::State().wanted;
                // Defeat all hostile raiders in the Outlaw camp -> zero wanted heat & automatic bounty completion!
                for (size_t rIdx = 1; rIdx < it->residents.size(); ++rIdx)
                {
                    if (it->residents[rIdx].hostile && it->residents[rIdx].health > 0)
                    {
                        assert(community->Damage(it->residents[rIdx].id, 200, true));
                    }
                }
                assert(CivicJustice::State().wanted == wantedBefore && CivicJustice::State().heat == 0);
                assert(player.salvage >= salvageBefore);
                assert(it->task == CommunityTaskProgress::Measured);
                const std::string title = PlanetCommunity::ZoneTaskName(*it, plan.get());
                assert(title != PlanetCommunity::TaskName(*it));
                testedOutlaw = true;
            }
        }
        else if ((region.definition.kind == world::RegionKind::AncientRuins ||
                  region.definition.kind == world::RegionKind::Wilderness ||
                  region.definition.kind == world::RegionKind::Mine) &&
                 !testedNonCityQuest)
        {
            assert(g.RelocateSurfaceSurvey(region.access.position));
            g.Update(0);
            for (int i = 0; i < 120; ++i)
            {
                community->Update(.05f, true);
                auto it = std::find_if(community->State().sites.begin(), community->State().sites.end(),
                                       [&](const auto &s) { return s.regionId == region.definition.id; });
                if (it != community->State().sites.end())
                {
                    break;
                }
            }
            auto it = std::find_if(community->State().sites.begin(), community->State().sites.end(),
                                   [&](const auto &s) { return s.regionId == region.definition.id; });
            if (it != community->State().sites.end() && it->residents.size() >= 2)
            {
                const std::string title = PlanetCommunity::ZoneTaskName(*it, plan.get());
                assert(title != PlanetCommunity::TaskName(*it));
                testedNonCityQuest = true;
            }
        }
        if (testedOutlaw && testedNonCityQuest)
        {
            break;
        }
    }
    assert(testedOutlaw && testedNonCityQuest);
    g.Reset();
}

void TestFullChapterOneStoryArc()
{
    using namespace tutorial;
    using namespace story;
    auto &g = Game::Get();
    auto &w = g.GetState();
    auto &arc = StoryArc::Get();

    auto finishDialogue = [&](int choiceIdx = 0) {
        for (int safety = 0; Dialogue::Active() && safety < 128; ++safety)
        {
            auto &d = w.dialogue;
            if (d.page < d.pages.size() && !d.pages[d.page].choices.empty() && d.selection < 0)
            {
                assert(Dialogue::Select(size_t(choiceIdx)));
            }
            assert(Dialogue::Advance());
        }
        assert(!Dialogue::Active());
    };
    auto clearZoneHostiles = [&](OpEnemyKind skipKind = OpEnemyKind::CoreCoupling) {
        for (auto &e : arc.State().enemies)
        {
            if (e.active && e.kind != skipKind)
            {
                e.health = 0;
                e.active = false;
            }
        }
    };
    auto interactNode = [&](OpNodeKind kind, int idx = 0) {
        for (const auto &n : arc.State().nodes)
        {
            if (n.visible && n.kind == kind && n.index == idx)
            {
                w.playerPosition = n.position;
                assert(arc.Interact());
                return;
            }
        }
        assert(false && "Expected StoryArc OpNode not found/visible");
    };

    // Start from completed Act 1 (including side quests to verify payoff branches)
    g.Reset();
    auto &ch = game::GameLevels::Chapter().GetState();
    ch.unlocked = true;
    ch.generatorInspected = ch.generatorRestored = ch.portableLightRepaired = true;
    ch.convoyBriefed = ch.convoyEvacuated = ch.convoyRescued = ch.kainJoined = ch.vehicleRepaired = true;
    ch.surveyMarkers = 3;
    ch.ledgerFound = ch.detailLedgerFound = true;
    ch.recordsCompared = ch.waterwayUnlocked = true;
    w.isPowerRestored = true;
    w.isInsideHome = false;
    mineState.loadout.GrantPistol();
    mineState.loadout.Select(combat::WeaponSlot::Secondary);
    assert(arc.IsActTwoUnlocked());

    // Act 2 (S20-S25): Enter Waterway via chapter1/return choice #2 ("waterway")
    game::GameLevels::Chapter().InteractHub();
    assert(Dialogue::Active() && w.dialogue.sceneId == "chapter1/return");
    finishDialogue(2);
    assert(arc.State().activeZone == OperationZone::Waterway && g.Mode() == WorldMode::StoryOperation);

    // S20 Beacon + 2 Distortion Pylons + Scanner F
    interactNode(OpNodeKind::WaterwayBeacon);
    finishDialogue();
    assert(arc.State().returnBeaconPlaced);
    assert(arc.TriggerScanner());
    interactNode(OpNodeKind::DistortionPylon, 0);
    interactNode(OpNodeKind::DistortionPylon, 1);
    assert(arc.State().stabilizedPylons == 2);

    // S21 Survey Station: blocked while drones live, then succeeds after combat
    interactNode(OpNodeKind::SurveyTerminal);
    assert(!arc.State().surveyStationInvestigated);
    clearZoneHostiles();
    interactNode(OpNodeKind::SurveyTerminal);
    finishDialogue();
    assert(arc.State().surveyStationInvestigated);

    // S22 Old Camp + Kain Confession choice ("criticize" = index 1)
    interactNode(OpNodeKind::OldCampRollCall);
    finishDialogue(1); // commits confession & queues closing
    finishDialogue();
    assert(arc.State().oldCampInvestigated && arc.State().kainRetreatRevealed &&
           arc.State().kainChoice == KainChoice::Criticize);

    // S23 Relay Switches + Veil Vault comparison
    interactNode(OpNodeKind::RelaySwitch, 0);
    interactNode(OpNodeKind::RelaySwitch, 1);
    assert(arc.State().relaySwitchesRestored == 2);
    interactNode(OpNodeKind::VeilVaultTerminal);
    finishDialogue();
    assert(arc.State().veilHypothesisFound && arc.State().observatoryDocsFound &&
           arc.State().cityPhase == CityPhase::Uneasy);

    // S24-S25 Halo return, report, Mara talk, NO timed auto-disaster on "prepare"
    interactNode(OpNodeKind::ReturnGate);
    assert(!arc.IsActive());
    assert(arc.InteractHaloReceiver()); // S24 report
    finishDialogue();
    assert(arc.State().chapter2Reported && !arc.State().chapter3Committed);
    assert(arc.InteractHaloMara()); // S24 Mara date discrepancy
    finishDialogue();
    assert(arc.InteractHaloReceiver()); // S25 choice
    finishDialogue(1);                  // "아직 준비한다"
    for (int i = 0; i < 20; ++i)
    {
        g.Update(0.05f);
    }
    assert(!arc.State().chapter3Committed && !arc.IsActive()); // Zero timed auto-disaster!

    // Explicit Act 3 commit ("depart" = index 0)
    assert(arc.InteractHaloReceiver());
    finishDialogue(0);
    assert(arc.State().chapter3Committed && arc.State().activeZone == OperationZone::CutoffFacility);

    // Act 3 S30: Cutoff Facility (3 actuators + main breaker + echoed abort voice -> HaloEvacuation)
    clearZoneHostiles();
    interactNode(OpNodeKind::CutoffActuator, 0);
    interactNode(OpNodeKind::CutoffActuator, 1);
    interactNode(OpNodeKind::CutoffActuator, 2);
    interactNode(OpNodeKind::MainPowerBreaker);
    finishDialogue();
    assert(arc.State().cutoffEchoTriggered && arc.State().cityPhase == CityPhase::Evacuation &&
           arc.State().activeZone == OperationZone::HaloEvacuation);

    // Act 3 S31-S33: Halo Evacuation (plaza route, closed house side rescue, backup power choice)
    clearZoneHostiles();
    interactNode(OpNodeKind::EvacDebrisBarricade);
    finishDialogue();
    interactNode(OpNodeKind::ClosedHouseSideAlley);
    finishDialogue();
    assert(arc.State().backupWarehouseRevealed); // detailLedgerFound revealed it immediately!
    interactNode(OpNodeKind::BackupPowerConsole);
    finishDialogue(1); // "signal_records"
    assert(arc.State().evacPriority == EvacuationPriority::SignalRecords && arc.State().evacCommsConnected &&
           arc.State().haloEvacuationComplete);

    // Act 3 S34: Outer Control safe-base clinic prep -> Gate Interior
    interactNode(OpNodeKind::ReturnGate);
    assert(arc.State().activeZone == OperationZone::OuterControl);
    interactNode(OpNodeKind::OuterControlPrep);
    finishDialogue();
    assert(arc.State().outerControlChecked);
    interactNode(OpNodeKind::GateThresholdPortal);
    assert(arc.State().gateEntryReached && arc.State().activeZone == OperationZone::GateInterior);

    // Act 3 S35-S36: 3 independent survival proofs + boundary test & dual-channel radio
    clearZoneHostiles();
    interactNode(OpNodeKind::ProofMarker);
    assert(!arc.State().missingExpeditionConfirmedAlive);
    interactNode(OpNodeKind::ProofRadio);
    finishDialogue();
    assert(!arc.State().missingExpeditionConfirmedAlive);
    interactNode(OpNodeKind::ProofScanner);
    finishDialogue();
    assert(arc.State().missingExpeditionConfirmedAlive);
    interactNode(OpNodeKind::BoundaryTestConsole);
    finishDialogue();
    assert(arc.State().boundaryTestDone && arc.State().chapter3Complete && arc.IsRiaAvailable());

    // Act 4 S40-S41: Sequential extraction of 3 fixed survivors + Medevac safe-base preservation
    clearZoneHostiles();
    for (int s = 0; s < 3; ++s)
    {
        interactNode(OpNodeKind::SurvivorExtract);
        clearZoneHostiles();
        for (int step = 0; step < 300 && arc.ReceivedSurvivorCount() <= s; ++step)
        {
            arc.Update(0.05f);
        }
        assert(arc.ReceivedSurvivorCount() == s + 1);
        if (s == 0)
        {
            // Verify medevac recovers at Outer Control and NEVER reverts received survivor #0!
            assert(arc.HandleMedevacRecovery());
            assert(arc.State().activeZone == OperationZone::OuterControl && arc.ReceivedSurvivorCount() == 1 &&
                   arc.State().survivors[0] == SurvivorStatus::Received);
            interactNode(OpNodeKind::GateThresholdPortal);
            clearZoneHostiles();
        }
    }
    finishDialogue(); // MQ04A/all-received
    assert(arc.State().allSurvivorsReceived);

    // Act 4 S42-S43: Gate Core (2 control points -> 4 shootable couplings -> return line -> external sector loss)
    interactNode(OpNodeKind::AdvanceToCore);
    assert(arc.State().activeZone == OperationZone::GateCore);
    clearZoneHostiles(OpEnemyKind::CoreCoupling);
    interactNode(OpNodeKind::CoreControlPoint, 0);
    interactNode(OpNodeKind::CoreControlPoint, 1);
    assert(arc.State().controlPointsDisabled[0] && arc.State().controlPointsDisabled[1]);
    for (auto &e : arc.State().enemies)
    {
        if (e.active && e.kind == OpEnemyKind::CoreCoupling)
        {
            w.playerPosition = {e.position.x - 1.f, e.position.y};
            for (int swings = 0; swings < 4 && e.active; ++swings)
            {
                arc.TryMelee(w.playerPosition, 0.f);
            }
        }
    }
    finishDialogue(); // MQ04B/core-stopped
    assert(arc.State().coreStopped && arc.State().couplingsDestroyed == 4);

    interactNode(OpNodeKind::CoreReturnLine);
    finishDialogue(); // MQ04C/sector-loss ("여긴… 아직 빛이 있어.")
    assert(arc.State().returnConfirmed && arc.State().sectorLossTriggered &&
           arc.State().riaStatus == RiaStatus::Missing && arc.State().cityPhase == CityPhase::Recovery &&
           !arc.IsActive());
    // Strict invariant: Ria NEVER auto-respawns anywhere after Act 4!
    assert(!g.HasCompanion(CompanionId::Ria, CompanionPlace::Surface));
    assert(!g.HasCompanion(CompanionId::Ria, CompanionPlace::Mine));
    assert(!g.HasCompanion(CompanionId::Ria, CompanionPlace::Frontier));
    assert(!g.IsQuestInteraction(InteractionTarget::Lia));

    // Act 4 S44: Safety line + file Ria as Missing
    assert(arc.InteractHaloSafetyLine());
    finishDialogue();
    assert(arc.State().safetyLineChecked);
    assert(arc.InteractHaloReceiver());
    finishDialogue();
    assert(arc.State().supportLogsRecovered && arc.State().missingFiled && arc.State().chapter4Complete);

    // Epilogue S50-S52: Shelter (Mara + lamp toggle), Clinic, Workshop (Kain choice payoff), 17-day Nereus signal
    assert(arc.InteractHaloMara());
    finishDialogue();
    assert(arc.State().epilogueShelterVisited);
    const bool lampBefore = arc.State().shelterLampOn;
    assert(arc.InteractHaloShelterLamp() && arc.State().shelterLampOn != lampBefore);
    assert(arc.InteractHaloEpilogueClinic());
    finishDialogue();
    assert(arc.State().epilogueClinicVisited);
    assert(arc.InteractHaloEpilogueWorkshop());
    finishDialogue();
    assert(arc.State().epilogueWorkshopVisited && arc.State().epilogueRecoveryTaskDone);
    assert(arc.InteractHaloReceiver()); // S52 day skip choice
    finishDialogue(0);                  // "수신 기록 정리를 마친다 (17일 후)"
    finishDialogue();                   // Nereus signal dialogue
    assert(arc.State().epilogueDaySkipped && arc.State().nereusSignalReceived);
    // Replayable audio log works cleanly without mutating flags
    assert(arc.InteractHaloReceiver());
    finishDialogue();

    // Journal records now accurately contain Veil & Nereus facts ONLY after achieved
    const auto records = StoryJournal::Collect(w, mineState, ch);
    assert(std::any_of(records.begin(), records.end(), [](const InvestigationRecord &r) {
        return r.id == "act2/veil" && r.fact.find("장막") != std::string::npos;
    }));
    assert(std::any_of(records.begin(), records.end(), [](const InvestigationRecord &r) {
        return r.id == "epilogue/nereus" && r.fact.find("네레우스") != std::string::npos;
    }));

    // Save Schema 15 round-trip persistence check
    PrepareCheckpointFixture();
    arc.Reset();
    arc.State().cityPhase = CityPhase::Recovery;
    arc.State().riaStatus = RiaStatus::Missing;
    arc.State().kainChoice = KainChoice::Criticize;
    arc.State().evacPriority = EvacuationPriority::SignalRecords;
    arc.State().chapter2Reported = arc.State().chapter3Committed = arc.State().chapter3Complete = true;
    arc.State().allSurvivorsReceived = arc.State().coreStopped = arc.State().returnConfirmed = true;
    arc.State().sectorLossTriggered = arc.State().chapter4Complete = true;
    arc.State().survivors = {SurvivorStatus::Received, SurvivorStatus::Received, SurvivorStatus::Received};
    arc.State().epilogueShelterVisited = arc.State().epilogueClinicVisited = arc.State().epilogueWorkshopVisited = true;
    arc.State().epilogueDaySkipped = arc.State().nereusSignalReceived = true;
    const auto saveFolder =
        std::filesystem::path("x64/RevisionChecks") /
        ("story-arc-s15-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto saveFile = saveFolder / "chapter1-epilogue.sav";
    const bool savedOk = SaveSystem::Save(saveFile);
    if (!savedOk)
    {
        std::cerr << "StoryArc Schema 15 Save failed: " << SaveSystem::Message() << "\n";
    }
    assert(savedOk);
    assert(SaveSystem::CheckFile(saveFile));
    arc.Reset();
    assert(arc.State().riaStatus == RiaStatus::Available);
    assert(SaveSystem::Load(saveFile));
    WaitForCheckpointLoad();
    assert(arc.State().riaStatus == RiaStatus::Missing && arc.State().cityPhase == CityPhase::Recovery &&
           arc.State().kainChoice == KainChoice::Criticize &&
           arc.State().evacPriority == EvacuationPriority::SignalRecords &&
           arc.State().chapter4Complete && arc.State().nereusSignalReceived);

    // Open-World integration verification: Act 2->Halo surface return, n0_outskirts portal, Epilogue Halo surface props & missing Ria guard.
    assert(g.EnterTestStage(TestStage::ActTwoWaterway));
    assert(w.surfaceExploration && arc.IsActive());
    arc.LeaveZoneToHalo(true);
    assert(w.surfaceExploration && g.Mode() == WorldMode::SurfaceExterior && g.SurfaceCities() &&
           g.SurfaceCities()->ActiveCity() == 0);
    const auto &n0Pos = g.Surface()->Stream().Snapshot()->planet->Region("n0_outskirts").access.position;
    assert(g.RelocateSurfaceSurvey(n0Pos));
    assert(g.FindNearbyInteraction() == InteractionTarget::StoryOperationPortal);

    assert(g.EnterTestStage(TestStage::EpilogueRecovery));
    assert(w.surfaceExploration && g.Mode() == WorldMode::SurfaceExterior && arc.IsRiaMissing());
    assert(!g.SurfaceParty() || !g.SurfaceParty()->Joined(CompanionId::Ria));
    if (auto liaPt = g.SurfaceCities()->Point(*g.Surface(), 0, kLiaPosition))
    {
        assert(g.RelocateSurfaceSurvey(*liaPt));
        assert(g.FindNearbyInteraction() != InteractionTarget::Lia);
    }
    if (auto clinicPt = g.SurfaceCities()->Point(*g.Surface(), 0, {11.f, -2.f}))
    {
        assert(g.RelocateSurfaceSurvey(*clinicPt));
        assert(g.FindNearbyInteraction() == InteractionTarget::StoryEpilogueClinic);
    }
    const auto epGoal = ActivityJournal::Current();
    assert(!epGoal.title.empty() && epGoal.surfaceTarget.has_value());
    g.Reset();
}

void TestWorldEncounters()
{
    using namespace tutorial;
    using namespace encounters;
    auto &g = Game::Get();
    auto &w = g.GetState();
    auto &we = WorldEncounters::Get();

    auto finishDialogue = [&](int choiceIdx = 0) {
        for (int safety = 0; Dialogue::Active() && safety < 64; ++safety)
        {
            auto &d = w.dialogue;
            if (d.page < d.pages.size() && !d.pages[d.page].choices.empty() && d.selection < 0)
            {
                assert(Dialogue::Select(size_t(choiceIdx)));
            }
            assert(Dialogue::Advance());
        }
        assert(!Dialogue::Active());
    };

    // 1) Legacy Halo outskirts: 4 rich 4-stage encounters playable end-to-end
    g.Reset();
    w.isInsideHome = false;
    we.PopulateForCurrentWorld();
    assert(we.State().live.size() == 4);

    // 1A) ConvoyAmbush: Briefing -> Wave 1 combat -> 2 Repairs -> Wave 2 + Boss -> Escort -> Branch Choice
    {
        auto &enc = we.State().live[0];
        assert(enc.archetype == Archetype::ConvoyAmbush);
        w.playerPosition = enc.legacyAnchor;
        we.Update(0.05f);
        assert(enc.activeInRange && we.HasNearbyInteraction());
        assert(we.Interact());
        finishDialogue(0); // Accept
        assert(enc.phase == Phase::Stage1);
        // Defeat Wave 1 via HitAtLegacyPoint
        for (auto &e : enc.enemies)
        {
            if (e.active && e.wave == 1)
            {
                assert(we.HitAtLegacyPoint(we.ToLegacyWorld(enc, e.offset), 200, true));
            }
        }
        assert(enc.phase == Phase::Stage2);
        // Complete both vehicle repair nodes
        for (const auto &n : enc.nodes)
        {
            if (n.requiredStage == 2)
            {
                w.playerPosition = we.ToLegacyWorld(enc, n.offset);
                assert(we.Interact());
            }
        }
        assert(enc.phase == Phase::Stage3 && enc.wave2Spawned);
        // Defeat Wave 2 + Boss -> escort starts
        for (auto &e : enc.enemies)
        {
            if (e.active)
            {
                assert(we.HitAtLegacyPoint(we.ToLegacyWorld(enc, e.offset), 300, true));
            }
        }
        for (int step = 0; step < 300 && enc.phase == Phase::Stage3; ++step)
        {
            we.Update(0.05f);
        }
        assert(enc.phase == Phase::Stage4);
        w.playerPosition = enc.legacyAnchor;
        const int crBefore = g.Player().credits;
        assert(we.Interact());
        finishDialogue(0); // OptionA
        assert(enc.phase == Phase::Completed && enc.rewardClaimed && g.Player().credits > crBefore + 200 &&
               we.State().totalCompleted == 1);
    }

    // 1B) SmugglerStandoff: Briefing -> 2 Clues -> 3-way Standoff Choice (Negotiate bloodless OptionB)
    {
        auto &enc = we.State().live[2];
        assert(enc.archetype == Archetype::SmugglerStandoff);
        w.playerPosition = enc.legacyAnchor;
        we.Update(0.05f);
        assert(we.Interact());
        finishDialogue(0);
        assert(enc.phase == Phase::Stage2);
        for (const auto &n : enc.nodes)
        {
            if (n.requiredStage == 2)
            {
                w.playerPosition = we.ToLegacyWorld(enc, n.offset);
                assert(we.Interact());
            }
        }
        assert(enc.phase == Phase::Stage3);
        for (const auto &n : enc.nodes)
        {
            if (n.requiredStage == 3)
            {
                w.playerPosition = we.ToLegacyWorld(enc, n.offset);
                assert(we.Interact());
            }
        }
        const int salvBefore = g.Player().salvage;
        finishDialogue(1); // "negotiate" -> bloodless OptionB
        assert(enc.phase == Phase::Completed && enc.choice == ResolutionChoice::OptionB &&
               g.Player().salvage >= salvBefore + 5 && we.State().totalCompleted == 2);
    }

    // 1C) RelayDefenseHold: Briefing -> 2 Cooling Valves -> 26s 2-Wave Hold Defense -> Perfect Bonus
    {
        auto &enc = we.State().live[3];
        assert(enc.archetype == Archetype::RelayDefenseHold);
        w.playerPosition = enc.legacyAnchor;
        we.Update(0.05f);
        assert(we.Interact());
        finishDialogue(0);
        for (const auto &n : enc.nodes)
        {
            if (n.requiredStage == 2)
            {
                w.playerPosition = we.ToLegacyWorld(enc, n.offset);
                assert(we.Interact());
            }
        }
        assert(enc.phase == Phase::Stage3);
        // Clear Wave 1 & hold through 26 seconds + Wave 2
        for (int step = 0; step < 600 && enc.phase == Phase::Stage3; ++step)
        {
            for (auto &e : enc.enemies)
            {
                if (e.active)
                {
                    we.HitAtLegacyPoint(we.ToLegacyWorld(enc, e.offset), 300, true);
                }
            }
            we.Update(0.05f);
        }
        assert(enc.wave2Spawned && enc.phase == Phase::Stage4 && enc.beaconHealth >= 60);
        w.playerPosition = enc.legacyAnchor;
        assert(we.Interact());
        finishDialogue(0);
        assert(enc.phase == Phase::Completed && we.State().totalCompleted == 3);
    }

    // 2) Planet Surface: 16+ encounters covering ALL 10 archetypes + F scanner reveals hidden nodes + Schema 16 Save/Load
    PrepareCheckpointFixture();
    assert(g.LeaveSurfaceMine());
    we.PopulateForCurrentWorld();
    assert(we.State().live.size() >= 16);
    std::set<Archetype> seenTypes;
    for (const auto &enc : we.State().live)
    {
        seenTypes.insert(enc.archetype);
    }
    assert(seenTypes.size() == size_t(Archetype::Count));
    // Verify F scanner reveals hidden OutlawBountyRaid antennas
    for (auto &enc : we.State().live)
    {
        if (enc.archetype == Archetype::OutlawBountyRaid)
        {
            assert(g.RelocateSurfaceSurvey(enc.surfaceAnchor));
            we.Update(0.05f);
            assert(we.TriggerScannerPing());
            for (const auto &n : enc.nodes)
            {
                assert(n.revealed);
            }
            enc.phase = Phase::Stage2;
            enc.stageObjectivesDone = 1;
            we.State().totalCompleted = 4;
            we.State().activeEncounterId = enc.id;
            break;
        }
    }
    // Schema 16 Save/Load round-trip check
    const auto saveFolder =
        std::filesystem::path("x64/RevisionChecks") /
        ("encounters-s16-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto saveFile = saveFolder / "encounters.sav";
    assert(SaveSystem::Save(saveFile) && SaveSystem::CheckFile(saveFile));
    we.Reset();
    assert(we.State().totalCompleted == 0);
    assert(SaveSystem::Load(saveFile));
    WaitForCheckpointLoad();
    we.PopulateForCurrentWorld();
    assert(we.State().totalCompleted == 4 && !we.State().activeEncounterId.empty());
    g.Reset();
}

void TestPart2ContentAndUiOverhaul()
{
    using namespace tutorial;
    auto &g = Game::Get();
    auto &w = g.GetState();

    // 1) FieldMinigame: all 4 modes (CircuitBalance, SignalTuner, GlyphCipher, ExtractionTiming) + Tab bypass.
    bool done = false;
    bool wasPerfect = false;
    FieldMinigame::Begin(
        FieldMinigameKind::CircuitBalance, "회로 테스트", "테스트", 101u,
        [&](const FieldMinigameOutcome &out) {
            done = out.succeeded;
            wasPerfect = out.perfect;
        });
    assert(FieldMinigame::Active());
    FieldMinigame::CompleteForTest(true);
    assert(!FieldMinigame::Active() && done && wasPerfect);

    done = false;
    bool bypassed = false;
    FieldMinigame::Begin(
        FieldMinigameKind::GlyphCipher, "문양 테스트", "테스트", 202u,
        [&](const FieldMinigameOutcome &out) {
            done = out.succeeded;
            bypassed = out.bypassed;
        });
    assert(FieldMinigame::Active());
    assert(FieldMinigame::HandleKey('d'));
    assert(FieldMinigame::HandleKey('\t')); // Tab bypass succeeds without bonus.
    assert(!FieldMinigame::Active() && done && bypassed);

    done = false;
    FieldMinigame::Begin(
        FieldMinigameKind::SignalTuner, "파형 테스트", "테스트", 303u,
        [&](const FieldMinigameOutcome &out) { done = out.succeeded; });
    assert(FieldMinigame::Active());
    assert(FieldMinigame::HandleKey('a') && FieldMinigame::HandleKey('w'));
    FieldMinigame::CompleteForTest(true);
    assert(done);

    // 2) Mine overhaul: 4 enemy archetypes, 20 resource nodes, scanner resourcePings, harvest & streak.
    ReadyMine();
    auto &mine = GameLevels::Mine();
    auto &ms = mine.GetState();
    assert(ms.enemies.size() == game::kEnemyPopulation);
    assert(ms.resourceNodes.size() == 20);
    int crawlers = 0, spitters = 0, brutes = 0, elites = 0;
    for (const auto &e : ms.enemies)
    {
        if (e.kind == game::MineEnemyKind::Crawler)
            ++crawlers;
        else if (e.kind == game::MineEnemyKind::Spitter)
            ++spitters;
        else if (e.kind == game::MineEnemyKind::ArmoredBrute)
            ++brutes;
        else if (e.kind == game::MineEnemyKind::RuinElite)
            ++elites;
    }
    assert(crawlers > 0 && spitters > 0 && brutes > 0 && elites == 3);
    const int beforeSalvage = ms.player.salvage;
    const int beforeCredits = ms.player.credits;
    assert(mine.HarvestResourceNode(0, true));
    assert(ms.veinsHarvested == 1 && ms.player.salvage > beforeSalvage && ms.player.credits > beforeCredits);
    assert(!mine.HarvestResourceNode(0, false)); // Already harvested.
    ms.scannerCooldown = 0.f;
    w.playerPosition = ms.resourceNodes[1].position;
    mine.Rescue().Scan();
    mine.Rescue().Update(0.5f);
    assert(!ms.resourcePings.empty());

    // 3) Frontier & Kain first encounter: rogue drones + distress flare + perimeter ambush.
    g.Reset();
    auto &ch = GameLevels::Chapter();
    ch.Enter(true);
    auto &cs = ch.GetState();
    assert(cs.scrappers.size() == 3 && cs.survivors.size() == 5);
    assert(cs.frontierThreats.size() == 2); // 2 RogueDrones at Generator Yard.
    w.playerPosition = {26.f, 10.5f};       // Within 10.5u of Kain triggers distress flare + ambush!
    ch.Update(0.1f);
    assert(cs.distressFlareNoticed && cs.convoyAmbushTriggered);
    assert(ch.ActiveThreatsNear({31.f, 11.f}, 10.f) == 2);

    // 4) Visual Wardrobe (3D Mixamo 13-part modular) & Visual Home Storage & Facility Shop items.
    g.Reset();
    Hud::OpenMenu(UiScreen::HomeStorage);
    assert(w.ui.screen == UiScreen::HomeStorage);
    assert(Hud::Controls().size() >= 8);
    Hud::OpenMenu(UiScreen::Wardrobe);
    assert(w.ui.screen == UiScreen::Wardrobe);
    w.playerModularMedic = true;
    const auto fullModular = Drawing::BuildPlayerModularCharacter(w.playerModularPartsMask);
    assert(fullModular.body != nullptr && fullModular.outfits.size() >= 4 && fullModular.attachments.size() == 8);
    w.playerModularPartsMask &= ~(1u << 9u); // Toggle off Rescue Pack
    const auto trimmedModular = Drawing::BuildPlayerModularCharacter(w.playerModularPartsMask);
    assert(trimmedModular.attachments.size() == 7);
    Hud::CloseMenu();
    // 5) V key triggers 3D mission navigation guidance on the open world (replaces V melee).
    g.EnterTestStage(TestStage::BikeUnlocked);
    w.isBikeMounted = false;
    w.guidanceSecondsRemaining = 0.f;
    g.KeyDown('v', 0, 0);
    g.KeyUp('v', 0, 0);
    assert(w.guidanceSecondsRemaining > 0.f);

    // 6) GroundVehicle driving regression with boarded companion (Ria) must accelerate, steer, and advance.
    g.EnterTestStage(TestStage::ChapterOne);
    w.isBikeMounted = false;
    for (int i = 0; i < 120 && !w.car.position; ++i)
    {
        g.Update(0.05f);
    }
    if (w.car.position)
    {
        g.RelocateSurfaceSurvey(*w.car.position);
        const auto carLoc = g.Surface()->Local(*w.car.position);
        w.playerPosition = {float(carLoc.x / 2), float(carLoc.y / 2)};
        w.car.mounted = true;
        w.facingYaw = w.car.Yaw(g.Surface()->Origin());
        const auto startOrigin = g.Surface()->Pose().origin;
        const float startYaw = w.facingYaw;
        w.pressedKeys['w'] = true;
        w.pressedKeys['d'] = true;
        for (int i = 0; i < 20; ++i)
        {
            g.Update(0.05f);
        }
        w.pressedKeys['w'] = false;
        w.pressedKeys['d'] = false;
        const double drivenMeters = world::SurfaceGeometry::Distance(
            g.Surface()->Planet().radiusMeters, startOrigin, g.Surface()->Pose().origin);
        assert(drivenMeters > 1.0 && std::fabs(w.facingYaw - startYaw) > 0.05f);
        w.car.mounted = false;
    }

    // 7) Gate Core gunfire emits swept per-frame tracers (no duplicate static beam at spawn).
    g.EnterTestStage(TestStage::ActFourGateCore);
    auto &arc = story::StoryArc::Get();
    assert(arc.EnterZone(story::OperationZone::GateCore));
    if (!g.Loadout().IsFirearmEquipped())
    {
        g.Loadout().GrantPistol();
        g.Loadout().Select(combat::WeaponSlot::Secondary);
    }
    g.ActiveFirearm().Update(1.f);
    auto &arcState = arc.State();
    arcState.bullets.clear();
    arcState.tracers.clear();
    assert(arc.TryAttack(w.playerPosition, {w.playerPosition.x + 5.f, w.playerPosition.y}));
    assert(arcState.bullets.size() == 1 && arcState.tracers.empty());
    arc.Update(0.05f);
    assert(!arcState.tracers.empty());

    // 8) Part 3: Wrist-projected 3D globe map (zoom-scaled LMB drag rotate, RMB destination pin, G space map).
    g.EnterTestStage(TestStage::BikeUnlocked);
    w.isBikeMounted = false;
    w.mapVisible = false;
    g.KeyDown('m', 0, 0);
    g.KeyUp('m', 0, 0);
    assert(w.mapVisible && w.mapHologramProgress < 0.1f && !w.mapSpaceMode);
    for (int i = 0; i < 10; ++i)
    {
        g.Update(0.05f);
    }
    assert(w.mapHologramProgress >= 0.99f);
    const double planetR = g.Surface()->Planet().radiusMeters;
    // Zoomed-in LMB drag rotation distance vs zoomed-out ('p') full-globe LMB drag rotation distance.
    w.planetMapRange = 1000.f;
    const auto centerBeforeNear = w.planetGlobeCenter;
    const Vector2 dragStart{488.f + WorldCamera::CenterOffsetX(), 400.f};
    assert(MapHud::HandlePointer(GLFW_MOUSE_BUTTON_LEFT, true, dragStart));
    MapHud::HandleMotion({dragStart.x + 60.f, dragStart.y});
    assert(MapHud::HandlePointer(GLFW_MOUSE_BUTTON_LEFT, false, {dragStart.x + 60.f, dragStart.y}));
    const double nearDragMeters = world::SurfaceGeometry::Distance(planetR, centerBeforeNear, w.planetGlobeCenter);
    assert(nearDragMeters > 50.0 && nearDragMeters < 5000.0);

    assert(MapHud::HandleMapKey('p'));
    const auto centerBeforeFar = w.planetGlobeCenter;
    assert(MapHud::HandlePointer(GLFW_MOUSE_BUTTON_LEFT, true, dragStart));
    MapHud::HandleMotion({dragStart.x + 60.f, dragStart.y});
    assert(MapHud::HandlePointer(GLFW_MOUSE_BUTTON_LEFT, false, {dragStart.x + 60.f, dragStart.y}));
    const double farDragMeters = world::SurfaceGeometry::Distance(planetR, centerBeforeFar, w.planetGlobeCenter);
    assert(farDragMeters > nearDragMeters * 20.0);

    // RMB sets destination on the 3D planet globe.
    w.planetDestination.reset();
    assert(MapHud::HandlePointer(GLFW_MOUSE_BUTTON_RIGHT, true, {dragStart.x + 40.f, dragStart.y - 30.f}));
    assert(w.planetDestination.has_value());

    // [G] toggles Space / Star System map view.
    assert(MapHud::HandleMapKey('g') && w.mapSpaceMode);
    assert(MapHud::HandleMapKey('g') && !w.mapSpaceMode);
    g.Reset();
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--encounters")
    {
        TestWorldEncounters();
        std::cout << "WorldEncounters (10 archetypes, 4-stage combat/escort/branch/hold, scanner, Schema 16) passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--story-arc")
    {
        TestFullChapterOneStoryArc();
        std::cout << "Full Chapter 1 story arc (Acts 2-4, Epilogue, Ria missing invariant, Schema 15 save) passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--support")
    {
        TestNpcSupport();
        std::cout << "Companion aid interruption, cover/work roles and quest isolation checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--cooperation")
    {
        TestMineCooperation();
        std::cout << "Mine ally shot hold, ammo preservation and direct-sighting warning checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--perception")
    {
        TestMinePerceptionMemory();
        std::cout << "Mine remembered-position pursuit, expiry and reacquisition checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--navigation")
    {
        TestMineNavigationProgress();
        std::cout << "Mine crowd, companion spacing and evacuation navigation checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--aircraft")
    {
        TestAircraftFlightContracts();
        std::cout << "Aircraft flight sweep, altitude, hull and rebase checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--residence")
    {
        TestResidenceFeatures();
        std::cout << "Residence stations, rest/wanted, crafting/upgrades, stash/wardrobe and Schema 13 save checks passed.\n";
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--checkpoint-write")
    {
        PrepareCheckpointFixture();
        assert(tutorial::SaveSystem::Save(std::filesystem::u8path(argv[2])));
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--checkpoint-read")
    {
        tutorial::Game::Get().Reset();
        assert(tutorial::SaveSystem::Load(std::filesystem::u8path(argv[2])));
        WaitForCheckpointLoad();
        VerifyCheckpointFixture();
        return 0;
    }
    TestFrameScheduleAndTextCache();
    if (argc > 1 && std::string(argv[1]) == "--city-facilities")
    {
        TestFacilityAccessContracts();
        TestFacilityApproachContracts();
        TestFacilityExitIntegration();
        std::cout << "City facility metadata, sidewalk alignment and connected exit checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--story-followups")
    {
        TestStoryDialogueContext();
        TestStoryFollowUpContracts();
        TestStoryJournalContracts();
        TestChapterFollowUpInteractions();
        TestStoryReturnFlow();
        std::cout << "Story context and optional follow-up checks passed.\n";
        return 0;
    }
    static_assert(std::is_base_of<game::Level, game::MineLevel>::value);
    static_assert(!std::is_copy_constructible<game::MineLevel>::value);
    static_assert(std::is_base_of<game::Scene, game::MineScene>::value);
    if (argc > 1 && std::string(argv[1]) == "--cq10-facilities")
    {
        TestCq10AndUrbanFacilities();
        std::cout << "CQ-10 empty-zone, legacy city border and CQ-04 C2 urban facility checks passed.\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--urban")
    {
        TestUrbanTraffic();
        TestCityPlanAndSignalProgress();
        TestVehicleQueueProgress();
        TestCityResidentsAndShadowLifetime();
        std::ofstream("x64/RevisionChecks/state.json") << monitoring::WorldSnapshot::Build(1, "urban-test");
        std::cout << "Urban traffic, crossing and resident checks passed.\n";
        return 0;
    }
    TestAimMovementAndLayers();
    TestForwardFlashlight();
    TestMineNavigationProgress();
    TestStageSelectionAndLoadout();
    TestFacilityAccessContracts();
    TestFacilityApproachContracts();
    TestFacilityExitIntegration();
    TestPlanetTraffic();
    TestCitySimulationPolicy();
    TestPlanetCityMigration();
    TestPlanetCityRuntime();
    TestStoryDialogueContext();
    TestStoryFollowUpContracts();
    TestStoryJournalContracts();
    TestChapterFollowUpInteractions();
    TestStoryReturnFlow();
    TestStoryMissionCompletionContracts();
    TestOpenWorldParticipation();
    TestActivityObjectives();
    TestCommunityTaskTransactions();
    TestActivityTrackingContracts();
    TestActivityTrackingPersistence();
    TestResidenceFeatures();
    TestAircraftFlightContracts();
    TestRelayTravelAndAtlas();
    TestWorldCheckpoints();
    TestCityTrafficHandoff();
    TestPlanetCommunity();
    TestCq10AndUrbanFacilities();
    TestPlanetSecurity();
    TestPlanetParty();
    TestPlanetProfileVisits();
    TestMapZoomAndSelection();
    TestUiMenusAndTransactions();
    TestDialogueProgress();
    TestExplorationAndMelee();
    TestBikeSpeedsAndSliding();
    TestLevelLifecycle();
    TestWeapon();
    TestQuest();
    TestMapAndGate();
    TestNpcFacing();
    TestWorldCamera();
    TestNpcCombat();
    TestAimLightingAndWallShots();
    TestRescueAndScanner();
    TestMinePerceptionMemory();
    TestScannerPropagation();
    TestMineCooperation();
    TestStoryAndReentry();
    TestNpcSupport();
    TestChapterOne();
    TestFullChapterOneStoryArc();
    TestWorldEncounters();
    TestPart2ContentAndUiOverhaul();
    TestConnectedOverworld();
    TestCityQuestAccess();
    TestImportedWorldAndFlights();
    TestUrbanTraffic();
    TestCityPlanAndSignalProgress();
    TestVehicleQueueProgress();
    TestCityResidentsAndShadowLifetime();
    if (argc > 1)
    {
        std::ofstream snapshot(argv[1]);
        snapshot << monitoring::WorldSnapshot::Build(1, "mine-tests");
    }
    std::cout << "Mine gameplay checks passed: weapon, quest, 30 maps, aiming, lighting, walls, combat, "
                 "scanner, rescue, reentry.\n";
}
