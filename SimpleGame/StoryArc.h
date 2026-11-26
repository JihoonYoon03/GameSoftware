#pragma once
#include "Drawing.h"
#include "Game.h"
#include "ModelLibrary.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tutorial
{
class SaveArchive;
}

namespace story
{
using tutorial::ColorRGBA;
using tutorial::Vector2;

enum class CityPhase : int
{
    Normal = 0,
    Uneasy,
    Evacuation,
    Recovery,
    Count
};

enum class RiaStatus : int
{
    Available = 0,
    Injured,
    Missing,
    Count
};

enum class KainChoice : int
{
    None = 0,
    Understand,
    Criticize,
    FocusPresent,
    Count
};

enum class EvacuationPriority : int
{
    None = 0,
    ShelterPower,
    SignalRecords,
    Count
};

enum class SurvivorStatus : int
{
    Inside = 0,
    Crossing,
    Received,
    Count
};

enum class OperationZone : int
{
    None = 0,
    Waterway,       // Act 2 (S20-S23)
    CutoffFacility, // Act 3 (S30)
    HaloEvacuation, // Act 3 (S31-S33)
    OuterControl,   // Act 3 (S34)
    GateInterior,   // Act 3-4 (S35-S41)
    GateCore,       // Act 4 (S42-S43)
    Count
};

enum class OpEnemyKind : int
{
    ObservatoryDrone = 0,
    ObservatoryGuard,
    RuinStalker,
    VoidSentinel,
    CoreCoupling, // Environmental gate core structural coupling (NOT a cosmic entity)
    Count
};

enum class OpNodeKind : int
{
    ReturnGate = 0,
    // Act 2: Waterway
    WaterwayBeacon,
    DistortionPylon,
    SurveyTerminal,
    OldCampRollCall,
    RelaySwitch,
    VeilVaultTerminal,
    // Act 3: Cutoff Facility
    CutoffActuator,
    MainPowerBreaker,
    // Act 3: Halo Evacuation
    EvacDebrisBarricade,
    ClosedHouseSideAlley,
    BackupWarehouseClue,
    BackupPowerConsole,
    // Act 3: Outer Control
    OuterControlPrep,
    GateThresholdPortal,
    // Act 3-4: Gate Interior
    ProofMarker,
    ProofRadio,
    ProofScanner,
    BoundaryTestConsole,
    SurvivorExtract,
    AdvanceToCore,
    // Act 4: Gate Core
    CoreControlPoint,
    CoreReturnLine,
    Count
};

struct OpEnemy
{
    int id = 0;
    OpEnemyKind kind = OpEnemyKind::ObservatoryDrone;
    Vector2 position = {};
    Vector2 home = {};
    float yaw = 0.f;
    float radius = .46f;
    int health = 65;
    int maxHealth = 65;
    float fireCooldown = .8f;
    float alertSeconds = 0.f;
    float hitFlash = 0.f;
    int couplingIndex = -1;
    bool active = true;
};

struct OpNpc
{
    std::string id;
    std::string name;
    models::CharacterType type = models::CharacterType::Civilian;
    models::Gender gender = models::Gender::Female;
    models::HairStyle hair = models::HairStyle::Bob;
    ColorRGBA coat = {.42f, .52f, .58f, 1.f};
    Vector2 position = {};
    Vector2 target = {};
    float yaw = 0.f;
    float fireCooldown = 0.f;
    int survivorIndex = -1; // 0..2 for fixed expedition survivors
    bool isCompanion = false;
    bool escorting = false;
    bool visible = true;
};

struct OpNode
{
    OpNodeKind kind = OpNodeKind::ReturnGate;
    int index = 0;
    Vector2 position = {};
    float radius = 1.65f;
    std::string label;
    ColorRGBA glow = {.32f, .92f, .96f, 1.f};
    bool completed = false;
    bool visible = true;
};

struct OpObstacle
{
    Vector2 min = {};
    Vector2 max = {};
    float height = 2.6f;
    ColorRGBA color = {.26f, .31f, .36f, 1.f};
    ColorRGBA accent = {.22f, .64f, .72f, 1.f};
    bool blocksSight = true;
};

struct OpAnomaly
{
    Vector2 position = {};
    float radius = 3.2f;
    float yaw = 0.f;
    bool monolith = false; // Observer monolith turns only when outside player view cone
    bool stabilized = false;
};

struct OpBullet
{
    Vector2 position = {};
    Vector2 velocity = {};
    float life = 0.f;
    int damage = 14;
    bool playerOwned = true;
};

struct OpTracer
{
    Vector2 from = {};
    Vector2 to = {};
    ColorRGBA color = {1.f, .86f, .54f, 1.f};
    float life = 0.f;
};

// Persistent story progression across Acts 2, 3, 4 & Epilogue (serialized in Save Schema 15).
struct StoryArcState
{
    CityPhase cityPhase = CityPhase::Normal;
    RiaStatus riaStatus = RiaStatus::Available;
    KainChoice kainChoice = KainChoice::None;
    EvacuationPriority evacPriority = EvacuationPriority::None;

