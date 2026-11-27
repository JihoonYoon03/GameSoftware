#include "stdafx.h"
#include "StoryArc.h"
#include "Civic.h"
#include "Level.h"
#include "Overworld.h"
#include "Weapon.h"
#include <algorithm>
#include <cmath>

namespace story
{
namespace
{
using namespace tutorial;

constexpr float kPi = 3.14159265f;

float WrapAngle(float a)
{
    while (a > kPi)
    {
        a -= 2.f * kPi;
    }
    while (a < -kPi)
    {
        a += 2.f * kPi;
    }
    return a;
}

bool CircleIntersectsRect(Vector2 c, float r, Vector2 mn, Vector2 mx)
{
    const float cx = Game::Clamp(c.x, mn.x, mx.x);
    const float cy = Game::Clamp(c.y, mn.y, mx.y);
    const float dx = c.x - cx;
    const float dy = c.y - cy;
    return dx * dx + dy * dy < r * r;
}

bool SegmentIntersectsRect(Vector2 a, Vector2 b, Vector2 mn, Vector2 mx)
{
    float tmin = 0.f;
    float tmax = 1.f;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    auto clip = [&](float p, float q) {
        if (std::fabs(p) < 1e-6f)
        {
            return q >= 0.f;
        }
        const float r = q / p;
        if (p < 0.f)
        {
            if (r > tmax)
            {
                return false;
            }
            if (r > tmin)
            {
                tmin = r;
            }
        }
        else
        {
            if (r < tmin)
            {
                return false;
            }
            if (r < tmax)
            {
                tmax = r;
            }
        }
        return true;
    };
    return clip(-dx, a.x - mn.x) && clip(dx, mx.x - a.x) && clip(-dy, a.y - mn.y) && clip(dy, mx.y - a.y);
}

const char *kSurvivorNames[3] = {"윤서진 · 탐사 통신관", "강도현 · 선임 측량사", "박민재 · 현장 정비원"};
} // namespace

bool StoryArcState::IsDefaultClean() const
{
    return cityPhase == CityPhase::Normal && riaStatus == RiaStatus::Available && kainChoice == KainChoice::None &&
           evacPriority == EvacuationPriority::None && !returnBeaconPlaced && stabilizedPylons == 0 &&
           !surveyStationInvestigated && !oldCampInvestigated && !kainRetreatRevealed && relaySwitchesRestored == 0 &&
           !veilHypothesisFound && !observatoryDocsFound && !chapter2Reported && !chapter3Committed &&
           cutoffRelaysDisabled == 0 && !cutoffEchoTriggered && !evacRouteSecured && !evacSideRescueDone &&
           !evacCommsConnected && !haloEvacuationComplete && !outerControlChecked && !gateEntryReached &&
           !proofMarkerRead && !proofLiveQuestion && !proofInstrumentGap && !missingExpeditionConfirmedAlive &&
           !boundaryTestDone && !chapter3Complete &&
           survivors[0] == SurvivorStatus::Inside && survivors[1] == SurvivorStatus::Inside &&
           survivors[2] == SurvivorStatus::Inside && !allSurvivorsReceived && !controlPointsDisabled[0] &&
           !controlPointsDisabled[1] && couplingsDestroyed == 0 && !coreStopped && !returnConfirmed &&
           !sectorLossTriggered && !safetyLineChecked && !supportLogsRecovered && !missingFiled && !chapter4Complete &&
           !epilogueShelterVisited && !epilogueClinicVisited && !epilogueWorkshopVisited && !epilogueRecoveryTaskDone &&
           !epilogueDaySkipped && !nereusSignalReceived && activeZone == OperationZone::None &&
           resumeZone == OperationZone::None;
}

StoryArc &StoryArc::Get()
{
    static StoryArc instance;
    return instance;
}

void StoryArc::Reset()
{
    state = {};
}

bool StoryArc::IsActTwoUnlocked() const
{
    const auto &ch = game::GameLevels::Chapter().GetState();
    return ch.recordsCompared && ch.waterwayUnlocked;
}

bool StoryArc::IsPostActOneArcActive() const
{
    return IsActTwoUnlocked() && !state.nereusSignalReceived;
}

const char *StoryArc::CityPhaseKey(CityPhase phase)
{
    switch (phase)
    {
    case CityPhase::Normal:
        return "normal";
    case CityPhase::Uneasy:
        return "uneasy";
    case CityPhase::Evacuation:
        return "evacuation";
    case CityPhase::Recovery:
        return "recovery";
    default:
        return "normal";
    }
}

const char *StoryArc::RiaStatusKey(RiaStatus status)
{
    switch (status)
    {
    case RiaStatus::Available:
        return "available";
    case RiaStatus::Injured:
        return "injured";
    case RiaStatus::Missing:
        return "missing";
    default:
        return "available";
    }
}

const char *StoryArc::KainChoiceKey(KainChoice choice)
{
    switch (choice)
    {
    case KainChoice::Understand:
        return "understand";
    case KainChoice::Criticize:
        return "criticize";
    case KainChoice::FocusPresent:
        return "focus_present";
    default:
        return "none";
    }
}

const char *StoryArc::EvacPriorityKey(EvacuationPriority priority)
{
    switch (priority)
    {
    case EvacuationPriority::ShelterPower:
        return "shelter_power";
    case EvacuationPriority::SignalRecords:
        return "signal_records";
    default:
        return "none";
    }
}

const char *StoryArc::ZoneKey(OperationZone zone)
{
    switch (zone)
    {
    case OperationZone::Waterway:
        return "waterway";
    case OperationZone::CutoffFacility:
        return "cutoff_facility";
    case OperationZone::HaloEvacuation:
        return "halo_evacuation";
    case OperationZone::OuterControl:
        return "outer_control";
    case OperationZone::GateInterior:
        return "gate_interior";
    case OperationZone::GateCore:
        return "gate_core";
    default:
        return "none";
    }
}

const char *StoryArc::SurvivorStatusKey(SurvivorStatus status)
{
    switch (status)
    {
    case SurvivorStatus::Inside:
        return "inside";
    case SurvivorStatus::Crossing:
        return "crossing";
    case SurvivorStatus::Received:
        return "received";
    default:
        return "inside";
    }
}

int StoryArc::ReceivedSurvivorCount() const
{
    int count = 0;
    for (auto s : state.survivors)
    {
        if (s == SurvivorStatus::Received)
        {
            ++count;
        }
    }
    return count;
}

int StoryArc::RemainingHostiles(OpEnemyKind filter) const
{
    int count = 0;
    for (const auto &e : state.enemies)
    {
        if (!e.active || e.health <= 0)
        {
            continue;
        }
        if (filter != OpEnemyKind::Count && e.kind != filter)
        {
            continue;
        }
        ++count;
    }
    return count;
}

bool StoryArc::HasNearbyHostiles(Vector2 position, float range) const
{
    for (const auto &e : state.enemies)
    {
        if (!e.active || e.health <= 0 || e.kind == OpEnemyKind::CoreCoupling)
        {
            continue;
        }
        if (Game::Distance(position, e.position) <= range)
        {
            return true;
        }
    }
    return false;
}

bool StoryArc::IsPositionBlocked(Vector2 position, float radius) const
{
    if (position.x < 1.2f + radius || position.x > state.worldSize.x - 1.2f - radius ||
        position.y < 1.2f + radius || position.y > state.worldSize.y - 1.2f - radius)
    {
        return true;
    }
    for (const auto &obs : state.obstacles)
    {
        if (CircleIntersectsRect(position, radius, obs.min, obs.max))
        {
            return true;
        }
    }
    return false;
}

Vector2 StoryArc::ResolveMovement(Vector2 from, Vector2 to, float radius) const
{
    if (!IsPositionBlocked(to, radius))
    {
        return to;
    }
    const Vector2 tryX{to.x, from.y};
    if (!IsPositionBlocked(tryX, radius))
    {
        return tryX;
    }
    const Vector2 tryY{from.x, to.y};
    if (!IsPositionBlocked(tryY, radius))
    {
        return tryY;
    }
    return from;
}

bool StoryArc::HasLineOfSight(Vector2 a, Vector2 b) const
{
    for (const auto &obs : state.obstacles)
    {
        if (obs.blocksSight && SegmentIntersectsRect(a, b, obs.min, obs.max))
        {
            return false;
        }
    }
    return true;
}

void StoryArc::RebuildActiveZone()
{
    state.obstacles.clear();
    state.nodes.clear();
    state.enemies.clear();
    state.npcs.clear();
    state.anomalies.clear();
    state.bullets.clear();
    state.tracers.clear();
    state.scannerPulseRadius = 0.f;
    state.pulseWaveTimer = 0.f;
    state.pulseWaveRadius = 0.f;
    switch (state.activeZone)
    {
    case OperationZone::Waterway:
        BuildWaterwayZone();
        break;
    case OperationZone::CutoffFacility:
        BuildCutoffFacilityZone();
        break;
    case OperationZone::HaloEvacuation:
        BuildHaloEvacuationZone();
        break;
    case OperationZone::OuterControl:
        BuildOuterControlZone();
        break;
    case OperationZone::GateInterior:
        BuildGateInteriorZone();
        break;
    case OperationZone::GateCore:
        BuildGateCoreZone();
        break;
    default:
        break;
    }
    RefreshNodeStates();
}

bool StoryArc::EnterZone(OperationZone zone)
{
    if (zone == OperationZone::None || zone == OperationZone::Count)
    {
        return false;
    }
    auto &w = Game::Get().GetState();
    game::GameLevels::Mine().GetState().active = false;
    game::GameLevels::Chapter().GetState().active = false;
    w.isInsideHome = false;
    w.isInObservatory = false;
    w.facilityInterior.Clear();
    w.isBikeMounted = false;
    w.car.mounted = false;
    w.aircraft.mounted = false;
    w.StopMotion();

    state.activeZone = zone;
    state.resumeZone = zone;
    RebuildActiveZone();
    w.playerPosition = state.playerStart;
    w.cameraPosition = state.playerStart;
    w.facingYaw = 0.f;
    return true;
}

void StoryArc::LeaveZoneToHalo(bool preserveResume)
{
    if (!preserveResume)
    {
        state.resumeZone = OperationZone::None;
    }
    state.activeZone = OperationZone::None;
    state.bullets.clear();
    state.tracers.clear();
    auto &g = Game::Get();
    auto &w = g.GetState();
    w.StopMotion();
    if (w.surfaceExploration && g.Surface())
    {
        if (g.Surface()->Ready())
        {
            if (!g.ReturnToSurface({16.f, 7.f}, 0))
            {
                g.ReturnToSurface(kHomeDoor, 0);
            }
        }
        else
        {
            g.RelocateSurfaceSurvey(g.Surface()->Pose().origin);
        }
        return;
    }
    w.playerPosition = {16.f, 7.f};
    w.cameraPosition = w.playerPosition;
}

void StoryArc::BuildWaterwayZone()
{
    state.worldSize = {52.f, 44.f};
    state.playerStart = {6.5f, 22.f};

    state.obstacles = {
        {{12.f, 16.5f}, {14.5f, 19.5f}, 2.8f, {.30f, .34f, .37f}, {.32f, .72f, .76f}, true},
        {{12.f, 24.5f}, {14.5f, 27.5f}, 2.8f, {.30f, .34f, .37f}, {.32f, .72f, .76f}, true},
        {{22.f, 4.f}, {33.f, 6.2f}, 3.6f, {.26f, .30f, .35f}, {.92f, .64f, .30f}, true},
        {{22.f, 13.5f}, {26.5f, 15.2f}, 3.2f, {.26f, .30f, .35f}, {.92f, .64f, .30f}, true},
        {{22.f, 36.f}, {33.f, 38.2f}, 2.4f, {.25f, .29f, .32f}, {.76f, .64f, .44f}, true},
        {{38.f, 14.5f}, {48.f, 16.5f}, 4.2f, {.18f, .24f, .30f}, {.34f, .90f, .96f}, true},
        {{38.f, 27.5f}, {48.f, 29.5f}, 4.2f, {.18f, .24f, .30f}, {.34f, .90f, .96f}, true},
        {{46.5f, 16.5f}, {48.5f, 27.5f}, 4.2f, {.18f, .24f, .30f}, {.34f, .90f, .96f}, true},
        {{28.f, 20.5f}, {31.f, 23.5f}, 1.6f, {.28f, .33f, .35f}, {.42f, .54f, .62f}, false},
    };

    state.anomalies = {
        {{18.5f, 14.5f}, 3.6f, 0.f, false, state.stabilizedPylons >= 1},
        {{18.5f, 29.5f}, 3.6f, 0.f, false, state.stabilizedPylons >= 2},
        {{35.5f, 22.0f}, 3.4f, 0.f, false, state.stabilizedPylons >= 2},
    };

    state.npcs.push_back({"ria", "리아", models::CharacterType::Researcher, models::Gender::Female,
                          models::HairStyle::Ponytail, {.36f, .70f, .86f, 1.f}, {5.2f, 20.6f}, {5.2f, 20.6f}, 0.f, 0.f,
                          -1, true, false, state.riaStatus == RiaStatus::Available});
    state.npcs.push_back({"kain", "카인", models::CharacterType::Guard, models::Gender::Male,
                          models::HairStyle::SidePart, {.38f, .44f, .50f, 1.f}, {5.2f, 23.4f}, {5.2f, 23.4f}, 0.f, 0.f,
                          -1, true, false, true});

    state.nodes.push_back({OpNodeKind::ReturnGate, 0, {3.8f, 22.f}, 2.2f, "헤일로 복귀", {.44f, .92f, .78f, 1.f}, false, true});
    state.nodes.push_back({OpNodeKind::WaterwayBeacon, 0, {10.5f, 22.f}, 2.2f, "수로 입구에 귀환 표식 설치",
                           {1.f, .86f, .38f, 1.f}, state.returnBeaconPlaced, true});
    state.nodes.push_back({OpNodeKind::DistortionPylon, 0, {18.5f, 17.5f}, 2.2f, "북쪽 왜곡 보정 표식 정렬",
                           {.34f, .92f, .98f, 1.f}, state.stabilizedPylons >= 1, true});
    state.nodes.push_back({OpNodeKind::DistortionPylon, 1, {18.5f, 26.5f}, 2.2f, "남쪽 왜곡 보정 표식 정렬",
                           {.34f, .92f, .98f, 1.f}, state.stabilizedPylons >= 2, true});
    state.nodes.push_back({OpNodeKind::SurveyTerminal, 0, {28.5f, 9.5f}, 2.4f, "폐측량소 점검 단말 · 배전 대조",
                           {1.f, .74f, .36f, 1.f}, state.surveyStationInvestigated, true});
    state.nodes.push_back({OpNodeKind::OldCampRollCall, 0, {28.5f, 33.f}, 2.4f, "옛 야영지 점호 기록 확인 · 카인과 대화",
                           {.96f, .72f, .48f, 1.f}, state.oldCampInvestigated && state.kainRetreatRevealed, true});
    state.nodes.push_back({OpNodeKind::RelaySwitch, 0, {36.5f, 13.f}, 2.1f, "중계실 북쪽 보조 전원 복구",
                           {.42f, .96f, .64f, 1.f}, state.relaySwitchesRestored >= 1, true});
    state.nodes.push_back({OpNodeKind::RelaySwitch, 1, {36.5f, 31.f}, 2.1f, "중계실 남쪽 보조 전원 복구",
                           {.42f, .96f, .64f, 1.f}, state.relaySwitchesRestored >= 2, true});
    state.nodes.push_back({OpNodeKind::VeilVaultTerminal, 0, {43.5f, 22.f}, 2.5f, "봉쇄 중계실 도식 · 관측국 문서 대조",
                           {.32f, .92f, 1.f, 1.f}, state.veilHypothesisFound && state.observatoryDocsFound, true});

    int eid = 1;
    if (!state.surveyStationInvestigated)
    {
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {25.5f, 10.5f}, {25.5f, 10.5f}, 3.1f, .45f, 55, 55});
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {31.5f, 11.5f}, {31.5f, 11.5f}, 3.1f, .45f, 55, 55});
    }
    if (!state.oldCampInvestigated)
    {
        state.enemies.push_back({eid++, OpEnemyKind::RuinStalker, {25.0f, 31.5f}, {25.0f, 31.5f}, 2.8f, .48f, 65, 65});
        state.enemies.push_back({eid++, OpEnemyKind::RuinStalker, {31.0f, 34.0f}, {31.0f, 34.0f}, 2.8f, .48f, 65, 65});
    }
    if (!state.veilHypothesisFound)
    {
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {40.0f, 19.5f}, {40.0f, 19.5f}, 3.14f, .46f, 65, 65});
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryGuard, {41.5f, 24.5f}, {41.5f, 24.5f}, 3.14f, .48f, 78, 78});
    }
}

