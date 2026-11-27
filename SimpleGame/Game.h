#pragma once
#include "ModelLibrary.h"
#include "Weapon.h"
#include "Animation.h"
#include "PlayerMobility.h"
#include "PlayerAircraft.h"
#include "NpcPerception.h"
#include "World.h"
#include "WorldStreaming.h"
#include <array>
#include <cstdint>
#include <string>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace game
{
struct PlayerStats;
struct MineState;
struct ChapterOneState;
} // namespace game

namespace tutorial
{
class SaveArchive;
class SaveSystem;
class PlanetCities;
class PlanetTraffic;
class PlanetParty;
class PlanetCommunity;
struct CommunitySite;
struct PlanetCommunityState;
struct GameState;
enum class CompanionId : std::uint8_t;
enum class CompanionPlace : std::uint8_t;
struct Vector2
{
    float x, y;
};
// ============================================================================
// MotionMath / PlayerMotion — bounded acceleration, braking and heading changes
// ============================================================================
class MotionMath
{
  public:
    static constexpr float kResponse = 1.3f;
    static Vector2 Approach(Vector2 velocity, Vector2 target, float maximumChange);
    static float Turn(float yaw, float target, float maximumChange);
    static Vector2 ContactVelocity(Vector2 velocity, Vector2 requested, Vector2 actual);
};
struct PlayerMotion
{
    PlayerMobility mobility;
    Vector2 velocity = {};
    float forwardLean = 0, sideLean = 0;
    bool pivoting = false;
    void Update(Vector2 target, float &yaw, bool aiming, float deltaSeconds);
    void Stop();
};
// ============================================================================
// WorldCamera — world view magnification; HUD/cursor stay in viewport coordinates
// ============================================================================
struct WorldCamera
{
    static constexpr float kDefaultZoom = 1.35f;
    static constexpr float kMinZoom = 0.70f;
    static constexpr float kMaxZoom = 4.75f; // ~1/3 screen height for 1.80 m character, guarantees LOD0 detail.
    static float zoom;
    static float targetZoom;
    static float topViewBlend; // 0 = isometric quarter-view, 1 = smooth top-down aircraft camera.
    static float IsoYScale();
    static float IsoZScale();
    static float LogicalWidth();
    static float CenterOffsetX();
    static float RightOffsetX();
    static void StepZoom(int direction);
    static void SetZoom(float value, bool immediate = false);
    static void Update(float deltaSeconds, bool aircraftMounted);
    static void Reset();
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
    bool following = false;
    float motionSpeed = 0, motionPhase = 0, lastMotionSeconds = -1000;
    void Update(Vector2 from, Vector2 to, float deltaSeconds = 1.f / 60);
    bool Follow(float distance, float stopDistance, bool visible = true);
    void ApplyMotion(models::CharacterPose &pose) const;
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
    ai::ThreatMemory threat;
    Vector2 lastThreat = {};
    ai::Decision decision = ai::Decision::Routine;
    float perceptionCooldown = 0;
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
enum class MissionId
{
    Signal,
    Mine,
    Frontier,
    Mara,
    Count
};
enum class MissionStatus
{
    NotAccepted,
    Active,
    Paused,
    Completed
};
// ============================================================================
// QuestParticipation — acceptance is separate from physical discoveries and persistent world actions
// ============================================================================
class QuestParticipation
{
    friend class SaveArchive;

  public:
    MissionStatus Status(MissionId id) const
    {
        return states.at(size_t(id));
    }
    bool Active(MissionId id) const
    {
        return Status(id) == MissionStatus::Active;
    }
    bool Accept(MissionId id, bool eligible);
    bool Pause(MissionId id);
    bool Complete(MissionId id);
    bool Track(MissionId id);
    MissionId Tracked() const;
    void ClearTracking()
    {
        tracked = MissionId::Count;
    }
    static const char *Name(MissionId id);
    static const char *Key(MissionId id);
    static const char *Label(MissionStatus status);