    // Act 2 (MQ02A-D, S20-S25)
    bool returnBeaconPlaced = false;
    int stabilizedPylons = 0;
    bool surveyStationInvestigated = false;
    bool oldCampInvestigated = false;
    bool kainRetreatRevealed = false;
    int relaySwitchesRestored = 0;
    bool veilHypothesisFound = false;
    bool observatoryDocsFound = false;
    bool chapter2Reported = false;
    bool chapter3Committed = false;

    // Act 3 (MQ03A-D, S30-S36)
    int cutoffRelaysDisabled = 0;
    bool cutoffEchoTriggered = false;
    bool evacRouteSecured = false;
    bool evacSideRescueDone = false;
    bool evacCommsConnected = false;
    bool haloEvacuationComplete = false;
    bool outerControlChecked = false;
    bool gateEntryReached = false;
    bool proofMarkerRead = false;
    bool proofLiveQuestion = false;
    bool proofInstrumentGap = false;
    bool missingExpeditionConfirmedAlive = false;
    bool boundaryTestDone = false;
    bool chapter3Complete = false;

    // Act 4 (MQ04A-C, S40-S44)
    std::array<SurvivorStatus, 3> survivors = {SurvivorStatus::Inside, SurvivorStatus::Inside,
                                               SurvivorStatus::Inside};
    bool allSurvivorsReceived = false;
    std::array<bool, 2> controlPointsDisabled = {false, false};
    int couplingsDestroyed = 0;
    bool coreStopped = false;
    bool returnConfirmed = false;
    bool sectorLossTriggered = false;
    bool safetyLineChecked = false;
    bool supportLogsRecovered = false;
    bool missingFiled = false;
    bool chapter4Complete = false;

    // Epilogue (MQE01-02, S50-S52)
    bool epilogueShelterVisited = false;
    bool epilogueClinicVisited = false;
    bool epilogueWorkshopVisited = false;
    bool epilogueRecoveryTaskDone = false;
    bool shelterLampOn = true;
    bool epilogueDaySkipped = false;
    bool nereusSignalReceived = false;

    // Active zone & live tactical runtime (not required in base archive; zone rebuilt on load)
    OperationZone activeZone = OperationZone::None;
    OperationZone resumeZone = OperationZone::None;
    Vector2 worldSize = {52.f, 44.f};
    Vector2 playerStart = {6.f, 22.f};
    float invulnerability = 0.f;
    float scannerCooldown = 0.f;
    float scannerPulseRadius = 0.f;
    Vector2 scannerOrigin = {};
    float pulseWaveTimer = 0.f;
    float pulseWaveRadius = 0.f;
    bool backupWarehouseRevealed = false;
    int warehouseCluesFound = 0;
    float epilogueTransitionSeconds = 0.f;

    std::vector<OpObstacle> obstacles;
    std::vector<OpNode> nodes;
    std::vector<OpEnemy> enemies;
    std::vector<OpNpc> npcs;
    std::vector<OpAnomaly> anomalies;
    std::vector<OpBullet> bullets;
    std::vector<OpTracer> tracers;

