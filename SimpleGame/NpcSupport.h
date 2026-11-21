#pragma once
#include "ModelLibrary.h"
#include <cstdint>

namespace ai
{
enum class SupportTask
{
    None,
    Aid,
    Cover,
    Inspect,
    Supply
};
struct SupportState
{
    SupportTask task = SupportTask::None;
    std::uint64_t subjectId = 0;
    models::Point goal{}; // Current owner's local frame; reset at ownership changes and load.
    float progressSeconds = 0, retrySeconds = 0, stalledSeconds = 0;
    bool performing = false, blocked = false;
};
class Support
{
  public:
    static constexpr float kAidSeconds = 3, kInspectSeconds = 4, kSupplySeconds = 1;
    static constexpr float kSearchRange = 6, kPlayerLeash = 8, kDangerRange = 3;
    static constexpr float kAidDistance = .9f, kCoverDistance = 1.8f, kWorkDistance = .45f;
    static constexpr float kRetrySeconds = .8f, kMoveSpeed = 3;
    static constexpr float kFailureSeconds = 3, kFailureRetrySeconds = 2;
    static constexpr int kPathBudget = 2;
    static float Duration(SupportTask task);
    static bool Advance(SupportState &state, float dt, bool reached, bool safe);
    static bool Busy(const SupportState &state);
    static const char *TaskName(SupportTask task);
    static const char *TaskLabel(SupportTask task);
};
} // namespace ai
