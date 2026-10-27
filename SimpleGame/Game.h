#pragma once
#include "ModelLibrary.h"
#include "Weapon.h"
#include "Animation.h"
#include <string>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace tutorial
{
struct Vector2
{
    float x, y;
};
// ============================================================================
// WorldCamera — world view magnification; HUD/cursor stay in viewport coordinates
// ============================================================================
struct WorldCamera
{
    static constexpr float zoom = 1.35f;
    static Vector2 ToViewport(Vector2 point);
    static Vector2 FromViewport(Vector2 point);
};
// ============================================================================
// MovementFacing — persistent heading from actual movement, including route turns
// ============================================================================
struct MovementFacing
{
    float yaw = .78539816f;
    bool walking = false;
    void Update(Vector2 from, Vector2 to);
};
struct Building
{
    float x, y, width, depth, height;
    models::BuildingType type = models::BuildingType::Habitat;
    models::Palette palette = {};
};

enum class ResidentProfession
{
    Courier,
    Worker,
    Engineer,
    Guard,
    Student,
    Merchant,
    Citizen,
    Observer,
    Technician,
    Explorer
};
enum class ResidentActivity
{
    Resting,
    Walking,
    Patrolling,
    WaitingSignal,
    Crossing,
    Working,
    Talking,
    Yielding,
    NoRoute,
    Fleeing,
    Injured,
    Dead
};
// Physics owns displacement; future reaction clips consume these states without root motion.
enum class BodyResponse
{
    None,
    Stagger,
    Knockdown,
    Dead
};
struct BodyImpact
{
    Vector2 velocity = {};
    float seconds = 0, invulnerability = 0;
    BodyResponse response = BodyResponse::None;
};
struct ResidentWaypoint
{
    Vector2 position;
    int crossing = -1; // The edge into this waypoint is an exclusive signal-controlled crossing.
};
struct Resident
{
    Vector2 position;
    Vector2 home = {}, destination = {};
    std::string name;
    std::string dialogue;
    models::CharacterType type = models::CharacterType::Explorer;
    std::array<float, 4> coat = {.55f, .65f, .75f, 1};
    MovementFacing facing;
    models::Gender gender = models::Gender::Male;
    models::HairStyle hair = models::HairStyle::SidePart;
    int cityIndex = 0;
    ResidentProfession profession = ResidentProfession::Citizen;
    ResidentActivity activity = ResidentActivity::Resting;
    std::vector<ResidentWaypoint> route;
    size_t routeCursor = 0;
    int crossingJunction = -1, routineStep = 0;
    float speed = 1.2f, waitSeconds = 0, conversationSeconds = 0, blockedSeconds = 0;
    unsigned int randomState = 1;
    bool destinationIsWork = false;
    int health = 100;
    float fearSeconds = 0, corpseSeconds = 0;
    BodyImpact impact;
};

enum class MainQuestStage
{
    ReadHomeTerminal,
    MeetLia,
    EnterObservatory,
    ReadGateSignal,
    DescendObservatory,
    MountJetBike,
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
    Resident,
    ObservatorySignal,
    ObservatoryExit,
    JetBike,
    CityReceiver,
    Kain,
    MineEntrance
};

constexpr float kPi = 3.14159265f;
constexpr int kCanvasWidth = 1280;
constexpr int kCanvasHeight = 800;
constexpr Vector2 kHomeBuildingPosition = {-9, -7};
constexpr Vector2 kObservatoryBuildingPosition = {10, -9};
constexpr Vector2 kHomeDoor = {-7, -3.5f};
constexpr Vector2 kLiaPosition = {-4, 0};
constexpr Vector2 kCityKainPosition = {-1, .5f};
constexpr Vector2 kMaraPosition = {0, 3};
constexpr Vector2 kRelayPosition = {8, 3};
constexpr Vector2 kBeaconPosition = {12, -3.5f};
constexpr Vector2 kObservatoryLiaPosition = {-1.5f, -1};
constexpr Vector2 kJetBikePosition = {-4.5f, -6.5f};
constexpr float kJetBikeYaw = -kPi * .5f;
constexpr Vector2 kBikeRoadExit = {-4.5f, -17};
constexpr Vector2 kCityReceiverPosition = {1, -3};
constexpr Vector2 kObservatoryConsole = {2, -2};
constexpr Vector2 kObservatoryStairs = {-2.8f, 2.8f};
constexpr float kWorldMinX = -440;
constexpr float kWorldMaxX = 440;
constexpr float kWorldMinY = -440;
constexpr float kWorldMaxY = 440;

// Each playable area keeps its own destination in its own world coordinates.
enum class MapRegion
{
    City,
    Mine,
    Frontier,
    Count
};
struct MapDestination
{
    bool active = false;
    Vector2 position = {};
};

// ============================================================================
// Dialogue — stable scene/line identities, readable pages and session checkpoints
// ============================================================================
struct DialogueChoice
{
    std::string id, text;
};
struct DialogueLine
{
    std::string id, speaker, channel, text;
    std::vector<DialogueChoice> choices;
};
enum class DialoguePhase
{
    NotStarted,
    InProgress,
    Completed
};
struct DialogueRecord
{
    DialoguePhase phase = DialoguePhase::NotStarted;
    size_t nextPage = 0;
    std::string choice, pendingChoice;
};
struct QueuedDialogue
{
    std::string id;
    std::vector<DialogueLine> lines;
    std::function<void(const std::string &)> completion;
    bool repeatable = false;
};
struct DialogueState
{
    bool active = false, blocking = false, suspended = false, repeatable = false;
    std::string sceneId;
    std::vector<DialogueLine> pages, history;
    std::vector<QueuedDialogue> pending;
    size_t page = 0, historyOffset = 0;
    int selection = -1;
    std::map<std::string, DialogueRecord> scenes;
    std::set<std::string> events;
    std::function<void(const std::string &)> completion;
};
class Dialogue
{
  public:
    static bool Start(const std::string &sceneId, std::vector<DialogueLine> lines,
                      std::function<void(const std::string &)> completion = {}, bool repeatable = false);
    static void Say(const std::string &sceneId, const std::string &message);
    static bool Advance();
    static bool Select(size_t index);
    static void Interrupt();
    static bool Resume();
    static bool Active();
    static bool LocksWorld();
    static bool Danger();
    static std::vector<std::string> Wrap(const std::string &text, float width, int fontSize = 19);
    static std::vector<std::string> Journal();
};

enum class UiScreen
{
    None,
    Title,
    Pause,
    Stages,
    Options,
    Summary,
    Help,
    Supply,
    Confirm,
    Medical
};
enum class UiAction
{
    Continue,
    Start,
    Stages,
    EnterStage,
    Options,
    Summary,
    Help,
    Restart,
    Quit,
    Back,
    Background,
    Border,
    Content,
    Cartoon,
    Resolution,
    Antialiasing,
    Exchange,
    Choice,
    Advance,
    Journal,
    History,
    Previous,
    Next
};
struct UiPreferences
{
    float background = .94f, border = 1, content = 1;
};
struct UiState
{
    UiScreen screen = UiScreen::None, returnTo = UiScreen::Pause;
    UiAction confirmation = UiAction::Restart;
    int focus = 0, pressed = -1;
};
enum class TestStage
{
    Prologue,
    Observatory,
    BikeUnlocked,
    WeaponUnlocked,
    Mine,
    ChapterOne,
    Count
};
struct WeaponWheelState
{
    bool visible = false, qHeld = false;
    Vector2 origin = {}, offset = {};
    combat::WeaponSlot hovered = combat::WeaponSlot::Unarmed;
};
// ============================================================================
// WeaponWheel — hold/release selection, independent of the equipped weapon.
// ============================================================================
class WeaponWheel
{
  public:
    static bool Open();
    static void Motion();
    static void Close(bool commit = false);
    static void RefreshCursorCapture();
};
// State owned by Game. Renderers read it without changing gameplay.
struct GameState
{
    int viewportWidth = kCanvasWidth;
    int viewportHeight = kCanvasHeight;
    int lastUpdateMilliseconds = 0;
    MainQuestStage mainQuestStage = MainQuestStage::ReadHomeTerminal;
    bool pressedKeys[256] = {};
    Vector2 cursorPosition = {900, 400};
    Vector2 cursorWindowPosition = {900, 400};
    bool cursorInsideViewport = true;
    bool mouseFiring = false;
    bool mouseAiming = false;
    models::CharacterAnimation playerAnimation;
    bool interactionHeld = false;
    bool dialogueEnterHeld = false;
    bool isInsideHome = true;
    bool isInObservatory = false;
    bool isBikeMounted = false;
    float bikeSpeed = 0;
    float bikeElevation = 0;
    float bikeYaw = kJetBikeYaw;
    Vector2 bikePosition = kJetBikePosition;
    bool bikeSafePoseValid = false;
    Vector2 bikeSafePosition = kJetBikePosition;
    float bikeSafeYaw = kJetBikeYaw, bikeSafeElevation = 0;
    Vector2 mineReturnPosition = {128, 18};
    bool gateSignalHeard = false;
    bool hasDescendedObservatory = false;
    bool isPaused = false;
    bool mapVisible = false;
    bool journalVisible = false, dialogueHistoryVisible = false;
    DialogueState dialogue;
    UiPreferences uiPreferences;
    UiState ui;
    WeaponWheelState weaponWheel;
    std::array<MapDestination, size_t(MapRegion::Count)> mapDestinations = {};
    bool isComplete = false;
    bool isSideQuestAccepted = false;
    bool isPowerRestored = false;
    bool isSideQuestComplete = false;
    float playTimeSeconds = 0;
    float animationTimeSeconds = 0;
    float noticeSecondsRemaining = 0;
    float movementAmount = 0;
    float movementSpeed = 0;
    int facingDirection = 0;
    float facingYaw = .78539816f;
    Vector2 playerPosition = {0, 0};
    models::Gender playerGender = models::Gender::Male;
    models::HairStyle playerHair = models::HairStyle::SidePart;
    Vector2 cameraPosition = {0, 0};
    std::string noticeText;
    std::vector<Building> buildings;
    std::vector<Resident> residents;
};
// ============================================================================
// CityLayout — road footprints and reserved quest/parking access
// ============================================================================
class CityLayout
{
  public:
    // Ground extends beyond the outdoor movement limits to cover viewport corners at the boundary.
    static constexpr int kTerrainMinX = int(kWorldMinX) - 32, kTerrainMaxX = int(kWorldMaxX) + 32;
    static constexpr int kTerrainMinY = int(kWorldMinY) - 32, kTerrainMaxY = int(kWorldMaxY) + 32;
    static bool IsRoadTile(int x, int y);
    static bool OverlapsRoad(const Building &building, float clearance = .3f);
    static constexpr Vector2 kPlazaMin = {-10, -4.5f}, kPlazaMax = {14.5f, 6.5f};
    static constexpr Vector2 kBikeAccessMin = {-5.5f, -18.8f}, kBikeAccessMax = {-3.4f, -4.9f};
    static bool IsQuestSpace(Vector2 position, float clearance = 0);
    static bool OverlapsQuestSpace(const Building &building, float clearance = .2f);
    static void ValidateBuildings(std::vector<Building> &buildings);
};

// ============================================================================
// Game — state, input, city progression
// ============================================================================

class Game
{
  public:
    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;
    static Game &Get();
    GameState &GetState();
    void Reset();
    bool EnterTestStage(TestStage stage);
    static const char *StageName(TestStage stage);
    void Resize(int width, int height);
    void KeyDown(unsigned char key, int x, int y);
    void KeyUp(unsigned char key, int x, int y);
    void SpecialKeyDown(int key, int x, int y);
    void MouseMotion(int x, int y);
    void MouseButton(int button, int state, int x, int y);
    void MouseEntry(int state);
    // Deterministic simulation step, also used by the window timer.
    void Update(float deltaSeconds);
    bool IsAiming() const;
    models::CharacterPose PlayerPose(float recoil = 0) const;
    void Tick(int timer);
    static float Clamp(float value, float minimum, float maximum);
    static float Distance(Vector2 first, Vector2 second);
    void ShowNotice(const std::string &message);
    bool IsPositionBlocked(Vector2 position);
    InteractionTarget FindNearbyInteraction();
    bool IsQuestInteraction(InteractionTarget target) const;
    void InteractWithNearbyTarget();

  private:
    Game() = default;
    GameState state;
    static unsigned char NormalizeKey(unsigned char key);
    void UpdatePlayerMovement(float deltaSeconds);
    void UpdateCamera(float deltaSeconds);
};
// ============================================================================
// GameCallbacks - GLUT adapters
// ============================================================================
class GameCallbacks
{
  public:
    static void Draw();
    static void Shutdown();
    static void Resize(int width, int height);
    static void KeyDown(unsigned char key, int x, int y);
    static void KeyUp(unsigned char key, int x, int y);
    static void SpecialKeyDown(int key, int x, int y);
    static void MouseMotion(int x, int y);
    static void MouseButton(int button, int state, int x, int y);
    static void MouseEntry(int state);
    static void Tick(int timer);
};

} // namespace tutorial