    bool IsDefaultClean() const;
};

// ============================================================================
// StoryArc — Chapter 1 full arc (Acts 2, 3, 4 & Epilogue) gameplay & narrative controller
// ============================================================================
class StoryArc
{
    friend class tutorial::SaveArchive;

  public:
    static StoryArc &Get();

    StoryArcState &State()
    {
        return state;
    }
    const StoryArcState &State() const
    {
        return state;
    }

    void Reset();
    bool IsActive() const
    {
        return state.activeZone != OperationZone::None;
    }
    bool IsRiaAvailable() const
    {
        return state.riaStatus == RiaStatus::Available;
    }
    bool IsRiaMissing() const
    {
        return state.riaStatus == RiaStatus::Missing;
    }
    bool IsActTwoUnlocked() const;
    bool IsPostActOneArcActive() const;

    // Operation zone lifecycle
    bool EnterZone(OperationZone zone);
    void LeaveZoneToHalo(bool preserveResume = true);
    void RebuildActiveZone();

    // Real-time tactical update, collision, combat & scanner
    void Update(float dt);
    bool IsPositionBlocked(Vector2 position, float radius = .35f) const;
    Vector2 ResolveMovement(Vector2 from, Vector2 to, float radius = .35f) const;
    bool HasLineOfSight(Vector2 a, Vector2 b) const;
    bool TryAttack(Vector2 origin, Vector2 target);
    bool TryMelee(Vector2 origin, float yaw);
    bool TriggerScanner();
    bool Interact();
    const OpNode *NearbyNode(Vector2 position, float maxDistance = 2.6f) const;
    std::string NearbyInteractionLabel(Vector2 position) const;

    // Halo city & hub hooks (S24-S25, S44, S50-S52)
    bool InteractHaloReceiver();
    bool InteractHaloMara();
    bool InteractHaloKain();
    bool InteractHaloSafetyLine();
    bool InteractHaloShelterLamp();
    bool InteractHaloEpilogueClinic();
    bool InteractHaloEpilogueWorkshop();

    // Medevac safe-base clinic & ops resume (never reverts received survivors, core shutdown, or missing status)
    bool HandleMedevacRecovery();

    // Stage-select / test entry helpers (deterministic jump into Acts 2, 3, 4, Epilogue)
    void PrepareActTwoEntry();
    void PrepareActThreeEntry();
    void PrepareActFourEntry();
    void PrepareEpilogueEntry();

    // HUD & Journal read-only projections
    std::string CurrentObjectiveTitle() const;
    std::string CurrentObjectiveAction() const;
    std::string CurrentObjectiveLocation() const;
    std::string ActiveZoneTitle() const;
    static const char *CityPhaseKey(CityPhase phase);
    static const char *RiaStatusKey(RiaStatus status);
    static const char *KainChoiceKey(KainChoice choice);
    static const char *EvacPriorityKey(EvacuationPriority priority);
    static const char *ZoneKey(OperationZone zone);
    static const char *SurvivorStatusKey(SurvivorStatus status);

    // Check if an enemy is alive within range (blocks non-safe dialogue choices)
    bool HasNearbyHostiles(Vector2 position, float range = 11.f) const;
    int RemainingHostiles(OpEnemyKind filter = OpEnemyKind::Count) const;
    int ReceivedSurvivorCount() const;

  private:
    StoryArcState state;

    void BuildWaterwayZone();
    void BuildCutoffFacilityZone();
    void BuildHaloEvacuationZone();
    void BuildOuterControlZone();
    void BuildGateInteriorZone();
    void BuildGateCoreZone();
    void UpdateEnemies(float dt);
    void UpdateNpcs(float dt);
    void UpdateBullets(float dt);
    void UpdateAnomalies(float dt);
    void RefreshNodeStates();
    void DamagePlayer(int amount);
    void DamageEnemy(OpEnemy &enemy, int damage, Vector2 from);
    void CheckCouplingShutdown();
    void TriggerSectorLossSequence();
    void OpenS22KainConfession();
    void OpenS23VeilComparison();
    void OpenS25CommitChoice();
    void OpenS33BackupPowerChoice();
    void OpenS52NereusChoice();
};
} // namespace story
