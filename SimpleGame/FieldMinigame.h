#pragma once
#include <array>
#include <functional>
#include <string>

namespace tutorial
{
enum class FieldMinigameKind
{
    CircuitBalance,
    SignalTuner,
    GlyphCipher,
    ExtractionTiming,
};

struct FieldMinigameOutcome
{
    bool succeeded = false;
    bool perfect = false;
    bool bypassed = false;
    int misses = 0;
};

struct FieldMinigameState
{
    bool active = false;
    FieldMinigameKind kind = FieldMinigameKind::CircuitBalance;
    std::string title;
    std::string subtitle;
    float elapsedSeconds = 0.f;
    float flashSeconds = 0.f;
    bool lastFlashGood = true;
    int misses = 0;
    int perfectHits = 0;

    // CircuitBalance: 3 coupled power rails (0..100 scale).
    std::array<float, 3> railValue{35.f, 65.f, 40.f};
    std::array<float, 3> railTarget{60.f, 50.f, 70.f};
    float railTolerance = 9.f;
    int selectedRail = 0;

    // SignalTuner: dual-waveform frequency (1.2..4.2) and phase (0..2*pi).
    float targetFreq = 2.4f;
    float targetPhase = 1.2f;
    float playerFreq = 1.6f;
    float playerPhase = 3.8f;
    float syncHoldSeconds = 0.f;

    // GlyphCipher: 3 concentric 8-notch rune rings (0..7, 0 == aligned with top keystone).
    std::array<int, 3> ringAngle{2, 5, 3};
    int selectedRing = 0;

    // ExtractionTiming: oscillating needle along 0..1 with cyan normal zone and gold critical core.
    float needlePos = 0.1f;
    float needleVelocity = 1.08f;
    float zoneCenter = 0.56f;
    float zoneHalfWidth = 0.14f;
    float perfectHalfWidth = 0.048f;
    int strikesDone = 0;
    int strikesNeeded = 2;
};

class FieldMinigame
{
  public:
    static void Begin(FieldMinigameKind kind, const std::string &title, const std::string &subtitle, unsigned seed,
                      std::function<void(const FieldMinigameOutcome &)> onComplete,
                      std::function<void()> onMiss = {}, int strikesNeeded = 2);
    static bool Active();
    static void Cancel();
    static void Reset();
    static void Update(float deltaSeconds);
    static bool HandleKey(unsigned char key);
    static bool HandleSpecialKey(int key);
    static bool HandlePointer(int button, bool down);
    static void Draw();
    static float SignalSyncRatio();
    static bool IsCircuitBalanced();
    static bool IsCipherAligned();
    static const FieldMinigameState &State();
    static void CompleteForTest(bool perfect = true);
};
} // namespace tutorial
