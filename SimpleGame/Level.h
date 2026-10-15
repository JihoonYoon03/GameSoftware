#pragma once
#include "Game.h"
#include "Weapon.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace game
{

// ============================================================================
// MineQuest — ordered prologue state
// ============================================================================
enum class MineQuestStage
{
    ShowRecord,
    CheckCityRelay,
    TravelToMine,
    BypassPower,
    InvestigateSignal,
    InvestigateGlyph,
    CollectCoordinates,
    ReturnToHalo,
    Complete
};

class MineQuest
{
  public:
    MineQuestStage GetStage() const;
    bool Advance(MineQuestStage expected);
    bool BeginJourney();
    bool IsRiaJoined() const;
    bool IsPowerBypassed() const;
    bool AreCoordinatesFound() const;
    const char *GetObjective() const;

  private:
    MineQuestStage stage = MineQuestStage::ShowRecord;
    bool riaJoined = false;
};
// ============================================================================
// Mine data
// ============================================================================
constexpr int kMapSize = 64;
constexpr int kEnemyPopulation = 30;
constexpr float kSafeAreaRadius = 4.f;
constexpr float kEnemySpawnRadius = 7.f;
constexpr int kEnemyMaxHealth = 40;
constexpr int kExperiencePerKill = 20;
constexpr tutorial::Vector2 kMineEntry = {8.5f, 9.5f};
constexpr tutorial::Vector2 kPowerBypass = {25.5f, 24.5f};
constexpr tutorial::Vector2 kRuinSignal = {47.5f, 46.5f};
constexpr tutorial::Vector2 kObserverGlyph = {51.5f, 46.5f};
constexpr tutorial::Vector2 kCoordinateTerminal = {52.5f, 51.5f};

enum class Tile
{
    Wall,
    Floor,
    Water
};
enum class ItemKind
{
    RecoveryKit,
    Salvage
};
struct PlayerStats
{
    int experience = 0;
    int level = 1;
    int maxHealth = 100;
    int health = 100;
    int attack = 18;
    int defense = 2;
    int recoveryKits = 3;
    int salvage = 0;
};
struct Enemy
{
    tutorial::Vector2 position;
    int health = kEnemyMaxHealth;
    float attackCooldown = 0;
    float respawnSeconds = 0;
    tutorial::MovementFacing facing;
    tutorial::Vector2 target = {};
};
struct Drop
{
    tutorial::Vector2 position;
    ItemKind kind;
    std::uint64_t actorId = 0;
};
struct Bullet
{
    tutorial::Vector2 position;
    tutorial::Vector2 direction;
    float speed;
    float remaining;
    int damage;
    bool playerOwned = true;
};
struct Tracer
{
    tutorial::Vector2 start;
    tutorial::Vector2 end;
    float remaining = .12f;
    bool npcOwned = false;
};
struct MineLight
{
    tutorial::Vector2 position;
    float radius;
    bool needsPower = false;
};
struct RescueGroup
{
    tutorial::Vector2 position;
    bool combatants = false;
    bool rescued = false;
};
struct NpcCombatState
{
    int health = 100;
    float cooldown = 0, hitGrace = 0, muzzleFlash = 0, downedSeconds = 0;
    bool engaged = false, evading = false;
    float aimYaw = 0;
    combat::Pistol pistol;
};
struct RescueNpc
{
    std::uint64_t id;
    size_t group;
    tutorial::Vector2 position;
    bool evacuating = false;
    float walkedDistance = 0;
    float outsideSeconds = 0;
    size_t routeIndex = 0;
    std::vector<tutorial::Vector2> route;
    tutorial::MovementFacing facing;
    models::Gender gender = models::Gender::Male;
    models::HairStyle hair = models::HairStyle::SidePart;
    NpcCombatState combat;
};
struct ScannerContact
{
    float angle;
    float distance;
    size_t group;
};
struct MineState
{
    bool active = false;
    bool generated = false;
    std::uint64_t mapRevision = 0;
    std::uint32_t seed = 0;
    MineQuest quest;
    PlayerStats player;
    combat::Pistol pistol;
    std::vector<Tile> tiles;
    std::vector<int> reachableCells;
    std::vector<int> navigationDistances;
    std::vector<Enemy> enemies;
    std::vector<Drop> drops;
    std::vector<Bullet> bullets;
    std::vector<Tracer> tracers;
    std::vector<MineLight> lights;
    std::vector<RescueGroup> rescueGroups;
    std::vector<RescueNpc> survivors;
    std::vector<ScannerContact> scannerContacts;
    tutorial::Vector2 entry = kMineEntry;
    tutorial::Vector2 riaPosition = {9.5f, 10.5f};
    std::vector<tutorial::Vector2> riaRoute;
    size_t riaRouteIndex = 0;
    float riaNavigationCooldown = 0;
    float riaMovement = 0;
    tutorial::MovementFacing riaFacing;
    NpcCombatState riaCombat;
    float invulnerability = 0;
    float navigationCooldown = 0;
    float scannerSeconds = 0;
    float scannerCooldown = 0;
    float muzzleFlash = 0;
    int kills = 0;
    int rescuedPeople = 0;
    int returnedPeople = 0;
    unsigned int totalEnemyDeaths = 0;
    std::uint64_t nextDropActorId = 1;
};

// ============================================================================
// Travel data
// ============================================================================
constexpr float kRouteLength = 1800;
constexpr float kRoutePixelScale = 12;
constexpr int kObstacleCount = 7;
struct Obstacle
{
    float distance;
    float lane;
    bool passed = false;
};
struct TravelState
{
    bool active = false;
    float elapsedSeconds = 0;
    float distance = 0;
    float speed = 35;
    float lateralOffset = 0;
    float lateralVelocity = 0;
    float yawRadians = 0;
    float boostCharge = 1;
    float speedPenaltySeconds = 0;
    int avoidedObstacles = 0;
    int collisionCount = 0;
    std::array<Obstacle, kObstacleCount> obstacles = {};
};

// ============================================================================
// Level — shared lifecycle
// ============================================================================

class Level
{
  public:
    Level() = default;
    Level(const Level &) = delete;
    Level &operator=(const Level &) = delete;
    virtual ~Level() = default;
    virtual void Enter(bool debugEntry = false) = 0;
    virtual void Reset() = 0;
    virtual void Update(float deltaSeconds) = 0;
    virtual bool IsActive() const = 0;
    virtual bool UsesWalkingControls() const
    {
        return true;
    }
};
class MineLevel;

// ============================================================================
// MineMap
// ============================================================================
class MineMap
{
  public:
    explicit MineMap(MineLevel &level);
    bool IsGateCell(int x, int y);
    bool IsWalkable(int x, int y);
    bool IsBlocked(tutorial::Vector2 position);
    bool HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to);
    std::vector<tutorial::Vector2> FindRoute(tutorial::Vector2 from, tutorial::Vector2 to);
    void UpdateNavigation();
    void Generate(std::uint32_t seed);

  private:
    MineLevel &level;
    MineState &state;
    void Carve(int x, int y);
    void Room(int left, int top, int right, int bottom);
    void Tunnel(int x, int y, int targetX, int targetY);
    std::vector<int> Distances(tutorial::Vector2 destination, bool ignoreGate = false);
};

