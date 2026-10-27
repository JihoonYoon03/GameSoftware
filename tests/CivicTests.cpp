#include "stdafx.h"
#include "Civic.h"
#include "Drawing.h"
#include "Overworld.h"
#include "ResidentAI.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>

using namespace tutorial;
namespace
{
GameState &World()
{
    return Game::Get().GetState();
}
game::PlayerStats &Stats()
{
    return game::GameLevels::Mine().GetState().player;
}
void Exterior()
{
    Game::Get().Reset();
    World().isInsideHome = false;
    World().residents.clear();
    World().playerPosition = World().cameraPosition = {2, 0};
}
void Aim(Vector2 target)
{
    World().cursorPosition = WorldCamera::ToViewport(
        Drawing::WorldToScreen(target.x, target.y, models::WorldScale::aimHeightPixels));
    World().cursorInsideViewport = true;
}
Resident Person(Vector2 position)
{
    Resident resident;
    resident.position = resident.home = resident.destination = position;
    resident.name = "검사 시민";
    return resident;
}
Vector2 Open(int city)
{
    auto center = Overworld::Cities()[city].center;
    for (int y = -25; y < 26; ++y)
    {
        for (int x = -25; x < 26; ++x)
        {
            Vector2 p = {center.x + x, center.y + y};
            if (!CivicJustice::Solid(p, 2) && !RoadNetwork::Get().Contains(p, 2) &&
                CityResidents::CanWalkSegment(World(), p, {p.x + 2, p.y}))
            {
                return p;
            }
        }
    }
    assert(false);
    return {};
}
void TestSeverityAndMemory()
{
    Exterior();
    auto &law = CivicJustice::State();
    for (auto offense : {Offense::Discharge, Offense::VehicleDamage, Offense::RecklessDriving})
    {
        CivicJustice::Report(offense, World().playerPosition);
    }
    assert(law.heat == 0 && law.wanted == 0 && law.infamy == 0 && law.pendingInfamy == 0);
    CivicJustice::Report(Offense::Assault, World().playerPosition);
    assert(law.wanted == 1 && law.infamy == 0 && law.pendingInfamy > 0);
    for (int i = 0; i < 30; ++i)
    {
        CivicJustice::Report(Offense::OfficerKilled, World().playerPosition);
    }
    assert(law.wanted == 1 && law.heat == 1000 && law.infamy == 0);
    // Both elapsed case time and a new qualified offense are required for each escalation.
    for (float gate : {25.f, 60.f, 105.f, 165.f})
    {
        int previous = law.wanted;
        World().isInsideHome = true;
        CivicJustice::Update(gate - law.pursuitSeconds - .1f);
        CivicJustice::Report(Offense::Assault, World().playerPosition);
        assert(law.wanted == previous);
        CivicJustice::Update(.11f);
        assert(law.wanted == previous); // Waiting alone never adds another star.
        CivicJustice::Report(Offense::Assault, World().playerPosition);
        assert(law.wanted == previous + 1);
    }
    World().isInsideHome = false;
    SecurityOfficer witness;
    witness.position = {World().playerPosition.x - 1, World().playerPosition.y};
    witness.facing.yaw = 0;
    witness.lastKnown = World().playerPosition;
    law.officers.push_back(witness);
    CivicJustice::Update(.01f);
    assert(law.wanted == 5 && law.identified && law.infamy == 100 && law.pendingInfamy == 0);
    assert(CivicJustice::ShopPrice(100) == 175 && CivicJustice::ShopPrice(-10) == 0);
    World().isInsideHome = true;
    CivicJustice::Update(150);
    assert(law.wanted == 0 && law.infamy > 99 && CivicJustice::ShopPrice(100) > 100);
    World().isInsideHome = false;
    World().playerPosition = Open(3);
    auto resident = Person({World().playerPosition.x + .8f, World().playerPosition.y});
    resident.cityIndex = 3;
    World().residents = {resident};
    float before = Game::Distance(resident.position, World().playerPosition);
    CityResidents::Get().Update(World(), .05f);
    assert(World().residents[0].activity == ResidentActivity::Fleeing);
    assert(Game::Distance(World().residents[0].position, World().playerPosition) > before);
}
void TestFireMeleeAndWalls()
{
    Exterior();
    World().residents = {Person({5, 0})};
    Aim({5, 0});
    auto &pistol = game::GameLevels::Mine().GetState().pistol;
    int rounds = pistol.GetRounds();
    CivicCombat::Fire();
    assert(pistol.GetRounds() == rounds - 1);
    CivicCombat::Update(.15f);
    assert(World().residents[0].health < 100 && CivicJustice::State().wanted >= 1);
    World().playerPosition = {4, 0};
    CivicJustice::State().bullets.clear();
    CivicCombat::Melee();
    int health = World().residents[0].health;
    assert(health <= 70);
    CivicCombat::Melee();
    assert(World().residents[0].health == health);
    CivicCombat::DamageResident(0, 999, {3, 0}, true);
    assert(World().residents[0].health == 0);
    auto heat = CivicJustice::State().heat;
    assert(!CivicCombat::DamageResident(0, 999, {3, 0}, true) && CivicJustice::State().heat == heat);
    Exterior();
    bool tested = false;
    for (const auto &building : World().buildings)
    {
        Vector2 a = {building.x - 1, building.y + building.depth * .5f};
        Vector2 b = {building.x + building.width + 1, a.y};
        if (CivicJustice::Solid(a, .4f) || CivicJustice::Solid(b, .4f))
        {
            continue;
        }
        assert(!CivicJustice::ClearLine(a, b));
        World().playerPosition = a;
        World().residents = {Person(b)};
        Aim(b);
        CivicCombat::Fire();
        CivicCombat::Update(.5f);
        assert(World().residents[0].health == 100 && CivicJustice::State().bullets.empty());
        tested = true;
        break;
    }
    assert(tested);
    // Story actors are seriously injured rather than permanently deleting the story's only contact.
    assert(CivicCombat::DamageLandmark(0, 999) && CivicCombat::LandmarkDown(kLiaPosition));
    World().isInsideHome = true;
    CivicJustice::Update(61);
    assert(!CivicCombat::LandmarkDown(kLiaPosition));
}
void TestBodiesAndVehicles()
{
    Exterior();
    World().playerPosition = {2, 0};
    World().residents = {Person({3, 0})};
    auto resolved = BodyCollision::MovePlayer({2, 0}, {8, 0});
    assert(resolved.x < 3 && Game::Distance(resolved, World().residents[0].position) >= .35f);
    auto &traffic = Overworld::Traffic();
    for (int i = 0; i < 180; ++i)
    {
        traffic.Update(World(), 1.f / 60);
    }
    assert(!traffic.Vehicles().empty());
    auto id = traffic.Vehicles().front().id;
    auto location = traffic.Vehicles().front().position;
    assert(traffic.DamageVehicle(id, 999));
    auto find = [&]() -> const TrafficVehicle & {
        return *std::find_if(traffic.Vehicles().begin(), traffic.Vehicles().end(),
                             [=](const TrafficVehicle &c) { return c.id == id; });
    };
    assert(find().health == 0 && find().speed == 0 && find().reservedFrom == -1);
    traffic.Update(World(), .5f);
    assert(Game::Distance(location, find().position) < .001f);
    auto heat = CivicJustice::State().heat;
    assert(!traffic.DamageVehicle(id, 30) && CivicJustice::State().heat == heat);
    // Existing moving traffic, not a mocked damage event, must strike a pedestrian on its swept hull.
    auto moving =
        std::find_if(traffic.Vehicles().begin(), traffic.Vehicles().end(), [&](const TrafficVehicle &c) {
            return c.health > 0 && c.speed > 1 &&
                   traffic.RenderElevation(size_t(&c - traffic.Vehicles().data())) < .1f;
        });
    assert(moving != traffic.Vehicles().end());
    World().playerPosition = moving->position;
    Stats().health = Stats().maxHealth;
    BodyCollision::VehicleContacts(.03f);
    assert(Stats().health < Stats().maxHealth);
    // A second contact in the same impact window cannot drain health once per simulation substep.
    int health = Stats().health;
    BodyCollision::VehicleContacts(.03f);
    assert(Stats().health == health);
    CivicJustice::State().playerImpact = {};
    Stats().health = 1;
    for (int i = 0; i < 120; ++i)
    {
        traffic.Update(World(), 1.f / 60);
    }
    moving = std::find_if(traffic.Vehicles().begin(), traffic.Vehicles().end(), [&](const TrafficVehicle &c) {
        return c.health > 0 && c.speed > 1 &&
               traffic.RenderElevation(size_t(&c - traffic.Vehicles().data())) < .1f;
    });
    assert(moving != traffic.Vehicles().end());
    World().playerPosition = moving->position;
    BodyCollision::VehicleContacts(.03f);
    assert(HospitalRecovery::Active() && Stats().health == 0);
}
void TestPolice()
{
    Exterior();
    World().playerPosition = World().cameraPosition = Open(1);
    auto &law = CivicJustice::State();
    law.playerGrace = 10000;
    law.wanted = 5;
    law.heat = 1000;
    law.identified = true;
    law.infamy = 80;
    law.sinceCrime = 0;
    law.lastCrime = World().playerPosition;
    size_t seen = 0;
    auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
    {
        CivicJustice::Update(.05f);
        BodyCollision::Update(.05f);
        if (law.officers.size() > seen)
        {
            const auto &officer = law.officers.back();
            assert(officer.tier == 5 && officer.health >= 260);
            assert(TrafficSystem::OutsideView(officer.position, World().cameraPosition, 85));
            seen = law.officers.size();
        }
        assert(law.officers.size() <= 16);
        for (const auto &officer : law.officers)
        {
            assert(!CivicJustice::Solid(officer.position, .28f));
        }
    }
    assert(seen >= 6);
    std::cout
        << "Security simulation: "
        << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() /
               1000
        << " ms/update\n";
    law.officers.clear();
    law.bullets.clear();
    SecurityOfficer officer;
    officer.position = {World().playerPosition.x + 3, World().playerPosition.y};
    officer.lastKnown = World().playerPosition;
    officer.tier = 1;
    officer.health = 100;
    officer.facing.yaw = 3.14159265f;
    law.officers.push_back(officer);
    law.playerGrace = 0;
    law.playerImpact = {};
    law.spawnSeconds = 100;
    Stats().health = Stats().maxHealth;
    CivicJustice::Update(.05f);
    assert(Stats().health == Stats().maxHealth && law.bullets.size() == 1);
    const auto shot = law.bullets.front();
    assert(!shot.playerOwned && std::fabs(shot.direction.y) > .001f);
    CivicCombat::Update(.15f); // Damage occurs along the deviated projectile path, never at trigger time.
    assert(Stats().health < Stats().maxHealth);
    int health = Stats().health;
    CivicJustice::Update(.05f);
    assert(Stats().health == health);
    auto infamy = law.infamy;
    assert(CivicCombat::DamageOfficer(0, 999, {1, 0}));
    assert(law.infamy >= infamy);
}
void TestSecuritySpreadAndEvasion()
{
    combat::SecurityCarbine carbine(1, 4253), elite(5, 4254);
    assert(carbine.GetSpread() > elite.GetSpread());
    float initial = carbine.GetSpread();
    combat::Shot shot;
    int misses = 0;
    for (int i = 0; i < carbine.GetMagazineSize(); ++i)
    {
        assert(carbine.TryFire(0, shot));
        if (std::fabs(std::sin(shot.angle)) * 8 > .18f)
        {
            ++misses;
        }
        assert(std::fabs(shot.angle) <= .28f);
        carbine.Update(1.41f);
    }
    assert(misses > 0 && carbine.GetSpread() > initial && carbine.GetRounds() == 0);
    assert(!carbine.TryFire(0, shot) && carbine.IsReloading());
    carbine.Update(2.2f);
    assert(!carbine.IsReloading() && carbine.GetRounds() == carbine.GetMagazineSize());
    carbine.Update(60);
    assert(std::fabs(carbine.GetSpread() - initial) < .0001f);
    Exterior();
    auto &law = CivicJustice::State();
    auto point = Open(0);
    World().playerPosition = point;
    law.playerGrace = 0;
    law.playerImpact = {};
    const int health = Stats().health;
    law.bullets.push_back({{point.x - 1, point.y}, {1, 0}, 32, 20, 100, false});
    World().playerPosition.y += 1;
    CivicCombat::Update(.1f);
    assert(Stats().health == health); // Moving off the path evades an already fired shot.
    assert(!law.bullets.empty() && std::fabs(law.bullets.front().position.y - point.y) < .001f);
    law.bullets.clear();
    // Hostile bullets use the same swept wall obstruction as player bullets.
    World().buildings = {{point.x + 1, point.y - 1, 2, 2, 100}};
    law.bullets.push_back({point, {1, 0}, 32, 20, 100, false});
    CivicCombat::Update(.2f);
    assert(law.bullets.empty() && Stats().health == health);
}
void TestUnidentifiedEscapeAndVision()
{
    Exterior();
    auto &law = CivicJustice::State();
    World().playerPosition = World().cameraPosition = Open(0);
    law.infamy = 35;
    CivicJustice::Report(Offense::Assault, World().playerPosition);
    law.spawnSeconds = 1000;
    CivicJustice::Update(30);
    assert(law.wanted == 1 && !law.responseStarted && law.initialUnseenSeconds == 0 && law.infamy == 35);
    SecurityOfficer officer;
    officer.position = World().playerPosition;
    officer.position.x -= 1;
    officer.lastKnown = officer.position;
    officer.facing.yaw = 3.14159265f;
    officer.navigationSeconds = 1000;
    assert(!CivicJustice::CanSee(officer, World().playerPosition));
    law.officers.push_back(officer);
    CivicJustice::Update(.01f);
    assert(law.responseStarted && !law.identified && law.initialUnseenSeconds == 0);
    CivicJustice::Report(Offense::Assault, World().playerPosition);
    assert(law.responseStarted && law.initialUnseenSeconds == 0);
    // Stay indoors while responders search the last reported location. Only simulation time counts.
    World().isInsideHome = true;
    CivicJustice::Update(19.9f);
    assert(law.wanted == 1 && law.infamy == 35 && law.pendingInfamy > 0);
    CivicJustice::Report(Offense::Assault, World().playerPosition);
    assert(law.initialUnseenSeconds == 19.9f); // Another crime does not restart the first-response clock.
    CivicJustice::Update(.11f);
    assert(law.wanted == 0 && law.heat == 0 && law.infamy == 35 && law.pendingInfamy == 0);
    Exterior();
    World().playerPosition = Open(0);
    CivicJustice::Report(Offense::Assault, World().playerPosition);
    officer.position = {World().playerPosition.x - 1, World().playerPosition.y};
    officer.lastKnown = World().playerPosition;
    officer.facing.yaw = 0;
    assert(CivicJustice::CanSee(officer, World().playerPosition));
    assert(!CivicJustice::CanSee(officer, {officer.position.x, officer.position.y + 2}));
    assert(!CivicJustice::CanSee(officer, {officer.position.x + 27, officer.position.y}));
    assert(!CivicJustice::CanSee(officer, World().playerPosition, 1));
    law.officers.push_back(officer);
    CivicJustice::Update(.01f);
    assert(law.identified && law.infamy > 0 && law.pendingInfamy == 0);
    float established = law.infamy;
    World().isInsideHome = true;
    law.wanted = 5;
    law.heat = 1000;
    CivicJustice::Update(19.9f);
    assert(law.wanted == 5 && law.heat == 1000 && law.infamy == established);
    // Being seen again restarts the continuous twenty-second escape window.
    World().isInsideHome = false;
    law.officers.front().facing.yaw = 0;
    law.officers.front().route.clear();
    CivicJustice::Update(.01f);
    assert(law.unseenSeconds == 0 && law.wanted == 5);
    World().isInsideHome = true;
    CivicJustice::Update(19.9f);
    assert(law.wanted == 5 && law.heat == 1000);
    CivicJustice::Report(Offense::Assault, World().playerPosition);
    established = law.infamy;
    CivicJustice::Update(.1f);
    assert(law.wanted == 5 && law.heat == 1000); // A new qualifying crime also restarts the window.
    CivicJustice::Update(19.9f);
    assert(law.wanted == 0 && law.heat == 0 && law.infamy == established);
    // Lower levels also clear in one step, with no gradual heat decay beforehand.
    for (int level = 1; level < 5; ++level)
    {
        law.wanted = level;
        law.heat = level * 180.f;
        law.identified = law.responseStarted = true;
        law.sinceCrime = law.unseenSeconds = 0;
        CivicJustice::Update(19.9f);
        assert(law.wanted == level && law.heat == level * 180.f);
        CivicJustice::Update(.1f);
        assert(law.wanted == 0 && law.heat == 0 && law.infamy == established);
    }
    // A wall blocks the same rays used by the minimap cone and by detection.
    Exterior();
    auto p = Open(0);
    World().buildings = {{p.x + 1, p.y - 1, 2, 2, 100}};
    CivicJustice::Reset(World());
    officer.position = p;
    officer.facing.yaw = 0;
    assert(!CivicJustice::CanSee(officer, {p.x + 4, p.y}));
    assert(CivicJustice::SightDistance(officer, 0) < 4);
}
void TestQualifyingDamageAndAircraft()
{
    Exterior();
    auto &law = CivicJustice::State();
    Aim({World().playerPosition.x + 3, World().playerPosition.y});
    CivicCombat::Fire();
    CivicCombat::Update(.05f);
    assert(law.heat == 0 && law.pendingInfamy == 0);
    auto &traffic = Overworld::Traffic();
    for (int i = 0; i < 240; ++i)
    {
        traffic.Update(World(), 1.f / 60);
    }
    assert(traffic.Vehicles().size() > 1);
    auto id = traffic.Vehicles()[0].id;
    assert(traffic.DamageVehicle(id, 1) && law.heat == 0);
    assert(traffic.DamageVehicle(id, 999) && law.wanted == 1 && law.pendingInfamy > 0);
    float heat = law.heat;
    assert(!traffic.DamageVehicle(id, 999) && law.heat == heat);
    auto &empty = const_cast<TrafficVehicle &>(traffic.Vehicles()[1]);
    empty.occupied = false;
    assert(traffic.DamageVehicle(empty.id, 999) && law.heat == heat);
    World().residents = {Person({2, 0})};
    assert(CivicCombat::DamageResident(0, 10, {1, 0}, false) && law.heat == heat);
    assert(CivicCombat::DamageLandmark(0, 1, false) && law.heat == heat);
    Exterior();
    auto &air = Overworld::Flights();
    for (int i = 0; i < 240; ++i)
    {
        air.Update(World(), .05f);
    }
    assert(air.Aircraft().size() > 1);
    auto first = air.Aircraft()[0].id;
    assert(air.DamageAircraft(first, 1) && law.heat == 0);
    assert(air.DamageAircraft(first, 999) && law.wanted == 1);
    heat = law.heat;
    assert(!air.DamageAircraft(first, 999) && law.heat == heat);
    auto &unmanned = const_cast<AircraftActor &>(air.Aircraft()[1]);
    unmanned.occupied = false;
    assert(air.DamageAircraft(unmanned.id, 999) && law.heat == heat);
    // Real weapon input selects a visible aircraft's elevation and uses a travelling 3D projectile.
    Exterior();
    for (int i = 0; i < 200; ++i)
    {
        Overworld::Flights().Update(World(), .05f);
    }
    bool hit = false;
    for (const auto &craft : Overworld::Flights().Aircraft())
    {
        const auto &mesh = models::ResourceAssets::Get(
            models::ModelLibrary::Aircraft(craft.type, craft.alternate).resourceId);
        models::Transform3D transform;
        transform.yaw = craft.yaw;
        transform.roll = craft.bank;
        transform.position.z = craft.altitude;
        auto center =
            transform.Apply({(mesh.minimum.x + mesh.maximum.x) * .5f, (mesh.minimum.y + mesh.maximum.y) * .5f,
                             (mesh.minimum.z + mesh.maximum.z) * .5f});
        for (Vector2 offset : {Vector2{-8, 0}, {8, 0}, {0, -8}, {0, 8}})
        {
            Vector2 origin = {craft.position.x + offset.x, craft.position.y + offset.y};
            if (CivicJustice::Solid(origin, .3f) || !CivicJustice::ClearLine(origin, craft.position))
            {
                continue;
            }
            World().playerPosition = World().cameraPosition = origin;
            World().cursorPosition = WorldCamera::ToViewport(Drawing::WorldToScreen(
                craft.position.x + center.x, craft.position.y + center.y, center.z * 44));
            game::GameLevels::Mine().GetState().pistol.Reset();
            law.airBullets.clear();
            law.bullets.clear();
            int before = craft.health;
            CivicCombat::Fire();
            if (law.airBullets.empty())
            {
                continue;
            }
            assert(craft.health == before);
            CivicCombat::Update(.7f);
            if (craft.health < before)
            {
                hit = true;
                assert(law.heat == 0);
                break;
            }
        }
        if (hit)
        {
            break;
        }
    }
    assert(hit);
}
void TestHospitalAllCauses()
{
    Exterior();
    auto &law = CivicJustice::State();
    assert(law.hospitals.size() == 5);
    for (const auto &site : law.hospitals)
    {
        assert(!CivicJustice::Solid(site.position, .3f));
    }
    Stats().level = 20;
    Stats().experience = game::MineLevel::ExperienceForLevel(20) + 50;
    Stats().credits = 1000;
    Stats().salvage = 11;
    Stats().health = 0;
    law.infamy = 80;
    auto before = World().playerPosition;
    HospitalRecovery::Update(0);
    assert(HospitalRecovery::Active() && HospitalRecovery::Opacity() == 0);
    HospitalRecovery::Update(.4f);
    assert(HospitalRecovery::Opacity() > .4f && law.deaths == 0);
    assert(Game::Distance(before, World().playerPosition) < .001f && Stats().credits == 1000);
    HospitalRecovery::Update(.5f);
    assert(law.deaths == 1 && Stats().level == 19 && Stats().credits == 800 && Stats().salvage == 9);
    assert(Stats().experience == game::MineLevel::ExperienceForLevel(19));
    assert(law.infamy == 80 && law.wanted == 0 && Stats().health == Stats().maxHealth);
    assert(Game::Distance(World().playerPosition, law.hospitals[size_t(law.hospital)].position) < .01f);
    HospitalRecovery::Update(3);
    HospitalRecovery::Update(3);
    assert(!HospitalRecovery::Active() && law.deaths == 1 && Stats().credits == 800);
    Stats().level = 1;
    Stats().credits = 1;
    Stats().salvage = 0;
    Stats().health = 0;
    HospitalRecovery::Begin();
    HospitalRecovery::Update(3);
    assert(Stats().level == 1 && Stats().credits == 0 && Stats().salvage == 0 && law.deaths == 2);
    Exterior();
    auto &mine = game::GameLevels::Mine();
    mine.Enter(true);
    auto quest = mine.GetState().quest.GetStage();
    Stats().health = 0;
    HospitalRecovery::Update(0);
    HospitalRecovery::Update(3);
    assert(!mine.IsActive() && mine.GetState().quest.GetStage() == quest && Stats().health > 0);
}
} // namespace
int main()
{
    TestSeverityAndMemory();
    TestFireMeleeAndWalls();
    TestBodiesAndVehicles();
    TestPolice();
    TestSecuritySpreadAndEvasion();
    TestUnidentifiedEscapeAndVision();
    TestQualifyingDamageAndAircraft();
    TestHospitalAllCauses();
    Game::Get().Reset();
    std::cout
        << "Crime, combat, physical contact, cross-city reputation, security and hospital checks passed.\n";
}