void StoryArc::BuildCutoffFacilityZone()
{
    state.worldSize = {50.f, 42.f};
    state.playerStart = {6.f, 21.f};

    state.obstacles = {
        {{15.f, 10.f}, {17.5f, 17.5f}, 3.4f, {.22f, .26f, .32f}, {.96f, .48f, .26f}, true},
        {{15.f, 24.5f}, {17.5f, 32.f}, 3.4f, {.22f, .26f, .32f}, {.96f, .48f, .26f}, true},
        {{26.f, 17.5f}, {29.5f, 24.5f}, 3.0f, {.20f, .25f, .32f}, {.34f, .86f, .96f}, true},
        {{36.f, 7.f}, {45.f, 9.2f}, 4.0f, {.18f, .22f, .29f}, {.96f, .38f, .30f}, true},
        {{36.f, 32.8f}, {45.f, 35.f}, 4.0f, {.18f, .22f, .29f}, {.96f, .38f, .30f}, true},
    };

    state.npcs.push_back({"ria", "리아", models::CharacterType::Researcher, models::Gender::Female,
                          models::HairStyle::Ponytail, {.36f, .70f, .86f, 1.f}, {4.8f, 19.6f}, {4.8f, 19.6f}, 0.f, 0.f,
                          -1, true, false, true});
    state.npcs.push_back({"kain", "카인", models::CharacterType::Guard, models::Gender::Male,
                          models::HairStyle::SidePart, {.38f, .44f, .50f, 1.f}, {4.8f, 22.4f}, {4.8f, 22.4f}, 0.f, 0.f,
                          -1, true, false, true});
    state.npcs.push_back({"obs_director", "관측국 현장 책임자", models::CharacterType::Researcher, models::Gender::Male,
                          models::HairStyle::SidePart, {.22f, .30f, .46f, 1.f}, {42.5f, 21.f}, {42.5f, 21.f}, 3.14f,
                          0.f, -1, false, false, true});

    state.nodes.push_back({OpNodeKind::CutoffActuator, 0, {21.5f, 11.5f}, 2.2f, "북부 기동기 선로 차단",
                           {1.f, .64f, .28f, 1.f}, state.cutoffRelaysDisabled >= 1, true});
    state.nodes.push_back({OpNodeKind::CutoffActuator, 1, {21.5f, 30.5f}, 2.2f, "남부 기동기 선로 차단",
                           {1.f, .64f, .28f, 1.f}, state.cutoffRelaysDisabled >= 2, true});
    state.nodes.push_back({OpNodeKind::CutoffActuator, 2, {33.5f, 21.0f}, 2.2f, "중앙 송전 중계기 차단",
                           {1.f, .64f, .28f, 1.f}, state.cutoffRelaysDisabled >= 3, true});
    state.nodes.push_back({OpNodeKind::MainPowerBreaker, 0, {41.0f, 21.0f}, 2.6f, "도시 전력 주 차단기 실행 · 독립 전원 확인",
                           {.98f, .36f, .32f, 1.f}, state.cutoffEchoTriggered, true});

    int eid = 1;
    if (!state.cutoffEchoTriggered)
    {
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {20.f, 14.5f}, {20.f, 14.5f}, 3.14f, .45f, 60, 60});
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryGuard, {23.f, 28.f}, {23.f, 28.f}, 3.14f, .48f, 80, 80});
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {31.f, 16.5f}, {31.f, 16.5f}, 3.14f, .45f, 65, 65});
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryGuard, {34.f, 24.5f}, {34.f, 24.5f}, 3.14f, .48f, 85, 85});
    }
}

void StoryArc::BuildHaloEvacuationZone()
{
    state.worldSize = {54.f, 44.f};
    state.playerStart = {9.f, 22.f};
    const auto &ch = game::GameLevels::Chapter().GetState();
    state.backupWarehouseRevealed = ch.detailLedgerFound || state.warehouseCluesFound >= 2;

    state.obstacles = {
        {{16.f, 6.f}, {24.f, 11.5f}, 4.2f, {.28f, .32f, .36f}, {.30f, .74f, .86f}, true},
        {{28.f, 26.f}, {38.f, 34.f}, 4.0f, {.26f, .30f, .34f}, {.92f, .48f, .32f}, true},
        {{36.f, 8.f}, {46.f, 14.5f}, 3.8f, {.24f, .29f, .35f}, {.42f, .92f, .64f}, true},
        {{19.f, 19.5f}, {22.f, 24.5f}, 1.8f, {.34f, .28f, .25f}, {.98f, .52f, .28f}, false},
    };

    state.anomalies = {
        {{26.f, 21.f}, 2.6f, 0.f, false, state.evacRouteSecured},
        {{33.f, 24.5f}, 2.2f, 0.f, false, state.evacSideRescueDone},
    };

    state.npcs.push_back({"ria", "리아", models::CharacterType::Researcher, models::Gender::Female,
                          models::HairStyle::Ponytail, {.36f, .70f, .86f, 1.f}, {7.5f, 20.5f}, {7.5f, 20.5f}, 0.f, 0.f,
                          -1, true, false, true});
    state.npcs.push_back({"kain", "카인", models::CharacterType::Guard, models::Gender::Male,
                          models::HairStyle::SidePart, {.38f, .44f, .50f, 1.f}, {7.5f, 23.5f}, {7.5f, 23.5f}, 0.f, 0.f,
                          -1, true, false, true});
    state.npcs.push_back({"mara", "마라", models::CharacterType::Civilian, models::Gender::Female,
                          models::HairStyle::Bob, {.74f, .54f, .38f, 1.f}, {11.f, 18.f}, {11.f, 18.f}, .3f, 0.f, -1,
                          false, false, true});
    state.npcs.push_back({"trapped_0", "거주자 · 07구역", models::CharacterType::Civilian, models::Gender::Female,
                          models::HairStyle::Bob, {.52f, .60f, .66f, 1.f}, {40.f, 30.f}, {12.f, 21.f}, 3.1f, 0.f, -1,
                          false, state.evacSideRescueDone, true});
    state.npcs.push_back({"trapped_1", "부상 주민", models::CharacterType::Civilian, models::Gender::Male,
                          models::HairStyle::Buzz, {.58f, .54f, .48f, 1.f}, {40.5f, 31.4f}, {12.5f, 22.5f}, 3.1f, 0.f,
                          -1, false, state.evacSideRescueDone, true});

    state.nodes.push_back({OpNodeKind::EvacDebrisBarricade, 0, {22.5f, 22.f}, 2.5f,
                           ch.vehicleRepaired ? "광장 집결로 잔해 정리 · 대피 차량 통과 확보"
                                              : "광장 집결로 확보 · 도보 우회로 정리",
                           {1.f, .76f, .32f, 1.f}, state.evacRouteSecured, true});
    state.nodes.push_back({OpNodeKind::ClosedHouseSideAlley, 0, {39.5f, 30.5f}, 2.5f,
                           "닫힌 집 측면 통로 개방 · 실제 거주자 구조",
                           {.96f, .54f, .36f, 1.f}, state.evacSideRescueDone, true});
    if (!state.backupWarehouseRevealed)
    {
        state.nodes.push_back({OpNodeKind::BackupWarehouseClue, 0, {26.f, 13.f}, 2.2f,
                               "현장 표지판 · 예비 전력 배선 추적 (1/2)", {.42f, .86f, .96f, 1.f},
                               state.warehouseCluesFound >= 1, true});
        state.nodes.push_back({OpNodeKind::BackupWarehouseClue, 1, {32.f, 16.f}, 2.2f,
                               "수리 기록 단말 · 예비 창고 위치 특정 (2/2)", {.42f, .86f, .96f, 1.f},
                               state.warehouseCluesFound >= 2, true});
    }
    state.nodes.push_back({OpNodeKind::BackupPowerConsole, 0, {41.5f, 16.5f}, 2.6f,
                           "예비 전원 창고 · 대피소 전력 / 수신 기록 우선순위 결정",
                           {.34f, .96f, .68f, 1.f}, state.evacCommsConnected, true});
    state.nodes.push_back({OpNodeKind::ReturnGate, 0, {48.f, 22.f}, 2.6f, "외부 제어소로 이동 (대피 완료 후)",
                           {.36f, .92f, 1.f, 1.f}, false, state.haloEvacuationComplete});

    int eid = 1;
    if (!state.evacRouteSecured)
    {
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {24.f, 19.5f}, {24.f, 19.5f}, 3.14f, .45f, 60, 60});
        state.enemies.push_back({eid++, OpEnemyKind::RuinStalker, {25.f, 24.5f}, {25.f, 24.5f}, 3.14f, .48f, 70, 70});
    }
    if (!state.evacSideRescueDone)
    {
        state.enemies.push_back({eid++, OpEnemyKind::RuinStalker, {39.5f, 27.5f}, {39.5f, 27.5f}, 2.5f, .48f, 72, 72});
    }
    if (!state.evacCommsConnected)
    {
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {38.f, 17.5f}, {38.f, 17.5f}, 3.14f, .45f, 65, 65});
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {43.f, 19.5f}, {43.f, 19.5f}, 3.14f, .45f, 65, 65});
    }
}

