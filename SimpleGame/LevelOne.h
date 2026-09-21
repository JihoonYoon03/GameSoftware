#pragma once
#include "TutorialState.h"
#include <cstdint>
#include <vector>

namespace levelone
{
constexpr int kMapSize = 32;
constexpr int kEnemyMaxHealth = 32;
constexpr int kExperiencePerKill = 20;
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
    int attack = 12;
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
struct State
{
    bool active = false;
    bool generated = false;
    bool growthGoalReached = false;
    std::uint32_t seed = 0;
    PlayerStats player;
    std::vector<Tile> tiles;
    std::vector<int> reachableCells;
    std::vector<int> navigationDistances;
    std::vector<Enemy> enemies;
    std::vector<Drop> drops;
    tutorial::Vector2 entry = {16.5f, 16.5f};
    float attackCooldown = 0;
    float attackFlash = 0;
    float invulnerability = 0;
    float navigationCooldown = 0;
    int kills = 0;
    std::uint64_t nextDropActorId = 1;
};
extern State g_level;

bool IsActive();
void Enter();
void Leave();
void ResetProgress();
void GenerateMap(std::uint32_t seed);
bool IsWalkable(int x, int y);
bool IsBlocked(tutorial::Vector2 position);
bool HasLineOfSight(tutorial::Vector2 from, tutorial::Vector2 to);
void UpdateNavigation();
void Update(float deltaSeconds);
void Attack();
void Interact();
void UseRecoveryKit();
int ExperienceForLevel(int level);
void AddExperience(int amount);
void DrawWorld();
void DrawHud();
} // namespace levelone
