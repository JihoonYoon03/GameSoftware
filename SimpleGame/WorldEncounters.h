#pragma once
#include "Drawing.h"
#include "Game.h"
#include "ModelLibrary.h"
#include "World.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tutorial
{
class SaveArchive;
}

namespace encounters
{
using tutorial::ColorRGBA;
using tutorial::Vector2;

enum class Archetype : int
{
    ConvoyAmbush = 0,     // 1. 조난 호송대 방어 · 차량 수리 · 동행 호위
    OutlawBountyRaid,     // 2. 불법 중계기 삼각측량 · 안테나 파괴 · 보스 소탕
    RuinsResonance,       // 3. 고대 석비 3중 공명 · 수호체 격퇴 · 코어 회수
    MineCollapseRescue,   // 4. 광맥 붕괴 잔해 해체 · 포식자 소탕 · 작업자 3명 구조
    RogueDroneSwarm,      // 5. 폭주 드론 제어 노드 3곳 해킹 · 지휘 드론 격추
    CrashedFreighter,     // 6. 추락 화물선 3구역 인양 · 약탈단 격퇴
    WildernessAnomaly,    // 7. 지질 이상 3지점 채취 · 스캔 삼각측량 · 변이체 소탕
    MissingPatrol,        // 8. 실종 순찰대 흔적 3곳 추적 · 포위 돌파 · 귀환 호위
    SmugglerStandoff,     // 9. 밀수 현장 단서 2곳 조사 · 3방향 협상/전투/정보 분기
    RelayDefenseHold,     // 10. 임시 중계 비컨 30초 2웨이브 방어 · 냉각 밸브 조작
    Count
};

enum class Phase : int
{
    Dormant = 0,    // Spawned on map, waiting for discovery / briefing
    Stage1 = 1,     // Initial briefing / reconnaissance / 1st wave
    Stage2 = 2,     // Multi-node investigation / sabotage / repair / triangulation
    Stage3 = 3,     // Climax wave / boss / escort / 30s hold defense
    Stage4 = 4,     // Resolution / branching choice / reward claim
    Completed = 5,  // Permanently resolved (one-time reward claimed)
    Count
};

enum class ResolutionChoice : int
{
    None = 0,
    OptionA, // e.g., Full handover / Security report / Rescue priority / Raid
    OptionB, // e.g., Salvage split / Core dismantle / Negotiate
    OptionC, // e.g., Black-market intel deal (SmugglerStandoff)
    Count
};

struct EncounterEnemy
{
    int id = 0;
    std::string label;
    Vector2 offset = {}; // Local meters (surface) or legacy units relative to anchor
    Vector2 home = {};
    float yaw = 0.f;
    float radius = .48f;
    int health = 65;
    int maxHealth = 65;
    float fireCooldown = .9f;
    float alertSeconds = 0.f;
    float hitFlash = 0.f;
    int wave = 1; // 1 = initial/stage1-2, 2 = stage3 reinforcement/boss
    bool isBoss = false;
    bool meleeOnly = false;
    bool active = false;
};

struct EncounterNpc
{
    std::string name;
    models::CharacterType type = models::CharacterType::Civilian;
    models::Gender gender = models::Gender::Male;
    models::HairStyle hair = models::HairStyle::SidePart;
    ColorRGBA coat = {.46f, .58f, .64f, 1.f};
    Vector2 offset = {};
    Vector2 escortTarget = {};
    float yaw = 0.f;
    bool escorting = false;
    bool arrived = false;
    bool rescued = false;
    bool visible = true;
};

struct EncounterNode
{
    int index = 0;
    int requiredStage = 1; // 0 = contact/briefing, 1..4 = stage nodes
    Vector2 offset = {};
    float radius = 2.4f;
    std::string label;
    ColorRGBA glow = {.36f, .92f, .96f, 1.f};
    bool hiddenUntilScanned = false;
    bool revealed = true;
    bool completed = false;
};

struct EncounterBullet
{
    Vector2 offset = {};
    Vector2 velocity = {};
    float life = 0.f;
    int damage = 8;
    bool playerOwned = true;
};

struct EncounterTracer
{
    Vector2 from = {};
    Vector2 to = {};
    ColorRGBA color = {1.f, .88f, .52f, 1.f};
    float life = 0.f;
};

struct EncounterInstance
{
    std::string id;
    std::string regionId;
    std::string title;
    std::string subtitle;
    Archetype archetype = Archetype::ConvoyAmbush;
    Phase phase = Phase::Dormant;
    ResolutionChoice choice = ResolutionChoice::None;
    bool surfaceSpace = true; // true = Planet SurfacePoint, false = Legacy Halo city Vector2
    world::SurfacePoint surfaceAnchor{};
    Vector2 legacyAnchor = {};

