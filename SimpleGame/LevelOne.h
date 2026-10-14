#pragma once
#include "TutorialState.h"
#include "MineQuest.h"
#include "Weapons/Pistol.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace levelone
{
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
};
struct Tracer
{
    tutorial::Vector2 start;
    tutorial::Vector2 end;
    float remaining = .12f;
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
};
struct ScannerContact
{
    float angle;
    float distance;
    size_t group;
};
struct State
{
    bool active = false;
    bool generated = false;
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
extern State g_level;

bool IsActive();
void Enter(bool debugEntry = false);
void Leave();
void ResetProgress();
void RestartMine();
void GenerateMap(std::uint32_t seed);
bool IsWalkable(int x, int y);
bool IsBlocked(tutorial::Vector2 position);
bool HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to);
bool IsGateCell(int x, int y);
std::vector<tutorial::Vector2> FindRoute(tutorial::Vector2 from, tutorial::Vector2 to);
void UpdateNavigation();
void Update(float deltaSeconds);
void Attack();
void Reload();
void Scan();
bool DamageEnemy(size_t index, int damage);
void Interact();
void UseRecoveryKit();
int ExperienceForLevel(int level);
void AddExperience(int amount);
float GetAimAngle();
tutorial::Vector2 GetAimDirection(float screenAngle);
float LightAt(tutorial::Vector2 position);
void DrawWorld();
void DrawDarkness();
void DrawHud();
} // namespace levelone