  private:
    std::array<MissionStatus, size_t(MissionId::Count)> states = {};
    // Participation stays in the base archive; the tracking extension preserves this preference.
    MissionId tracked = MissionId::Count;
};

// ============================================================================
// StoryMissionProgress — existing story conditions and explicit completion, independent of UI/session
// ============================================================================
class StoryMissionProgress
{
  public:
    static const char *Restriction(MissionId id, const GameState &state, const game::MineState &mine,
                                   bool storyPlanet);
    static bool CanComplete(MissionId id, const GameState &state, const game::MineState &mine,
                            const game::ChapterOneState &chapter, bool storyPlanet);
    static bool Complete(MissionId id, GameState &state, game::MineState &mine,
                         game::ChapterOneState &chapter, bool storyPlanet);
};

// ============================================================================
// ActivityJournal — selection, read-only objectives and persistent activity references
// ============================================================================
struct ActivityObjective
{
    MissionId mission = MissionId::Count;
    std::string communityRegion, title, action, location;
    std::optional<world::SurfacePoint> surfaceTarget;
    std::optional<Vector2> mineTarget;
};
struct ActivityTrackingState
{
    MissionId mission = MissionId::Count;
    std::string communityWorldKey, communityRegion;
};
class ActivityJournal
{
  public:
    static constexpr int LocalEntriesPerPage = 3;
    static ActivityObjective Current();
    static ActivityTrackingState CaptureTracking(const GameState &state,
                                                 const PlanetCommunityState &community,
                                                 const world::PlanetDefinition &planet);
    static void RestoreTracking(GameState &state, const ActivityTrackingState &tracking,
                                const PlanetCommunityState &community, const world::PlanetDefinition &planet);
    static bool TrackMission(MissionId id);
    static bool TrackCommunity(const std::string &regionId);
    static const CommunitySite *TrackedCommunity();
    static std::vector<size_t> CommunityEntries();
    static void Refresh();
    static std::optional<world::SurfacePoint> MissionGiverTarget(MissionId id);
    static std::optional<world::SurfacePoint> GuidanceTarget(std::string *labelOut = nullptr);
    static bool TriggerGuidance(std::optional<world::SurfacePoint> overrideTarget = std::nullopt,
                                const std::string &label = {});
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
    MineEntrance,
    FrontierSite,
    Relay,
    Community,
    GroundVehicle,
    Aircraft,
    HomeBed,
    HomeWorkbench,
    HomeStorage,
    FacilityEntrance,
    FacilityExit,
    FacilityNpc,
    FacilityServicePrimary,
    FacilityServiceSecondary,
    StoryOperationNode,
    StoryOperationPortal,
    StorySafetyLine,
    StoryShelterLamp,
    StoryEpilogueClinic,
    StoryEpilogueWorkshop,
    WorldEncounter
};

constexpr float kPi = 3.14159265f;
constexpr float kMovementSpeed = 3.2f;
constexpr int kCanvasWidth = 1280;
constexpr int kCanvasHeight = 800;
constexpr Vector2 kHomeBuildingPosition = {-9, -7};
constexpr Vector2 kObservatoryBuildingPosition = {10, -9};
constexpr Vector2 kHomeDoor = {-7, -3.5f};
constexpr Vector2 kHomeBedInteraction = {-1.35f, -1.65f};
constexpr Vector2 kHomeWorkbenchInteraction = {-1.15f, 1.4f};
constexpr Vector2 kHomeStorageInteraction = {1.65f, -.15f};
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
                      std::function<void(const std::string &)> completion = {}, bool repeatable = false,
                      bool blocking = true);
    static void Say(const std::string &sceneId, const std::string &message, bool blocking = true);
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
    Medical,
    Checkpoint,
    PlanetProfiles,
    FacilityShop,
    Wardrobe,
    HomeStorage
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
    DisplayResolution,
    DisplayFullscreen,
    FacilityService,
    OpenWardrobe,
    OpenHomeStorage,
    WardrobeGender,
    WardrobeOutfit,
    WardrobeHair,
    WardrobeCoatColor,
    WardrobeModularMode,
    WardrobeModularPart,
    WardrobeRotate,
    WardrobeAutoRotate,
    StorageAction,
    Exchange,
    Choice,
    Advance,
    Journal,
    History,
    Previous,
    Next,
    Mission,
    Checkpoint,
    SaveWorld,
    LoadWorld,
    CancelLoad,
    Profiles,
    SelectProfile,
    TrackMission,
    ActivityTab,
    ActivityPage,
    TrackCommunity,
    ViewActivity,
    SelectActivity,
    ActivityDetailPage
};
struct UiPreferences
{
    float background = .94f, border = 1, content = 1;
};
struct DisplayPreferences
{
    int resolutionIndex = 0; // 0:1280x800, 1:1600x900, 2:1920x1080, 3:2560x1440, 4:3840x2160
    bool fullscreen = false;
};
struct FacilityServiceItem
{
    std::string title;
    std::string subtitle;
    std::string priceLabel;
    bool enabled = true;
    std::string icon = "supply";
    std::string category = "보급";
    int propKind = 0; // 0:none, 1:recoveryKit, 2:salvage, 3:box, 4:vehicle
};
struct UiState
{
    UiScreen screen = UiScreen::None, returnTo = UiScreen::Pause;
    UiAction confirmation = UiAction::Restart;
    int focus = 0, pressed = -1;
    MissionId activityMission = MissionId::Count;
    std::string activityRegion;
    int activityDetailPage = 0;
    float wardrobeYawOffset = 0.f;
    bool wardrobeAutoRotate = true;
    bool wardrobeDragging = false;
    float wardrobeDragStartX = 0.f;
    float wardrobeDragStartYaw = 0.f;
};
enum class TestStage
{
    Prologue,
    Observatory,
    BikeUnlocked,
    WeaponUnlocked,
    Mine,
    ChapterOne,
    ActTwoWaterway,
    ActThreeCrisis,
    ActFourGateCore,
    EpilogueRecovery,
    SurfaceSurvey,
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
struct GroundVehicle
{
    static constexpr int MaxHealth = 260;
    std::optional<world::SurfacePoint> position;
    world::Vector3d heading{};
    int health = MaxHealth;
    float wreckSeconds = 0;
    bool mounted = false;
    Vector2 velocity{};
    float speed = 0;