void StoryArc::BuildOuterControlZone()
{
    state.worldSize = {40.f, 32.f};
    state.playerStart = {8.f, 16.f};

    state.obstacles = {
        {{12.f, 6.f}, {28.f, 8.2f}, 3.2f, {.22f, .28f, .34f}, {.34f, .90f, .96f}, true},
        {{12.f, 23.8f}, {28.f, 26.f}, 3.2f, {.22f, .28f, .34f}, {.34f, .90f, .96f}, true},
    };

    state.npcs.push_back({"ria", "리아", models::CharacterType::Researcher, models::Gender::Female,
                          models::HairStyle::Ponytail, {.36f, .70f, .86f, 1.f}, {19.5f, 13.5f}, {19.5f, 13.5f}, 1.4f,
                          0.f, -1, false, false, state.riaStatus == RiaStatus::Available});
    state.npcs.push_back({"kain", "카인", models::CharacterType::Guard, models::Gender::Male,
                          models::HairStyle::SidePart, {.38f, .44f, .50f, 1.f}, {10.5f, 17.5f}, {10.5f, 17.5f}, 0.f,
                          0.f, -1, true, false, true});
    state.npcs.push_back({"tech", "현장 지원 기술자", models::CharacterType::Engineer, models::Gender::Male,
                          models::HairStyle::Buzz, {.72f, .58f, .30f, 1.f}, {22.f, 12.f}, {22.f, 12.f}, 1.8f, 0.f, -1,
                          false, false, true});

    state.nodes.push_back({OpNodeKind::OuterControlPrep, 0, {20.f, 15.f}, 2.6f,
                           "예비 전원 · 통신 · 철수 표식 점검 (안전 거점 확보)",
                           {.34f, .96f, .72f, 1.f}, state.outerControlChecked, true});
    state.nodes.push_back({OpNodeKind::GateThresholdPortal, 0, {32.f, 16.f}, 2.8f,
                           "관문 N-0 내부 진입 (리아 외부 제어 유지)",
                           {.38f, .84f, 1.f, 1.f}, false, state.outerControlChecked});
}

void StoryArc::BuildGateInteriorZone()
{
    state.worldSize = {54.f, 44.f};
    state.playerStart = {7.f, 22.f};

    state.obstacles = {
        {{14.f, 10.f}, {16.5f, 17.5f}, 4.5f, {.16f, .20f, .28f}, {.44f, .74f, 1.f}, true},
        {{14.f, 26.5f}, {16.5f, 34.f}, 4.5f, {.16f, .20f, .28f}, {.44f, .74f, 1.f}, true},
        {{28.f, 13.f}, {31.f, 18.5f}, 4.5f, {.16f, .20f, .28f}, {.52f, .64f, 1.f}, true},
        {{28.f, 25.5f}, {31.f, 31.f}, 4.5f, {.16f, .20f, .28f}, {.52f, .64f, 1.f}, true},
    };

    state.anomalies = {
        {{21.f, 11.f}, 2.4f, 1.5f, true, false},
        {{21.f, 33.f}, 2.4f, -1.5f, true, false},
        {{36.f, 12.f}, 2.4f, 2.2f, true, false},
        {{36.f, 32.f}, 2.4f, -2.2f, true, false},
    };

    state.npcs.push_back({"kain", "카인", models::CharacterType::Guard, models::Gender::Male,
                          models::HairStyle::SidePart, {.38f, .44f, .50f, 1.f}, {5.8f, 23.2f}, {5.8f, 23.2f}, 0.f, 0.f,
                          -1, true, false, true});

    const Vector2 survPos[3] = {{37.f, 20.5f}, {39.f, 22.2f}, {37.5f, 23.8f}};
    const models::Gender survGender[3] = {models::Gender::Female, models::Gender::Male, models::Gender::Male};
    const models::HairStyle survHair[3] = {models::HairStyle::Ponytail, models::HairStyle::SidePart,
                                           models::HairStyle::Buzz};
    for (int i = 0; i < 3; ++i)
    {
        const bool alreadyOut = state.survivors[size_t(i)] == SurvivorStatus::Received;
        state.npcs.push_back({"expedition_" + std::to_string(i), kSurvivorNames[i], models::CharacterType::Explorer,
                              survGender[i], survHair[i], {.58f, .68f, .76f, 1.f}, survPos[i], {10.5f, 22.f}, 3.14f,
                              0.f, i, false, state.survivors[size_t(i)] == SurvivorStatus::Crossing, !alreadyOut});
    }

    state.nodes.push_back({OpNodeKind::ProofMarker, 0, {34.5f, 19.5f}, 2.4f,
                           "증거 1/3 · 새 표식 번호 직접 확인", {.38f, .92f, .98f, 1.f}, state.proofMarkerRead, true});
    state.nodes.push_back({OpNodeKind::ProofRadio, 0, {35.5f, 22.0f}, 2.4f,
                           "증거 2/3 · 리아 즉석 질문('바람') 무전 응답 확인", {.42f, .96f, .72f, 1.f},
                           state.proofLiveQuestion, true});
    state.nodes.push_back({OpNodeKind::ProofScanner, 0, {34.5f, 24.5f}, 2.4f,
                           "증거 3/3 · 내부 접촉 대 외부 계측 공백 대조", {.86f, .66f, 1.f, 1.f},
                           state.proofInstrumentGap, true});

    state.nodes.push_back({OpNodeKind::BoundaryTestConsole, 0, {11.5f, 22.f}, 2.6f,
                           "경계 횡단 시험 및 카인 점호 진행", {1.f, .78f, .38f, 1.f}, state.boundaryTestDone,
                           state.missingExpeditionConfirmedAlive});

    state.nodes.push_back({OpNodeKind::SurvivorExtract, 0, {10.5f, 22.f}, 2.8f,
                           "탐사대원 현재 응답 대조 · 순차 귀환 인수", {.38f, .98f, .74f, 1.f},
                           state.allSurvivorsReceived, state.chapter3Complete && !state.allSurvivorsReceived});

    state.nodes.push_back({OpNodeKind::AdvanceToCore, 0, {46.f, 22.f}, 2.8f,
                           "관문 N-0 내부 제어 구역 진입 (카인 동행)", {.98f, .44f, .38f, 1.f}, false,
                           state.allSurvivorsReceived});

    int eid = 1;
    if (!state.missingExpeditionConfirmedAlive)
    {
        state.enemies.push_back({eid++, OpEnemyKind::VoidSentinel, {22.f, 18.f}, {22.f, 18.f}, 3.14f, .50f, 75, 75});
        state.enemies.push_back({eid++, OpEnemyKind::VoidSentinel, {24.f, 26.f}, {24.f, 26.f}, 3.14f, .50f, 75, 75});
    }
    if (state.chapter3Complete && !state.allSurvivorsReceived)
    {
        state.enemies.push_back({eid++, OpEnemyKind::VoidSentinel, {25.f, 22.f}, {25.f, 22.f}, 3.14f, .50f, 80, 80});
    }
}

void StoryArc::BuildGateCoreZone()
{
    state.worldSize = {52.f, 44.f};
    state.playerStart = {7.f, 22.f};

    state.obstacles = {
        {{16.f, 13.f}, {18.5f, 16.f}, 3.6f, {.18f, .23f, .30f}, {.34f, .86f, .96f}, true},
        {{16.f, 28.f}, {18.5f, 31.f}, 3.6f, {.18f, .23f, .30f}, {.34f, .86f, .96f}, true},
        {{32.f, 19.5f}, {36.f, 24.5f}, 4.8f, {.16f, .20f, .26f}, {.96f, .42f, .32f}, true},
    };

    state.anomalies = {
        {{42.f, 10.f}, 2.6f, 2.4f, true, false},
        {{42.f, 34.f}, 2.6f, -2.4f, true, false},
    };

    state.npcs.push_back({"kain", "카인", models::CharacterType::Guard, models::Gender::Male,
                          models::HairStyle::SidePart, {.38f, .44f, .50f, 1.f}, {5.8f, 23.2f}, {5.8f, 23.2f}, 0.f, 0.f,
                          -1, true, false, true});

    state.nodes.push_back({OpNodeKind::CoreControlPoint, 0, {25.f, 10.5f}, 2.4f,
                           "내부 제어점 A 해제 (북쪽 동조 단말)", {.38f, .92f, 1.f, 1.f},
                           state.controlPointsDisabled[0], true});
    state.nodes.push_back({OpNodeKind::CoreControlPoint, 1, {25.f, 33.5f}, 2.4f,
                           "내부 제어점 B 해제 (남쪽 동조 단말)", {.38f, .92f, 1.f, 1.f},
                           state.controlPointsDisabled[1], true});
    state.nodes.push_back({OpNodeKind::CoreReturnLine, 0, {7.5f, 22.f}, 2.8f,
                           "귀환선 탈출 (핵 정지 후 외부 제어소로 복귀)", {.32f, 1.f, .62f, 1.f},
                           state.returnConfirmed, true});

    int eid = 1;
    const Vector2 couplingPos[4] = {{31.0f, 19.2f}, {31.0f, 24.8f}, {37.0f, 19.2f}, {37.0f, 24.8f}};
    if (!state.coreStopped)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (i < state.couplingsDestroyed)
            {
                continue;
            }
            OpEnemy c;
            c.id = eid++;
            c.kind = OpEnemyKind::CoreCoupling;
            c.position = couplingPos[i];
            c.home = couplingPos[i];
            c.radius = .72f;
            c.health = 70;
            c.maxHealth = 70;
            c.couplingIndex = i;
            c.active = true;
            state.enemies.push_back(c);
        }
        state.enemies.push_back({eid++, OpEnemyKind::VoidSentinel, {24.f, 16.f}, {24.f, 16.f}, 3.14f, .50f, 82, 82});
        state.enemies.push_back({eid++, OpEnemyKind::VoidSentinel, {24.f, 28.f}, {24.f, 28.f}, 3.14f, .50f, 82, 82});
        state.enemies.push_back({eid++, OpEnemyKind::ObservatoryDrone, {38.f, 14.f}, {38.f, 14.f}, 3.14f, .46f, 65, 65});
    }
}

void StoryArc::RefreshNodeStates()
{
    for (auto &n : state.nodes)
    {
        switch (n.kind)
        {
        case OpNodeKind::WaterwayBeacon:
            n.completed = state.returnBeaconPlaced;
            break;
        case OpNodeKind::DistortionPylon:
            n.completed = state.stabilizedPylons > n.index;
            break;
        case OpNodeKind::SurveyTerminal:
            n.completed = state.surveyStationInvestigated;
            break;
        case OpNodeKind::OldCampRollCall:
            n.completed = state.oldCampInvestigated && state.kainRetreatRevealed;
            break;
        case OpNodeKind::RelaySwitch:
            n.completed = state.relaySwitchesRestored > n.index;
            break;
        case OpNodeKind::VeilVaultTerminal:
            n.completed = state.veilHypothesisFound && state.observatoryDocsFound;
            break;
        case OpNodeKind::CutoffActuator:
            n.completed = state.cutoffRelaysDisabled > n.index;
            break;
        case OpNodeKind::MainPowerBreaker:
            n.completed = state.cutoffEchoTriggered;
            break;
        case OpNodeKind::EvacDebrisBarricade:
            n.completed = state.evacRouteSecured;
            break;
        case OpNodeKind::ClosedHouseSideAlley:
            n.completed = state.evacSideRescueDone;
            break;
        case OpNodeKind::BackupWarehouseClue:
            n.completed = state.warehouseCluesFound > n.index;
            break;
        case OpNodeKind::BackupPowerConsole:
            n.completed = state.evacCommsConnected;
            n.visible = state.backupWarehouseRevealed || state.evacCommsConnected;
            break;
        case OpNodeKind::OuterControlPrep:
            n.completed = state.outerControlChecked;
            break;
        case OpNodeKind::GateThresholdPortal:
            n.visible = state.outerControlChecked;
            break;
        case OpNodeKind::ProofMarker:
            n.completed = state.proofMarkerRead;
            break;
        case OpNodeKind::ProofRadio:
            n.completed = state.proofLiveQuestion;
            break;
        case OpNodeKind::ProofScanner:
            n.completed = state.proofInstrumentGap;
            break;
        case OpNodeKind::BoundaryTestConsole:
            n.completed = state.boundaryTestDone;
            n.visible = state.missingExpeditionConfirmedAlive;
            break;
        case OpNodeKind::SurvivorExtract:
            n.completed = state.allSurvivorsReceived;
            n.visible = state.chapter3Complete && !state.allSurvivorsReceived;
            break;
        case OpNodeKind::AdvanceToCore:
            n.visible = state.allSurvivorsReceived;
            break;
        case OpNodeKind::CoreControlPoint:
            n.completed = n.index >= 0 && n.index < 2 && state.controlPointsDisabled[size_t(n.index)];
            break;
        case OpNodeKind::CoreReturnLine:
            n.completed = state.returnConfirmed;
            break;
        case OpNodeKind::ReturnGate:
            if (state.activeZone == OperationZone::HaloEvacuation)
            {
                n.visible = state.haloEvacuationComplete;
            }
            break;
        default:
            break;
        }
    }
}