// ============================================================================
// MineCombat
// ============================================================================
class MineCombat
{
  public:
    explicit MineCombat(MineLevel &level);
    void UpdateEnemies(float deltaSeconds);
    void Attack();
    void Reload();
    bool DamageEnemy(size_t index, int damage, bool playerCredit = true);
    void UpdateNpcs(float deltaSeconds);
    void Update(float deltaSeconds);

  private:
    MineLevel &level;
    MineState &state;
    void RespawnPlayer();
    void MoveEnemy(Enemy &enemy, float deltaSeconds, tutorial::Vector2 target, bool playerTarget);
    void UpdateBullets(float deltaSeconds);
    void AdvanceBullet(Bullet &bullet, float movement);
};

// ============================================================================
// MineRescue
// ============================================================================
class MineRescue
{
  public:
    explicit MineRescue(MineLevel &level);
    void Update(float deltaSeconds);
    void Scan();
    bool TryInteract();

  private:
    MineLevel &level;
    MineState &state;
    float WalkRoute(tutorial::Vector2 &position, const std::vector<tutorial::Vector2> &route, size_t &index,
                    float amount, tutorial::MovementFacing &facing);
};

// ============================================================================
// MineLighting
// ============================================================================
class MineLighting
{
  public:
    explicit MineLighting(MineLevel &level);
    float At(tutorial::Vector2 position);