    void SetYaw(float yaw, const world::TangentFrame &origin);
    float Yaw(const world::TangentFrame &frame) const;
    bool Contains(const world::SurfaceSession &session, const world::SurfacePoint &point, float padding,
                  bool projectile = false) const;
};

struct ResidenceState
{
    static constexpr int kMaxUpgradeTier = 3;
    static constexpr int kMaxCarryKits = 8;
    static constexpr int kMaxStashKits = 12;
    static constexpr int kMaxStashSalvage = 999;
    static constexpr int kMaxStashCredits = 9999999;
    static constexpr int kCreditTransferStep = 200;
    int stashCredits = 0;
    int stashRecoveryKits = 0;
    int stashSalvage = 0;
    int armorTier = 0;
    int weaponTier = 0;
    int restCount = 0;
    float lastRestPlayTime = -1000.f;
    static int ArmorSalvageCost(int tier)
    {
        return 2 + tier;
    }
    static int ArmorCreditCost(int tier)
    {
        return 120 + tier * 80;
    }
    static int WeaponSalvageCost(int tier)
    {
        return 2 + tier;
    }
    static int WeaponCreditCost(int tier)
    {
        return 140 + tier * 100;
    }
};

// CQ-04 C2: Walkable urban facility interiors across all 5 legacy cities and procedural settlements.
enum class UrbanFacilityKind : std::uint8_t
{
    None = 0,
    Market = 1,
    Clinic = 2,
    Workshop = 3,
    CivicHall = 4
};
struct ActiveFacilityInterior
{
    UrbanFacilityKind kind = UrbanFacilityKind::None;
    std::string regionId, regionName, facilityId, label;
    world::SurfacePoint returnPoint{};
    float returnYaw = 0;
    bool Active() const
    {
        return kind != UrbanFacilityKind::None;
    }
    void Clear()
    {
        *this = {};
    }
};

// State owned by Game. Renderers read it without changing gameplay.
struct GameState
{
    ResidenceState residence;
    ActiveFacilityInterior facilityInterior;
    ActiveFacilityInterior nearbyFacilityEntrance;
    bool IsInsideAny() const
    {
        return isInsideHome || isInObservatory || facilityInterior.Active();
    }
    world::RelayNetwork relayNetwork;
    std::string selectedRelay, relaySource, relayTarget, relayStatus;
    float relayElapsed = 0;
    bool relayPreparing = false, mapRegisteredRelaysOnly = false, planetMapSurface = false;
    int relayPage = 0;
    bool surfaceExploration = false;
    float surfaceCameraHeight = 0;
    int viewportWidth = kCanvasWidth;
    int viewportHeight = kCanvasHeight;
    int lastUpdateMilliseconds = 0;
    MainQuestStage mainQuestStage = MainQuestStage::ReadHomeTerminal;
    QuestParticipation missions;
    bool homeMessageRead = false;
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
    GroundVehicle car;
    PlayerAircraft aircraft;
    bool Mounted() const
    {
        return isBikeMounted || car.mounted || aircraft.mounted;
    }
    float bikeSpeed = 0;
    Vector2 bikeVelocity = {};
    float bikeYawSpeed = 0;
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
    struct MapViewState
    {
        float zoom = 1;
        Vector2 pan = {};
    };
    std::array<MapViewState, size_t(MapRegion::Count)> mapViews = {};
    float planetMapRange = 1000; // Radius in metres, independent of the streamed render radius.
    Vector2 planetMapPan = {};
    std::optional<world::SurfacePoint> planetDestination;
    bool journalVisible = false, dialogueHistoryVisible = false;
    // Open panels/pages are transient; activity selection is stored in a separate archive extension.
    bool journalLocalActivities = false;
    int journalActivityPage = 0;
    std::string trackedCommunityRegion, trackedCommunityWorldKey;
    DialogueState dialogue;
    UiPreferences uiPreferences;
    DisplayPreferences displayPreferences;
    UiState ui;
    WeaponWheelState weaponWheel;
    std::array<MapDestination, size_t(MapRegion::Count)> mapDestinations = {};
    bool isComplete = false;
    bool isSideQuestAccepted = false;
    bool isPowerRestored = false;
    bool isSideQuestComplete = false;
    float playTimeSeconds = 0;
    float animationTimeSeconds = 0;
    float facilityReactionSeconds = 0;
    float noticeSecondsRemaining = 0;
    float noticeDurationSeconds = 0;
    float guidanceSecondsRemaining = 0;
    float guidanceDurationSeconds = 8.f;
    std::optional<world::SurfacePoint> guidanceOverrideTarget;
    std::string guidanceLabel;
    float movementAmount = 0;
    float movementSpeed = 0;
    PlayerMotion playerMotion;
    int facingDirection = 0;
    float facingYaw = .78539816f;
    Vector2 playerPosition = {0, 0};
    models::Gender playerGender = models::Gender::Male;
    models::HairStyle playerHair = models::HairStyle::SidePart;
    models::CharacterType playerOutfit = models::CharacterType::Explorer;
    int playerCoatColor = 0;
    bool playerModularMedic = false;
    unsigned playerModularPartsMask = 0x7FFu; // Bits 0..10 toggle the 11 swappable Field Medic modular parts.
    Vector2 cameraPosition = {0, 0};
    std::string noticeText;
    std::vector<Building> buildings;
    std::vector<Resident> residents;
    void StopMotion();
    void InterruptDodge();
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

// Absolute 60 Hz deadlines exclude work already spent in the update callback.
// Late frames are rebased, never replayed as an unbounded catch-up loop.
class FrameSchedule
{
  public:
    unsigned int NextDelay(double startedMilliseconds, double finishedMilliseconds);