void StoryArc::Update(float dt)
{
    if (state.epilogueTransitionSeconds > 0.f)
    {
        state.epilogueTransitionSeconds = (std::max)(0.f, state.epilogueTransitionSeconds - dt);
    }
    if (!IsActive())
    {
        return;
    }
    state.invulnerability = (std::max)(0.f, state.invulnerability - dt);
    state.scannerCooldown = (std::max)(0.f, state.scannerCooldown - dt);
    if (state.scannerPulseRadius > 0.f)
    {
        state.scannerPulseRadius += dt * 18.f;
        if (state.scannerPulseRadius > 34.f)
        {
            state.scannerPulseRadius = 0.f;
        }
    }

    if (state.activeZone == OperationZone::GateCore && !state.coreStopped)
    {
        state.pulseWaveTimer += dt;
        if (state.pulseWaveTimer >= 5.5f)
        {
            state.pulseWaveTimer = 0.f;
            state.pulseWaveRadius = 1.f;
        }
        if (state.pulseWaveRadius > 0.f)
        {
            const float prevR = state.pulseWaveRadius;
            state.pulseWaveRadius += dt * 9.5f;
            const auto &w = Game::Get().GetState();
            const float dist = Game::Distance(w.playerPosition, {34.f, 22.f});
            if (dist >= prevR - .45f && dist <= state.pulseWaveRadius + .45f && !w.playerMotion.mobility.Dodging())
            {
                DamagePlayer(10);
            }
            if (state.pulseWaveRadius > 22.f)
            {
                state.pulseWaveRadius = 0.f;
            }
        }
    }

    UpdateAnomalies(dt);
    UpdateEnemies(dt);
    UpdateNpcs(dt);
    UpdateBullets(dt);
    RefreshNodeStates();

    for (auto &tr : state.tracers)
    {
        tr.life -= dt;
    }
    state.tracers.erase(std::remove_if(state.tracers.begin(), state.tracers.end(),
                                       [](const OpTracer &t) { return t.life <= 0.f; }),
                        state.tracers.end());
}

void StoryArc::UpdateAnomalies(float dt)
{
    auto &w = Game::Get().GetState();
    for (auto &a : state.anomalies)
    {
        if (a.monolith)
        {
            const float toMono = std::atan2(a.position.y - w.playerPosition.y, a.position.x - w.playerPosition.x);
            const float diff = std::fabs(WrapAngle(toMono - w.facingYaw));
            if (diff > 1.25f)
            {
                a.yaw = std::atan2(w.playerPosition.y - a.position.y, w.playerPosition.x - a.position.x);
            }
        }
        else if (!a.stabilized && Game::Distance(w.playerPosition, a.position) < a.radius)
        {
            w.playerMotion.velocity.x *= (std::max)(0.f, 1.f - dt * 2.4f);
            w.playerMotion.velocity.y *= (std::max)(0.f, 1.f - dt * 2.4f);
        }
    }
}

void StoryArc::UpdateEnemies(float dt)
{
    const auto &w = Game::Get().GetState();
    for (auto &e : state.enemies)
    {
        if (!e.active || e.health <= 0)
        {
            continue;
        }
        e.hitFlash = (std::max)(0.f, e.hitFlash - dt * 4.f);
        if (e.kind == OpEnemyKind::CoreCoupling)
        {
            continue;
        }
        const float dist = Game::Distance(e.position, w.playerPosition);
        const bool sees = dist < 13.5f && HasLineOfSight(e.position, w.playerPosition);
        if (sees)
        {
            e.alertSeconds = 6.f;
        }
        else
        {
            e.alertSeconds = (std::max)(0.f, e.alertSeconds - dt);
        }
        if (e.alertSeconds <= 0.f)
        {
            continue;
        }
        const float aim = std::atan2(w.playerPosition.y - e.position.y, w.playerPosition.x - e.position.x);
        e.yaw = aim;
        const bool meleeOnly = e.kind == OpEnemyKind::RuinStalker;
        const float desiredDist = meleeOnly ? 0.9f : 6.2f;
        const float moveSpeed = meleeOnly ? 2.8f : (e.kind == OpEnemyKind::VoidSentinel ? 2.1f : 2.3f);
        if (dist > desiredDist)
        {
            const Vector2 step{e.position.x + std::cos(aim) * moveSpeed * dt,
                               e.position.y + std::sin(aim) * moveSpeed * dt};
            e.position = ResolveMovement(e.position, step, e.radius);
        }
        e.fireCooldown -= dt;
        if (meleeOnly)
        {
            if (dist < 1.25f && e.fireCooldown <= 0.f)
            {
                e.fireCooldown = 1.15f;
                DamagePlayer(12);
            }
        }
        else if (sees && dist < 12.f && e.fireCooldown <= 0.f)
        {
            e.fireCooldown = e.kind == OpEnemyKind::VoidSentinel ? 1.15f : 1.35f;
            const float speed = 13.f;
            state.bullets.push_back(
                {e.position, {std::cos(aim) * speed, std::sin(aim) * speed}, 1.3f, 9, false});
        }
    }
}

void StoryArc::UpdateNpcs(float dt)
{
    const auto &w = Game::Get().GetState();
    for (auto &npc : state.npcs)
    {
        if (!npc.visible)
        {
            continue;
        }
        if (npc.isCompanion)
        {
            const float side = npc.id == "ria" ? -1.4f : 1.4f;
            const Vector2 follow{w.playerPosition.x - std::cos(w.facingYaw) * 1.6f - std::sin(w.facingYaw) * side,
                                 w.playerPosition.y - std::sin(w.facingYaw) * 1.6f + std::cos(w.facingYaw) * side};
            const float d = Game::Distance(npc.position, follow);
            if (d > 1.2f)
            {
                const float ang = std::atan2(follow.y - npc.position.y, follow.x - npc.position.x);
                const float spd = (std::min)(4.2f, 1.8f + d * 0.9f);
                npc.position = ResolveMovement(
                    npc.position, {npc.position.x + std::cos(ang) * spd * dt, npc.position.y + std::sin(ang) * spd * dt},
                    .34f);
                npc.yaw = ang;
            }
            npc.fireCooldown = (std::max)(0.f, npc.fireCooldown - dt);
            if (npc.fireCooldown <= 0.f)
            {
                OpEnemy *best = nullptr;
                float bestD = 12.f;
                for (auto &e : state.enemies)
                {
                    if (!e.active || e.health <= 0 || e.kind == OpEnemyKind::CoreCoupling)
                    {
                        continue;
                    }
                    const float ed = Game::Distance(npc.position, e.position);
                    if (ed < bestD && HasLineOfSight(npc.position, e.position))
                    {
                        best = &e;
                        bestD = ed;
                    }
                }
                if (best)
                {
                    npc.fireCooldown = npc.id == "kain" ? .95f : 1.25f;
                    npc.yaw = std::atan2(best->position.y - npc.position.y, best->position.x - npc.position.x);
                    DamageEnemy(*best, npc.id == "kain" ? 14 : 10, npc.position);
                    state.tracers.push_back({npc.position, best->position, {.45f, .92f, 1.f, .9f}, .14f});
                }
            }
        }
        else if (npc.escorting)
        {
            const float d = Game::Distance(npc.position, npc.target);
            if (d > .6f)
            {
                const float ang = std::atan2(npc.target.y - npc.position.y, npc.target.x - npc.position.x);
                npc.yaw = ang;
                npc.position = ResolveMovement(
                    npc.position,
                    {npc.position.x + std::cos(ang) * 2.5f * dt, npc.position.y + std::sin(ang) * 2.5f * dt}, .32f);
            }
            else if (npc.survivorIndex >= 0 && npc.survivorIndex < 3 &&
                     state.survivors[size_t(npc.survivorIndex)] == SurvivorStatus::Crossing)
            {
                state.survivors[size_t(npc.survivorIndex)] = SurvivorStatus::Received;
                npc.escorting = false;
                npc.visible = false;
                const int received = ReceivedSurvivorCount();
                if (received >= 3)
                {
                    state.allSurvivorsReceived = true;
                    Dialogue::Start(
                        "MQ04A/all-received",
                        {{"s41_1", "윤서진 · 탐사 통신관", "외부 무전",
                          "먼지 냄새가 나요. 바람도 불고요. 여기 바깥 맞아요."},
                         {"s41_2", "카인", "현장",
                          "세 분 모두 외부 지원팀에 인수됐습니다. 확인되지 않은 나머지 대원은 계속 수색 기록으로 남기겠습니다."},
                         {"s41_3", "카인", "현장",
                          "이번에는 혼자 결정하고 돌아서지 않겠습니다. 두 제어점을 함께 봐야 합니다. 계획대로 같이 가겠습니다."}});
                }
                else
                {
                    Game::Get().ShowNotice(std::string(kSurvivorNames[npc.survivorIndex]) +
                                           " 외부 인수 완료 (" + std::to_string(received) + "/3)");
                }
            }
        }
    }
}

void StoryArc::UpdateBullets(float dt)
{
    const auto &w = Game::Get().GetState();
    for (auto &b : state.bullets)
    {
        if (b.life <= 0.f)
        {
            continue;
        }
        b.life -= dt;
        const Vector2 prev = b.position;
        const Vector2 next{b.position.x + b.velocity.x * dt, b.position.y + b.velocity.y * dt};
        if (IsPositionBlocked(next, .08f))
        {
            state.tracers.push_back({prev, next,
                                     b.playerOwned ? ColorRGBA{1.f, .72f, .28f, 1.f} : ColorRGBA{1.f, .38f, .30f, 1.f},
                                     .12f});
            b.life = 0.f;
            continue;
        }
        b.position = next;
        state.tracers.push_back({prev, next,
                                 b.playerOwned ? ColorRGBA{1.f, .72f, .28f, 1.f} : ColorRGBA{1.f, .38f, .30f, 1.f},
                                 .12f});
        if (b.playerOwned)
        {
            for (auto &e : state.enemies)
            {
                if (!e.active || e.health <= 0)
                {
                    continue;
                }
                if (Game::Distance(b.position, e.position) <= e.radius + .28f)
                {
                    DamageEnemy(e, b.damage, b.position);
                    b.life = 0.f;
                    break;
                }
            }
        }
        else if (Game::Distance(b.position, w.playerPosition) <= .46f && !w.playerMotion.mobility.Dodging())
        {
            DamagePlayer(b.damage);
            b.life = 0.f;
        }
    }
    state.bullets.erase(std::remove_if(state.bullets.begin(), state.bullets.end(),
                                       [](const OpBullet &b) { return b.life <= 0.f; }),
                        state.bullets.end());
}

void StoryArc::DamagePlayer(int amount)
{
    if (state.invulnerability > 0.f || HospitalRecovery::Active())
    {
        return;
    }
    auto &player = Game::Get().Player();
    const int reduced = (std::max)(1, amount - player.defense / 3);
    player.health = (std::max)(0, player.health - reduced);
    state.invulnerability = .45f;
    if (player.health <= 0)
    {
        Game::Get().ShowNotice("작전 중 부상으로 안전 거점 의료소로 긴급 이송됩니다.");
        HospitalRecovery::Begin();
    }
}

void StoryArc::DamageEnemy(OpEnemy &enemy, int damage, Vector2 from)
{
    if (!enemy.active || enemy.health <= 0)
    {
        return;
    }
    if (enemy.kind == OpEnemyKind::CoreCoupling &&
        (!state.controlPointsDisabled[0] || !state.controlPointsDisabled[1]))
    {
        Game::Get().ShowNotice("내부 제어점 A·B를 모두 해제해야 핵심 결합부를 파괴할 수 있습니다.");
        return;
    }
    enemy.health = (std::max)(0, enemy.health - damage);
    enemy.hitFlash = 1.f;
    enemy.alertSeconds = 8.f;
    (void)from;
    if (enemy.health <= 0)
    {
        enemy.active = false;
        if (enemy.kind == OpEnemyKind::CoreCoupling)
        {
            ++state.couplingsDestroyed;
            Game::Get().ShowNotice("관문 핵심 결합부 파괴 (" + std::to_string(state.couplingsDestroyed) + "/4)");
            CheckCouplingShutdown();
        }
        else
        {
            auto &player = Game::Get().Player();
            player.experience += 18;
            player.credits += 12;
        }
    }
}