  private:
    MineLevel &level;
    MineState &state;
    bool LightPath(tutorial::Vector2 from, tutorial::Vector2 to);
    float RadialLight(tutorial::Vector2 from, tutorial::Vector2 to, float radius);
};

// ============================================================================
// MineLevel : Level — investigation and feature orchestration
// ============================================================================
class MineLevel final : public Level
{
  public:
    MineLevel();
    MineState &GetState();
    MineMap &Map();
    MineCombat &Combat();
    MineRescue &Rescue();
    MineLighting &Lighting();
    void Update(float deltaSeconds) override;
    bool IsNear(tutorial::Vector2 position);
    bool IsActive() const override;
    static int ExperienceForLevel(int level);
    void AddExperience(int amount);
    void Enter(bool debugEntry = false) override;
    void Leave();
    void Reset() override;
    void Restart();
    float GetAimAngle();
    static tutorial::Vector2 GetAimDirection(float angle);
    void Interact();
    void UseRecoveryKit();

  private:
    friend class MineMap;
    friend class MineCombat;
    friend class MineRescue;
    MineState state;
    MineMap map;
    MineCombat combat;
    MineRescue rescue;
    MineLighting lighting;
    std::uint64_t generationRevision = 0;
    static tutorial::Vector2 Project(tutorial::Vector2 position, float height = 0);
    static void ClearInput();
};

// ============================================================================
// JetTravelLevel : Level — bike controls and travel progression
// ============================================================================
class JetTravelLevel final : public Level
{
  public:
    TravelState &GetState();
    bool UsesWalkingControls() const override
    {
        return false;
    }
    void Reset() override;
    void Enter(bool debugEntry = false) override;
    bool IsActive() const override;
    tutorial::Vector2 GetBikeScreenPosition();
    float GetObstacleScreenX(const Obstacle &obstacle);
    models::Palette GetBikePalette();
    void Update(float deltaSeconds) override;

  private:
    TravelState state;
};

