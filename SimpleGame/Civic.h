#pragma once
#include <functional>
#include "Game.h"
#include "Level.h"
#include <array>

namespace tutorial
{
enum class Offense
{
    Discharge,
    Assault,
    Homicide,
    VehicleDamage,
    VehicleDestruction,
    AssaultOfficer,
    OfficerKilled,
    RecklessDriving,
    AggravatedAssault
};
struct HospitalSite
{
    int city = 0;
    Vector2 position = {};
};
struct SecurityOfficer
{
    std::uint64_t id = 0;
    Vector2 position = {}, lastKnown = {};
    MovementFacing facing;
    int tier = 1, health = 80;
    float cooldown = 0, navigationSeconds = 0, corpseSeconds = 0, muzzleFlash = 0;
    BodyImpact impact;
    combat::SecurityCarbine weapon;
    int weaponTier = 0;
    std::vector<Vector2> route;
    size_t waypoint = 0;
};
enum class RecoveryPhase
{
    None,
    FadeOut,
    Transfer,
    FadeIn
};
struct AirProjectile
{
    models::Point position, direction;
    float speed = 0, remaining = 0;
    int damage = 0;
};
struct AirTracer
{
    models::Point start, end;
    float remaining = .12f;
};
struct CivicState
{
    float heat = 0, infamy = 0, sinceCrime = 1000, unseenSeconds = 0, spawnSeconds = 0;
    float dischargeCooldown = 0, playerGrace = 0, muzzleFlash = 0, meleeSeconds = 0;
    int wanted = 0;
    Vector2 lastCrime = {};
    std::array<int, 3> landmarkHealth = {100, 100, 100};
    std::array<float, 3> landmarkRecovery = {};
    std::vector<SecurityOfficer> officers;
    std::vector<game::Bullet> bullets;
    std::vector<game::Tracer> tracers;
    std::vector<AirProjectile> airBullets;
    std::vector<AirTracer> airTracers;
    std::vector<HospitalSite> hospitals;
    std::uint64_t nextOfficer = 1;
    BodyImpact playerImpact;
    RecoveryPhase recovery = RecoveryPhase::None;
    float recoverySeconds = 0;
    int hospital = 0, deaths = 0, lostLevels = 0, lostCredits = 0, lostSalvage = 0;
    float pursuitSeconds = 0, initialUnseenSeconds = 0, pendingInfamy = 0;
    bool responseStarted = false, identified = false;
    unsigned randomState = 0x39ab17u;
};
// ============================================================================
// CivicJustice — shared frontier security network, active pursuit and lasting reputation
// ============================================================================
class CivicJustice
{
  public:
    static constexpr float kMaximumHeat = 1000;
    static constexpr float kUnidentifiedEscapeSeconds = 20;
    static constexpr float kSightRange = 26;
    static constexpr float kSightHalfAngle = .95993109f; // 55 degrees; total field of view is 110 degrees.
    static CivicState &State();
    static void Reset(GameState &world);
    static void Report(Offense offense, Vector2 position);
    static int WantedForHeat(float heat);
    static float PriceMultiplier();
    static int ShopPrice(int basePrice);
    static int LivingPopulation(int city = -1);
    static bool IsExterior();
    static bool ClearLine(Vector2 from, Vector2 to);
    static bool Solid(Vector2 position, float radius = .3f, float altitude = 0, bool driving = false);
    static void Update(float deltaSeconds);
    static bool CanSee(const SecurityOfficer &officer, Vector2 position, float altitude = 0);
    static float SightDistance(const SecurityOfficer &officer, float angle);
};
// ============================================================================
// CivicCombat — city firearm/melee damage and bounded security response
// ============================================================================
class CivicCombat
{
  public:
    static bool Available();
    static void Update(float deltaSeconds);
    static void Fire();
    static void Melee();
    static bool DamageResident(size_t index, int damage, Vector2 impulse, bool playerOwned);
    static bool DamageOfficer(size_t index, int damage, Vector2 impulse, bool playerOwned = true);
    static bool DamageLandmark(size_t index, int damage, bool weaponOwned = true);
    static void DamagePlayer(int damage, Vector2 impulse = {});
    static bool LandmarkDown(Vector2 position);
};
// ============================================================================
// BodyCollision — swept bodies, separation and reaction state; animation never drives physics
// ============================================================================
class BodyCollision
{
  public:
    static Vector2 Slide(Vector2 from, Vector2 desired, const std::function<bool(Vector2)> &blocked);
    static bool BikeBlocked(Vector2 position, float yaw, float elevation, bool includeVehicles = true);
    static void RecoverBike();
    static Vector2 MovePlayer(Vector2 from, Vector2 desired);
    static Vector2 MoveResident(size_t index, Vector2 from, Vector2 desired);
    static void Update(float deltaSeconds);
    static void VehicleContacts(float deltaSeconds);
};
// ============================================================================
// HospitalRecovery — all-cause defeat, black-screen transfer and a single penalty transaction
// ============================================================================
class HospitalRecovery
{
  public:
    static constexpr float kLevelLoss = .05f, kAssetLoss = .20f;
    static bool Active();
    static void Begin();
    static void Update(float deltaSeconds);
    static float Opacity();
};
// ============================================================================
// CivicPresentation — read-only rendering of security, clinics and recovery overlays
// ============================================================================
class CivicPresentation
{
  public:
    static void DrawWorld();
    static void DrawHud();
    static void DrawRecovery();
};
} // namespace tutorial