void StoryArc::CheckCouplingShutdown()
{
    if (state.coreStopped || state.couplingsDestroyed < 4)
    {
        return;
    }
    state.coreStopped = true;
    state.pulseWaveRadius = 0.f;
    for (auto &e : state.enemies)
    {
        e.active = false;
        e.health = 0;
    }
    Dialogue::Start(
        "MQ04B/core-stopped",
        {{"s42_1", "리아", "외부 무전", "해냈어! 결합부 출력이 뚝 떨어졌어! 헤일로 쪽으로 번지던 왜곡이 멈췄어!"},
         {"s42_2", "카인", "현장", "녹색 귀환선이 살아 있습니다. 안쪽이 무너지기 전에 지금 빠져나갑시다!"}});
}

bool StoryArc::TryAttack(Vector2 origin, Vector2 target)
{
    if (!IsActive())
    {
        return false;
    }
    auto &loadout = Game::Get().Loadout();
    if (!loadout.IsFirearmEquipped())
    {
        return false;
    }
    auto &gun = Game::Get().ActiveFirearm();
    const float angle = std::atan2(target.y - origin.y, target.x - origin.x);
    combat::Shot shot;
    if (!gun.TryFire(angle, shot))
    {
        return false;
    }
    const int dmg = loadout.Selected() == combat::WeaponSlot::Primary ? 28 : 22;
    const float speed = 22.f;
    const float shotAngle = shot.angle;
    state.bullets.push_back(
        {origin, {std::cos(shotAngle) * speed, std::sin(shotAngle) * speed}, 1.1f, dmg, true});
    return true;
}

bool StoryArc::TryMelee(Vector2 origin, float yaw)
{
    if (!IsActive())
    {
        return false;
    }
    bool hit = false;
    for (auto &e : state.enemies)
    {
        if (!e.active || e.health <= 0)
        {
            continue;
        }
        const float d = Game::Distance(origin, e.position);
        if (d <= 1.85f + e.radius)
        {
            const float ang = std::atan2(e.position.y - origin.y, e.position.x - origin.x);
            if (std::fabs(WrapAngle(ang - yaw)) <= 1.35f)
            {
                DamageEnemy(e, 34, origin);
                hit = true;
            }
        }
    }
    return hit;
}

bool StoryArc::TriggerScanner()
{
    if (!IsActive() || state.scannerCooldown > 0.f)
    {
        return false;
    }
    const auto &w = Game::Get().GetState();
    state.scannerCooldown = 4.5f;
    state.scannerOrigin = w.playerPosition;
    state.scannerPulseRadius = 0.5f;
    if (state.activeZone == OperationZone::Waterway)
    {
        Game::Get().ShowNotice("스캐너 파동: 수로 왜곡 경계와 보정 표식·측량소/야영지 좌표를 표시했습니다.");
    }
    else if (state.activeZone == OperationZone::HaloEvacuation && !state.backupWarehouseRevealed)
    {
        state.backupWarehouseRevealed = true;
        RefreshNodeStates();
        Game::Get().ShowNotice("스캐너 파동: 북동쪽 예비 전원 창고의 전력 배선을 포착했습니다.");
    }
    else if (state.activeZone == OperationZone::GateInterior)
    {
        Game::Get().ShowNotice("스캐너 파동: 내부 야영지 생존자 3명의 생체 신호와 안전 경로를 표시했습니다.");
    }
    else if (state.activeZone == OperationZone::GateCore)
    {
        Game::Get().ShowNotice("스캐너 파동: 내부 제어점 2곳과 핵심 결합부 4곳, 녹색 귀환선 위치를 갱신했습니다.");
    }
    return true;
}

const OpNode *StoryArc::NearbyNode(Vector2 position, float maxDistance) const
{
    const OpNode *best = nullptr;
    float bestDist = maxDistance;
    for (const auto &n : state.nodes)
    {
        if (!n.visible)
        {
            continue;
        }
        const float d = Game::Distance(position, n.position);
        if (d <= (std::max)(maxDistance, n.radius) && d < bestDist)
        {
            best = &n;
            bestDist = d;
        }
    }
    return best;
}

std::string StoryArc::NearbyInteractionLabel(Vector2 position) const
{
    const auto *node = NearbyNode(position, 2.7f);
    return node ? node->label : std::string();
}

void StoryArc::OpenS22KainConfession()
{
    Dialogue::Start(
        "MQ02B/kain-confession",
        {{"s22_1", "카인", "현장",
          "잠깐—그 컵엔 손대지 마십시오. 포장재는 삭아 내렸는데 물에는 아직 김이 올라오고 있습니다. "
          "…통신원이 늘 앉아 있던 자리입니다."},
         {"s22_2", "카인", "현장",
          "사실 저는 실종된 탐사대의 경비원이었습니다. 이상 현상이 터졌을 때 철수 명령을 받았고… "
          "뒤에서 동료 발소리가 벽 안쪽으로 사라지는 걸 들으면서도 돌아섰습니다."},
         {"s22_3", "카인", "현장",
          "목소리는 분명 들렸습니다. 사람이 아직 거기 있었는지, 이미 사라진 뒤의 메아리였는지 지금도 "
          "확신하지 못합니다. 공식 보고서에는 그 이야기를 적지 않았습니다.",
          {{"understand", "그때 확인할 수 있는 사람부터 데리고 나온 거겠지."},
           {"criticize", "함께 들어올 거였다면 미리 말했어야 해."},
           {"focus_present", "지금 확인할 수 있는 것부터 찾자."}}}},
        [this](const std::string &choice) {
            state.oldCampInvestigated = true;
            state.kainRetreatRevealed = true;
            state.kainChoice = choice == "understand"  ? KainChoice::Understand
                               : choice == "criticize" ? KainChoice::Criticize
                                                       : KainChoice::FocusPresent;
            std::string reply =
                choice == "understand"
                    ? "저도 몇 년 동안 그렇게 믿으려 했습니다. 그렇다고 남겨 둔 이름까지 지워도 된다는 뜻은 아닌데 말입니다."
                : choice == "criticize"
                    ? "맞습니다. 판단하실 기회를 제가 숨겼습니다. 지금부터는 제 기억과 눈으로 확인한 사실을 반드시 나눠 말씀드리겠습니다."
                    : "알겠습니다. 변명은 여기까지 하죠. 점호 원본부터 챙기고 돌아갈 길부터 다시 짚겠습니다.";
            Dialogue::Start(
                "MQ02B/kain-closing",
                {{"s22_r1", "카인", "현장", reply},
                 {"s22_r2", "카인", "현장",
                  "다시 돌아오면 찾을 수 있을 줄 알았습니다. 그때는 길 자체가 지도에서 사라질 수도 있다는 걸 몰랐습니다."},
                 {"s22_r3", "리아", "현장",
                  "알았어요, 카인 씨. 이번엔 모르는 건 모른다고 말해 줘요. 그래야 우리가 같이 결정하죠."}});
            RefreshNodeStates();
        });
}

void StoryArc::OpenS23VeilComparison()
{
    Dialogue::Start(
        "MQ02C/veil-comparison",
        {{"s23_1", "리아", "현장",
          "잠깐만, 이 고대 도식 좀 봐… 목적지를 가리키는 화살표가 아니야. 텅 빈 구역의 테두리를 따라 "
          "선이 빙 둘러쳐져 있어. 출력을 올릴 때마다 주변 좌표가 틀어졌고."},
         {"s23_2", "리아", "현장",
          "어딘가로 보내는 문이 아니라… 바깥과 이곳 사이를 가려 두는 장막일 수도 있겠어. 아직 가설이지만, "
          "문을 억지로 열수록 그 경계가 얇아지는 거라면—"},
         {"s23_3", "단말 기록", "관측국 문서",
          "[심연 관측국 / 실종 구역 재고정 시험] 실종자를 되찾겠다며 도시 전력망을 시험에 끌어다 쓴 기록이다. "
          "원본에 있던 '확대 가동 즉시 중단 권고'는 최종 승인본에서 말끔히 삭제되어 있다."},
         {"s23_4", "카인", "현장",
          "경고를 못 본 게 아닙니다. 보고도 밀어붙인 겁니다. 지금 당장 헤일로 수신망에 원본을 올리고 "
          "마라 씨에게도 알려야 합니다."}},
        [this](const std::string &) {
            state.veilHypothesisFound = true;
            state.observatoryDocsFound = true;
            if (state.cityPhase == CityPhase::Normal)
            {
                state.cityPhase = CityPhase::Uneasy;
            }
            RefreshNodeStates();
        });
}

void StoryArc::OpenS25CommitChoice()
{
    Dialogue::Start(
        "MQ02D/commit-chapter3",
        {{"s25_1", "리아", "현장",
          "관측국 놈들, 또 출력을 올릴 모양이야. 외곽 차단 설비로 가서 도시 쪽 전력 선로부터 끊어 놓자. "
          "그것만 막아도 다음 시험은 늦출 수 있어."},
         {"s25_2", "카인", "현장",
          "출발하면 한동안 헤일로 시내로 돌아오기 어려워집니다. 탄약과 회복 키트, 남은 볼일부터 "
          "점검하십시오. 준비되시면 바로 움직이죠.",
          {{"depart", "외곽 차단 설비로 출발한다"}, {"prepare", "조금 더 준비한다"}}}},
        [this](const std::string &choice) {
            if (choice == "depart")
            {
                state.chapter3Committed = true;
                EnterZone(OperationZone::CutoffFacility);
            }
            else
            {
                Game::Get().ShowNotice("헤일로에서 정비를 마친 뒤 리아나 카인에게 말을 걸어 다시 출발할 수 있습니다.");
            }
        },
        true);
}

void StoryArc::OpenS33BackupPowerChoice()
{
    Dialogue::Start(
        "MQ03B/backup-priority",
        {{"s33_1", "리아", "현장",
          "예비 전원이 딱 하나뿐이야! 대피소 난방과 조명을 먼저 살릴지, 아니면 지금 들어오는 관문 수신 "
          "기록부터 복사할지 정해야 해. 남은 쪽은 내가 수동으로라도 이어 볼게."},
         {"s33_2", "마라", "무전",
          "아이들은 제가 챙길게요. 어떤 쪽을 먼저 묶으시든 우리가 버텨 볼 테니, 지금 더 급한 쪽부터 "
          "결정해 주세요!",
          {{"shelter_power", "대피소 전력부터 연결하자."}, {"signal_records", "수신 기록부터 보관하자."}}}},
        [this](const std::string &choice) {
            state.evacPriority = choice == "shelter_power" ? EvacuationPriority::ShelterPower
                                                           : EvacuationPriority::SignalRecords;
            state.evacCommsConnected = true;
            if (state.evacRouteSecured && state.evacSideRescueDone && state.evacCommsConnected)
            {
                state.haloEvacuationComplete = true;
                Game::Get().ShowNotice("헤일로 07구역 대피 완료 — 동쪽 외부 제어소로 이동할 수 있습니다.");
            }
            RefreshNodeStates();
        });
}

void StoryArc::TriggerSectorLossSequence()
{
    if (state.sectorLossTriggered)
    {
        return;
    }
    state.returnConfirmed = true;
    state.sectorLossTriggered = true;
    state.riaStatus = RiaStatus::Missing;
    state.cityPhase = CityPhase::Recovery;
    LeaveZoneToHalo(false);
    Dialogue::Start(
        "MQ04C/sector-loss",
        {{"s43_1", "카인", "현장", "경계를 넘었습니다! 두 명 다 무사합니다. 리아 씨, 이제 철수하십시오!"},
         {"s43_2", "리아", "외부 무전", "인계 끝났어, 나도 지금 나가— …잠깐, 바깥 지도가 왜 이래? 도로가—"},
         {"s43_3", "지원팀 경보", "무전",
          "외부 제어소와 헤일로 동부 구획이 계측에서 통째로 빠졌습니다! 반복합니다, 구획 좌표 소실!"},
         {"s43_4", "리아 · 마지막 수신", "수신 기록", "…여긴, 아직 빛이 있어."}},
        [](const std::string &) {
            Game::Get().ShowNotice("외부 안전선을 확인하고 지원팀 기록 회수 및 실종 접수를 진행하세요.");
        });
}