  private:
    double nextMilliseconds = -1;
};

enum class WorldMode : std::uint8_t
{
    LegacyInterior,
    LegacyObservatory,
    LegacyCity,
    LegacyMine,
    LegacyFrontier,
    SurfaceInterior,
    SurfaceObservatory,
    SurfaceFacility,
    SurfaceExterior,
    SurfaceMine,
    StoryOperation
};

class Game
{
  public:
    ~Game();
    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;
    static Game &Get();
    GameState &GetState();
    WorldMode Mode() const;
    bool IsSurfaceMode() const;
    bool IsInteriorMode() const;
    bool IsExteriorMode() const;
    game::PlayerStats &Player();
    const game::PlayerStats &Player() const;
    combat::PlayerLoadout &Loadout();
    const combat::PlayerLoadout &Loadout() const;
    combat::Pistol &Sidearm();
    const combat::Pistol &Sidearm() const;
    combat::PulseCarbine &PrimaryWeapon();
    const combat::PulseCarbine &PrimaryWeapon() const;
    combat::Firearm &ActiveFirearm();
    const combat::Firearm &ActiveFirearm() const;
    // Migrated city interiors and the physical mine report their actual planet/space/layer.
    std::optional<world::WorldLocation> PlanetLocation() const;
    const world::SurfaceSession *Surface() const
    {
        return surface.get();
    }
    std::optional<world::SurfacePoint> SurfaceBike() const
    {
        return surfaceBike;
    }
    world::SurfaceView SurfaceTerrainView() const;
    void StopSurface();
    PlanetCities *SurfaceCities() const
    {
        return surfaceCities.get();
    }
    PlanetTraffic *SurfaceTraffic() const
    {
        return surfaceTraffic.get();
    }
    PlanetParty *SurfaceParty() const
    {
        return surfaceParty.get();
    }
    PlanetCommunity *SurfaceCommunity() const
    {
        return surfaceCommunity.get();
    }
    bool HasCompanion(CompanionId id, CompanionPlace place) const;
    std::optional<Vector2> ResidentPosition(const Resident &resident) const;
    bool RelocateSurfaceSurvey(const world::SurfacePoint &point);
    bool ReturnToSurface(Vector2 legacyDoor, int city = 0);
    bool IsSurfaceMine() const
    {
        return surfaceMineReturn.has_value();
    }
    bool IsStoryWorld() const
    {
        return !state.surfaceExploration ||
               (surface && surface->Planet().SameWorld(world::WorldCatalog::Erma()));
    }
    bool IsGroundVehicleUnlocked() const;
    bool IsAircraftUnlocked() const;
    bool EnterSurfaceMine();
    bool LeaveSurfaceMine();
    // CQ-04 C2: Urban facility interior entry, exit, and station services across all settlements.
    static UrbanFacilityKind ClassifyUrbanFacility(const world::SurfaceStructure &structure);
    static UrbanFacilityKind EnterableUrbanFacility(const world::SurfaceStructure &structure,
                                                    const world::GeneratedPlanet *planet);
    static const char *FacilityKindName(UrbanFacilityKind kind);
    static const char *FacilityShortLabel(UrbanFacilityKind kind);
    static world::SurfacePoint FacilityDoorPoint(const world::SurfaceStructure &structure, double planetRadius);
    bool EnterFacilityInterior(const ActiveFacilityInterior &entry);
    bool ExitFacilityInterior();
    bool InteractFacilityStation(InteractionTarget target);
    std::vector<FacilityServiceItem> FacilityServices() const;
    bool ExecuteFacilityService(int serviceIndex);
    void ApplyDisplayPreferences(bool persist = true);
    void LoadDisplayPreferences();
    bool RecoverSurfaceAt(int city, Vector2 legacyPosition);
    void Reset();
    void StartNewGame();
    bool MissionEligible(MissionId id) const;
    const char *MissionRestriction(MissionId id) const;
    bool CanCompleteMission(MissionId id) const;
    bool CompleteMission(MissionId id);
    MissionId FocusedMission() const;
    const char *MissionObjective() const;
    const char *MissionObjective(MissionId mission) const;
    void OfferMission(MissionId id, const char *channel = "제안 기록");
    void ManageMission(MissionId id);
    void SyncMissionProgress();
    bool EnterTestStage(TestStage stage);
    static const char *StageName(TestStage stage);
    void Resize(int width, int height);
    void KeyDown(unsigned char key, int x, int y);
    void KeyUp(unsigned char key, int x, int y);
    void SpecialKeyDown(int key, int x, int y);
    void MouseMotion(int x, int y);
    void MouseButton(int button, int state, int x, int y);
    void MouseWheel(int steps, int x, int y);
    void MouseEntry(int state);
    // Deterministic simulation step, also used by the window timer.
    void Update(float deltaSeconds);
    bool IsAiming() const;
    bool HasWalkingControls() const;
    models::CharacterPose PlayerPose(float recoil = 0) const;
    void Tick(int timer);
    unsigned int NextFrameDelay(double startedMilliseconds, double finishedMilliseconds);
    static float Clamp(float value, float minimum, float maximum);
    static float Distance(Vector2 first, Vector2 second);
    void ShowNotice(const std::string &message);
    bool IsPositionBlocked(Vector2 position);
    InteractionTarget FindNearbyInteraction();
    bool IsQuestInteraction(InteractionTarget target) const;
    void InteractWithNearbyTarget();

