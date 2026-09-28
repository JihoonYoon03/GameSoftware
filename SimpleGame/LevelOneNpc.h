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
    Talking
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
};
const std::vector<QuarantineNpc> &GetNpcs();
const NpcProfile &GetNpcProfile(size_t index);
const char *GetNpcActivityName(NpcActivity activity);
void InitializeNpcs();
void ClearNpcs();
void UpdateNpcs(float deltaSeconds);
bool InteractWithNpc();
void DrawNpc(size_t index);
} // namespace levelone