bool StoryArc::Interact()
{
    if (!IsActive())
    {
        return false;
    }
    const auto &w = Game::Get().GetState();
    const auto *node = NearbyNode(w.playerPosition, 2.7f);
    if (!node)
    {
        return false;
    }
    const auto kind = node->kind;
    const int idx = node->index;
    const auto &ch = game::GameLevels::Chapter().GetState();

    switch (kind)
    {
    case OpNodeKind::ReturnGate:
        if (state.activeZone == OperationZone::HaloEvacuation)
        {
            if (!state.haloEvacuationComplete)
            {
                Game::Get().ShowNotice("집결로 확보, 측면 구조, 예비 전원 연결을 먼저 완료하세요.");
                return true;
            }
            EnterZone(OperationZone::OuterControl);
            return true;
        }
        LeaveZoneToHalo(true);
        Game::Get().ShowNotice("헤일로로 귀환했습니다.");
        return true;

    case OpNodeKind::WaterwayBeacon:
        state.returnBeaconPlaced = true;
        Dialogue::Start(
            "MQ02A/beacon",
            {{"s20_1", "리아", "현장",
              ch.portableLightRepaired
                  ? "아까 같이 고쳐 둔 휴대 조명을 여기 교각에 묶어 둘게. 지도가 거짓말을 해도 이 불빛만 "
                    "보고 따라오면 헤일로로 돌아갈 수 있어."
                  : "다리가 통째로 없어졌네… 작업등을 여기 교각에 고정해 둘게. 강바닥 노면은 아직 남아 있어."},
             {"s20_2", "카인", "현장",
              "제 기억과 지형이 벌써 어긋납니다. 왜곡 구간은 보정 표식을 정렬하거나 스캐너(F)로 짚어 "
              "가며 움직이십시오. 북쪽 폐측량소와 남쪽 옛 야영지, 어느 쪽부터 보셔도 됩니다."}});
        RefreshNodeStates();
        return true;

    case OpNodeKind::DistortionPylon:
        if (state.stabilizedPylons <= idx)
        {
            state.stabilizedPylons = idx + 1;
            for (size_t i = 0; i < state.anomalies.size(); ++i)
            {
                if (int(i) <= state.stabilizedPylons)
                {
                    state.anomalies[i].stabilized = true;
                }
            }
            Game::Get().ShowNotice("왜곡 보정 표식 정렬 완료 (" + std::to_string(state.stabilizedPylons) + "/2)");
            RefreshNodeStates();
        }
        return true;

    case OpNodeKind::SurveyTerminal:
        if (HasNearbyHostiles(node->position, 8.f))
        {
            Game::Get().ShowNotice("주변 관측국 경비 드론을 먼저 제압해야 단말을 조사할 수 있습니다.");
            return true;
        }
        state.surveyStationInvestigated = true;
        Dialogue::Start(
            "MQ02A/survey-station",
            {{"s21_1", "리아", "현장",
              "기가 막히네… 문은 녹슬어 붙었고 센서도 뜯겨 있는데, 단말에는 '오늘 아침 점검 완료'라고 "
              "찍혀 있어. N-0/17로 전력을 넘긴 시각까지 고스란히 남아 있네."},
             {"s21_2", "카인", "현장",
              "발자국 하나 없는 방에 오늘 자 서명이 찍혔습니다. 서명만으로 누구 소행인지 단정할 수는 "
              "없으니, 남쪽 옛 야영지와 중계실 기록까지 맞춰 보죠."}});
        RefreshNodeStates();
        return true;

    case OpNodeKind::OldCampRollCall:
        if (HasNearbyHostiles(node->position, 8.f))
        {
            Game::Get().ShowNotice("야영지 주변의 위협을 먼저 정리하세요.");
            return true;
        }
        OpenS22KainConfession();
        return true;

    case OpNodeKind::RelaySwitch:
        if (!state.surveyStationInvestigated || !state.kainRetreatRevealed)
        {
            Game::Get().ShowNotice("폐측량소 조사와 옛 야영지 카인의 기록을 먼저 확인하세요.");
            return true;
        }
        if (state.relaySwitchesRestored <= idx)
        {
            state.relaySwitchesRestored = idx + 1;
            Game::Get().ShowNotice("중계실 보조 전원 복구 (" + std::to_string(state.relaySwitchesRestored) + "/2)");
            RefreshNodeStates();
        }
        return true;

    case OpNodeKind::VeilVaultTerminal:
        if (!state.surveyStationInvestigated || !state.kainRetreatRevealed)
        {
            Game::Get().ShowNotice("폐측량소와 옛 야영지 조사를 마친 뒤 중계실을 열 수 있습니다.");
            return true;
        }
        if (state.relaySwitchesRestored < 2)
        {
            Game::Get().ShowNotice("중계실 남북 보조 전원 2곳을 먼저 복구하세요.");
            return true;
        }
        if (HasNearbyHostiles(node->position, 8.f))
        {
            Game::Get().ShowNotice("중계실 입구의 관측국 경비를 먼저 제압하세요.");
            return true;
        }
        OpenS23VeilComparison();
        return true;

    case OpNodeKind::CutoffActuator:
        if (state.cutoffRelaysDisabled <= idx)
        {
            state.cutoffRelaysDisabled = idx + 1;
            Game::Get().ShowNotice("기동기 전력 선로 차단 (" + std::to_string(state.cutoffRelaysDisabled) + "/3)");
            RefreshNodeStates();
        }
        return true;

    case OpNodeKind::MainPowerBreaker:
        if (state.cutoffRelaysDisabled < 3)
        {
            Game::Get().ShowNotice("외곽 기동기 선로 3곳을 먼저 차단하세요.");
            return true;
        }
        state.cutoffEchoTriggered = true;
        state.cityPhase = CityPhase::Evacuation;
        for (auto &e : state.enemies)
        {
            e.active = false;
        }
        Dialogue::Start(
            "MQ03A/forced-activation",
            {{"s30_1", "관측국 현장 책임자", "현장",
              "도시 전력은 끊겼지만 늦었습니다! 독립 전원으로 이미 내부에 명령이 넘어갔습니다— 본부, "
              "확대 가동을 중단합니다. 응답하십시오!"},
             {"s30_2", "수신기 · 메아리", "수신 기록", "본부, 확대 가동을 중단합니다. 응답하십시오!"},
             {"s30_3", "카인", "현장",
              "방금 건 책임자 본인의 목소리가 되돌아온 겁니다… 저기, 헤일로 쪽 하늘과 통로가 뒤틀리고 "
              "있습니다. 관문보다 주민들부터 빼내야 합니다!"}},
            [this](const std::string &) { EnterZone(OperationZone::HaloEvacuation); });
        return true;

    case OpNodeKind::EvacDebrisBarricade:
        if (HasNearbyHostiles(node->position, 8.f))
        {
            Game::Get().ShowNotice("집결로를 막은 드론과 이상 개체를 먼저 제압하세요.");
            return true;
        }
        state.evacRouteSecured = true;
        for (auto &a : state.anomalies)
        {
            if (Game::Distance(a.position, node->position) < 6.f)
            {
                a.stabilized = true;
            }
        }
        Dialogue::Start(
            "MQ03B/plaza-route",
            {{"s31_1", "리아", "현장",
              w.isPowerRestored
                  ? "마라 씨 집 창문 불빛과 비상 유도등이 켜져 있어서 주민들이 광장으로 바로 모이고 있어!"
                  : "전력이 끊겨 어둡네. 작업등을 세워 뒀으니 광장 쪽으로 안내하자."},
             {"s31_2", "카인", "현장",
              ch.vehicleRepaired
                  ? "수리해 둔 운송 차량이 부상자와 물자를 싣고 출발했습니다. 이제 닫힌 집 측면과 예비 전원을 확인합시다."
                  : "우회로 잔해를 치웠습니다. 운전사가 들것으로 부상자를 옮기고 있으니 닫힌 집과 예비 전원을 확인합시다."}});
        if (state.evacRouteSecured && state.evacSideRescueDone && state.evacCommsConnected)
        {
            state.haloEvacuationComplete = true;
        }
        RefreshNodeStates();
        return true;

    case OpNodeKind::ClosedHouseSideAlley:
        if (HasNearbyHostiles(node->position, 7.5f))
        {
            Game::Get().ShowNotice("측면 통로의 위협을 먼저 제거하세요.");
            return true;
        }
        state.evacSideRescueDone = true;
        for (auto &npc : state.npcs)
        {
            if (npc.id == "trapped_0" || npc.id == "trapped_1")
            {
                npc.escorting = true;
            }
        }
        Dialogue::Start(
            "MQ03B/closed-house",
            {{"s32_1", "거주자", "현장", "안에 있어? 문 좀 열어 봐!"},
             {"s32_2", "닫힌 문 너머", "불명", "안에 있어? 문 좀 열어 봐!"},
             {"s32_3", "카인", "현장",
              "정문은 열지 마십시오. 측면 통로로 실제 거주자 두 분을 빼냈습니다. 목소리가 아닌 사람을 데리고 광장으로 이동합니다."}});
        if (state.evacRouteSecured && state.evacSideRescueDone && state.evacCommsConnected)
        {
            state.haloEvacuationComplete = true;
        }
        RefreshNodeStates();
        return true;

    case OpNodeKind::BackupWarehouseClue:
        if (state.warehouseCluesFound <= idx)
        {
            state.warehouseCluesFound = idx + 1;
            if (state.warehouseCluesFound >= 2)
            {
                state.backupWarehouseRevealed = true;
                Game::Get().ShowNotice("예비 전원 창고 위치 확인 완료! 북동쪽 창고로 이동하세요.");
            }
            else
            {
                Game::Get().ShowNotice("예비 배선 단서 확보 (1/2) — 스캐너(F)나 다음 단말로 창고를 특정하세요.");
            }
            RefreshNodeStates();
        }
        return true;

    case OpNodeKind::BackupPowerConsole:
        if (HasNearbyHostiles(node->position, 8.f))
        {
            Game::Get().ShowNotice("예비 전원 창고 주변의 오작동 드론을 먼저 제압하세요.");
            return true;
        }
        OpenS33BackupPowerChoice();
        return true;

    case OpNodeKind::OuterControlPrep:
        state.outerControlChecked = true;
        Dialogue::Start(
            "MQ03C/outer-control",
            {{"s34_1", "현장 지원 기술자", "현장",
              "예비 전원이랑 통신 중계기, 철수 표식까지 전부 잡아 뒀습니다. 안쪽이 흔들리면 주저 말고 "
              "바로 빼십시오. 뒤는 저희가 받습니다."},
             {"s34_2", "리아", "현장",
              "바깥 좌표는 내가 붙들고 있을게. 이상하면 나도 바로 무전 칠 거니까… 들어가서 본 걸 그대로 "
              "말해 줘. 이해는 돌아와서 같이 하자."}});
        RefreshNodeStates();
        return true;

    case OpNodeKind::GateThresholdPortal:
        state.gateEntryReached = true;
        EnterZone(OperationZone::GateInterior);
        return true;

    case OpNodeKind::ProofMarker:
        if (HasNearbyHostiles(node->position, 9.f))
        {
            Game::Get().ShowNotice("주변 공간 파수꾼을 먼저 제압하세요.");
            return true;
        }
        state.proofMarkerRead = true;
        Game::Get().ShowNotice("증거 1/3 확인: 윤서진 통신관이 새 표식 뒷면 번호를 정확히 읽었습니다.");
        break;

    case OpNodeKind::ProofRadio:
        if (HasNearbyHostiles(node->position, 9.f))
        {
            Game::Get().ShowNotice("주변 공간 파수꾼을 먼저 제압하세요.");
            return true;
        }
        state.proofLiveQuestion = true;
        Dialogue::Start(
            "MQ03D/proof-radio",
            {{"s35_1", "리아", "외부 무전",
              "좋아, 즉석으로 물을게. 지금 바깥에서 가장 먼저 떠오르는 단어—바람, 불, 돌 중에 하나만 골라 줘."},
             {"s35_2", "윤서진 · 탐사 통신관", "현장",
              "'바람'이요. 지금 바깥에서 물으신 거 맞죠? 여긴 공기가 아예 멈춰 있어서…"}});
        break;

    case OpNodeKind::ProofScanner:
        if (HasNearbyHostiles(node->position, 9.f))
        {
            Game::Get().ShowNotice("주변 공간 파수꾼을 먼저 제압하세요.");
            return true;
        }
        state.proofInstrumentGap = true;
        Dialogue::Start(
            "MQ03D/proof-scanner",
            {{"s35_3", "리아", "외부 무전",
              "목소리는 또렷하게 들리는데… 내 계측기에는 네 앞이 텅 빈 공간으로만 떠. 진짜 사람이 만져지는 거야?"},
             {"s35_4", "카인", "현장",
              "손을 잡고 있습니다. 셋 다 숨을 쉬고, 지금 묻는 말에 답합니다. …늦었습니다. 이번엔 제가 여기 있습니다."}});
        break;

    case OpNodeKind::BoundaryTestConsole:
        state.boundaryTestDone = true;
        state.chapter3Complete = true;
        Dialogue::Start(
            "MQ03D/boundary-test",
            {{"s36_1", "윤서진 · 탐사 통신관", "현장",
              "빈 표식은 경계를 넘어갔는데… 제가 줄을 매고 한 걸음 딛자마자 바깥 좌표가 끊기며 다시 제자리로 돌아왔어요."},
             {"s36_2", "미확인 채널", "무전 · 채널 B", "다음 표식에서 멈춰"},
             {"s36_3", "리아", "외부 무전 · 채널 A", "다음 표식에서 기다려"},
             {"s36_4", "카인", "현장",
              "두 채널 끝말이 달랐습니다. 우선 전원 이름부터 점호합니다. 리아 씨, 외부 기록에 현재 응답을 묶어 주십시오."}},
            [this](const std::string &) {
                int nextId = int(state.enemies.size()) + 10;
                state.enemies.push_back(
                    {nextId++, OpEnemyKind::VoidSentinel, {24.f, 22.f}, {24.f, 22.f}, 3.14f, .50f, 80, 80});
                RefreshNodeStates();
            });
        return true;

    case OpNodeKind::SurvivorExtract:
    {
        int nextIdx = -1;
        for (int i = 0; i < 3; ++i)
        {
            if (state.survivors[size_t(i)] == SurvivorStatus::Inside)
            {
                nextIdx = i;
                break;
            }
        }
        if (nextIdx >= 0)
        {
            state.survivors[size_t(nextIdx)] = SurvivorStatus::Crossing;
            for (auto &npc : state.npcs)
            {
                if (npc.survivorIndex == nextIdx)
                {
                    npc.escorting = true;
                    npc.target = {10.5f, 22.f};
                }
            }
            Game::Get().ShowNotice(
                std::string(kSurvivorNames[nextIdx]) + " 현재 응답 좌표 연동 시작 — 경계까지 엄호하세요!");
            if (nextIdx == 1)
            {
                int eid = int(state.enemies.size()) + 20;
                state.enemies.push_back(
                    {eid, OpEnemyKind::VoidSentinel, {26.f, 19.f}, {26.f, 19.f}, 3.14f, .50f, 82, 82});
            }
        }
        else
        {
            Game::Get().ShowNotice("이동 중인 탐사대원이 경계 지점에 도착할 때까지 엄호하세요.");
        }
        RefreshNodeStates();
        return true;
    }

    case OpNodeKind::AdvanceToCore:
        EnterZone(OperationZone::GateCore);
        return true;

    case OpNodeKind::CoreControlPoint:
        if (idx >= 0 && idx < 2 && !state.controlPointsDisabled[size_t(idx)])
        {
            state.controlPointsDisabled[size_t(idx)] = true;
            const int done = (state.controlPointsDisabled[0] ? 1 : 0) + (state.controlPointsDisabled[1] ? 1 : 0);
            if (done == 2)
            {
                Game::Get().ShowNotice("내부 제어점 2곳 모두 해제! 이제 관문 핵심 결합부 4곳을 사격해 파괴하세요!");
            }
            else
            {
                Game::Get().ShowNotice("내부 제어점 해제 (" + std::to_string(done) + "/2) — 반대편 제어점을 해제하세요.");
            }
            RefreshNodeStates();
        }
        return true;

    case OpNodeKind::CoreReturnLine:
        if (!state.coreStopped)
        {
            Game::Get().ShowNotice("내부 제어점 2곳을 해제하고 핵심 결합부 4곳을 파괴한 뒤 귀환하세요.");
            return true;
        }
        TriggerSectorLossSequence();
        return true;

    default:
        break;
    }

    if (state.proofMarkerRead && state.proofLiveQuestion && state.proofInstrumentGap &&
        !state.missingExpeditionConfirmedAlive)
    {
        state.missingExpeditionConfirmedAlive = true;
        Game::Get().ShowNotice("탐사대원 3명 생존 증명 완료 — 서쪽 경계 지점에서 횡단 시험을 진행하세요.");
    }
    RefreshNodeStates();
    return true;
}

