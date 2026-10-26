#include "stdafx.h"
#include "Civic.h"
#include "Drawing.h"
#include "Overworld.h"
#include "ResidentAI.h"
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
    CivicJustice::Report(Offense::Discharge, World().playerPosition);
    assert(law.heat == 12 && law.wanted == 0);
    CivicJustice::Report(Offense::Discharge, World().playerPosition);
    assert(law.heat == 12); // burst debounce
    CivicJustice::Report(Offense::Assault, World().playerPosition);
    assert(law.wanted == 1);
    float assault = law.heat;
    CivicJustice::Report(Offense::Homicide, World().playerPosition);
    assert(law.heat - assault > 100 && law.wanted >= 3);
    for (int i = 0; i < 10; ++i)
    {
        CivicJustice::Report(Offense::OfficerKilled, World().playerPosition);
    }
    assert(law.wanted == 5 && law.heat <= 1000 && law.infamy == 100);
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
    for (int i = 0; i < 6; ++i)
    {
        CivicJustice::Report(Offense::Homicide, World().playerPosition);
    }
    assert(law.wanted == 5);
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
    TestHospitalAllCauses();
    Game::Get().Reset();
    std::cout
        << "Crime, combat, physical contact, cross-city reputation, security and hospital checks passed.\n";
}