    // Multi-stage progress counters
    int stageObjectivesDone = 0;
    int stageObjectivesTotal = 3;
    float defenseHoldSeconds = 0.f;
    float defenseHoldTarget = 26.f;
    int beaconHealth = 100;
    int beaconMaxHealth = 100;
    bool discoveredNotified = false;
    bool rewardClaimed = false;
    bool wave2Spawned = false;
    bool activeInRange = false;

    std::vector<EncounterNode> nodes;
    std::vector<EncounterEnemy> enemies;
    std::vector<EncounterNpc> npcs;
    std::vector<EncounterBullet> bullets;
    std::vector<EncounterTracer> tracers;
};

// Persistent compact record saved in Save Schema 16
struct EncounterSaveEntry
{
    std::string id;
    Archetype archetype = Archetype::ConvoyAmbush;
    Phase phase = Phase::Dormant;
    ResolutionChoice choice = ResolutionChoice::None;
    int stageObjectivesDone = 0;
    bool rewardClaimed = false;
};

struct WorldEncountersState
{
    std::string worldKey;
    int totalCompleted = 0;
    std::string activeEncounterId;
    std::vector<EncounterSaveEntry> saved;
    std::vector<EncounterInstance> live;

    bool IsDefaultClean() const
    {
        return totalCompleted == 0 && activeEncounterId.empty() && saved.empty();
    }
};

// ============================================================================
// WorldEncounters — 10-archetype, 4-stage open-world random encounters & subquests
// ============================================================================
class WorldEncounters
{
    friend class tutorial::SaveArchive;

  public:
    static WorldEncounters &Get();

    WorldEncountersState &State()
    {
        return state;
    }
    const WorldEncountersState &State() const
    {
        return state;
    }

    void Reset();
    void PopulateForCurrentWorld();
    void Update(float dt);
    void FailActiveEncounter();
    void Untrack();
    bool IsMainStoryFocused() const;

    // Combat & interaction hooks (both SurfaceExterior and LegacyCity)
    bool HitAtWorldPoint(const world::SurfacePoint &point, int damage, bool playerOwned);
    bool HitAtLegacyPoint(Vector2 pos, int damage, bool playerOwned);
    bool TryMelee(Vector2 playerPos, float yaw, bool surfaceMode);
    bool TriggerScannerPing();
    bool HasNearbyInteraction() const;
    std::string NearbyInteractionLabel() const;
    bool Interact();

    // Queries for HUD, Journal, Map & Monitor
    const EncounterInstance *ActiveEncounter() const;
    const EncounterInstance *NearestEncounter(float maxMeters = 320.f) const;
    std::string CurrentObjectiveTitle() const;
    std::string CurrentObjectiveAction() const;
    std::vector<std::string> JournalLines() const;

    static const char *ArchetypeName(Archetype a);
    static const char *ArchetypeKey(Archetype a);
    static const char *PhaseKey(Phase p);
    static int StageNumber(Phase p);
    static constexpr int kStagesPerEncounter = 4;

    // Persist/restore live state from compact save entries
    void SyncSaveRecords();
    void ApplySavedRecords();

    // Convert between player world position and encounter-local offset (in meters for surface, world units for legacy)
    std::optional<Vector2> PlayerOffsetIn(const EncounterInstance &enc) const;
    Vector2 ToLegacyWorld(const EncounterInstance &enc, Vector2 offset) const;
    world::SurfacePoint ToSurfaceWorld(const EncounterInstance &enc, Vector2 offsetMeters) const;

  private:
    WorldEncountersState state;
    float scannerCooldown = 0.f;
    float scannerPulseRadius = 0.f;
    Vector2 scannerOriginLegacy = {};
    world::SurfacePoint scannerOriginSurface{};
    bool scannerSurfaceSpace = false;

    void BuildLegacyOutskirtsEncounters();
    void BuildPlanetSurfaceEncounters();
    void BuildArchetypeLayout(EncounterInstance &enc, std::uint64_t seed);
    void SpawnWaveEnemies(EncounterInstance &enc, int wave);
    void AdvanceStageIfReady(EncounterInstance &enc);
    void OpenBriefingDialogue(EncounterInstance &enc);
    void OpenStage4ResolutionDialogue(EncounterInstance &enc);
    void GrantReward(EncounterInstance &enc, ResolutionChoice choice);
    void DamagePlayer(int amount);
    void DamageEnemy(EncounterInstance &enc, EncounterEnemy &enemy, int damage, bool playerOwned);
    EncounterInstance *FindLive(const std::string &id);
    int AliveEnemies(const EncounterInstance &enc, int waveFilter = 0) const;
};
} // namespace encounters