bool StoryArc::InteractHaloReceiver()
{
    if (!IsActTwoUnlocked())
    {
        return false;
    }
    if (state.nereusSignalReceived)
    {
        Dialogue::Start(
            "MQE02/nereus-replay",
            {{"s52_rep", "수신 기록 · 리아의 음성", "네레우스 · 송신 시각 미확인",
              "수신 확인. 여기 조명이 하나 켜졌어. 누가 듣고 있으면…"}},
            {}, true);
        return true;
    }
    if (state.chapter4Complete)
    {
        if (state.epilogueShelterVisited && state.epilogueClinicVisited && state.epilogueWorkshopVisited)
        {
            OpenS52NereusChoice();
        }
        else
        {
            Dialogue::Start(
                "MQE01/receiver-status",
                {{"s50_st", "수신망 단말", "복구 현황",
                  "대피소(마라), 임시 의료소(구조 탐사대 3명), 정비소(카인·정비팀)의 복구 확인을 마친 뒤 17일간의 수신 기록을 정리할 수 있습니다."}},
                {}, true);
        }
        return true;
    }
    if (state.sectorLossTriggered && !state.chapter4Complete)
    {
        if (!state.safetyLineChecked)
        {
            InteractHaloSafetyLine();
            return true;
        }
        state.supportLogsRecovered = true;
        state.missingFiled = true;
        state.chapter4Complete = true;
        Dialogue::Start(
            "MQ04C/file-missing",
            {{"s44_1", "지원팀 기록", "단말",
              "외부 안전선 계측과 지원팀 기록 회수를 마쳤다. N-0의 확대는 멈췄고, 탐사대원 세 명은 무사히 "
              "인계됐다. 대신 헤일로 동부 구획과 외부 제어소가 지도에서 빠졌다."},
             {"s44_2", "마라", "현장", "…명부에 리아 씨 이름은 뭐라고 적어야 할까요."},
             {"s44_3", "카인", "현장", "실종으로 남겨 주십시오. 마지막으로 확인한 위치와 시각도 빠짐없이요."}},
            [](const std::string &) {
                Game::Get().ShowNotice("4장 완료 — 대피소·의료소·정비소에서 남은 사람들을 확인하세요.");
            });
        return true;
    }
    if (state.chapter3Committed)
    {
        const OperationZone next = state.resumeZone != OperationZone::None
                                       ? state.resumeZone
                                       : (state.allSurvivorsReceived     ? OperationZone::GateCore
                                          : state.gateEntryReached       ? OperationZone::GateInterior
                                          : state.haloEvacuationComplete ? OperationZone::OuterControl
                                          : state.cutoffEchoTriggered    ? OperationZone::HaloEvacuation
                                                                         : OperationZone::CutoffFacility);
        EnterZone(next);
        return true;
    }
    if (state.veilHypothesisFound && state.observatoryDocsFound && !state.chapter2Reported)
    {
        state.chapter2Reported = true;
        Dialogue::Start(
            "MQ02D/report-halo",
            {{"s24_1", "헤일로 수신망", "보고 접수",
              "외곽 수로 폐측량소·봉쇄 중계실 시험 기록과 관측국 승인 문서를 접수했습니다."},
             {"s24_2", "리아", "현장",
              "당국은 승인된 시험이라고만 하네… 하지만 보안대 일부는 대피로를 점검하기 시작했어. 마라 씨한테도 들렀다가 준비되면 차단 설비로 출발하자."}});
        return true;
    }
    if (state.chapter2Reported && !state.chapter3Committed)
    {
        OpenS25CommitChoice();
        return true;
    }
    Dialogue::Start(
        "MQ02A/depart-waterway",
        {{"s20_hub", "헤일로 수신망", "외곽 수로 경로",
          "1장 기록 대조로 열린 외곽 수로 진입 경로가 활성화되어 있습니다. 어디로 이동하시겠습니까?",
          {{"waterway", "2장 · 외곽 수로 조사 출발"},
           {"revisit", "1장 현장 재방문"},
           {"stay", "지금은 기록만 확인한다"}}}},
        [this](const std::string &choice) {
            if (choice == "waterway")
            {
                EnterZone(OperationZone::Waterway);
            }
            else if (choice == "revisit")
            {
                auto &g = Game::Get();
                auto &w = g.GetState();
                if (!w.surfaceExploration)
                {
                    game::GameLevels::Chapter().Enter();
                }
                else
                {
                    if (g.Surface() && g.Surface()->Ready())
                    {
                        w.planetDestination = g.Surface()->Stream().Snapshot()->planet->Region("frontier").access.position;
                    }
                    g.ShowNotice("1장 빛나는 개척지 좌표를 지도에 표시했습니다. 바이크·차량·비행체로 이동하세요.");
                }
            }
        },
        true);
    return true;
}

bool StoryArc::InteractHaloMara()
{
    if (state.sectorLossTriggered && !state.chapter4Complete)
    {
        return InteractHaloReceiver();
    }
    if (state.chapter4Complete)
    {
        state.epilogueShelterVisited = true;
        const auto &w = Game::Get().GetState();
        const auto &ch = game::GameLevels::Chapter().GetState();
        std::vector<DialogueLine> lines = {
            {"s50_m1", "마라", "대피소",
             "늘 놓던 자리에 컵을 하나 더 꺼냈어요. 치우려다… 그냥 뒀어요."},
            {"s50_m2", "마라", "대피소",
             w.isPowerRestored
                 ? "그때 고쳐 주신 집 창문 불빛 덕분에 사람들이 광장까지 헤매지 않았어요."
                 : "대피소 아이들 방 조명이 자꾸 깜빡여서 예비 부품으로 다시 손을 봐야 해요."},
        };
        if (ch.portableLightRepaired)
        {
            lines.push_back(
                {"s50_m3", "마라", "대피소",
                 "수로에서 회수해 온 리아 씨 휴대 조명은 대피소 탁자에 놓아두었어요. 켜고 싶으실 때 켜 두세요."});
        }
        Dialogue::Start("MQE01/mara-shelter", lines, {}, true);
        return true;
    }
    if (state.chapter2Reported && !state.chapter3Committed)
    {
        Dialogue::Start(
            "MQ02D/mara-date",
            {{"s24_m1", "마라", "현장",
              "어라, 벌써 또 나가세요? 어제도 같은 장비를 메고 수로 쪽으로 가시던데… 잠깐, 제 장부에 "
              "같은 물건을 두 번 산 줄이 왜 적혀 있죠? 제가 날짜를 헷갈렸나 봐요."},
             {"s24_m2", "리아", "현장",
              "마라 씨 착각이 아닐 수도 있어. 이쪽 시간까지 어긋나기 시작했네… 준비 끝나면 수신망에서 "
              "차단 설비로 바로 출발하자."}},
            {}, true);
        return true;
    }
    return false;
}

bool StoryArc::InteractHaloKain()
{
    if (state.chapter4Complete)
    {
        if (state.epilogueShelterVisited && state.epilogueClinicVisited && state.epilogueWorkshopVisited &&
            !state.nereusSignalReceived)
        {
            OpenS52NereusChoice();
            return true;
        }
        state.epilogueWorkshopVisited = true;
        state.epilogueRecoveryTaskDone = true;
        const auto &ch = game::GameLevels::Chapter().GetState();
        std::string kainChoiceLine =
            state.kainChoice == KainChoice::Understand
                ? "이해해 주신 말은 기억합니다. 확인하지 못한 이름까지 지우지는 않겠습니다."
            : state.kainChoice == KainChoice::Criticize
                ? "오늘 기록도 먼저 보여 드리겠습니다. 모르는 부분은 따로 표시했습니다."
                : "오늘 할 일은 셋입니다. 명단 대조, 물자 전달, 새 응답 확인.";
        std::string evacLine =
            state.evacPriority == EvacuationPriority::ShelterPower
                ? "대피소 전력을 먼저 묶어 둔 덕분에 보급은 여유가 있습니다. 일부 유실된 장거리 수신 로그는 복구 중입니다."
                : "수신 기록을 먼저 보존한 덕분에 관측국 단서가 온전합니다. 대피소 소모품 보급도 방금 마쳤습니다.";
        std::vector<DialogueLine> lines = {
            {"s51_k1", "카인", "정비소",
             "오늘 확인한 분들은 모두 답했습니다. 리아 씨 채널에는 새 응답이 없습니다. 내일도 확인하겠습니다."},
            {"s51_k2", "카인", "정비소", kainChoiceLine},
            {"s51_k3", "정비팀 · 기록관", "현장", evacLine},
            {"s51_k4", "운송대 · 기술자", "현장",
             (ch.vehicleRepaired ? "수리한 차량으로 임시 거처 이사를 돕고 있습니다. "
                                 : "남겨 둔 화물과 차량 소유자 확인 작업을 이어가고 있습니다. ") +
                 std::string(ch.detailLedgerFound
                                 ? "세부 장부 서명과 삭제된 중단 권고안을 대조해 증언자를 찾았습니다."
                                 : "관측국 내부에서 가동을 반대한 인물을 계속 추적하고 있습니다.")},
        };
        Dialogue::Start("MQE01/kain-workshop", lines, {}, true);
        return true;
    }
    if (IsActTwoUnlocked())
    {
        return InteractHaloReceiver();
    }
    return false;
}

