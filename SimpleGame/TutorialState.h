#pragma once
#include <string>
#include <vector>

namespace tutorial
{
struct Vector2
{
    float x, y;
};
struct Building
{
    float x, y, width, depth, height;
};

struct Resident
{
    Vector2 position;
    std::string name;
    std::string dialogue;
};

enum class MainQuestStage
{
    ReadHomeTerminal,
    MeetLia,
    ReadGateSignal,
    Complete
};

enum class InteractionTarget
{
    None,
    HomeTerminal,
    ApartmentExit,
    HomeEntrance,
    Lia,
    Mara,
    PowerRelay,
    GateBeacon,
    Resident
};

constexpr float kPi = 3.14159265f;
constexpr int kCanvasWidth = 1280;
constexpr int kCanvasHeight = 800;
constexpr Vector2 kHomeDoor = {-5, 2};
constexpr Vector2 kLiaPosition = {-3, 2};
constexpr Vector2 kMaraPosition = {0, 1};
constexpr Vector2 kRelayPosition = {3, 1};
constexpr Vector2 kBeaconPosition = {6, -3};
// Double both dimensions of the original 18 x 13 walkable bounds: 4x area.
constexpr float kWorldMinX = -18;
constexpr float kWorldMaxX = 18;
constexpr float kWorldMinY = -12.5f;
constexpr float kWorldMaxY = 13.5f;

// Internal state shared by tutorial modules. Renderers only read this state.
struct TutorialState
{
    int viewportWidth = kCanvasWidth;
    int viewportHeight = kCanvasHeight;
    int lastUpdateMilliseconds = 0;
    MainQuestStage mainQuestStage = MainQuestStage::ReadHomeTerminal;
    bool pressedKeys[256] = {};
    bool isInsideHome = true;
    bool isPaused = false;
    bool isComplete = false;
    bool isSideQuestAccepted = false;
    bool isPowerRestored = false;
    bool isSideQuestComplete = false;
    float playTimeSeconds = 0;
    float animationTimeSeconds = 0;
    float noticeSecondsRemaining = 0;
    float movementAmount = 0;
    int facingDirection = 0;
    Vector2 playerPosition = {0, 0};
    Vector2 cameraPosition = {0, 0};
    std::string noticeText;
    std::vector<Building> buildings;
    std::vector<Resident> residents;
};
extern TutorialState g_state;

float Clamp(float value, float minimum, float maximum);
float Distance(Vector2 first, Vector2 second);
void ShowNotice(const std::string &message);
bool IsPositionBlocked(Vector2 position);
InteractionTarget FindNearbyInteraction();
void InteractWithNearbyTarget();
} // namespace tutorial