// ============================================================================
// ChapterOneLevel : Level — Light of the Frontier, three freely ordered sites
// ============================================================================
constexpr int kFrontierWidth = 48, kFrontierHeight = 44;
constexpr tutorial::Vector2 kFrontierEntry = {5.5f, 5.5f};
enum class FrontierTile
{
    Grass,
    Sand,
    Road,
    Canal,
    Foundation
};
enum class ChapterAction
{
    Exit,
    Generator,
    IsolateLine,
    PortableLight,
    Kain,
    Receiver,
    SurveyOne,
    SurveyTwo,
    SurveyThree,
    Escort,
    Vehicle,
    TradeProof,
    Scrappers,
    Ledger,
    DetailLedger,
    ConvoySupply
};
struct ChapterInteraction
{
    ChapterAction action;
    tutorial::Vector2 position;
    const char *label;
};
struct FrontierPerson
{
    tutorial::Vector2 position = {};
    tutorial::MovementFacing facing;
    NpcCombatState combat;
    std::vector<tutorial::Vector2> route;
    size_t waypoint = 0;
    bool arrived = false;
    float departureDelay = 0;
};
enum class ChapterTeam
{
    Player,
    Companion,
    Scrapper
};
struct ChapterProjectile
{
    Bullet shot;
    ChapterTeam team;
};
struct ChapterOneState
{
    bool unlocked = false, active = false, generated = false;
    bool generatorInspected = false, generatorRestored = false, portableLightRepaired = false;
    bool convoyBriefed = false, receiverInspected = false, escortStarted = false, convoyRescued = false;
    int surveyMarkers = 0;
    bool kainJoined = false, vehicleRepaired = false, tradeProofFound = false;
    bool hostile = false, negotiated = false, ledgerFound = false, detailLedgerFound = false;
    bool comparisonStarted = false, recordsCompared = false, waterwayUnlocked = false;
    float comparisonSeconds = 0, invulnerability = 0, muzzleFlash = 0;
    bool mapVisible = true, journalVisible = false;
    std::vector<FrontierTile> tiles;
    std::vector<tutorial::Building> structures;
    std::vector<FrontierPerson> survivors, scrappers;
    FrontierPerson ria, kain;
    std::vector<ChapterProjectile> bullets;
    std::vector<Tracer> tracers;
};
class ChapterOneLevel;
// ============================================================================
// FrontierMap — connected outdoor terrain and route planning
// ============================================================================
class FrontierMap
{
  public:
    explicit FrontierMap(ChapterOneLevel &owner);
    void Generate();
    bool IsBlocked(tutorial::Vector2 position) const;
    bool HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to) const;
    std::vector<tutorial::Vector2> FindRoute(tutorial::Vector2 from, tutorial::Vector2 to) const;

  private:
    ChapterOneState &state;
    bool IsWalkable(int x, int y) const;
};
// ============================================================================
// FrontierCombat — firearms, faction hits and combat recovery
// ============================================================================
class FrontierCombat
{
  public:
    explicit FrontierCombat(ChapterOneLevel &owner);
    void Attack();
    void Update(float deltaSeconds);

  private:
    ChapterOneLevel &level;
    ChapterOneState &state;
    void Advance(ChapterProjectile &projectile, float distance);
    void Fire(FrontierPerson &person, tutorial::Vector2 target, ChapterTeam team);
};
// ============================================================================
// FrontierParty — companions, escorted survivors and departing scrappers
// ============================================================================
class FrontierParty
{
  public:
    explicit FrontierParty(ChapterOneLevel &owner);
    void Update(float deltaSeconds);

  private:
    ChapterOneLevel &level;
    ChapterOneState &state;
    bool Move(FrontierPerson &person, float amount);
};
// ============================================================================
// ChapterOneLevel : Level — quest progression and field/hub lifecycle
// ============================================================================
class ChapterOneLevel final : public Level
{
  public:
    ChapterOneLevel();
    ChapterOneState &GetState();
    bool IsAvailable() const;
    bool IsActive() const override;
    bool Unlock();
    void Enter(bool debugEntry = false) override;
    void Leave();
    void Reset() override;
    void Update(float deltaSeconds) override;
    void Interact();
    void InteractHub();
    bool IsBlocked(tutorial::Vector2 position) const;
    bool HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to) const;
    std::vector<tutorial::Vector2> FindRoute(tutorial::Vector2 from, tutorial::Vector2 to) const;
    std::vector<ChapterInteraction> Interactions() const;
    bool NearestInteraction(ChapterInteraction &result) const;
    int CompletedSites() const;
    const char *Objective() const;
    float GetAimAngle() const;
    void Attack();

  private:
    ChapterOneState state;
    FrontierMap map;
    FrontierCombat combat;
    FrontierParty party;
    bool RelayClear() const;
};

// ============================================================================
// GameLevels — lifetime and polymorphic active-level access
// ============================================================================
class GameLevels
{
  public:
    static MineLevel &Mine();
    static JetTravelLevel &Travel();
    static ChapterOneLevel &Chapter();
    static bool IsCombatActive();
    static float AimAngle();
    static float MuzzleFlash();
    static Level *Active();
    static void Reset();

  private:
    static GameLevels &Get();
    MineLevel mine;
    JetTravelLevel travel;
    ChapterOneLevel chapter;
};

} // namespace game
