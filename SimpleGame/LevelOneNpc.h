#pragma once
#include "TutorialState.h"
#include <cstddef>
#include <vector>

namespace levelone
{
enum class NpcPattern
{
    Engineer,
    Guard,
    Medic,
    Courier,
    Researcher,
    Civilian,
    Trader
};
enum class NpcActivity
{
    Working,
    Walking,
    Following,
    Retreating,
    Talking,
    Repairing,
    Defending,
    Supplying,
    Healing,
    Investigating,
    Recovering
};
enum class WorkstationStatus
{
    AwaitingSupplies,
    Repairing,
    Threatened,
    Operational
};
enum class WorkstationType
{
    CommunicationRelay,
    EmergencyPower,
    WaterPump,
    SalvageCrane
};
struct Workstation
{
    int id = 0;
    int teamId = 0;
    const char *name = "";
    WorkstationType type = WorkstationType::CommunicationRelay;
    tutorial::Vector2 position;
    float integrity = 100;
    float repairProgress = 0;
    float cycleCooldown = 0;
    int supplies = 1;
    int completedCycles = 0;
    WorkstationStatus status = WorkstationStatus::AwaitingSupplies;
    std::vector<int> navigationDistances;
};
struct NpcProfile
{
    const char *name;
    const char *personality;
    NpcPattern pattern;
    const char *dialogue;
    const char *warning;
};
struct QuarantineNpc
{
    size_t profileIndex = 0;
    tutorial::Vector2 position;
    tutorial::Vector2 home;
    tutorial::Vector2 destination;
    std::vector<tutorial::Vector2> route;
    size_t routeIndex = 0;
    NpcActivity activity = NpcActivity::Working;
    float decisionCooldown = 0;
    float talkSeconds = 0;
    float serviceCooldown = 0;
    unsigned int routineStep = 0;
    bool following = false;
    int teamId = -1;
    int stationId = -1;
    int targetEnemy = -1;
    int health = 100;
    int maxHealth = 100;
    int kills = 0;
    float combatCooldown = 0;
    float actionFlash = 0;
    float walkPhase = 0;
    tutorial::Vector2 actionTarget = {};
    bool carryingSupplies = false;
};
const std::vector<QuarantineNpc> &GetNpcs();
const std::vector<Workstation> &GetWorkstations();
const NpcProfile &GetNpcProfile(size_t index);
const char *GetNpcActivityName(NpcActivity activity);
const char *GetWorkstationStatusName(WorkstationStatus status);
int FindNpcThreatTarget(tutorial::Vector2 position, int teamId);
void DamageNpc(size_t index, int damage);
void DamageWorkstation(size_t index, float damage);
void InitializeNpcs();
void ClearNpcs();
void UpdateNpcs(float deltaSeconds);
bool InteractWithNpc();
void DrawNpc(size_t index);
void DrawWorkstation(size_t index);
} // namespace levelone