    bool ResidenceRest(bool saveCheckpoint);
    bool ResidenceCraftKit();
    bool ResidenceUpgradeArmor();
    bool ResidenceUpgradeWeapon();
    bool ResidenceRepairVehicles();
    bool ResidenceRecallVehicles();
    bool ResidenceDepositCredits(int amount = ResidenceState::kCreditTransferStep);
    bool ResidenceWithdrawCredits(int amount = ResidenceState::kCreditTransferStep);
    bool ResidenceDepositKit();
    bool ResidenceWithdrawKit();
    bool ResidenceDepositSalvage();
    bool ResidenceWithdrawSalvage();
    void ResidenceCycleGender();
    void ResidenceCycleHair();

  private:
    Game();
    GameState state;
    std::unique_ptr<world::SurfaceSession> surface;
    std::unique_ptr<PlanetCities> surfaceCities;
    std::unique_ptr<PlanetTraffic> surfaceTraffic;
    std::unique_ptr<PlanetParty> surfaceParty;
    std::unique_ptr<PlanetCommunity> surfaceCommunity;
    std::optional<world::SurfacePoint> surfaceBike;
    std::optional<world::Vector3d> surfaceBikeHeading;
    std::optional<world::SurfacePoint> surfaceMineReturn;
    std::optional<std::size_t> surfaceSurveyRegion;
    struct TerrainViewCache
    {
        const world::SurfaceSession *session = nullptr;
        std::uint64_t epoch = 0, revision = 0;
        double originX = 0, originY = 0, originZ = 0;
        Vector2 camera = {};
        float camHeight = 0, zoom = 0, topView = 0;
        world::SurfaceView result;
        bool valid = false;
    };
    mutable TerrainViewCache terrainViewCache;
    FrameSchedule frameSchedule;
    void StartSurface(bool insideHome);
    void InteractSurfaceStory(InteractionTarget target);
    void InteractResidence(InteractionTarget target);
    static unsigned char NormalizeKey(unsigned char key);
    void UpdatePlayerMovement(float deltaSeconds);
    void UpdateCamera(float deltaSeconds);
    void UpdateSurface(float deltaSeconds);
    void ToggleSurfaceBike();
    void UpdateGroundVehicle(float dt);
    void UpdateGroundVehicleLifecycle(float dt);
    bool GroundVehicleBlocked(Vector2 position, float yaw, bool includePeople) const;
    bool ExitGroundVehicle();
    void InteractGroundVehicle();
    float groundVehicleSpawnRetry = 0;
    void StoreSurfaceBikeHeading();
    void RestoreSurfaceBikeHeading();
    bool SurfaceLandingClear(const world::SurfacePoint &point, int city, bool ignoreServiceCar = false,
                             bool ignoreAircraft = false) const;
    bool LandOnSurface(const world::SurfacePoint &point, int city = -1, unsigned searchRings = 8);
    friend class RelayTravel;
    friend class SaveSystem;
    friend class SaveArchive;
    friend class PlayerAircraftController;
};
// ============================================================================
// RelayTravel — registration, travel policy and cancellable, preloaded landing transaction
// ============================================================================
class RelayTravel
{
  public:
    static const world::RelayCandidate *Nearby();
    static const char *Restriction(std::string_view destination);
    static bool Begin(const std::string &destination);
    static void Cancel(const std::string &reason = "중계 이동을 취소했습니다.");
    static void Interact();
    static void Update(float deltaSeconds);
};
// ============================================================================
// GameCallbacks - window callback adapters
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
