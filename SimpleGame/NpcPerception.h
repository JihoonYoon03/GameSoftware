#pragma once
#include "ModelLibrary.h"

namespace ai
{
enum class Stimulus
{
    None,
    Sight,
    Sound,
    Report
};
enum class Role
{
    Civilian,
    Watcher,
    Predator,
    Officer
};
enum class Decision
{
    Routine,
    Withdraw,
    Observe,
    Investigate,
    Engage
};
struct PerceptionSettings
{
    static constexpr float kCivilianMemorySeconds = 6, kWatcherMemorySeconds = 4;
    static constexpr float kCombatMemorySeconds = 4, kOfficerMemorySeconds = 6, kReportMemorySeconds = 8;
    static constexpr float kScanSeconds = .25f, kScanSpreadSeconds = .025f, kScanPhaseSeconds = .04f;
    static constexpr float kSoundRange = 12, kEnemySightRange = 10, kEnemyNearRange = 2;
    static constexpr float kSearchRetrySeconds = .8f;
    static constexpr int kEnemySearchBudget = 2;
};
struct Vision
{
    float range = 10, halfAngle = 1.04719755f, nearRange = 1.2f, heightTolerance = 2;
};
// Actor-local transient timers. The owner holds its last known position in its own coordinate frame.
class ThreatMemory
{
  public:
    void Advance(float dt);
    void Observe(bool visible, float duration);
    void Remember(Stimulus source, float duration);
    void Forget();
    bool Known() const;
    bool Visible() const;
    bool Initialized() const;
    float Remaining() const;
    Stimulus Source() const;
    const char *StateName() const;

  private:
    float remaining = 0;
    bool visible = false, initialized = false;
    Stimulus source = Stimulus::None;
};
class Perception
{
  public:
    // Geometry only. Callers must also check their scene's line of sight before Observe(true).
    static bool InSight(models::Point origin, float yaw, models::Point target, Vision vision);
    static Decision Decide(Role role, const ThreatMemory &memory, float distance);
    static const char *DecisionName(Decision decision);
    static const char *StimulusName(Stimulus stimulus);
};
} // namespace ai