bool StoryArc::InteractHaloSafetyLine()
{
    if (!state.sectorLossTriggered || state.chapter4Complete)
    {
        return false;
    }
    state.safetyLineChecked = true;
    Dialogue::Start(
        "MQ04C/safety-line",
        {{"s44_s1", "나", "없는 거리",
          "도로 표시선이 칼로 자른 듯 끊겨 있고 그림자도 맞물리지 않는다. 돌아가면 다른 거리가 나오지만, 외부 제어소가 있던 좌표로 이어지는 길은 어디에도 없다."},
         {"s44_s2", "카인", "현장",
          "마지막 수신을 다시 재생해도 같은 문장뿐입니다. 광장의 마라 씨에게 가서 지원팀 기록을 넘기고 실종 접수를 마칩시다."}});
    return true;
}

bool StoryArc::InteractHaloShelterLamp()
{
    if (!state.chapter4Complete || !game::GameLevels::Chapter().GetState().portableLightRepaired)
    {
        return false;
    }
    state.shelterLampOn = !state.shelterLampOn;
    Game::Get().ShowNotice(state.shelterLampOn ? "대피소 휴대 조명을 켰습니다." : "대피소 휴대 조명을 껐습니다.");
    return true;
}

bool StoryArc::InteractHaloEpilogueClinic()
{
    if (!state.chapter4Complete)
    {
        return false;
    }
    state.epilogueClinicVisited = true;
    Dialogue::Start(
        "MQE01/clinic-survivors",
        {{"s50_c1", "윤서진 · 탐사 통신관", "임시 의료소",
          "강도현 씨와 박민재 씨도 모두 안정을 찾았습니다. 리아 씨 소식은 들었어요… 우리가 받았던 응답 기록은 전부 남겨 두겠습니다."}},
        {}, true);
    return true;
}

bool StoryArc::InteractHaloEpilogueWorkshop()
{
    return InteractHaloKain();
}

void StoryArc::OpenS52NereusChoice()
{
    Dialogue::Start(
        "MQE02/day-skip-confirm",
        {{"s52_c", "헤일로 수신망", "기록 정리",
          "대피소·임시 의료소·정비소의 복구 기록이 모두 올라왔다. 지금까지의 수신 로그를 묶어 마감할까?",
          {{"finish", "수신 기록 정리를 마친다 (17일 후)"}, {"later", "아직 도시를 더 둘러본다"}}}},
        [this](const std::string &choice) {
            if (choice != "finish")
            {
                return;
            }
            state.epilogueDaySkipped = true;
            state.epilogueTransitionSeconds = 4.2f;
            const bool firstReception = !state.nereusSignalReceived;
            state.nereusSignalReceived = true;
            if (firstReception)
            {
                Dialogue::Start(
                    "MQE02/nereus-signal",
                    {{"s52_1", "수신망 로그", "붕괴 후 17일",
                      "다른 항성계 관문들에서도 위치 오차와 반복 통신이 보고되고 있습니다. 은하 규모의 장막 약화가 확인되었습니다."},
                     {"s52_2", "수신 기록 · 리아의 음성", "네레우스 / 미확인 행성 · 송신 시각 미확인",
                      "수신 확인. 여기 조명이 하나 켜졌어. 누가 듣고 있으면…"},
                     {"s52_3", "카인", "현장", "음성은 일치합니다. 송신 시각은 확인되지 않습니다."},
                     {"s52_4", "나", "현장", "새로 들어온 기록은 맞지?"},
                     {"s52_5", "카인", "현장",
                      "예. 오늘 처음 수신됐습니다. 어디서 온 건지부터 확인하죠."}},
                    [](const std::string &) {
                        Game::Get().ShowNotice("에필로그 완료 — 새 목표: 네레우스 신호의 경로를 조사할 준비를 한다");
                    });
            }
        },
        true);
}

bool StoryArc::HandleMedevacRecovery()
{
    if (!IsActive())
    {
        return false;
    }
    state.bullets.clear();
    state.tracers.clear();
    if (state.outerControlChecked &&
        (state.activeZone == OperationZone::GateInterior || state.activeZone == OperationZone::GateCore))
    {
        EnterZone(OperationZone::OuterControl);
        Game::Get().ShowNotice("외부 제어소 안전 거점 의료소에서 회복했습니다. 진행한 인수와 제어 상태는 유지됩니다.");
        return true;
    }
    LeaveZoneToHalo(true);
    return true;
}

void StoryArc::PrepareActTwoEntry()
{
    Reset();
    auto &ch = game::GameLevels::Chapter().GetState();
    ch.unlocked = true;
    ch.generatorInspected = ch.generatorRestored = true;
    ch.convoyBriefed = ch.convoyEvacuated = ch.convoyRescued = ch.kainJoined = true;
    ch.surveyMarkers = 3;
    ch.ledgerFound = true;
    ch.recordsCompared = true;
    ch.waterwayUnlocked = true;
    Game::Get().GetState().missions.Complete(MissionId::Frontier);
    EnterZone(OperationZone::Waterway);
}

void StoryArc::PrepareActThreeEntry()
{
    PrepareActTwoEntry();
    state.returnBeaconPlaced = true;
    state.stabilizedPylons = 2;
    state.surveyStationInvestigated = true;
    state.oldCampInvestigated = true;
    state.kainRetreatRevealed = true;
    state.kainChoice = KainChoice::Understand;
    state.relaySwitchesRestored = 2;
    state.veilHypothesisFound = true;
    state.observatoryDocsFound = true;
    state.chapter2Reported = true;
    state.chapter3Committed = true;
    state.cityPhase = CityPhase::Uneasy;
    EnterZone(OperationZone::CutoffFacility);
}

void StoryArc::PrepareActFourEntry()
{
    PrepareActThreeEntry();
    state.cutoffRelaysDisabled = 3;
    state.cutoffEchoTriggered = true;
    state.cityPhase = CityPhase::Evacuation;
    state.evacRouteSecured = true;
    state.evacSideRescueDone = true;
    state.evacCommsConnected = true;
    state.evacPriority = EvacuationPriority::ShelterPower;
    state.haloEvacuationComplete = true;
    state.outerControlChecked = true;
    state.gateEntryReached = true;
    state.proofMarkerRead = state.proofLiveQuestion = state.proofInstrumentGap = true;
    state.missingExpeditionConfirmedAlive = true;
    state.boundaryTestDone = true;
    state.chapter3Complete = true;
    state.survivors = {SurvivorStatus::Received, SurvivorStatus::Received, SurvivorStatus::Received};
    state.allSurvivorsReceived = true;
    EnterZone(OperationZone::GateCore);
}

void StoryArc::PrepareEpilogueEntry()
{
    PrepareActFourEntry();
    state.controlPointsDisabled = {true, true};
    state.couplingsDestroyed = 4;
    state.coreStopped = true;
    state.returnConfirmed = true;
    state.sectorLossTriggered = true;
    state.riaStatus = RiaStatus::Missing;
    state.cityPhase = CityPhase::Recovery;
    state.safetyLineChecked = true;
    state.supportLogsRecovered = true;
    state.missingFiled = true;
    state.chapter4Complete = true;
    LeaveZoneToHalo(false);
}

std::string StoryArc::ActiveZoneTitle() const
{
    switch (state.activeZone)
    {
    case OperationZone::Waterway:
        return "2장 · 외곽 수로";
    case OperationZone::CutoffFacility:
        return "3장 · 외곽 차단 설비";
    case OperationZone::HaloEvacuation:
        return "3장 · 헤일로 07구역 대피";
    case OperationZone::OuterControl:
        return "3장 · N-0 외부 제어소";
    case OperationZone::GateInterior:
        return state.chapter3Complete ? "4장 · 관문 N-0 내부 (생존자 인수)" : "3장 · 관문 N-0 내부";
    case OperationZone::GateCore:
        return "4장 · 관문 N-0 핵심 제어 구역";
    default:
        return "";
    }
}

std::string StoryArc::CurrentObjectiveTitle() const
{
    if (state.nereusSignalReceived)
    {
        return "에필로그 · 빈 채널";
    }
    if (state.chapter4Complete)
    {
        return "에필로그 · 복구된 헤일로";
    }
    if (state.chapter3Complete)
    {
        return "4장 · 닫히는 문";
    }
    if (state.chapter3Committed)
    {
        return "3장 · 관문이 열리는 밤";
    }
    if (IsActTwoUnlocked())
    {
        return "2장 · 돌아오지 않은 사람들";
    }
    return "";
}

std::string StoryArc::CurrentObjectiveAction() const
{
    if (state.nereusSignalReceived)
    {
        return "네레우스 신호의 경로를 조사할 준비를 한다";
    }
    if (state.chapter4Complete)
    {
        if (!state.epilogueShelterVisited || !state.epilogueClinicVisited || !state.epilogueWorkshopVisited)
        {
            return "대피소(마라)·의료소(탐사대)·정비소(카인) 방문";
        }
        return "전망대 콘솔 또는 카인과 17일 후 기록 정리 진행";
    }
    if (state.sectorLossTriggered)
    {
        return state.safetyLineChecked ? "광장의 마라에게 지원팀 기록 전달 및 실종 접수"
                                       : "헤일로 동부 소실 구획 안전선 확인";
    }
    if (state.coreStopped)
    {
        return "녹색 귀환선을 따라 외부 제어소로 즉시 탈출";
    }
    if (state.allSurvivorsReceived)
    {
        if (!state.controlPointsDisabled[0] || !state.controlPointsDisabled[1])
        {
            return "관문 내부 제어점 A·B 2곳 해제";
        }
        return "관문 핵심 결합부 4곳 사격 파괴 (" + std::to_string(state.couplingsDestroyed) + "/4)";
    }
    if (state.chapter3Complete)
    {
        return "안전 구획에서 생존자 3명 순차 귀환 인수 (" + std::to_string(ReceivedSurvivorCount()) + "/3)";
    }
    if (state.gateEntryReached)
    {
        if (!state.missingExpeditionConfirmedAlive)
        {
            const int p = (state.proofMarkerRead ? 1 : 0) + (state.proofLiveQuestion ? 1 : 0) +
                          (state.proofInstrumentGap ? 1 : 0);
            return "안쪽 야영지 탐사대 3명 생존 증거 확인 (" + std::to_string(p) + "/3)";
        }
        return "서쪽 경계 지점에서 횡단 시험 및 점호 진행";
    }
    if (state.haloEvacuationComplete)
    {
        return state.outerControlChecked ? "외부 제어소에서 관문 N-0 내부 진입"
                                         : "외부 제어소 예비 전원·통신·철수 표식 점검";
    }
    if (state.cutoffEchoTriggered)
    {
        const int done = (state.evacRouteSecured ? 1 : 0) + (state.evacSideRescueDone ? 1 : 0) +
                         (state.evacCommsConnected ? 1 : 0);
        return "헤일로 07구역 대피 완료 (집결로·측면 구조·예비 전원 " + std::to_string(done) + "/3)";
    }
    if (state.chapter3Committed)
    {
        return state.cutoffRelaysDisabled < 3
                   ? "외곽 차단 설비 기동기 선로 차단 (" + std::to_string(state.cutoffRelaysDisabled) + "/3)"
                   : "도시 전력 주 차단기 실행 및 독립 전원 확인";
    }
    if (state.chapter2Reported)
    {
        return "준비 완료 후 리아·카인 또는 N-0 외곽에서 3장 출발";
    }
    if (state.veilHypothesisFound && state.observatoryDocsFound)
    {
        return "헤일로의 리아·카인에게 수로 시험 기록 보고";
    }
    if (IsActTwoUnlocked())
    {
        if (state.activeZone != OperationZone::Waterway)
        {
            return "리아·카인 대화 또는 N-0 외곽으로 이동해 2장 시작";
        }
        if (!state.returnBeaconPlaced)
        {
            return "수로 입구 교각에 귀환 표식 설치";
        }
        if (!state.surveyStationInvestigated || !state.kainRetreatRevealed)
        {
            return "폐측량소 점검 단말 및 옛 야영지 조사 (자유 순서)";
        }
        if (state.relaySwitchesRestored < 2)
        {
            return "봉쇄 중계실 보조 전원 복구 (" + std::to_string(state.relaySwitchesRestored) + "/2)";
        }
        return "봉쇄 중계실에서 관문 도식과 관측국 문서 대조";
    }
    return "";
}

std::string StoryArc::CurrentObjectiveLocation() const
{
    if (IsActive())
    {
        return ActiveZoneTitle();
    }
    if (state.cityPhase == CityPhase::Recovery)
    {
        return "헤일로 · 복구 구역";
    }
    if (state.cityPhase == CityPhase::Evacuation)
    {
        return "헤일로 · 대피 구역";
    }
    return "헤일로 · 중앙 광장";
}
} // namespace story
