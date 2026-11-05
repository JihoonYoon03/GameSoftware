#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Game.h"
#include "Story.h"
#include "StoryJournal.h"
#include "CityFacilities.h"
#include "Civic.h"
#include "Hud.h"
#include "Save.h"
#include "RenderAssets.h"
#include "Overworld.h"
#include "ResidentAI.h"
#include "TransitLayout.h"
#include "Level.h"
#include "Scene.h"
#include "Renderer.h"
#include "Profiler.h"
#include "Monitoring/Monitor.h"
#include "Dependencies/glew/include/GL/glew.h"
#include "Window.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>

namespace tutorial
{
namespace
{
constexpr float kMovementSpeed = 3.2f;
constexpr float kMaximumDeltaSeconds = 0.05f;
constexpr float kCameraFollowRate = 5.0f;

// ============================================================================
// WeaponCursorCapture — confine the native cursor to the game's letterboxed canvas.
// ============================================================================
class WeaponCursorCapture
{
  public:
    ~WeaponCursorCapture()
    {
        Release();
    }
    bool Refresh()
    {
        if (window && (!IsWindow(window) || IsIconic(window) || GetForegroundWindow() != window))
        {
            Release();
            return false;
        }
        if (!window)
        {
            HWND candidate = GetActiveWindow();
            DWORD process = 0;
            GetWindowThreadProcessId(candidate, &process);
            // CPU/hidden rendering checks have no visible game window; never capture another app.
            if (!candidate || process != GetCurrentProcessId() || !IsWindowVisible(candidate) ||
                GetForegroundWindow() != candidate)
            {
                return true;
            }
            window = candidate;
            if (!GetClipCursor(&previous))
            {
                window = nullptr;
                return false;
            }
        }
        RECT client{};
        POINT origin{};
        if (!GetClientRect(window, &client) || !ClientToScreen(window, &origin))
        {
            Release();
            return false;
        }
        float scale = (std::min)(client.right / float(kCanvasWidth), client.bottom / float(kCanvasHeight));
        int width = (std::max)(1, int(kCanvasWidth * scale));
        int height = (std::max)(1, int(kCanvasHeight * scale));
        RECT next = {origin.x + (client.right - width) / 2, origin.y + (client.bottom - height) / 2, 0, 0};
        next.right = next.left + width;
        next.bottom = next.top + height;
        if (width > 2 && height > 2)
        {
            InflateRect(&next, -1, -1);
        }
        RECT current{};
        if ((!active || !EqualRect(&next, &bounds) || !GetClipCursor(&current) ||
             !EqualRect(&next, &current)) &&
            !ClipCursor(&next))
        {
            Release();
            return false;
        }
        bounds = next;
        active = true;
        return true;
    }
    void Release()
    {
        RECT current{};
        if (active && GetClipCursor(&current) && EqualRect(&current, &bounds))
        {
            // Preserve an earlier restriction, but never replace a different app's new capture.
            ClipCursor(&previous);
        }
        active = false;
        window = nullptr;
    }

  private:
    HWND window = nullptr;
    bool active = false;
    RECT previous{}, bounds{};
};
WeaponCursorCapture weaponCursorCapture;
} // namespace
Game::Game() = default;
Game::~Game() = default;
Game &Game::Get()
{
    static Game game;
    return game;
}
GameState &Game::GetState()
{
    return state;
}
bool QuestParticipation::Accept(MissionId id, bool eligible)
{
    auto &status = states.at(size_t(id));
    if (!eligible || status == MissionStatus::Completed || status == MissionStatus::Active)
    {
        return false;
    }
    status = MissionStatus::Active;
    if (tracked == MissionId::Count || !Active(tracked))
    {
        tracked = id;
    }
    return true;
}
bool QuestParticipation::Pause(MissionId id)
{
    auto &status = states.at(size_t(id));
    if (status != MissionStatus::Active)
    {
        return false;
    }
    status = MissionStatus::Paused;
    if (tracked == id)
    {
        tracked = MissionId::Count;
    }
    return true;
}
bool QuestParticipation::Complete(MissionId id)
{
    auto &status = states.at(size_t(id));
    if (status != MissionStatus::Active)
    {
        return false;
    }
    status = MissionStatus::Completed;
    if (tracked == id)
    {
        tracked = MissionId::Count;
    }
    return true;
}
bool QuestParticipation::Track(MissionId id)
{
    if (size_t(id) >= states.size() || !Active(id))
    {
        return false;
    }
    tracked = id;
    return true;
}
MissionId QuestParticipation::Tracked() const
{
    return tracked != MissionId::Count && Active(tracked) ? tracked : MissionId::Count;
}
const char *QuestParticipation::Key(MissionId id)
{
    switch (id)
    {
    case MissionId::Signal:
        return "signal";
    case MissionId::Mine:
        return "mine";
    case MissionId::Frontier:
        return "frontier";
    case MissionId::Mara:
        return "mara";
    default:
        return "unknown";
    }
}
const char *QuestParticipation::Name(MissionId id)
{
    switch (id)
    {
    case MissionId::Signal:
        return "전망대의 목소리";
    case MissionId::Mine:
        return "광산 구조 신호";
    case MissionId::Frontier:
        return "빛나는 개척지";
    case MissionId::Mara:
        return "남겨 둔 불빛";
    default:
        return "임무";
    }
}
const char *QuestParticipation::Label(MissionStatus status)
{
    switch (status)
    {
    case MissionStatus::Active:
        return "진행 중 · 보류";
    case MissionStatus::Paused:
        return "보류 · 재개";
    case MissionStatus::Completed:
        return "완료";
    default:
        return "미수락 · 제안 보기";
    }
}
// ============================================================================
// StoryMissionProgress — facts are observed freely; completion still requires active participation
// ============================================================================
const char *StoryMissionProgress::Restriction(MissionId id, const GameState &state,
                                              const game::MineState &mine, bool storyPlanet)
{
    if (!storyPlanet)
    {
        return "이 행성에서는 에르마의 임무를 수락할 수 없습니다.";
    }
    switch (id)
    {
    case MissionId::Signal:
    case MissionId::Mara:
        return nullptr;
    case MissionId::Mine:
        return state.gateSignalHeard && state.hasDescendedObservatory
                   ? nullptr
                   : "전망대 기록을 확보하고 지상으로 돌아와야 합니다.";
    case MissionId::Frontier:
        return mine.quest.GetStage() == game::MineQuestStage::Complete
                   ? nullptr
                   : "광산 좌표를 확보해 헤일로 수신망에 보고해야 합니다.";
    default:
        return "알 수 없는 임무입니다.";
    }
}
bool StoryMissionProgress::CanComplete(MissionId id, const GameState &state, const game::MineState &mine,
                                       const game::ChapterOneState &chapter, bool storyPlanet)
{
    if (!state.surfaceExploration || !storyPlanet || unsigned(id) >= unsigned(MissionId::Count) ||
        !state.missions.Active(id))
    {
        return false;
    }
    switch (id)
    {
    case MissionId::Signal:
        return state.gateSignalHeard && state.hasDescendedObservatory;
    case MissionId::Mine:
        return mine.quest.GetStage() == game::MineQuestStage::ReturnToHalo;
    case MissionId::Frontier:
        return chapter.generatorRestored && chapter.convoyRescued && chapter.ledgerFound &&
               !chapter.recordsCompared;
    case MissionId::Mara:
        return state.isPowerRestored && !state.isSideQuestComplete;
    default:
        return false;
    }
}
bool StoryMissionProgress::Complete(MissionId id, GameState &state, game::MineState &mine,
                                    game::ChapterOneState &chapter, bool storyPlanet)
{
    if (!CanComplete(id, state, mine, chapter, storyPlanet))
    {
        return false;
    }
    // No callbacks or world access between validation and commit; rejected reports change no facts.
    state.missions.Complete(id);
    switch (id)
    {
    case MissionId::Signal:
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::MountJetBike);
        break;
    case MissionId::Mine:
        mine.quest.Advance(game::MineQuestStage::ReturnToHalo);
        state.isComplete = true;
        state.mainQuestStage = MainQuestStage::Complete;
        break;
    case MissionId::Frontier:
        chapter.recordsCompared = chapter.waterwayUnlocked = true;
        break;
    case MissionId::Mara:
        state.isSideQuestComplete = true;
        break;
    default:
        break;
    }
    return true;
}

const char *Game::MissionRestriction(MissionId id) const
{
    return StoryMissionProgress::Restriction(id, state, game::GameLevels::Mine().GetState(), IsStoryWorld());
}
bool Game::CanCompleteMission(MissionId id) const
{
    return StoryMissionProgress::CanComplete(id, state, game::GameLevels::Mine().GetState(),
                                             game::GameLevels::Chapter().GetState(), IsStoryWorld());
}
bool Game::CompleteMission(MissionId id)
{
    if (!StoryMissionProgress::Complete(id, state, game::GameLevels::Mine().GetState(),
                                        game::GameLevels::Chapter().GetState(), IsStoryWorld()))
    {
        return false;
    }
    ActivityJournal::Refresh();
    return true;
}
bool Game::MissionEligible(MissionId id) const
{
    return MissionRestriction(id) == nullptr;
}
MissionId Game::FocusedMission() const
{
    if (ActivityJournal::TrackedCommunity())
    {
        return MissionId::Count;
    }
    if (!IsStoryWorld())
    {
        return MissionId::Count;
    }
    auto selected = state.missions.Tracked();
    if (selected != MissionId::Count)
    {
        return selected;
    }
    if (IsSurfaceMine() && state.missions.Active(MissionId::Mine))
    {
        return MissionId::Mine;
    }
    if (game::GameLevels::Chapter().IsActive() && state.missions.Active(MissionId::Frontier))
    {
        return MissionId::Frontier;
    }
    for (size_t i = 0; i < size_t(MissionId::Count); ++i)
    {
        if (state.missions.Active(MissionId(i)))
        {
            return MissionId(i);
        }
    }
    return MissionId::Count;
}
const char *Game::MissionObjective() const
{
    if (!IsStoryWorld())
    {
        return "행성 자유 탐사 · 중계환 발견·등록 / M 지도";
    }
    const auto focused = FocusedMission();
    if (focused == MissionId::Signal)
    {
        return !state.homeMessageRead   ? "집 단말에서 측정 요청 확인 [E]"
               : !state.gateSignalHeard ? "전망대에서 신호 기록 조사 [E]"
                                        : "전망대 승강기로 지상 복귀 [E]";
    }
    if (focused == MissionId::Mine)
    {
        const auto &quest = game::GameLevels::Mine().GetState().quest;
        return quest.GetStage() == game::MineQuestStage::TravelToMine && !quest.IsRiaJoined()
                   ? "리아에게 광산 동행 요청 [E]"
                   : quest.GetObjective();
    }
    if (focused == MissionId::Frontier)
    {
        return game::GameLevels::Chapter().Objective();
    }
    if (focused == MissionId::Mara)
    {
        return state.isPowerRestored ? "마라에게 전력 복구 보고 [E]" : "동쪽 전력 장치 복구 [E]";
    }
    return "자유 탐사 · J에서 임무 제안 확인";
}
ActivityTrackingState ActivityJournal::CaptureTracking(const GameState &state,
                                                       const PlanetCommunityState &community,
                                                       const world::PlanetDefinition &planet)
{
    ActivityTrackingState result;
    if (planet.storyPlanet)
    {
        result.mission = state.missions.Tracked();
    }
    world::RelayNetwork identity;
    identity.Bind(planet);
    if (state.trackedCommunityRegion.empty() || state.trackedCommunityWorldKey != identity.WorldKey() ||
        community.worldKey != identity.WorldKey())
    {
        return result;
    }
    for (const auto &site : community.sites)
    {
        if (site.regionId == state.trackedCommunityRegion && CommunityTaskProgress::IsActive(site))
        {
            result.communityWorldKey = identity.WorldKey();
            result.communityRegion = site.regionId;
            break;
        }
    }
    return result;
}
void ActivityJournal::RestoreTracking(GameState &state, const ActivityTrackingState &tracking,
                                      const PlanetCommunityState &community,
                                      const world::PlanetDefinition &planet)
{
    state.missions.ClearTracking();
    if (planet.storyPlanet && unsigned(tracking.mission) < unsigned(MissionId::Count) &&
        state.missions.Active(tracking.mission))
    {
        state.missions.Track(tracking.mission);
    }
    state.trackedCommunityRegion.clear();
    state.trackedCommunityWorldKey.clear();
    state.journalLocalActivities = false;
    state.journalActivityPage = 0;
    world::RelayNetwork identity;
    identity.Bind(planet);
    if (tracking.communityRegion.empty() || tracking.communityWorldKey != identity.WorldKey() ||
        community.worldKey != identity.WorldKey())
    {
        return;
    }
    for (const auto &site : community.sites)
    {
        if (site.regionId == tracking.communityRegion && CommunityTaskProgress::IsActive(site))
        {
            state.trackedCommunityRegion = site.regionId;
            state.trackedCommunityWorldKey = identity.WorldKey();
            state.journalLocalActivities = true;
            int preceding = 0;
            for (const auto &candidate : community.sites)
            {
                if (CommunityTaskProgress::IsActive(candidate) && candidate.regionId < site.regionId)
                {
                    ++preceding;
                }
            }
            state.journalActivityPage = preceding / LocalEntriesPerPage;
            return;
        }
    }
}
const CommunitySite *ActivityJournal::TrackedCommunity()
{
    auto &g = Game::Get();
    const auto &id = g.GetState().trackedCommunityRegion;
    const auto *community = g.SurfaceCommunity();
    if (!g.GetState().surfaceExploration || !g.Surface() || !g.Surface()->Ready() || !community ||
        id.empty() || community->State().worldKey.empty() ||
        g.GetState().trackedCommunityWorldKey != community->State().worldKey)
    {
        return nullptr;
    }
    for (const auto &site : community->State().sites)
    {
        if (site.regionId == id && CommunityTaskProgress::IsActive(site))
        {
            return &site;
        }
    }
    return nullptr;
}
bool ActivityJournal::TrackMission(MissionId id)
{
    auto &g = Game::Get();
    if (!g.IsStoryWorld() || !g.GetState().missions.Track(id))
    {
        return false;
    }
    g.GetState().trackedCommunityRegion.clear();
    g.GetState().trackedCommunityWorldKey.clear();
    return true;
}
bool ActivityJournal::TrackCommunity(const std::string &id)
{
    auto &g = Game::Get();
    const auto *community = g.SurfaceCommunity();
    if (!g.GetState().surfaceExploration || !g.Surface() || !g.Surface()->Ready() || !community ||
        community->State().worldKey.empty())
    {
        return false;
    }
    for (const auto &site : community->State().sites)
    {
        if (site.regionId == id && CommunityTaskProgress::IsActive(site))
        {
            g.GetState().trackedCommunityRegion = id;
            g.GetState().trackedCommunityWorldKey = community->State().worldKey;
            return true;
        }
    }
    return false;
}
void ActivityJournal::Refresh()
{
    auto &w = Game::Get().GetState();
    if (!TrackedCommunity())
    {
        w.trackedCommunityRegion.clear();
        w.trackedCommunityWorldKey.clear();
    }
}
std::vector<size_t> ActivityJournal::CommunityEntries()
{
    std::vector<size_t> result;
    auto &g = Game::Get();
    const auto *community = g.SurfaceCommunity();
    if (!g.GetState().surfaceExploration || !g.Surface() || !g.Surface()->Ready() || !community)
    {
        return result;
    }
    const auto &sites = community->State().sites;
    for (size_t i = 0; i < sites.size(); ++i)
    {
        if (CommunityTaskProgress::IsActive(sites[i]))
        {
            result.push_back(i);
        }
    }
    std::sort(result.begin(), result.end(),
              [&](size_t a, size_t b) { return sites[a].regionId < sites[b].regionId; });
    return result;
}
ActivityObjective ActivityJournal::Current()
{
    auto &g = Game::Get();
    const auto &w = g.GetState();
    ActivityObjective goal;
    goal.action = g.MissionObjective();
    if (!w.surfaceExploration || !g.Surface() || !g.Surface()->Ready())
    {
        return goal;
    }
    const auto plan = g.Surface()->Stream().Snapshot()->planet;
    if (const auto *site = TrackedCommunity())
    {
        goal.communityRegion = site->regionId;
        goal.title = PlanetCommunity::TaskName(*site);
        goal.location = plan->Region(site->regionId).definition.name;
        goal.action = site->task == CommunityTaskProgress::Accepted ? "현장 센서에서 측정 기록 확보 [E]"
                                                                    : "현장 접수 단말에 측정 기록 제출 [E]";
        goal.surfaceTarget = site->task == CommunityTaskProgress::Accepted ? site->target : site->contact;
    }
    else
    {
        goal.mission = g.FocusedMission();
        if (goal.mission == MissionId::Count)
        {
            if (g.IsSurfaceMine())
            {
                goal.action = "광산 자유 탐사 · 기록 보관";
            }
            return goal;
        }
        goal.title = QuestParticipation::Name(goal.mission);
        auto city = [&](Vector2 point) {
            goal.location = "헤일로";
            if (g.SurfaceCities())
            {
                goal.surfaceTarget = g.SurfaceCities()->Point(*g.Surface(), 0, point);
            }
        };
        const auto &mine = game::GameLevels::Mine().GetState();
        switch (goal.mission)
        {
        case MissionId::Signal:
            city(!w.homeMessageRead ? kHomeDoor : kBeaconPosition);
            if (!w.homeMessageRead)
            {
                goal.location = "헤일로 · 집 단말";
            }
            else
            {
                goal.location = "헤일로 · 전망대";
            }
            break;
        case MissionId::Mara:
            city(w.isPowerRestored ? kMaraPosition : kRelayPosition);
            break;
        case MissionId::Mine:
            switch (mine.quest.GetStage())
            {
            case game::MineQuestStage::ShowRecord:
                city(kLiaPosition);
                break;
            case game::MineQuestStage::CheckCityRelay:
                city(kCityReceiverPosition);
                break;
            case game::MineQuestStage::TravelToMine:
                if (!mine.quest.IsRiaJoined())
                {
                    city(kLiaPosition);
                    break;
                }
                [[fallthrough]];
            default:
                if (mine.quest.GetStage() == game::MineQuestStage::ReturnToHalo)
                {
                    city(kCityReceiverPosition);
                }
                else
                {
                    goal.location = "광산";
                    goal.surfaceTarget = plan->Region("mq00_mine").access.position;
                    if (!g.IsSurfaceMine())
                    {
                        goal.action = "광산 입구로 이동 · 하차 후 입장 [E]";
                    }
                }
                break;
            }
            if (g.IsSurfaceMine())
            {
                switch (mine.quest.GetStage())
                {
                case game::MineQuestStage::BypassPower:
                    goal.mineTarget = game::kPowerBypass;
                    break;
                case game::MineQuestStage::InvestigateSignal:
                    goal.mineTarget = game::kRuinSignal;
                    break;
                case game::MineQuestStage::InvestigateGlyph:
                    goal.mineTarget = game::kObserverGlyph;
                    break;
                case game::MineQuestStage::CollectCoordinates:
                    goal.mineTarget = game::kCoordinateTerminal;
                    break;
                default:
                    break;
                }
            }
            break;
        case MissionId::Frontier: {
            const auto &chapter = game::GameLevels::Chapter();
            const auto &field = chapter.GetState();
            if (chapter.CompletedSites() == 3 || field.comparisonStarted)
            {
                city(kCityReceiverPosition);
                break;
            }
            goal.location = "빛나는 개척지";
            goal.surfaceTarget = plan->Region("frontier").access.position;
            if (chapter.IsActive())
            {
                // Concurrent field sites stay optional; choose the closest unfinished required action.
                double best = std::numeric_limits<double>::infinity();
                for (const auto &point : chapter.Interactions())
                {
                    using A = game::ChapterAction;
                    if (point.action == A::Exit || point.action == A::Vehicle ||
                        point.action == A::PortableLight || point.action == A::TradeProof ||
                        point.action == A::DetailLedger || point.action == A::ConvoySupply ||
                        point.action == A::Mission)
                    {
                        continue;
                    }
                    auto target = chapter.Surface().Point(point.position);
                    double distance = world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters,
                                                                       g.Surface()->Pose().origin, target);
                    if (distance < best)
                    {
                        best = distance;
                        goal.surfaceTarget = target;
                        goal.action = point.label;
                    }
                }
                if (field.escortStarted && !field.convoyRescued)
                {
                    for (const auto &person : field.survivors)
                    {
                        if (!person.arrived && person.combat.health > 0)
                        {
                            goal.surfaceTarget = chapter.Surface().Point(person.position);
                            break;
                        }
                    }
                    goal.action = chapter.Objective();
                }
            }
            break;
        }
        default:
            break;
        }
    }
    // Exterior objectives route via the current interior's actual exit, without changing participation.
    if (g.IsSurfaceMine() && !goal.mineTarget)
    {
        goal.mineTarget = game::GameLevels::Mine().GetState().entry;
        goal.action = "광산 출구로 이동 [E]";
    }
    if (w.isInsideHome && !(goal.mission == MissionId::Signal && !w.homeMessageRead))
    {
        goal.action = "집 밖으로 이동해 추적 중인 활동 계속 [E]";
    }
    if (w.isInObservatory && !(goal.mission == MissionId::Signal && w.homeMessageRead))
    {
        goal.action = "전망대 승강기로 지상 복귀 [E]";
    }
    return goal;
}
void Game::OfferMission(MissionId id, const char *channel)
{
    if (unsigned(id) >= unsigned(MissionId::Count) || !state.surfaceExploration || !IsStoryWorld() ||
        Dialogue::Active() || state.dialogue.suspended || HospitalRecovery::Active())
    {
        return;
    }
    if (state.missions.Status(id) == MissionStatus::Completed || state.missions.Active(id))
    {
        return;
    }
    if (!MissionEligible(id))
    {
        ShowNotice(std::string("조사 조건 / ") + MissionRestriction(id));
        return;
    }
    state.journalVisible = state.dialogueHistoryVisible = false;
    Dialogue::Start(
        std::string("mission/proposal/") + QuestParticipation::Key(id),
        StoryDialogue::Proposal(id, state, game::GameLevels::Mine().GetState(),
                                game::GameLevels::Chapter().GetState(), channel),
        [this, id](const std::string &choice) {
            if (choice != "accept")
            {
                ShowNotice("제안 보관 / 지역은 계속 탐사할 수 있습니다. J 조사 기록이나 제안자에게서 다시 "
                           "시작하세요.");
                return;
            }
            if (state.missions.Accept(id, MissionEligible(id)))
            {
                if (id == MissionId::Mine)
                {
                    game::GameLevels::Mine().GetState().quest.Advance(game::MineQuestStage::ShowRecord);
                }
                if (id == MissionId::Frontier)
                {
                    game::GameLevels::Chapter().Unlock();
                }
                if (id == MissionId::Mara)
                {
                    state.isSideQuestAccepted = true;
                }
                SyncMissionProgress();
                ShowNotice(std::string("임무 시작 / ") + QuestParticipation::Name(id) +
                           " · J에서 보류/재개 가능");
            }
        },
        true);
}
void Game::ManageMission(MissionId id)
{
    if (Dialogue::Active() || state.dialogue.suspended || HospitalRecovery::Active())
    {
        ShowNotice("진행 중인 대화를 마치거나 이력에서 재개한 뒤 임무 참여를 변경하세요.");
        return;
    }
    if (state.missions.Pause(id))
    {
        ShowNotice(std::string("임무 보류 / ") + QuestParticipation::Name(id) +
                   " · 발견 기록·보유품·현장 상태는 유지됩니다.");
    }
    else
    {
        OfferMission(id);
    }
}
void Game::SyncMissionProgress()
{
    if (!state.surfaceExploration || !IsStoryWorld())
    {
        return;
    }
    if (state.missions.Active(MissionId::Signal))
    {
        auto stage = !state.homeMessageRead           ? MainQuestStage::ReadHomeTerminal
                     : !state.gateSignalHeard         ? MainQuestStage::EnterObservatory
                     : !state.hasDescendedObservatory ? MainQuestStage::DescendObservatory
                                                      : MainQuestStage::MountJetBike;
        state.mainQuestStage = (std::max)(state.mainQuestStage, stage);
        CompleteMission(MissionId::Signal);
    }
    game::GameLevels::Mine().SyncInvestigation();
    game::GameLevels::Chapter().SyncEvidence();
}
Vector2 WorldCamera::ToViewport(Vector2 p)
{
    return {kCanvasWidth * .5f + (p.x - kCanvasWidth * .5f) * zoom,
            kCanvasHeight * .57f + (p.y - kCanvasHeight * .57f) * zoom};
}
Vector2 WorldCamera::FromViewport(Vector2 p)
{
    return {kCanvasWidth * .5f + (p.x - kCanvasWidth * .5f) / zoom,
            kCanvasHeight * .57f + (p.y - kCanvasHeight * .57f) / zoom};
}
void MovementFacing::Update(Vector2 from, Vector2 to)
{
    const float dx = to.x - from.x, dy = to.y - from.y;
    walking = dx * dx + dy * dy > 1.e-10f;
    if (walking)
    {
        yaw = std::atan2(dy, dx);
    }
}
Vector2 MotionMath::Approach(Vector2 velocity, Vector2 target, float maximumChange)
{
    float dx = target.x - velocity.x, dy = target.y - velocity.y;
    float distance = std::hypot(dx, dy);
    float t = distance > 0 ? (std::min)(1.f, (std::max)(0.f, maximumChange) / distance) : 0;
    return {velocity.x + dx * t, velocity.y + dy * t};
}
float MotionMath::Turn(float yaw, float target, float maximumChange)
{
    return std::remainder(
        yaw + Game::Clamp(std::remainder(target - yaw, 2 * kPi), -maximumChange, maximumChange), 2 * kPi);
}
Vector2 MotionMath::ContactVelocity(Vector2 velocity, Vector2 requested, Vector2 actual)
{
    Vector2 normal = {requested.x - actual.x, requested.y - actual.y};
    float length = std::hypot(normal.x, normal.y);
    // Substepped world coordinates accumulate float rounding; that is not a wall contact.
    float tolerance = .0002f + std::hypot(requested.x, requested.y) * .002f;
    if (length > tolerance)
    {
        normal.x /= length;
        normal.y /= length;
        float inward = (std::max)(0.f, velocity.x * normal.x + velocity.y * normal.y);
        velocity.x -= normal.x * inward;
        velocity.y -= normal.y * inward;
    }
    return velocity;
}
void PlayerMotion::Update(Vector2 target, float &yaw, bool aiming, float dt)
{
    if (dt <= 0)
    {
        return;
    }
    constexpr float response = MotionMath::kResponse;
    auto before = velocity;
    float speed = std::hypot(velocity.x, velocity.y), goal = std::hypot(target.x, target.y);
    float alignment =
        speed > .01f && goal > .01f ? (velocity.x * target.x + velocity.y * target.y) / (speed * goal) : 1;
    if (!aiming && speed > 1.2f && goal > .01f && alignment < -.55f)
    {
        pivoting = true;
    }
    if (aiming || goal < .01f || (speed > .4f && alignment > .5f))
    {
        pivoting = false;
    }
    if (goal > .01f && !aiming)
    {
        float heading = std::atan2(target.y, target.x);
        // Plant/brake first during a reversal; finish the turn before pushing off.
        float rate = pivoting && speed > .7f ? 5.f : 12.f;
        yaw = MotionMath::Turn(yaw, heading, rate * response * dt);
        float facing = std::cos(std::remainder(heading - yaw, 2 * kPi));
        if (pivoting && speed < .4f && facing > .85f)
        {
            pivoting = false;
        }
        float scale = pivoting ? 0.f : .2f + .8f * (std::max)(0.f, facing);
        target.x *= scale;
        target.y *= scale;
    }
    if (pivoting)
    {
        target = {};
    }
    bool braking = goal < .01f || pivoting || alignment < .25f || goal < speed;
    velocity = MotionMath::Approach(velocity, target, (braking ? 22.f : 14.f) * response * dt);
    if (std::hypot(velocity.x, velocity.y) < .008f && goal < .01f)
    {
        velocity = {};
    }
    Vector2 acceleration = {(velocity.x - before.x) / dt, (velocity.y - before.y) / dt};
    float cy = std::cos(yaw), sy = std::sin(yaw), blend = 1 - std::exp(-18 * dt);
    float forward = Game::Clamp((cy * acceleration.x + sy * acceleration.y) * .004f, -.09f, .07f);
    float side = Game::Clamp((-sy * acceleration.x + cy * acceleration.y) * -.003f, -.07f, .07f);
    forwardLean += (forward - forwardLean) * blend;
    sideLean += (side - sideLean) * blend;
}
void GameState::StopMotion()
{
    playerMotion = {};
    bikeVelocity = {};
    bikeYawSpeed = bikeSpeed = movementAmount = movementSpeed = 0;
}
bool CityLayout::IsRoadTile(int x, int y)
{
    return Overworld::IsRoad(x, y);
}
bool CityLayout::OverlapsRoad(const Building &b, float clearance)
{
    if (TransitLayout::OverlapsPavement({b.x, b.y}, {b.x + b.width, b.y + b.depth}, clearance))
    {
        return true;
    }
    for (int x = int(std::floor(b.x - clearance)); x <= int(std::floor(b.x + b.width + clearance)); ++x)
    {
        for (int y = int(std::floor(b.y - clearance)); y <= int(std::floor(b.y + b.depth + clearance)); ++y)
        {
            if (RoadNetwork::Get().ContainsFootprint({x + .5f, y + .5f}))
            {
                return true;
            }
        }
    }
    return false;
}
bool CityLayout::IsQuestSpace(Vector2 p, float clearance)
{
    auto inside = [=](Vector2 minimum, Vector2 maximum) {
        return p.x >= minimum.x - clearance && p.x <= maximum.x + clearance && p.y >= minimum.y - clearance &&
               p.y <= maximum.y + clearance;
    };
    return inside(kPlazaMin, kPlazaMax) || inside(kBikeAccessMin, kBikeAccessMax);
}
bool CityLayout::OverlapsQuestSpace(const Building &b, float clearance)
{
    auto overlaps = [&](Vector2 minimum, Vector2 maximum) {
        return b.x - clearance < maximum.x && b.x + b.width + clearance > minimum.x &&
               b.y - clearance < maximum.y && b.y + b.depth + clearance > minimum.y;
    };
    return overlaps(kPlazaMin, kPlazaMax) || overlaps(kBikeAccessMin, kBikeAccessMax);
}
void CityLayout::ValidateBuildings(std::vector<Building> &buildings)
{
    std::vector<Building> valid;
    for (const auto &b : buildings)
    {
        if (OverlapsRoad(b) || OverlapsQuestSpace(b))
        {
            continue;
        }
        bool overlaps = false;
        for (const auto &other : valid)
        {
            overlaps = overlaps || (b.x < other.x + other.width + .2f && b.x + b.width > other.x - .2f &&
                                    b.y < other.y + other.depth + .2f && b.y + b.depth > other.y - .2f);
        }
        if (!overlaps)
        {
            valid.push_back(b);
        }
    }
    buildings.swap(valid);
}
unsigned char Game::NormalizeKey(unsigned char key)
{
    if (key >= 'A' && key <= 'Z')
    {
        return key + ('a' - 'A');
    }
    return key;
}
void Game::UpdatePlayerMovement(float deltaSeconds)
{
    profiling::Scope profileTimer(profiling::Timer::Movement);
    const Vector2 previous = state.playerPosition;
    // A mounted rider's body reaction must not disable the vehicle's controls.
    if (!state.isBikeMounted && CivicJustice::IsExterior() &&
        CivicJustice::State().playerImpact.seconds > 0 &&
        CivicJustice::State().playerImpact.response == BodyResponse::Knockdown)
    {
        state.StopMotion();
        return;
    }
    state.movementSpeed = 0;
    if (state.isBikeMounted &&
        (!game::GameLevels::Active() || (state.surfaceExploration && !IsSurfaceMine())) &&
        !state.isInsideHome && !state.isInObservatory)
    {
        Overworld::UpdateBike(state, deltaSeconds);
        return;
    }
    float screenX = (state.pressedKeys['d'] ? 1.f : 0) - (state.pressedKeys['a'] ? 1.f : 0);
    float screenY = (state.pressedKeys['s'] ? 1.f : 0) - (state.pressedKeys['w'] ? 1.f : 0);
    float inputLength = std::sqrt(screenX * screenX + screenY * screenY);
    if (inputLength > 0)
    {
        screenX /= inputLength;
        screenY /= inputLength;
    }
    if (inputLength > 0 && std::fabs(screenX) > std::fabs(screenY))
    {
        state.facingDirection = screenX < 0 ? 1 : 2;
    }
    else if (inputLength > 0)
    {
        state.facingDirection = screenY < 0 ? 3 : 0;
    }
    // Invert the 2:1 isometric projection so WASD follows screen directions.
    Vector2 target = {screenX * .5f + screenY, screenY - screenX * .5f};
    float length = std::hypot(target.x, target.y);
    if (length > 0)
    {
        target.x *= kMovementSpeed / length;
        target.y *= kMovementSpeed / length;
    }
    state.playerMotion.Update(target, state.facingYaw, IsAiming(), deltaSeconds);
    Vector2 requested = {state.playerMotion.velocity.x * deltaSeconds,
                         state.playerMotion.velocity.y * deltaSeconds};
    Vector2 candidate = {previous.x + requested.x, previous.y + requested.y};
    state.playerPosition =
        BodyCollision::Slide(previous, candidate, [this](Vector2 p) { return IsPositionBlocked(p); });
    state.playerPosition = BodyCollision::MovePlayer(previous, state.playerPosition);
    state.playerMotion.velocity = MotionMath::ContactVelocity(
        state.playerMotion.velocity, requested,
        {state.playerPosition.x - previous.x, state.playerPosition.y - previous.y});
    state.movementSpeed = deltaSeconds > 0 ? Distance(previous, state.playerPosition) / deltaSeconds : 0;
    state.movementAmount = Game::Clamp(state.movementSpeed / kMovementSpeed, 0, 1);
}
void Game::UpdateCamera(float deltaSeconds)
{
    Vector2 target = (state.isInsideHome || state.isInObservatory) ? Vector2{0, 0} : state.playerPosition;
    float followAmount = 1 - std::exp(-kCameraFollowRate * deltaSeconds);
    state.cameraPosition.x += (target.x - state.cameraPosition.x) * followAmount;
    state.cameraPosition.y += (target.y - state.cameraPosition.y) * followAmount;
}
void Game::Reset()
{
    SaveSystem::ResetCampaign();
    StopSurface();
    WeaponWheel::Close();
    game::SceneManager::Reset();
    game::GameLevels::Reset();
    // Keep the current window size when restarting the play session.
    const int viewportWidth = state.viewportWidth;
    const int viewportHeight = state.viewportHeight;
    const UiPreferences preferences = state.uiPreferences;
    state = GameState{};
    frameSchedule = {};
    state.uiPreferences = preferences;
    state.viewportWidth = viewportWidth;
    state.viewportHeight = viewportHeight;
    state.lastUpdateMilliseconds = platform::ElapsedMilliseconds();
    // Only narrative landmarks precede the road plan; ordinary lots are generated from its frontages.
    Building home{kHomeBuildingPosition.x, kHomeBuildingPosition.y, 1, 1,
                  7.2f * models::WorldScale::pixelsPerMeter};
    home.type = models::BuildingType::Habitat;
    home.palette.body = {.3f, .42f, .5f, 1};
    home.palette.accent = {.32f, .7f, .78f, 1};
    home.palette.emission = {.25f, .75f, .85f, 1};
    state.buildings = {home};
    Building observatory{kObservatoryBuildingPosition.x, kObservatoryBuildingPosition.y, 5, 4,
                         11.5f * models::WorldScale::pixelsPerMeter};
    observatory.type = models::BuildingType::Observatory;
    observatory.palette.body = {.46f, .55f, .6f, 1};
    observatory.palette.accent = {.3f, .76f, .86f, 1};
    observatory.palette.emission = {.42f, .9f, 1, 1};
    state.buildings.push_back(observatory);
    Overworld::Populate(state);
    CivicJustice::Reset(state);
    ShowNotice("리아 · 수신 메시지 / 좋은 아침. 전망대 측정을 좀 도와줄래? 집 단말에 내용을 보냈어. 밖에서 "
               "기다릴게.");
}
void Game::StartSurface(bool insideHome)
{
    const auto planet = world::WorldCatalog::Erma();
    state.relayNetwork.Bind(planet);
    auto spawn =
        world::LegacyWorldAdapter::ErmaExterior()
            .Locate("halo_07", insideHome ? world::Vector3d{kHomeDoor.x, kHomeDoor.y, 0} : world::Vector3d{})
            .position;
    state.surfaceExploration = true;
    state.isInsideHome = insideHome;
    state.playerPosition = insideHome ? Vector2{2, 2} : Vector2{};
    state.cameraPosition = {};
    surface = std::make_unique<world::SurfaceSession>(planet, spawn);
    surfaceBike = world::LegacyWorldAdapter::ErmaExterior()
                      .Locate("halo_07", {kJetBikePosition.x, kJetBikePosition.y, 0})
                      .position;
    auto bike = surface->Local(*surfaceBike);
    state.bikePosition = {float(bike.x / 2), float(bike.y / 2)};
    StoreSurfaceBikeHeading();
    surfaceCities = std::make_unique<PlanetCities>();
    surfaceTraffic = std::make_unique<PlanetTraffic>();
    surfaceParty = std::make_unique<PlanetParty>();
    surfaceCommunity = std::make_unique<PlanetCommunity>();
    surface->Start(
        world::PlanetGenerator::Erma(Overworld::StoryAnchors()).WithCityCores(PlanetCities::Describe(state)));
}
void Game::StartNewGame()
{
    Reset();
    StartSurface(true);
    ShowNotice("헤일로 / 임무는 대화의 제안을 수락해 시작합니다. 집을 나와 자유롭게 탐사할 수 있습니다. J "
               "조사 기록.");
}
void Game::Resize(int width, int height)
{
    state.viewportWidth = (std::max)(width, 1);
    state.viewportHeight = (std::max)(height, 1);
    WeaponWheel::RefreshCursorCapture();
    MouseMotion(int(state.cursorWindowPosition.x), int(state.cursorWindowPosition.y));
}
const char *Game::StageName(TestStage stage)
{
    switch (stage)
    {
    case TestStage::Prologue:
        return "서장 · 헤일로 시작";
    case TestStage::Observatory:
        return "전망대 · 스테이지 시작";
    case TestStage::BikeUnlocked:
        return "바이크 해금 · 광산 출발";
    case TestStage::WeaponUnlocked:
        return "권총 해금 · 광산 입구";
    case TestStage::Mine:
        return "광산 · 스테이지 시작";
    case TestStage::ChapterOne:
        return "1장 · 빛나는 개척지 시작";
    case TestStage::SurfaceSurvey:
        return "행성 자유 탐사 · 도보 / 바이크";
    default:
        return "";
    }
}
bool Game::EnterTestStage(TestStage stage)
{
    if (stage < TestStage::Prologue || stage >= TestStage::Count)
    {
        return false;
    }
    Reset();
    game::GameLevels::PrepareTestEntry(stage);
    if (stage == TestStage::Prologue)
    {
        StartSurface(true);
    }
    else if (stage == TestStage::Observatory)
    {
        state.isInsideHome = false;
        state.isInObservatory = true;
        state.playerPosition = {0, 2};
    }
    else if (stage == TestStage::BikeUnlocked)
    {
        state.isInsideHome = false;
        state.isBikeMounted = true;
        state.playerPosition = state.bikePosition;
    }
    else if (stage == TestStage::WeaponUnlocked || stage == TestStage::Mine)
    {
        state.isInsideHome = false;
        state.mineReturnPosition = {Overworld::kMineEntrance.x - 1, Overworld::kMineEntrance.y};
        state.playerPosition = state.mineReturnPosition;
        state.bikePosition = {Overworld::kMineEntrance.x - 3, Overworld::kMineEntrance.y};
        if (stage == TestStage::Mine)
        {
            game::GameLevels::Mine().Enter();
        }
    }
    else if (stage == TestStage::ChapterOne)
    {
        game::GameLevels::Chapter().Enter();
    }
    else if (stage == TestStage::SurfaceSurvey)
    {
        StartSurface(false);
    }
    state.cameraPosition = state.playerPosition;
    ShowNotice(std::string("테스트 진입 / ") + StageName(stage) + " · 이전 필수 진행 완료 / 기본 무장: 맨손");
    return true;
}

// ============================================================================
// WeaponWheel
// ============================================================================
bool WeaponWheel::Open()
{
    auto &w = Game::Get().GetState();
    if (w.weaponWheel.visible || w.isPaused || w.mapVisible || w.journalVisible || w.dialogueHistoryVisible ||
        Dialogue::Active() || w.dialogue.suspended || HospitalRecovery::Active() || w.isBikeMounted ||
        !w.cursorInsideViewport)
    {
        return false;
    }
    auto &wheel = w.weaponWheel;
    wheel.visible = true;
    wheel.qHeld = true;
    // Keep room to point in every direction, even when Q is pressed at a window edge.
    wheel.origin = {Game::Clamp(w.cursorPosition.x, 96, kCanvasWidth - 96),
                    Game::Clamp(w.cursorPosition.y, 96, kCanvasHeight - 96)};
    wheel.offset = {};
    wheel.hovered = game::GameLevels::Mine().GetState().loadout.Selected();
    w.mouseFiring = w.mouseAiming = false;
    w.StopMotion();
    std::fill(std::begin(w.pressedKeys), std::end(w.pressedKeys), false);
    w.pressedKeys['q'] = true;
    RefreshCursorCapture();
    return wheel.visible;
}
void WeaponWheel::RefreshCursorCapture()
{
    auto &w = Game::Get().GetState();
    if (w.weaponWheel.visible && !weaponCursorCapture.Refresh())
    {
        Close();
        w.weaponWheel.qHeld = false;
    }
}
void WeaponWheel::Motion()
{
    auto &w = Game::Get().GetState();
    auto &wheel = w.weaponWheel;
    if (!wheel.visible)
    {
        return;
    }
    wheel.offset = {w.cursorPosition.x - wheel.origin.x, w.cursorPosition.y - wheel.origin.y};
    if (std::hypot(wheel.offset.x, wheel.offset.y) < 24)
    {
        wheel.hovered = game::GameLevels::Mine().GetState().loadout.Selected();
        return;
    }
    // Three equal sectors: up = primary, lower-left = hands, lower-right = secondary.
    float angle = std::atan2(wheel.offset.y, wheel.offset.x);
    wheel.hovered = angle >= -5 * kPi / 6 && angle < -kPi / 6 ? combat::WeaponSlot::Primary
                    : wheel.offset.x < 0                      ? combat::WeaponSlot::Unarmed
                                                              : combat::WeaponSlot::Secondary;
}
void WeaponWheel::Close(bool commit)
{
    weaponCursorCapture.Release();
    auto &w = Game::Get().GetState();
    if (!w.weaponWheel.visible)
    {
        return;
    }
    if (commit)
    {
        game::GameLevels::Mine().GetState().loadout.Select(w.weaponWheel.hovered);
    }
    w.weaponWheel.visible = false;
    w.mouseFiring = w.mouseAiming = false;
    std::fill(std::begin(w.pressedKeys), std::end(w.pressedKeys), false);
}
void Game::KeyDown(unsigned char key, int, int)
{
    if (SaveSystem::Loading())
    {
        if (key == 27)
        {
            SaveSystem::Cancel();
        }
        return;
    }
    if (HospitalRecovery::Active())
    {
        return;
    }
    key = NormalizeKey(key);
    if (state.relayPreparing)
    {
        if (key == 27 || key == 'x')
        {
            RelayTravel::Cancel();
        }
        return;
    }
    if (key == 'q')
    {
        if (state.weaponWheel.qHeld)
        {
            return;
        }
        state.weaponWheel.qHeld = true;
    }
    if (state.weaponWheel.visible)
    {
        if (key == 27)
        {
            WeaponWheel::Close();
        }
        return;
    }
    if (key == 13)
    {
        if (state.dialogueEnterHeld)
        {
            return;
        }
        state.dialogueEnterHeld = true;
    }
    if (key == 'e' && state.interactionHeld)
    {
        return;
    }
    if (Hud::HandleKey(key))
    {
        if (key == 'e')
        {
            state.interactionHeld = true;
        }
        return;
    }
    if (key == 27) // Escape
    {
        if (state.journalVisible || state.dialogueHistoryVisible)
        {
            state.journalVisible = state.dialogueHistoryVisible = false;
            return;
        }
        if (state.mapVisible)
        {
            state.mapVisible = false;
            return;
        }
        state.isPaused = !state.isPaused;
        state.StopMotion();
        state.mouseFiring = state.mouseAiming = false;
        std::fill(state.pressedKeys, state.pressedKeys + 256, false);
        return;
    }
    bool repeated = state.pressedKeys[key];
    if (key == 'e' && state.interactionHeld)
    {
        return;
    }
    state.pressedKeys[key] = true;
    if (key == 'e')
    {
        state.interactionHeld = true;
    }
    if (repeated && (key == 'e' || key == 13 || key == 'j' || key == 'h' || key == 'm'))
    {
        return;
    }
    if (state.mapVisible && MapHud::HandleMapKey(key))
    {
        return;
    }
    if (state.surfaceExploration && !IsSurfaceMine() && !Dialogue::Active() && !state.journalVisible &&
        !state.dialogueHistoryVisible && key != 'j' && key != 'h')
    {
        if (key == 'm')
        {
            state.mapVisible = !state.mapVisible;
            state.StopMotion();
        }
        else if ((key == '[' || key == ']') && !repeated && !state.mapVisible && !IsSurfaceMine() &&
                 !state.isInsideHome && !state.isInObservatory && surface && surface->Ready())
        {
            const auto plan = surface->Stream().Snapshot()->planet;
            if (!plan->regions.empty())
            {
                auto count = plan->regions.size();
                auto current = surfaceSurveyRegion.value_or(0);
                if (!surfaceSurveyRegion)
                {
                    auto region = surface->Location().regionId;
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        if (plan->regions[i].definition.id == region)
                        {
                            current = i;
                        }
                    }
                }
                auto next = key == ']' ? (current + 1) % count : (current + count - 1) % count;
                // An unavailable landing must not trap the developer's region browser on one entry.
                surfaceSurveyRegion = next;
                if (RelocateSurfaceSurvey(plan->regions[next].access.position))
                {
                    ShowNotice("개발용 배치 확인 / " + plan->regions[next].definition.name +
                               " · 임무 진행은 바뀌지 않습니다.");
                }
                else
                {
                    ShowNotice("해당 지역 입구 주변에서 안전한 착지점을 찾지 못했습니다.");
                }
            }
        }
        else if (key == 'e' && !state.mapVisible && surface && surface->Ready())
        {
            InteractWithNearbyTarget();
        }
        else if (key == 'v' && !repeated && game::GameLevels::Chapter().IsActive())
        {
            game::GameLevels::Chapter().Melee();
        }
        else if (key == 'r' && game::GameLevels::Chapter().IsActive())
        {
            game::GameLevels::Mine().Combat().Reload();
        }
        else if (key == 'g' && !repeated && game::GameLevels::Chapter().IsActive())
        {
            game::GameLevels::Mine().UseRecoveryKit();
        }
        else if (key == 'q' && !repeated)
        {
            WeaponWheel::Open();
        }
        return;
    }
    if (key == 'j' || key == 'h')
    {
        bool opening = key == 'j' ? !state.journalVisible : !state.dialogueHistoryVisible;
        state.journalVisible = key == 'j' && opening;
        state.dialogueHistoryVisible = key == 'h' && opening;
        state.dialogue.historyOffset = 0;
        state.StopMotion();
        state.mapVisible = false;
        state.mouseFiring = state.mouseAiming = false;
        std::fill(std::begin(state.pressedKeys), std::end(state.pressedKeys), false);
        state.pressedKeys[key] = true;
        return;
    }
    if (state.journalVisible || state.dialogueHistoryVisible)
    {
        if (key == 13 && state.dialogue.suspended)
        {
            Dialogue::Resume();
        }
        return;
    }
    if (Dialogue::Active())
    {
        if (key == 'e' || key == 13)
        {
            Dialogue::Advance();
            // Start/transition callbacks clear movement keys; retain the advance latch until key-up.
            state.pressedKeys[key] = true;
            if (key == 'e')
            {
                state.interactionHeld = true;
            }
            return;
        }
        if (key >= '1' && key <= '9')
        {
            if (!repeated)
            {
                Dialogue::Select(size_t(key - '1'));
            }
            return;
        }
        if (key == 'm' || Dialogue::LocksWorld())
        {
            state.pressedKeys[key] = false;
            return;
        }
    }
    if (key == 'm')
    {
        state.mapVisible = !state.mapVisible;
        if (state.mapVisible && IsSurfaceMine())
        {
            state.planetMapSurface = false;
        }
        state.StopMotion();
        state.mouseFiring = state.mouseAiming = false;
        std::fill(std::begin(state.pressedKeys), std::end(state.pressedKeys), false);
        state.pressedKeys['m'] = true;
        return;
    }
    if (state.mapVisible)
    {
        return;
    }
    if (key == 'q')
    {
        if (!repeated)
        {
            WeaponWheel::Open();
        }
        return;
    }
    if (key == 'v' && !repeated)
    {
        if (game::GameLevels::Mine().IsActive())
        {
            game::GameLevels::Mine().Combat().Melee();
        }
        else if (game::GameLevels::Chapter().IsActive())
        {
            game::GameLevels::Chapter().Melee();
        }
        else
        {
            CivicCombat::Melee();
        }
        return;
    }
    if (key == 'r' && CivicCombat::Available())
    {
        game::GameLevels::Mine().Combat().Reload();
        return;
    }
    auto &chapter = game::GameLevels::Chapter();
    if (chapter.IsActive())
    {
        if (key == 'e')
        {
            chapter.Interact();
        }
        if (key == 'r')
        {
            game::GameLevels::Mine().Combat().Reload();
        }
        if (key == 'g' && !repeated)
        {
            game::GameLevels::Mine().UseRecoveryKit();
        }
        return;
    }
    if (game::GameLevels::Mine().IsActive())
    {
        if (key == 'e')
        {
            game::GameLevels::Mine().Interact();
        }
        if (key == 'g' && !repeated)
        {
            game::GameLevels::Mine().UseRecoveryKit();
        }
        if (key == 'r')
        {
            game::GameLevels::Mine().Combat().Reload();
        }
        if (key == 'f')
        {
            game::GameLevels::Mine().Rescue().Scan();
        }
        return;
    }
    if (key == '2')
    {
        EnterTestStage(TestStage::ChapterOne);
        return;
    }
    // Direct access for first-level iteration without replaying the tutorial.
    if (key == '1')
    {
        EnterTestStage(TestStage::Mine);
        return;
    }
    if (key == 'e')
    {
        InteractWithNearbyTarget();
    }
}
void Game::SpecialKeyDown(int key, int, int)
{
    if (SaveSystem::Loading())
    {
        return;
    }
    if (key == GLFW_KEY_F5)
    {
        SaveSystem::Save();
        return;
    }
    if (key == GLFW_KEY_F9)
    {
        if (state.relayPreparing)
        {
            return;
        }
        auto parent = state.ui.screen == UiScreen::Title ? UiScreen::Title : UiScreen::Pause;
        Hud::OpenMenu(UiScreen::Checkpoint);
        state.ui.returnTo = parent;
        return;
    }
    if (HospitalRecovery::Active())
    {
        return;
    }
    if (Hud::HandleSpecialKey(key))
    {
        return;
    }
    if (state.mapVisible && !state.isPaused && state.ui.screen == UiScreen::None)
    {
        if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN)
        {
            MapHud::Pan({key == GLFW_KEY_LEFT    ? -56.f
                         : key == GLFW_KEY_RIGHT ? 56.f
                                                 : 0,
                         key == GLFW_KEY_UP     ? -56.f
                         : key == GLFW_KEY_DOWN ? 56.f
                                                : 0});
        }
        return;
    }
    if (state.journalVisible || state.dialogueHistoryVisible)
    {
        auto &offset = state.dialogue.historyOffset;
        if (key == GLFW_KEY_PAGE_DOWN)
        {
            ++offset;
        }
        if (key == GLFW_KEY_PAGE_UP && offset > 0)
        {
            --offset;
        }
        return;
    }
    if (key != GLFW_KEY_F7 && key != GLFW_KEY_F8)
    {
        return;
    }
    auto &renderer = GameRenderer::Get().Backend();
    auto style = renderer.GetStyle();
    if (key == GLFW_KEY_F7)
    {
        style.cartoon = !style.cartoon;
        renderer.SetStyle(style);
        ShowNotice(style.cartoon ? "화면 표현 / 카툰 명암" : "화면 표현 / 부드러운 명암");
    }
    else
    {
        style.resolution = style.resolution == WorldResolution::Native ? WorldResolution::Half
                           : style.resolution == WorldResolution::Half ? WorldResolution::Quarter
                                                                       : WorldResolution::Native;
        renderer.SetStyle(style);
        auto current = renderer.GetStyle().resolution;
        ShowNotice(current == WorldResolution::Native ? "화면 표현 / 기본 해상도"
                   : current == WorldResolution::Half ? "화면 표현 / 도트 미리보기 640 × 400"
                                                      : "화면 표현 / 도트 미리보기 320 × 200");
    }
}
void Game::KeyUp(unsigned char key, int, int)
{
    if (NormalizeKey(key) == 'q')
    {
        WeaponWheel::Close(true);
        state.weaponWheel.qHeld = false;
    }
    if (key == 13)
    {
        state.dialogueEnterHeld = false;
    }
    state.pressedKeys[NormalizeKey(key)] = false;
    if (NormalizeKey(key) == 'e')
    {
        state.interactionHeld = false;
    }
}
void Game::MouseMotion(int x, int y)
{
    state.cursorWindowPosition = {float(x), float(y)};
    float scale =
        (std::min)(state.viewportWidth / float(kCanvasWidth), state.viewportHeight / float(kCanvasHeight));
    int width = (std::max)(1, int(kCanvasWidth * scale));
    int height = (std::max)(1, int(kCanvasHeight * scale));
    int left = (state.viewportWidth - width) / 2, top = (state.viewportHeight - height) / 2;
    state.cursorInsideViewport = x >= left && y >= top && x < left + width && y < top + height;
    state.cursorPosition = {Clamp((x - left) * float(kCanvasWidth) / width, 0, float(kCanvasWidth)),
                            Clamp((y - top) * float(kCanvasHeight) / height, 0, float(kCanvasHeight))};
    if (!state.cursorInsideViewport)
    {
        state.mouseFiring = state.mouseAiming = false;
        if (state.weaponWheel.visible)
        {
            WeaponWheel::Close();
        }
    }
    WeaponWheel::Motion();
    Hud::HandleMotion();
}
void Game::MouseButton(int button, int buttonState, int x, int y)
{
    MouseMotion(x, y);
    if (button == GLFW_MOUSE_BUTTON_RIGHT && buttonState == GLFW_RELEASE)
    {
        state.mouseAiming = false;
    }
    if (state.weaponWheel.visible)
    {
        state.mouseFiring = state.mouseAiming = false;
        return;
    }
    if (HospitalRecovery::Active())
    {
        state.mouseFiring = state.mouseAiming = false;
        return;
    }
    if (Hud::HandlePointer(button, buttonState == GLFW_PRESS))
    {
        state.mouseFiring = state.mouseAiming = false;
        return;
    }
    if (state.mapVisible)
    {
        state.mouseFiring = state.mouseAiming = false;
        if (buttonState == GLFW_PRESS && state.cursorInsideViewport && !state.isPaused)
        {
            if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT)
            {
                MapHud::HandleClick(state.cursorPosition, button == GLFW_MOUSE_BUTTON_RIGHT);
            }
        }
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        state.mouseFiring = buttonState == GLFW_PRESS &&
                            (game::GameLevels::IsCombatActive() || CivicCombat::Available()) &&
                            !state.isPaused && state.cursorInsideViewport && !Dialogue::LocksWorld() &&
                            !state.journalVisible && !state.dialogueHistoryVisible;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        state.mouseAiming = buttonState == GLFW_PRESS && state.cursorInsideViewport && !state.isPaused &&
                            !state.journalVisible && !state.dialogueHistoryVisible &&
                            !Dialogue::LocksWorld() && !state.isBikeMounted &&
                            game::GameLevels::Mine().GetState().loadout.IsPistolEquipped() &&
                            (game::GameLevels::IsCombatActive() || CivicCombat::Available());
    }
}
std::optional<world::WorldLocation> Game::PlanetLocation() const
{
    if (state.surfaceExploration && surface)
    {
        if (IsSurfaceMine() && surface->Ready())
        {
            auto location = surface->Stream().Snapshot()->planet->Region("mq00_mine").center;
            auto entry = game::GameLevels::Mine().GetState().entry;
            location.position = world::SurfaceGeometry::FromLocal(
                surface->Stream().Snapshot()->planet->Region("mq00_mine").frame,
                {(state.playerPosition.x - entry.x) * 2, (state.playerPosition.y - entry.y) * 2, 0},
                surface->Planet().radiusMeters);
            return location;
        }
        auto location = surface->Location();
        if ((state.isInsideHome || state.isInObservatory) && surfaceCities)
        {
            Vector2 door = state.isInsideHome ? kHomeDoor : kBeaconPosition;
            auto anchor = surfaceCities->Point(*surface, 0, door);
            if (anchor)
            {
                auto localDoor = state.isInsideHome ? Vector2{2.4f, 2.4f} : kObservatoryStairs;
                location.position = world::SurfaceGeometry::FromLocal(
                    world::SurfaceGeometry::Frame(*anchor),
                    {(state.playerPosition.x - localDoor.x) * 2, (state.playerPosition.y - localDoor.y) * 2,
                     state.isInsideHome ? 0. : 8.},
                    surface->Planet().radiusMeters);
                location.regionId = "halo_07";
                location.space = world::SpaceKind::Interior;
                location.layer = state.isInsideHome ? 1 : 2;
            }
        }
        return location;
    }
    static_assert(world::WorldCatalog::MetersPerLegacyUnit == models::WorldScale::metersPerUnit,
                  "Planet adapter and model units must agree");
    if (state.isInsideHome || state.isInObservatory || game::GameLevels::Active())
    {
        return std::nullopt;
    }
    int city = Overworld::CityAt(state.playerPosition);
    const char *region = city >= 0 ? Overworld::Cities()[size_t(city)].id : "overworld";
    return world::LegacyWorldAdapter::ErmaExterior().Locate(
        region, {double(state.playerPosition.x), double(state.playerPosition.y),
                 state.isBikeMounted ? double(state.bikeElevation) : 0});
}
bool Game::IsAiming() const
{
    return state.mouseAiming && !state.isBikeMounted && !state.isPaused && !state.mapVisible &&
           !state.weaponWheel.visible && !state.journalVisible && !state.dialogueHistoryVisible &&
           !HospitalRecovery::Active() && !Dialogue::LocksWorld() &&
           game::GameLevels::Mine().GetState().loadout.IsPistolEquipped() &&
           (game::GameLevels::IsCombatActive() || CivicJustice::IsExterior());
}
models::CharacterPose Game::PlayerPose(float recoil) const
{
    bool armed = game::GameLevels::Mine().GetState().loadout.IsPistolEquipped() && !state.isBikeMounted &&
                 (game::GameLevels::IsCombatActive() || CivicJustice::IsExterior());
    auto pose = models::ModelRig::Pose(state.animationTimeSeconds * 10, state.movementAmount > 0, armed,
                                       recoil, state.isBikeMounted, state.playerGender);
    pose.locomotionSpeed = state.movementSpeed;
    pose.meleeSeconds = game::GameLevels::Mine().IsActive() ? game::GameLevels::Mine().GetState().meleeSeconds
                        : game::GameLevels::Chapter().IsActive()
                            ? game::GameLevels::Chapter().GetState().meleeSeconds
                        : CivicJustice::IsExterior() ? CivicJustice::State().meleeSeconds
                                                     : 0;
    state.playerAnimation.Apply(pose);
    if (!state.isBikeMounted)
    {
        pose.motionForwardLean = state.playerMotion.forwardLean;
        pose.motionSideLean = state.playerMotion.sideLean;
    }
    const auto &gun = game::GameLevels::Mine().GetState().pistol;
    pose.reloadProgress = armed && gun.IsReloading() ? gun.GetReloadProgress() : -1;
    if (!armed)
    {
        pose.aimWeight = pose.fireSeconds = 0;
    }
    return pose;
}
void Game::MouseWheel(int steps, int x, int y)
{
    MouseMotion(x, y);
    if (!SaveSystem::Loading() && !HospitalRecovery::Active() && state.cursorInsideViewport)
    {
        MapHud::Zoom(steps, state.cursorPosition);
    }
}
void Game::MouseEntry(int pointerState)
{
    if (pointerState == GLFW_FALSE)
    {
        if (state.weaponWheel.visible)
        {
            WeaponWheel::Close();
        }
        state.ui.pressed = -1;
        state.mouseFiring = state.mouseAiming = false;
    }
}
void Game::Update(float deltaSeconds)
{
    if (SaveSystem::Loading())
    {
        SaveSystem::Update();
        return;
    }
    deltaSeconds = Clamp(deltaSeconds, 0, kMaximumDeltaSeconds);
    if (state.surfaceExploration)
    {
        UpdateSurface(deltaSeconds);
        return;
    }
    HospitalRecovery::Update(deltaSeconds);
    if (!state.weaponWheel.visible && !state.isPaused && !state.mapVisible && !HospitalRecovery::Active() &&
        Dialogue::LocksWorld())
    {
        game::GameLevels::Chapter().Update(deltaSeconds);
    }
    if (!state.weaponWheel.visible && !state.isPaused && !state.mapVisible && !HospitalRecovery::Active() &&
        !Dialogue::LocksWorld())
    {
        int healthBefore = game::GameLevels::Mine().GetState().player.health;
        state.animationTimeSeconds += deltaSeconds;
        if (!state.isComplete)
        {
            state.playTimeSeconds += deltaSeconds;
        }
        if (!Dialogue::Active())
        {
            state.noticeSecondsRemaining -= deltaSeconds;
        }
        game::Level *activeLevel = game::GameLevels::Active();
        auto previous = state.playerPosition;
        if (!activeLevel || activeLevel->UsesWalkingControls())
        {
            UpdatePlayerMovement(deltaSeconds);
        }
        if (IsAiming())
        {
            auto direction = game::MineLevel::GetAimDirection(game::GameLevels::AimAngle());
            state.facingYaw = std::atan2(direction.y, direction.x);
        }
        models::Point velocity = deltaSeconds > 0
                                     ? models::Point{(state.playerPosition.x - previous.x) / deltaSeconds,
                                                     (state.playerPosition.y - previous.y) / deltaSeconds, 0}
                                     : models::Point{};
        state.playerAnimation.Update(deltaSeconds, velocity, state.facingYaw, IsAiming(),
                                     state.isBikeMounted);
        if (activeLevel)
        {
            activeLevel->Update(deltaSeconds);
        }
        if (!HospitalRecovery::Active())
        {
            CivicCombat::Update(deltaSeconds);
            CivicJustice::Update(deltaSeconds);
        }
        if (!activeLevel && !state.isInsideHome && !state.isInObservatory)
        {
            BodyCollision::Update(deltaSeconds);
            Overworld::Traffic().Update(state, deltaSeconds);
            CityResidents::Get().Update(state, deltaSeconds);
            Overworld::Flights().Update(state, deltaSeconds);
        }
        if (activeLevel != &game::GameLevels::Chapter())
        {
            game::GameLevels::Chapter().Update(deltaSeconds);
        }
        HospitalRecovery::Update(0);
        int healthAfter = game::GameLevels::Mine().GetState().player.health;
        if (healthAfter < healthBefore && !HospitalRecovery::Active())
        {
            ShowNotice("피해 / 체력 -" + std::to_string(healthBefore - healthAfter));
        }
        UpdateCamera(deltaSeconds);
        game::SceneManager::Update(deltaSeconds);
    }
}
void Game::StopSurface()
{
    PlanetSecurity::Reset();
    SaveSystem::Cancel();
    state.relayPreparing = false;
    state.relaySource.clear();
    state.relayTarget.clear();
    if (state.surfaceExploration)
    {
        game::GameLevels::Chapter().GetState().active = false;
    }
    if (IsSurfaceMine())
    {
        game::GameLevels::Mine().GetState().active = false;
    }
    if (surface)
    {
        surface->Stop();
        surface.reset();
    }
    surfaceBike.reset();
    surfaceBikeHeading.reset();
    surfaceMineReturn.reset();
    surfaceCities.reset();
    surfaceTraffic.reset();
    surfaceParty.reset();
    surfaceCommunity.reset();
    state.surfaceExploration = false;
    state.surfaceCameraHeight = 0;
    surfaceSurveyRegion.reset();
}
bool Game::RelocateSurfaceSurvey(const world::SurfacePoint &point)
{
    if (state.relayPreparing)
    {
        return false;
    }
    if (surfaceParty)
    {
        surfaceParty->Capture();
    }
    const auto travelers = surfaceParty ? surfaceParty->NearbyTravelers() : std::array<bool, 2>{};
    if (!state.surfaceExploration || IsSurfaceMine() || state.isInsideHome || state.isInObservatory ||
        !surface || !surface->Relocate(point))
    {
        return false;
    }
    // Developer-only transport includes the test bike; normal relay travel keeps it parked.
    WeaponWheel::Close();
    state.StopMotion();
    state.mouseAiming = state.mouseFiring = false;
    state.mapVisible = false;
    std::fill(std::begin(state.pressedKeys), std::end(state.pressedKeys), false);
    state.playerPosition = state.cameraPosition = {};
    state.surfaceCameraHeight = 0;
    state.bikePosition = {};
    state.bikeElevation = 0;
    state.bikeSafePoseValid = false;
    surfaceBike = surface->Pose().origin;
    StoreSurfaceBikeHeading();
    state.planetMapPan = {};
    game::GameLevels::Chapter().Surface().Refresh();
    if (surfaceParty)
    {
        surfaceParty->Arrive(travelers);
    }
    return true;
}
bool Game::EnterSurfaceMine()
{
    if (!IsStoryWorld() || !state.surfaceExploration || !surface || !surface->Ready() ||
        game::GameLevels::Active() || state.isInsideHome || state.isInObservatory || IsSurfaceMine() ||
        state.isPaused || state.mapVisible || state.weaponWheel.visible || Dialogue::Active() ||
        state.journalVisible || state.dialogueHistoryVisible || HospitalRecovery::Active())
    {
        return false;
    }
    const auto &region = surface->Stream().Snapshot()->planet->Region("mq00_mine");
    if (world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, surface->Pose().origin,
                                         region.access.position) > 5)
    {
        return false;
    }
    if (state.isBikeMounted)
    {
        ShowNotice("광산 / 바이크를 주차하고 내린 뒤 입장하세요.");
        return false;
    }
    if (surfaceParty)
    {
        surfaceParty->Capture();
    }
    surfaceMineReturn = surface->Pose().origin;
    auto facing = world::SurfaceGeometry::TransformDirection(
        {std::cos(state.facingYaw), std::sin(state.facingYaw), 0}, surface->Origin(), region.frame);
    state.facingYaw = float(std::atan2(facing.y, facing.x));
    WeaponWheel::Close();
    state.mapVisible = false;
    state.surfaceCameraHeight = 0;
    game::GameLevels::Mine().Enter();
    if (surfaceParty)
    {
        surfaceParty->Update(0);
    }
    if (surfaceCities)
    {
        surfaceCities->Update(state, *surface, 0);
    }
    return game::GameLevels::Mine().IsActive();
}
bool Game::LeaveSurfaceMine()
{
    if (!surfaceMineReturn || !LandOnSurface(*surfaceMineReturn))
    {
        return false;
    }
    auto &mine = game::GameLevels::Mine().GetState();
    mine.active = false;
    mine.bullets.clear();
    mine.tracers.clear();
    mine.scannerContacts.clear();
    mine.scannerSeconds = mine.scannerRadius = 0;
    surfaceMineReturn.reset();
    ShowNotice(
        "오르딘 광산 입구 / 갱도·발견한 길·현장 인원·조사 기록을 유지합니다. E로 재입장할 수 있습니다.");
    return true;
}
bool Game::RecoverSurfaceAt(int city, Vector2 position)
{
    if (state.surfaceExploration && !IsStoryWorld())
    {
        if (!surface || !surface->Ready())
        {
            return false;
        }
        const auto plan = surface->Stream().Snapshot()->planet;
        const auto *nearest = world::RelayNetwork::Nearest(
            *plan, surface->Pose().origin, world::SurfaceGeometry::Pi * surface->Planet().radiusMeters);
        const auto point =
            nearest ? nearest->location.position : plan->Region("landing_zone").access.position;
        return LandOnSurface(point);
    }
    if (!state.surfaceExploration || !ReturnToSurface(position, city))
    {
        return false;
    }
    game::GameLevels::Mine().GetState().active = false;
    surfaceMineReturn.reset();
    return true;
}
std::optional<Vector2> Game::ResidentPosition(const Resident &resident) const
{
    if (state.surfaceExploration)
    {
        return surface && surfaceCities
                   ? surfaceCities->Local(*surface, resident.cityIndex, resident.position)
                   : std::nullopt;
    }
    return resident.position;
}
void Game::ToggleSurfaceBike()
{
    if (!surface || IsSurfaceMine() || state.isInsideHome || state.isInObservatory)
    {
        return;
    }
    auto before = state.playerPosition;
    Overworld::ToggleBike(state);
    surface->Move((state.playerPosition.x - before.x) * 2, (state.playerPosition.y - before.y) * 2,
                  state.isBikeMounted);
    auto local = surface->Local(surface->Pose().origin);
    state.playerPosition = {float(local.x / 2), float(local.y / 2)};
    if (state.isBikeMounted)
    {
        surfaceBike = surface->Pose().origin;
    }
    StoreSurfaceBikeHeading();
}
void Game::StoreSurfaceBikeHeading()
{
    if (!surface || !surfaceBike)
    {
        return;
    }
    const auto &f = surface->Origin();
    const double c = std::cos(state.bikeYaw), s = std::sin(state.bikeYaw);
    surfaceBikeHeading = world::Vector3d{f.right.x * c + f.forward.x * s, f.right.y * c + f.forward.y * s,
                                         f.right.z * c + f.forward.z * s};
}
void Game::RestoreSurfaceBikeHeading()
{
    if (!surface || !surfaceBike || !surfaceBikeHeading ||
        world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, *surfaceBike,
                                         surface->Pose().origin) >= 1000)
    {
        return;
    }
    const auto &f = surface->Origin();
    state.bikeYaw = float(std::atan2(world::SurfaceGeometry::Dot(*surfaceBikeHeading, f.forward),
                                     world::SurfaceGeometry::Dot(*surfaceBikeHeading, f.right)));
}
bool Game::ReturnToSurface(Vector2 door, int city)
{
    if (city == 0 && surface && surface->Ready() && surfaceCities)
    {
        if (const auto *facility = CityFacilities::ExitAt(door))
        {
            profiling::Scope accessTimer(profiling::Timer::FacilityExitSearch);
            auto staticClear = [&](Vector2 local) {
                const auto point = surfaceCities->Point(*surface, city, local);
                return point && !RoadNetwork::Get().Contains(local, .2f) &&
                       surface->LandingClear(*point, .32);
            };
            auto available = [&](Vector2 local) {
                const auto point = surfaceCities->Point(*surface, city, local);
                return point && SurfaceLandingClear(*point, city);
            };
            unsigned visited = 0;
            const auto landing = CityFacilities::FindExit(*facility, staticClear, available, &visited);
            profiling::Count(profiling::Counter::FacilityExitVisitedNodes, visited);
            const auto point = landing ? surfaceCities->Point(*surface, city, *landing) : std::nullopt;
            if (!point)
            {
                profiling::Count(profiling::Counter::FacilityExitFailures);
                ShowNotice("출입구 앞 보행 공간이 막혀 있습니다. 잠시 후 다시 나가세요.");
                return false;
            }
            // The connected front pad was already checked; never fall back to the opposite side of a wall.
            const bool landed = LandOnSurface(*point, city, 0);
            if (!landed)
            {
                profiling::Count(profiling::Counter::FacilityExitFailures);
            }
            return landed;
        }
    }
    auto point = surface && surfaceCities ? surfaceCities->Point(*surface, city, door) : std::nullopt;
    return point && LandOnSurface(*point, city);
}
bool Game::SurfaceLandingClear(const world::SurfacePoint &point, int city) const
{
    if (!surface || !surface->Ready() || !surface->LandingClear(point, .32))
    {
        return false;
    }
    if (IsStoryWorld())
    {
        const auto &frontier = surface->Stream().Snapshot()->planet->Region("frontier");
        if (world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, point,
                                             frontier.center.position) < 150)
        {
            const auto local =
                world::SurfaceGeometry::ToLocal(frontier.frame, point, surface->Planet().radiusMeters);
            const Vector2 position{game::kFrontierEntry.x + float(local.x / 2),
                                   game::kFrontierEntry.y + float(local.y / 2)};
            const auto &field = game::GameLevels::Chapter().GetState();
            auto occupied = [&](const game::FrontierPerson &person) {
                return person.combat.health > 0 && Distance(position, person.position) < .6f;
            };
            if ((HasCompanion(CompanionId::Ria, CompanionPlace::Frontier) && occupied(field.ria)) ||
                ((!field.kainJoined || HasCompanion(CompanionId::Kain, CompanionPlace::Frontier)) &&
                 occupied(field.kain)) ||
                std::any_of(field.survivors.begin(), field.survivors.end(), occupied) ||
                std::any_of(field.scrappers.begin(), field.scrappers.end(), occupied))
            {
                return false;
            }
        }
    }
    return !(surfaceParty && surfaceParty->Blocked(point, .32)) &&
           !(surfaceTraffic && surfaceTraffic->Blocked(point, .32)) &&
           !(surfaceCommunity && surfaceCommunity->Blocked(point, .32)) &&
           !(surfaceCities && surfaceCities->OccupiedAt(state, *surface, point, city)) &&
           !(surfaceBike &&
             world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, *surfaceBike, point) < 1);
}
bool Game::LandOnSurface(const world::SurfacePoint &point, int city, unsigned searchRings)
{
    if (!surface || !surface->Ready())
    {
        return false;
    }
    if (surfaceParty)
    {
        surfaceParty->Capture();
    }
    const auto travelers = surfaceParty && !IsSurfaceMine() && !state.isInsideHome && !state.isInObservatory
                               ? surfaceParty->NearbyTravelers()
                               : std::array<bool, 2>{};
    auto oldFrame = surface ? surface->Origin() : world::TangentFrame{};
    if (surfaceBike && world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, *surfaceBike,
                                                        surface->Pose().origin) < 1000)
    {
        StoreSurfaceBikeHeading();
    }
    auto facingFrame =
        IsSurfaceMine() ? surface->Stream().Snapshot()->planet->Region("mq00_mine").frame : oldFrame;
    bool placed = false;
    {
        auto frame = world::SurfaceGeometry::Frame(point);
        for (unsigned ring = 0; ring <= searchRings && !placed; ++ring)
        {
            for (unsigned step = 0; step < (ring == 0 ? 1u : 8u) && !placed; ++step)
            {
                double angle = step * world::SurfaceGeometry::Pi / 4;
                auto candidate =
                    world::SurfaceGeometry::Advance(frame, ring * std::cos(angle), ring * std::sin(angle),
                                                    surface->Planet().radiusMeters)
                        .origin;
                if (!SurfaceLandingClear(candidate, city))
                {
                    continue;
                }
                placed = surface->Relocate(candidate, .32, 0);
            }
        }
    }
    if (!placed)
    {
        ShowNotice("출입구 주변이 막혀 있습니다. 잠시 후 다시 나가세요.");
        return false;
    }
    CivicCombat::ClearProjectiles();
    state.isInsideHome = state.isInObservatory = false;
    auto bikeHeading = world::SurfaceGeometry::TransformDirection(
        {std::cos(state.bikeYaw), std::sin(state.bikeYaw), 0}, oldFrame, surface->Origin());
    state.bikeYaw = float(std::atan2(bikeHeading.y, bikeHeading.x));
    auto facing = world::SurfaceGeometry::TransformDirection(
        {std::cos(state.facingYaw), std::sin(state.facingYaw), 0}, facingFrame, surface->Origin());
    state.facingYaw = float(std::atan2(facing.y, facing.x));
    RestoreSurfaceBikeHeading();
    state.StopMotion();
    state.surfaceCameraHeight = 0;
    state.playerPosition = state.cameraPosition = {};
    state.planetMapPan = {};
    WeaponWheel::Close();
    state.mapVisible = false;
    if (surfaceBike && world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, *surfaceBike,
                                                        surface->Pose().origin) < 1000)
    {
        auto bike = surface->Local(*surfaceBike);
        state.bikePosition = {float(bike.x / 2), float(bike.y / 2)};
        state.bikeElevation = float(bike.z / 2);
    }
    else
    {
        state.bikePosition = {1000000, 1000000};
    }
    std::fill(std::begin(state.pressedKeys), std::end(state.pressedKeys), false);
    state.mouseAiming = state.mouseFiring = false;
    if (surfaceParty)
    {
        surfaceParty->Arrive(travelers);
    }
    return true;
}
bool Game::HasCompanion(CompanionId id, CompanionPlace place) const
{
    if (state.surfaceExploration)
    {
        return surfaceParty && surfaceParty->Owns(id, place);
    }
    if (place == CompanionPlace::Mine)
    {
        return id == CompanionId::Ria && game::GameLevels::Mine().GetState().quest.IsRiaJoined();
    }
    return place == CompanionPlace::Frontier &&
           (id == CompanionId::Ria || game::GameLevels::Chapter().GetState().kainJoined);
}
void Game::UpdateSurface(float dt)
{
    if (!surface)
    {
        return;
    }
    std::optional<world::SurfacePoint> relayDestination;
    if (state.relayPreparing && surface->Ready())
    {
        if (auto relay = world::RelayNetwork::Find(*surface->Stream().Snapshot()->planet, state.relayTarget))
        {
            relayDestination = relay->location.position;
        }
    }
    auto velocity = state.isBikeMounted ? state.bikeVelocity : state.playerMotion.velocity;
    surface->Update(IsSurfaceMine() || state.isInsideHome || state.isInObservatory
                        ? world::Vector3d{}
                        : world::Vector3d{velocity.x * 2, velocity.y * 2, 0},
                    relayDestination);
    if (!surface->Ready())
    {
        state.StopMotion();
        return;
    }
    if (surfaceTraffic)
    {
        surfaceTraffic->Bind(surface->Stream().Snapshot()->planet);
    }
    if (surfaceParty)
    {
        surfaceParty->Bind(surface->Stream().Snapshot()->planet);
        surfaceParty->Capture();
    }
    PlanetSecurity::Project();
    HospitalRecovery::Update(dt);
    if (HospitalRecovery::Active())
    {
        if (state.relayPreparing)
        {
            RelayTravel::Cancel("의료 이송으로 중계 이동을 취소했습니다.");
        }
        return;
    }
    if (state.relayPreparing)
    {
        RelayTravel::Update(dt);
        return;
    }
    if (!IsSurfaceMine() && !state.isInsideHome && !state.isInObservatory)
    {
        state.relayNetwork.Observe(*surface->Stream().Snapshot()->planet, surface->Location());
    }
    game::GameLevels::Chapter().Surface().Refresh();
    bool advancing = !state.isPaused && !state.mapVisible && !state.weaponWheel.visible &&
                     !state.journalVisible && !state.dialogueHistoryVisible && !Dialogue::LocksWorld();
    if (surfaceParty)
    {
        surfaceParty->Update(advancing ? dt : 0);
    }
    if (surfaceCities)
    {
        surfaceCities->Update(state, *surface, 0);
        if (advancing && (IsSurfaceMine() || state.isInsideHome || state.isInObservatory))
        {
            surfaceCities->Update(state, *surface, dt);
        }
    }
    if (advancing)
    {
        if (surfaceCommunity)
        {
            surfaceCommunity->Update(dt, !IsSurfaceMine() && !state.isInsideHome && !state.isInObservatory);
        }
        state.playTimeSeconds += dt;
        CivicJustice::Update(dt);
        PlanetSecurity::UpdateCompanions(dt);
        CivicCombat::Update(dt);
        if (surfaceTraffic)
        {
            surfaceTraffic->Update(state, *surface, dt,
                                   !IsSurfaceMine() && !state.isInsideHome && !state.isInObservatory);
        }
    }
    if (HospitalRecovery::Active())
    {
        return;
    }
    ActivityJournal::Refresh();
    if (IsSurfaceMine())
    {
        if (advancing)
        {
            state.animationTimeSeconds += dt;
            if (!Dialogue::Active())
            {
                state.noticeSecondsRemaining -= dt;
            }
            auto before = state.playerPosition;
            UpdatePlayerMovement(dt);
            if (IsAiming())
            {
                auto direction = game::MineLevel::GetAimDirection(game::GameLevels::AimAngle());
                state.facingYaw = std::atan2(direction.y, direction.x);
            }
            models::Point animationVelocity = dt > 0
                                                  ? models::Point{(state.playerPosition.x - before.x) / dt,
                                                                  (state.playerPosition.y - before.y) / dt, 0}
                                                  : models::Point{};
            state.playerAnimation.Update(dt, animationVelocity, state.facingYaw, IsAiming(), false);
            game::GameLevels::Mine().Update(dt);
            if (surfaceParty)
            {
                surfaceParty->Capture();
            }
            HospitalRecovery::Update(0);
            UpdateCamera(dt);
            game::SceneManager::Update(dt);
        }
        return;
    }
    if (advancing && (state.isInsideHome || state.isInObservatory))
    {
        state.animationTimeSeconds += dt;
        if (!Dialogue::Active())
        {
            state.noticeSecondsRemaining -= dt;
        }
        UpdatePlayerMovement(dt);
        UpdateCamera(dt);
        state.surfaceCameraHeight = 0;
        state.playerAnimation.Update(dt, {state.playerMotion.velocity.x, state.playerMotion.velocity.y, 0},
                                     state.facingYaw, false, false);
        game::SceneManager::Update(dt);
        return;
    }
    if (advancing)
    {
        state.animationTimeSeconds += dt;
        if (!Dialogue::Active())
        {
            state.noticeSecondsRemaining -= dt;
        }
        auto before = state.playerPosition;
        const auto surfaceFrom = surface->Pose().origin;
        const float previousBikeYaw = state.bikeYaw;
        UpdatePlayerMovement(dt);
        auto desired = state.playerPosition;
        surface->Move((desired.x - before.x) * 2, (desired.y - before.y) * 2, state.isBikeMounted);
        auto position = surface->Local(surface->Pose().origin);
        if (state.isBikeMounted)
        {
            auto contact = BodyCollision::SurfaceBikeContacts(surfaceFrom, surface->Pose().origin,
                                                              previousBikeYaw, std::fabs(state.bikeSpeed));
            if (std::hypot(contact.x * 2 - position.x, contact.y * 2 - position.y) > .0001)
            {
                surface->Move(contact.x * 2 - position.x, contact.y * 2 - position.y, true);
                position = surface->Local(surface->Pose().origin);
                state.bikeSafePosition = {float(position.x / 2), float(position.y / 2)};
                state.bikeSafeYaw = state.bikeYaw;
                state.bikeSafePoseValid = true;
            }
        }
        state.playerPosition = {float(position.x / 2), float(position.y / 2)};
        Vector2 requested{desired.x - before.x, desired.y - before.y},
            actual{state.playerPosition.x - before.x, state.playerPosition.y - before.y};
        if (state.isBikeMounted)
        {
            state.bikeVelocity = MotionMath::ContactVelocity(state.bikeVelocity, requested, actual);
            float direction = state.bikeSpeed < 0 ? -1.f : 1.f;
            state.bikeSpeed = direction * std::hypot(state.bikeVelocity.x, state.bikeVelocity.y);
        }
        else
        {
            state.playerMotion.velocity =
                MotionMath::ContactVelocity(state.playerMotion.velocity, requested, actual);
        }
        state.movementSpeed = dt > 0 ? Distance(before, state.playerPosition) / dt : 0;
        state.movementAmount =
            Clamp(state.movementSpeed / (state.isBikeMounted ? 12.8f : kMovementSpeed), 0, 1);
        auto animationVelocity = state.isBikeMounted ? state.bikeVelocity : state.playerMotion.velocity;
        if (IsAiming())
        {
            auto direction = game::MineLevel::GetAimDirection(game::GameLevels::Mine().GetAimAngle());
            state.facingYaw = std::atan2(direction.y, direction.x);
        }
        state.playerAnimation.Update(dt, {animationVelocity.x, animationVelocity.y, 0}, state.facingYaw,
                                     IsAiming(), state.isBikeMounted);
        if (state.isBikeMounted)
        {
            surfaceBike = surface->Pose().origin;
        }
        auto cameraPoint = surface->PointAt(state.cameraPosition.x * 2, state.cameraPosition.y * 2);
        cameraPoint.altitudeMeters = surface->Origin().origin.altitudeMeters + state.surfaceCameraHeight * 2;
        auto old = surface->Rebase();
        if (old)
        {
            auto camera = surface->Local(cameraPoint);
            state.cameraPosition = {float(camera.x / 2), float(camera.y / 2)};
            state.surfaceCameraHeight = float(camera.z / 2);
            auto rotate = [&](Vector2 v) {
                auto d = world::SurfaceGeometry::TransformDirection({v.x, v.y, 0}, *old, surface->Origin());
                return Vector2{float(d.x), float(d.y)};
            };
            auto heading = rotate({std::cos(state.facingYaw), std::sin(state.facingYaw)});
            auto bikeHeading = rotate({std::cos(state.bikeYaw), std::sin(state.bikeYaw)});
            state.facingYaw = std::atan2(heading.y, heading.x);
            state.bikeYaw = std::atan2(bikeHeading.y, bikeHeading.x);
            state.playerMotion.velocity = rotate(state.playerMotion.velocity);
            state.bikeVelocity = rotate(state.bikeVelocity);
            CivicJustice::State().playerImpact.velocity = rotate(CivicJustice::State().playerImpact.velocity);
            state.planetMapPan = rotate(state.planetMapPan);
            state.playerPosition = {};
        }
        if (state.isBikeMounted)
        {
            StoreSurfaceBikeHeading();
        }
        else
        {
            RestoreSurfaceBikeHeading();
        }
        if (surfaceBike && world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, *surfaceBike,
                                                            surface->Pose().origin) < 1000)
        {
            auto bike = surface->Local(*surfaceBike);
            state.bikePosition = {float(bike.x / 2), float(bike.y / 2)};
            auto ground = surface->ContactAt(bike.x, bike.y);
            surfaceBike->altitudeMeters =
                (std::max)(world::PlanetTerrain::SeaLevelMeters, ground.heightMeters);
            state.bikeElevation =
                float((surfaceBike->altitudeMeters - surface->Origin().origin.altitudeMeters) / 2);
        }
        else
        {
            state.bikePosition = {1000000, 1000000};
        }
        if (surfaceCities)
        {
            surfaceCities->Update(state, *surface, dt);
            // Contacts ran in a city frame; apply their correction to the authoritative sphere too.
            const auto current = surface->Local(surface->Pose().origin);
            const double dx = state.playerPosition.x * 2 - current.x,
                         dy = state.playerPosition.y * 2 - current.y;
            if (std::hypot(dx, dy) > .0001)
            {
                surface->Move(dx, dy, state.isBikeMounted);
                profiling::Count(profiling::Counter::CityContactCorrections);
            }
            const auto corrected = surface->Local(surface->Pose().origin);
            state.playerPosition = {float(corrected.x / 2), float(corrected.y / 2)};
            if (state.isBikeMounted)
            {
                surfaceBike = surface->Pose().origin;
                state.bikePosition = state.playerPosition;
                StoreSurfaceBikeHeading();
            }
        }
        game::GameLevels::Chapter().Surface().Refresh();
        game::GameLevels::Chapter().Surface().Update(dt);
        if (surfaceParty)
        {
            surfaceParty->Capture();
        }
        PlanetSecurity::Project();
        HospitalRecovery::Update(0);
        UpdateCamera(dt);
        float targetHeight = float(surface->Local(surface->Pose().origin).z / 2);
        state.surfaceCameraHeight +=
            (targetHeight - state.surfaceCameraHeight) * (1 - std::exp(-kCameraFollowRate * dt));
        game::SceneManager::Update(dt);
    }
}

unsigned int FrameSchedule::NextDelay(double startedMilliseconds, double finishedMilliseconds)
{
    constexpr double kFrameMilliseconds = 1000.0 / 60.0;
    if (nextMilliseconds < 0 || startedMilliseconds > nextMilliseconds + kFrameMilliseconds)
    {
        nextMilliseconds = startedMilliseconds;
    }
    nextMilliseconds += kFrameMilliseconds;
    nextMilliseconds = (std::max)(nextMilliseconds, finishedMilliseconds);
    return static_cast<unsigned int>((std::max)(1.0, std::ceil(nextMilliseconds - finishedMilliseconds)));
}

unsigned int Game::NextFrameDelay(double startedMilliseconds, double finishedMilliseconds)
{
    return frameSchedule.NextDelay(startedMilliseconds, finishedMilliseconds);
}
void Game::Tick(int)
{
    profiling::Scope profileTimer(profiling::Timer::Update);
    WeaponWheel::RefreshCursorCapture();
    HWND activeWindow = GetActiveWindow();
    if (activeWindow && IsWindowVisible(activeWindow) && GetForegroundWindow() != activeWindow)
    {
        state.mouseFiring = state.mouseAiming = false;
        std::fill(std::begin(state.pressedKeys), std::end(state.pressedKeys), false);
    }
    const int nowMilliseconds = platform::ElapsedMilliseconds();
    float deltaSeconds =
        Clamp((nowMilliseconds - state.lastUpdateMilliseconds) / 1000.f, 0, kMaximumDeltaSeconds);
    state.lastUpdateMilliseconds = nowMilliseconds;
    Update(deltaSeconds);
    monitoring::GameMonitor::Get().Update();
    platform::SetCursorHidden(
        (state.weaponWheel.visible || game::GameLevels::IsCombatActive() || CivicCombat::Available()) &&
        !state.mapVisible && !state.isPaused && state.ui.screen == UiScreen::None &&
        !Dialogue::LocksWorld() && !state.journalVisible && !state.dialogueHistoryVisible);
}
float Game::Clamp(float value, float minimum, float maximum)
{
    return (std::max)(minimum, (std::min)(maximum, value));
}
float Game::Distance(Vector2 first, Vector2 second)
{
    return std::sqrt((first.x - second.x) * (first.x - second.x) +
                     (first.y - second.y) * (first.y - second.y));
}
// ============================================================================
// Dialogue — input/state transitions; rendering never changes quest or page state.
// ============================================================================
namespace
{
void RememberDialoguePage()
{
    auto &d = Game::Get().GetState().dialogue;
    const auto &line = d.pages[d.page];
    if (std::none_of(d.history.begin(), d.history.end(),
                     [&](const DialogueLine &other) { return other.id == line.id; }))
    {
        d.history.push_back(line);
    }
    d.scenes[d.sceneId].nextPage = d.page;
}
} // namespace
std::vector<std::string> Dialogue::Wrap(const std::string &text, float width, int fontSize)
{
    return RenderAssets::WrapUtf8(text, width, fontSize);
}

bool Dialogue::Danger()
{
    const auto &world = Game::Get().GetState();
    if (CivicJustice::IsExterior() &&
        (CivicJustice::State().wanted > 0 ||
         std::any_of(CivicJustice::State().bullets.begin(), CivicJustice::State().bullets.end(),
                     [](const game::Bullet &bullet) { return !bullet.playerOwned; })))
    {
        return true;
    }
    auto &mine = game::GameLevels::Mine();
    if (mine.IsActive())
    {
        if (!mine.GetState().bullets.empty() ||
            (mine.GetState().invulnerability > 0 &&
             mine.GetState().player.health < mine.GetState().player.maxHealth))
        {
            return true;
        }
        for (const auto &enemy : mine.GetState().enemies)
        {
            if (enemy.health > 0 && Game::Distance(enemy.position, world.playerPosition) < 12 &&
                mine.Map().HasLineOfSight(world.playerPosition, enemy.position))
            {
                return true;
            }
        }
    }
    auto &chapter = game::GameLevels::Chapter();
    if (chapter.IsActive() && chapter.GetState().hostile)
    {
        auto player = world.playerPosition;
        if (world.surfaceExploration && !chapter.Surface().Observing())
        {
            player = chapter.Surface().Local(player);
        }
        if (!chapter.GetState().bullets.empty())
        {
            return true;
        }
        for (const auto &person : chapter.GetState().scrappers)
        {
            if (person.combat.health > 0 && Game::Distance(person.position, player) < 14 &&
                chapter.HasLineOfSight(player, person.position))
            {
                return true;
            }
        }
    }
    return false;
}
bool Dialogue::Active()
{
    return Game::Get().GetState().dialogue.active;
}
bool Dialogue::LocksWorld()
{
    const auto &w = Game::Get().GetState();
    return ((w.dialogue.active && w.dialogue.blocking) || w.journalVisible || w.dialogueHistoryVisible) &&
           !Danger();
}
bool Dialogue::Start(const std::string &id, std::vector<DialogueLine> lines,
                     std::function<void(const std::string &)> completion, bool repeatable)
{
    auto &world = Game::Get().GetState();
    auto &d = world.dialogue;
    if (id.empty() || lines.empty() || HospitalRecovery::Active())
    {
        return false;
    }
    WeaponWheel::Close();
    if (d.suspended && d.sceneId == id)
    {
        return Resume();
    }
    if (d.active || d.suspended)
    {
        if (d.sceneId == id || std::any_of(d.pending.begin(), d.pending.end(),
                                           [&](const QueuedDialogue &q) { return q.id == id; }))
        {
            return false;
        }
        if (d.pending.size() >= 16)
        {
            auto expendable = std::find_if(d.pending.begin(), d.pending.end(),
                                           [](const QueuedDialogue &q) { return !q.completion; });
            if (expendable == d.pending.end())
            {
                return false;
            }
            d.pending.erase(expendable);
        }
        d.pending.push_back({id, std::move(lines), std::move(completion), repeatable});
        return true;
    }
    d.pages.clear();
    for (size_t index = 0; index < lines.size(); ++index)
    {
        auto rows = Wrap(lines[index].text, 816, 20);
        constexpr size_t pageLines = 3;
        for (size_t row = 0; row < rows.size(); row += pageLines)
        {
            auto page = lines[index];
            page.id = id + "/" + (page.id.empty() ? std::to_string(index) : page.id) + "/" +
                      std::to_string(row / pageLines);
            page.text.clear();
            for (size_t k = row; k < (std::min)(rows.size(), row + pageLines); ++k)
            {
                page.text += (k == row ? "" : "\n") + rows[k];
            }
            if (row + pageLines < rows.size())
            {
                page.choices.clear();
            }
            d.pages.push_back(std::move(page));
        }
    }
    auto &record = d.scenes[id];
    d.page = record.phase == DialoguePhase::InProgress ? (std::min)(record.nextPage, d.pages.size() - 1) : 0;
    if (record.phase != DialoguePhase::Completed)
    {
        record.phase = DialoguePhase::InProgress;
    }
    d.sceneId = id;
    d.selection = -1;
    for (size_t i = 0; i < d.pages[d.page].choices.size(); ++i)
    {
        if (d.pages[d.page].choices[i].id == record.pendingChoice)
        {
            d.selection = int(i);
        }
    }
    d.active = true;
    d.suspended = false;
    d.blocking = !Danger();
    d.repeatable = repeatable;
    d.completion = std::move(completion);
    world.mapVisible = false;
    world.journalVisible = world.dialogueHistoryVisible = false;
    if (d.blocking)
    {
        std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
        world.mouseFiring = world.mouseAiming = false;
        world.StopMotion();
    }
    RememberDialoguePage();
    return true;
}
bool Dialogue::Select(size_t index)
{
    auto &d = Game::Get().GetState().dialogue;
    if (!d.active || index >= d.pages[d.page].choices.size() || Danger())
    {
        return false;
    }
    d.selection = int(index);
    d.scenes[d.sceneId].pendingChoice = d.pages[d.page].choices[index].id;
    return true;
}
bool Dialogue::Advance()
{
    auto &d = Game::Get().GetState().dialogue;
    if (!d.active || HospitalRecovery::Active() || Game::Get().GetState().isPaused)
    {
        return false;
    }
    const auto &options = d.pages[d.page].choices;
    if (!options.empty() && (d.selection < 0 || Danger()))
    {
        return false;
    }
    if (!options.empty())
    {
        d.scenes[d.sceneId].pendingChoice = options[size_t(d.selection)].id;
    }
    Game::Get().GetState().ui.pressed = -1;
    if (++d.page < d.pages.size())
    {
        d.selection = -1;
        RememberDialoguePage();
        return true;
    }
    if (LocksWorld())
    {
        auto &world = Game::Get().GetState();
        std::fill(std::begin(world.pressedKeys), std::end(world.pressedKeys), false);
        world.mouseFiring = world.mouseAiming = false;
    }
    auto id = d.sceneId;
    auto &record = d.scenes[id];
    record.choice = record.pendingChoice;
    record.phase = DialoguePhase::Completed;
    record.nextPage = d.page;
    auto callback = std::move(d.completion);
    bool apply = d.repeatable || d.events.insert(id + "/complete").second;
    d.active = false;
    d.suspended = false;
    if (callback && apply)
    {
        callback(record.choice);
    }
    if (!d.active && !d.pending.empty())
    {
        auto next = std::move(d.pending.front());
        d.pending.erase(d.pending.begin());
        Start(next.id, std::move(next.lines), std::move(next.completion), next.repeatable);
    }
    return true;
}
void Dialogue::Interrupt()
{
    auto &w = Game::Get().GetState();
    w.journalVisible = w.dialogueHistoryVisible = false;
    if (w.dialogue.active)
    {
        w.dialogue.active = false;
        w.dialogue.suspended = true;
    }
}
bool Dialogue::Resume()
{
    auto &w = Game::Get().GetState();
    auto &d = w.dialogue;
    if (!d.suspended || Danger() || HospitalRecovery::Active())
    {
        return false;
    }
    d.active = d.blocking = true;
    d.suspended = false;
    w.journalVisible = w.dialogueHistoryVisible = false;
    w.mouseFiring = w.mouseAiming = false;
    std::fill(std::begin(w.pressedKeys), std::end(w.pressedKeys), false);
    return true;
}
void Dialogue::Say(const std::string &id, const std::string &message)
{
    auto delimiter = message.find(" / ");
    std::string speaker = delimiter == std::string::npos ? "현장 기록" : message.substr(0, delimiter);
    std::string body = delimiter == std::string::npos ? message : message.substr(delimiter + 3);
    std::string channel = "현장";
    auto medium = speaker.find(" · ");
    if (medium != std::string::npos)
    {
        channel = speaker.substr(medium + 4);
        speaker.resize(medium);
    }
    else if (speaker.find("기록") != std::string::npos || speaker.find("수신") != std::string::npos ||
             speaker.find("단말") != std::string::npos)
    {
        channel = "단말 기록";
    }
    auto instruction = body.find('[');
    if (instruction != std::string::npos)
    {
        Game::Get().ShowNotice(body.substr(instruction));
        body.resize(instruction);
    }
    std::vector<DialogueLine> lines;
    size_t start = 0, sentences = 0;
    for (size_t i = 0; i < body.size(); ++i)
    {
        if ((body[i] == '.' || body[i] == '?' || body[i] == '!') &&
            (i + 1 == body.size() || body[i + 1] == ' '))
        {
            ++sentences;
            if (sentences == 2)
            {
                lines.push_back(
                    {std::to_string(lines.size()), speaker, channel, body.substr(start, i + 1 - start), {}});
                start = i + 1;
                while (start < body.size() && body[start] == ' ')
                {
                    ++start;
                }
                sentences = 0;
            }
        }
    }
    if (start < body.size())
    {
        lines.push_back({std::to_string(lines.size()), speaker, channel, body.substr(start), {}});
    }
    if (!lines.empty())
    {
        Start(id, std::move(lines));
    }
}
std::vector<std::string> Dialogue::Journal()
{
    if (!Game::Get().IsStoryWorld())
    {
        const auto &s = Game::Get().GetState();
        std::vector<std::string> lines = {
            Game::Get().Surface()->Planet().name + " 탐사 기록",
            "이 행성의 지형·도시·지대와 중계환은 고유 시드로 생성됩니다.",
            "발견한 중계환을 E로 등록하면 같은 행성 안에서 이동할 수 있습니다.",
            "발견 " + std::to_string(s.relayNetwork.Discoveries().size()) + " / 등록 " +
                std::to_string(s.relayNetwork.Registrations().size()),
            "행성 선택은 탐사 테스트 기능입니다. 현장 기록은 행성별로 유지됩니다.",
            "F5로 방문한 모든 행성의 기록을 디스크에 보관합니다."};
        if (auto *community = Game::Get().SurfaceCommunity())
        {
            auto local = community->Journal();
            lines.insert(lines.end(), local.begin(), local.end());
        }
        return lines;
    }
    const auto &w = Game::Get().GetState();
    const auto &mine = game::GameLevels::Mine().GetState();
    const auto &s = game::GameLevels::Chapter().GetState();
    std::vector<std::string> lines = {"인물: 리아는 헤일로의 정비 담당, 마라는 주거 구역 주민이다.",
                                      "N-0은 도시 가까이의 관문이다."};
    if (w.surfaceExploration)
    {
        for (int i = 0; i < int(MissionId::Count); ++i)
        {
            lines.push_back(std::string("임무: ") + QuestParticipation::Name(MissionId(i)) + " / " +
                            QuestParticipation::Label(w.missions.Status(MissionId(i))));
            if (w.missions.Status(MissionId(i)) != MissionStatus::Completed)
            {
                if (auto reason = Game::Get().MissionRestriction(MissionId(i)))
                {
                    lines.push_back(std::string("선행 조건: ") + reason);
                }
            }
        }
        lines.push_back("지역 방문과 기록 열람은 자유입니다. 왼쪽에서 수락·보류·재개하고, 목표 추적을 별도로 "
                        "선택합니다.");
        if (auto *community = Game::Get().SurfaceCommunity())
        {
            auto local = community->Journal();
            lines.insert(lines.end(), local.begin(), local.end());
        }
    }
    for (const auto &record : StoryJournal::Collect(w, mine, s))
    {
        lines.push_back("확인한 사실 / " + record.title + ": " + record.fact);
        if (!record.unresolved.empty())
        {
            lines.push_back("남은 의문 / " + record.title + ": " + record.unresolved);
        }
    }
    lines.push_back(std::string("다음 행동: ") +
                    (w.surfaceExploration ? ActivityJournal::Current().action
                     : s.unlocked         ? game::GameLevels::Chapter().Objective()
                     : w.mainQuestStage >= MainQuestStage::MountJetBike ? mine.quest.GetObjective()
                     : w.gateSignalHeard ? "전망대에서 내려와 리아에게 기록을 보여 준다."
                                         : "집 단말과 리아의 안내에 따라 전망대에서 신호를 측정한다."));
    return lines;
}

void Game::ShowNotice(const std::string &message)
{
    state.noticeText = message;
    // Long notices keep every page visible long enough to read; short events stay brief.
    const auto pages = Hud::TextPages(message, 294, 16, 4).size();
    state.noticeDurationSeconds = pages > 1 ? 6.f * pages : 4.f;
    state.noticeSecondsRemaining = state.noticeDurationSeconds;
}
bool Game::IsPositionBlocked(Vector2 position)
{
    profiling::Count(profiling::Counter::CollisionQueries);
    if (state.surfaceExploration && surfaceCities && surfaceCities->Observing())
    {
        auto point = surfaceCities->Point(*surface, surfaceCities->ObservedCity(), position);
        if (point && !state.isBikeMounted &&
            ((surfaceParty && surfaceParty->Blocked(*point, state.isBikeMounted ? .5 : .32)) ||
             PlanetSecurity::OfficerBlocked(*point, state.isBikeMounted ? .5 : .32)))
        {
            return true;
        }
        return CivicJustice::Solid(position, .16f, state.isBikeMounted ? state.bikeElevation : 0,
                                   state.isBikeMounted);
    }
    if (state.surfaceExploration && !IsSurfaceMine() && !state.isInsideHome && !state.isInObservatory)
    {
        return !surface || surface->Blocked(position.x * 2, position.y * 2, state.isBikeMounted) ||
               game::GameLevels::Chapter().Surface().Blocked(position, state.isBikeMounted) ||
               (surfaceCities &&
                surfaceCities->Occupied(state, *surface, position, -1, !state.isBikeMounted)) ||
               (!state.isBikeMounted && surfaceParty &&
                surfaceParty->Blocked(surface->PointAt(position.x * 2, position.y * 2),
                                      state.isBikeMounted ? .5 : .32)) ||
               (!state.isBikeMounted &&
                PlanetSecurity::OfficerBlocked(surface->PointAt(position.x * 2, position.y * 2), .32)) ||
               (surfaceTraffic && surfaceTraffic->Blocked(surface->PointAt(position.x * 2, position.y * 2),
                                                          state.isBikeMounted ? .5 : .32)) ||
               (surfaceCommunity &&
                surfaceCommunity->Blocked(surface->PointAt(position.x * 2, position.y * 2),
                                          state.isBikeMounted ? .5 : .32, !state.isBikeMounted));
    }
    if (game::GameLevels::Chapter().IsActive())
    {
        return game::GameLevels::Chapter().IsBlocked(position);
    }
    if (game::GameLevels::Mine().IsActive())
    {
        return game::GameLevels::Mine().Map().IsBlocked(position);
    }
    if (state.isInsideHome)
    {
        if (position.x < -2.8f || position.x > 2.8f || position.y < -2.8f || position.y > 2.8f)
        {
            return true;
        }
        const bool bed =
            position.x > -2.48f && position.x < -1.62f && position.y > -2.75f && position.y < -1.35f;
        const bool console =
            position.x > 1.23f && position.x < 1.97f && position.y > -2.69f && position.y < -2.1f;
        const bool table =
            position.x > -2.32f && position.x < -1.28f && position.y > 1.03f && position.y < 1.77f;
        return bed || console || table;
    }
    if (state.isInObservatory)
    {
        return position.x < -3.8f || position.x > 3.8f || position.y < -3.8f || position.y > 3.8f;
    }
    if (position.x < kWorldMinX || position.x > kWorldMaxX || position.y < kWorldMinY ||
        position.y > kWorldMaxY)
    {
        return true;
    }
    if (TransitLayout::IsBlocked(position, .2f, state.isBikeMounted ? state.bikeElevation : 0,
                                 state.isBikeMounted))
    {
        return true;
    }
    if (state.isBikeMounted && state.bikeElevation > 1)
    {
        return false;
    }
    auto entrance = Overworld::kMineEntrance;
    float entranceX = position.x - entrance.x, entranceY = position.y - entrance.y;
    bool entrancePillars =
        entranceY > -1.3f && entranceY < -.4f && std::fabs(entranceX) > 2.7f && std::fabs(entranceX) < 3.2f;
    if (entrancePillars)
    {
        return true;
    }
    return CivicJustice::Solid(position, .16f, state.isBikeMounted ? state.bikeElevation : 0,
                               state.isBikeMounted);
}
bool Game::IsQuestInteraction(InteractionTarget target) const
{
    if ((target == InteractionTarget::Lia && CivicCombat::LandmarkDown(kLiaPosition)) ||
        (target == InteractionTarget::Mara && CivicCombat::LandmarkDown(kMaraPosition)) ||
        (target == InteractionTarget::Kain && CivicCombat::LandmarkDown(kCityKainPosition)))
    {
        return false;
    }
    auto stage = game::GameLevels::Mine().GetState().quest.GetStage();
    if (state.surfaceExploration)
    {
        if (state.missions.Active(MissionId::Signal))
        {
            if (target == InteractionTarget::HomeTerminal)
            {
                return state.isInsideHome && !state.homeMessageRead;
            }
            if (target == InteractionTarget::GateBeacon)
            {
                return !state.gateSignalHeard;
            }
            if (target == InteractionTarget::ObservatorySignal)
            {
                return state.isInObservatory && !state.gateSignalHeard;
            }
            if (target == InteractionTarget::ObservatoryExit)
            {
                return state.isInObservatory && state.gateSignalHeard && !state.hasDescendedObservatory;
            }
        }
        if (state.missions.Active(MissionId::Mine))
        {
            if (target == InteractionTarget::Lia)
            {
                return stage == game::MineQuestStage::ShowRecord ||
                       (stage == game::MineQuestStage::TravelToMine &&
                        !game::GameLevels::Mine().GetState().quest.IsRiaJoined());
            }
            if (target == InteractionTarget::CityReceiver)
            {
                return stage == game::MineQuestStage::CheckCityRelay || CanCompleteMission(MissionId::Mine);
            }
            if (target == InteractionTarget::MineEntrance || target == InteractionTarget::JetBike)
            {
                return stage == game::MineQuestStage::TravelToMine;
            }
        }
        if (state.missions.Active(MissionId::Mara))
        {
            if (target == InteractionTarget::Mara)
            {
                return CanCompleteMission(MissionId::Mara);
            }
            if (target == InteractionTarget::PowerRelay)
            {
                return !state.isPowerRestored;
            }
        }
        return target == InteractionTarget::CityReceiver && CanCompleteMission(MissionId::Frontier);
    }
    if (game::GameLevels::Chapter().IsAvailable() &&
        (target == InteractionTarget::HomeTerminal || target == InteractionTarget::ApartmentExit ||
         target == InteractionTarget::HomeEntrance || target == InteractionTarget::Lia ||
         target == InteractionTarget::GateBeacon || target == InteractionTarget::JetBike ||
         target == InteractionTarget::ObservatorySignal || target == InteractionTarget::ObservatoryExit))
    {
        return false;
    }
    switch (target)
    {
    case InteractionTarget::HomeTerminal:
        return state.isInsideHome && state.mainQuestStage == MainQuestStage::ReadHomeTerminal;
    case InteractionTarget::ApartmentExit:
        return state.isInsideHome && state.mainQuestStage == MainQuestStage::MeetLia;
    case InteractionTarget::HomeEntrance:
        return !state.isInsideHome && state.mainQuestStage == MainQuestStage::ReadHomeTerminal;
    case InteractionTarget::Lia:
        return (!state.gateSignalHeard && state.mainQuestStage == MainQuestStage::MeetLia) ||
               (state.hasDescendedObservatory && stage == game::MineQuestStage::ShowRecord);
    case InteractionTarget::Mara:
        return !state.isSideQuestComplete && (!state.isSideQuestAccepted || state.isPowerRestored);
    case InteractionTarget::PowerRelay:
        return state.isSideQuestAccepted && !state.isPowerRestored;
    case InteractionTarget::GateBeacon:
        return state.mainQuestStage == MainQuestStage::EnterObservatory && !state.gateSignalHeard;
    case InteractionTarget::CityReceiver:
        return (game::GameLevels::Chapter().IsAvailable() && !state.isInsideHome && !state.isInObservatory &&
                (!game::GameLevels::Chapter().GetState().recordsCompared ||
                 !game::GameLevels::Chapter().GetState().portableLightRepaired ||
                 !game::GameLevels::Chapter().GetState().vehicleRepaired ||
                 !game::GameLevels::Chapter().GetState().detailLedgerFound)) ||
               (state.hasDescendedObservatory && stage == game::MineQuestStage::CheckCityRelay) ||
               stage == game::MineQuestStage::ReturnToHalo;
    case InteractionTarget::JetBike:
        return state.hasDescendedObservatory && state.gateSignalHeard &&
               stage == game::MineQuestStage::TravelToMine;
    case InteractionTarget::MineEntrance:
        return stage >= game::MineQuestStage::TravelToMine;
    case InteractionTarget::ObservatorySignal:
        return state.isInObservatory && !state.gateSignalHeard;
    case InteractionTarget::ObservatoryExit:
        return state.isInObservatory && state.gateSignalHeard;
    default:
        return false;
    }
}
InteractionTarget Game::FindNearbyInteraction()
{
    if (state.surfaceExploration && !IsSurfaceMine() && !state.isInsideHome && !state.isInObservatory)
    {
        if (state.isBikeMounted)
        {
            return InteractionTarget::JetBike;
        }
        float distance = 1000000;
        game::ChapterInteraction interaction{};
        if (!state.isBikeMounted && game::GameLevels::Chapter().NearestInteraction(interaction))
        {
            return InteractionTarget::FrontierSite;
        }
        auto target = surface && surfaceCities ? surfaceCities->Nearby(state, *surface, &distance)
                                               : InteractionTarget::None;
        if (surfaceCommunity)
        {
            float communityDistance = 1000000;
            auto communityTarget = surfaceCommunity->Nearby(&communityDistance);
            if (communityDistance < distance)
            {
                target = communityTarget;
                distance = communityDistance;
            }
        }
        if (surfaceParty)
        {
            float companionDistance = 1000000;
            auto companionTarget = surfaceParty->Nearby(&companionDistance);
            if (companionDistance < distance)
            {
                target = companionTarget;
                distance = companionDistance;
            }
        }
        if (IsStoryWorld() && surface && surface->Ready())
        {
            const auto &mine = surface->Stream().Snapshot()->planet->Region("mq00_mine");
            if (world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, surface->Pose().origin,
                                                 mine.access.position) <= 5)
            {
                return InteractionTarget::MineEntrance;
            }
        }
        float bikeDistance = Distance(state.playerPosition, state.bikePosition);
        if (bikeDistance < 1.8f && bikeDistance < distance)
        {
            distance = bikeDistance;
            target = InteractionTarget::JetBike;
        }
        if (auto relay = RelayTravel::Nearby())
        {
            if (world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, surface->Pose().origin,
                                                 relay->location.position) *
                    .5 <
                distance)
            {
                return InteractionTarget::Relay;
            }
        }
        return target;
    }
    if (HospitalRecovery::Active())
    {
        return InteractionTarget::None;
    }
    if (state.isInObservatory)
    {
        if (Distance(state.playerPosition, kObservatoryConsole) < 1.4f)
        {
            return InteractionTarget::ObservatorySignal;
        }
        if (Distance(state.playerPosition, CityFacilities::Find(InteractionTarget::GateBeacon)->exit) < 1.4f)
        {
            return InteractionTarget::ObservatoryExit;
        }
        return InteractionTarget::None;
    }
    if (state.isInsideHome)
    {
        if (Distance(state.playerPosition, {1, -1.2f}) < 1.5f)
        {
            return InteractionTarget::HomeTerminal;
        }
        if (Distance(state.playerPosition, CityFacilities::Find(InteractionTarget::HomeEntrance)->exit) <
            1.5f)
        {
            return InteractionTarget::ApartmentExit;
        }
        return InteractionTarget::None;
    }

    if (state.isBikeMounted)
    {
        return InteractionTarget::JetBike;
    }
    if (Distance(state.playerPosition, Overworld::kMineEntrance) < 2.5f)
    {
        return InteractionTarget::MineEntrance;
    }
    if (Distance(state.playerPosition, state.bikePosition) < 1.8f)
    {
        return InteractionTarget::JetBike;
    }

    if (game::GameLevels::Chapter().IsAvailable() &&
        Distance(state.playerPosition, kCityReceiverPosition) < 1.5f)
    {
        return InteractionTarget::CityReceiver;
    }
    if (game::GameLevels::Chapter().GetState().kainJoined &&
        Distance(state.playerPosition, kCityKainPosition) < 1.1f &&
        !CivicCombat::LandmarkDown(kCityKainPosition))
    {
        return InteractionTarget::Kain;
    }
    struct InteractionPoint
    {
        Vector2 position;
        InteractionTarget target;
    };
    const InteractionPoint points[] = {{kLiaPosition, InteractionTarget::Lia},
                                       {kMaraPosition, InteractionTarget::Mara}};
    InteractionTarget nearestTarget = InteractionTarget::None;
    float nearestDistance = 1.35f;
    for (const auto &point : points)
    {
        if (CivicCombat::LandmarkDown(point.position))
        {
            continue;
        }
        float distance = Distance(state.playerPosition, point.position);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestTarget = point.target;
        }
    }
    for (const auto &facility : CityFacilities::All())
    {
        const float distance = Distance(state.playerPosition, facility.exterior);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestTarget = facility.target;
        }
    }
    for (const auto &resident : state.residents)
    {
        if (&resident - state.residents.data() == Overworld::kKainResidentSlot &&
            game::GameLevels::Chapter().GetState().kainJoined)
        {
            continue;
        }
        if (resident.health <= 0 || resident.fearSeconds > 0)
        {
            continue;
        }
        float distance = Distance(state.playerPosition, resident.position);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestTarget = InteractionTarget::Resident;
        }
    }
    return nearestTarget;
}
void Game::InteractWithNearbyTarget()
{
    if (HospitalRecovery::Active() || Dialogue::Active() || state.journalVisible ||
        state.dialogueHistoryVisible)
    {
        return;
    }
    if (!state.surfaceExploration)
    {
        for (auto position : {kLiaPosition, kMaraPosition, kCityKainPosition})
        {
            if (Distance(state.playerPosition, position) < 1.35f && CivicCombat::LandmarkDown(position))
            {
                ShowNotice("의료 지원 / 중상자를 치료하고 있습니다. 회복 후 다시 대화할 수 있습니다.");
                return;
            }
        }
    }
    const InteractionTarget target = FindNearbyInteraction();
    if (target == InteractionTarget::Community && surfaceCommunity)
    {
        surfaceCommunity->Interact();
        return;
    }
    if (target == InteractionTarget::Relay)
    {
        RelayTravel::Interact();
        return;
    }
    if (state.surfaceExploration &&
        (target == InteractionTarget::HomeTerminal || target == InteractionTarget::Lia ||
         target == InteractionTarget::Mara || target == InteractionTarget::PowerRelay ||
         target == InteractionTarget::CityReceiver || target == InteractionTarget::ObservatorySignal ||
         target == InteractionTarget::ObservatoryExit))
    {
        InteractSurfaceStory(target);
        return;
    }
    if (target == InteractionTarget::FrontierSite)
    {
        game::GameLevels::Chapter().Interact();
        return;
    }
    if (state.surfaceExploration && target == InteractionTarget::JetBike)
    {
        ToggleSurfaceBike();
        return;
    }
    auto tell = [&](const std::string &message) {
        auto &chapter = game::GameLevels::Chapter();
        Dialogue::Say(
            "city/" + std::to_string(int(target)) + "/" + std::to_string(int(state.mainQuestStage)) + "/" +
                std::to_string(int(game::GameLevels::Mine().GetState().quest.GetStage())) + "/" +
                std::to_string(chapter.CompletedSites()) + "/" +
                std::to_string(chapter.GetState().recordsCompared) + "/" +
                std::to_string(state.isSideQuestComplete) + "/" + std::to_string(state.isSideQuestAccepted) +
                "/" + std::to_string(state.isPowerRestored) + "/" +
                std::to_string(chapter.GetState().generatorRestored),
            message);
    };
    if (target == InteractionTarget::MineEntrance)
    {
        if (state.surfaceExploration)
        {
            auto &mine = game::GameLevels::Mine().GetState();
            if (state.missions.Active(MissionId::Mine) && mine.quest.IsRiaJoined() &&
                !mine.loadout.HasPistol())
            {
                Dialogue::Start("MQ00/mine-weapon",
                                {{"pistol", "리아", "장비 인계",
                                  "직접 들어가기로 했구나. 이 코일 권총을 가져가. Q 홀드로 보조 무장을 "
                                  "고르고 R로 재장전해. 위험할 때만 써."}},
                                [this](const std::string &) {
                                    if (!state.missions.Active(MissionId::Mine))
                                    {
                                        return;
                                    }
                                    game::GameLevels::Mine().GetState().loadout.GrantPistol();
                                    EnterSurfaceMine();
                                });
                return;
            }
            EnterSurfaceMine();
            return;
        }
        if (game::GameLevels::Mine().GetState().quest.GetStage() < game::MineQuestStage::TravelToMine)
        {
            ShowNotice("광산 / 리아에게 기록을 보여 주고 헤일로 수신망에서 접근 경로를 확인하세요.");
            return;
        }
        state.mineReturnPosition = state.playerPosition;
        auto &mine = game::GameLevels::Mine().GetState();
        if (!mine.loadout.HasPistol())
        {
            Dialogue::Start(
                "MQ00/mine-weapon",
                {{"safety", "리아", "광산 입구",
                  "안쪽 통신이 끊겼어. 구조 신호를 보낸 사람들을 찾으려면 직접 들어가야 해."},
                 {"pistol", "리아", "장비 인계",
                  "이 코일 권총을 가져가. 위험할 때만 써. Q를 누른 채 마우스를 오른쪽으로 "
                  "움직이면 보조 무장을 고를 수 있어. 재장전은 R이야."}},
                [](const std::string &) {
                    bool granted = game::GameLevels::Mine().GetState().loadout.GrantPistol();
                    game::GameLevels::Mine().Enter();
                    if (granted)
                    {
                        auto &g = Game::Get();
                        g.ShowNotice(
                            "장비 획득 / 개척자 권총 · 보조 무장 사용 가능. Q 홀드 + 마우스로 선택하세요.\n" +
                            g.GetState().noticeText);
                    }
                });
            return;
        }
        game::GameLevels::Mine().Enter();
        return;
    }
    if (target == InteractionTarget::Resident)
    {
        Resident *nearest = nullptr;
        float distance = 1.35f;
        for (auto &resident : state.residents)
        {
            if (&resident - state.residents.data() == Overworld::kKainResidentSlot &&
                game::GameLevels::Chapter().GetState().kainJoined)
            {
                continue;
            }
            if (resident.health <= 0 || resident.fearSeconds > 0)
            {
                continue;
            }
            auto position = ResidentPosition(resident);
            float candidate = position ? Distance(state.playerPosition, *position) : 1000000.f;
            if (candidate < distance)
            {
                distance = candidate;
                nearest = &resident;
            }
        }
        if (nearest)
        {
            nearest->conversationSeconds = 3;
            Dialogue::Say("resident/" + std::to_string(nearest - state.residents.data()),
                          nearest->dialogue + (nearest->profession == ResidentProfession::Merchant
                                                   ? " [거래 안내] 악명에 따른 가격 배율: " +
                                                         std::to_string(CivicJustice::PriceMultiplier())
                                                   : ""));
        }
        return;
    }
    if (target == InteractionTarget::HomeTerminal)
    {
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::MeetLia);
        tell("리아 · 단말 메시지 / N-0 정기 측정일이야. 전망대 수신기가 이상한 응답을 잡았어. 같이 "
             "확인해 줘. 접근 코드는 첨부했어.");
    }
    if (target == InteractionTarget::ApartmentExit)
    {
        if (!state.surfaceExploration && state.mainQuestStage == MainQuestStage::ReadHomeTerminal)
        {
            ShowNotice("외출하기 전에 집 단말을 확인하세요. 청록색 불빛을 찾으세요.");
            return;
        }
        if (state.surfaceExploration)
        {
            if (ReturnToSurface(kHomeDoor))
            {
                ShowNotice("헤일로 / 거주지 앞 보행로로 나왔습니다.");
            }
            return;
        }
        state.isInsideHome = false;
        state.playerPosition = kHomeDoor;
        state.StopMotion();
        state.cameraPosition = state.playerPosition;
        ShowNotice(
            "헤일로 / 주거 구역 07. 머리 위로 자기부상 차량이 지나갑니다. 새벽부터 관문이 낮게 울립니다.");
    }
    if (target == InteractionTarget::HomeEntrance)
    {
        state.isInsideHome = true;
        state.playerPosition = CityFacilities::Find(target)->arrival;
        state.StopMotion();
        state.cameraPosition = {0, 0};
        state.surfaceCameraHeight = 0;
        ShowNotice("집 / 문이 닫히자 도시의 웅성거림이 멀어집니다.");
    }
    if (target == InteractionTarget::CityReceiver)
    {
        if (state.surfaceExploration)
        {
            if (game::GameLevels::Chapter().IsAvailable())
            {
                game::GameLevels::Chapter().InteractHub();
            }
            else
            {
                tell(std::string("도시 수신망 / ") +
                     game::GameLevels::Mine().GetState().quest.GetObjective());
            }
            return;
        }
        if (game::GameLevels::Mine().GetState().quest.Advance(game::MineQuestStage::ReturnToHalo))
        {
            state.isComplete = true;
            state.mainQuestStage = MainQuestStage::Complete;
            game::GameLevels::Chapter().Unlock();
            tell("광산 조사 보고 완료 / 좌표가 N-0 외곽 설비 목록과 일치합니다. 정전·운송대 "
                 "실종·폐설비를 조사하세요. 이 단말에서 다시 E를 누르면 1장 현장으로 출발합니다.");
            return;
        }
        if (game::GameLevels::Chapter().IsAvailable())
        {
            game::GameLevels::Chapter().InteractHub();
            return;
        }
        if (state.gateSignalHeard &&
            game::GameLevels::Mine().GetState().quest.Advance(game::MineQuestStage::CheckCityRelay))
        {
            tell("수신망 조회 / 전망대 신호가 인근 광산 통신선을 거쳤습니다. 같은 시각, 그 광산에서 "
                 "실종 탐사선 식별자의 구조 신호도 수신됐습니다. 바이크로 광산에 가서 하차한 뒤 입구를 "
                 "조사하세요.");
        }
        else
        {
            tell(game::GameLevels::Mine().GetState().quest.GetStage() < game::MineQuestStage::CheckCityRelay
                     ? "수신망 / 전망대 신호 기록을 먼저 리아에게 보여 주세요."
                     : "수신망 / 광산을 거친 신호입니다. 전망대 음성과 탐사선 구조 신호가 같은 곳에서 "
                       "왔는지는 아직 알 수 없습니다.");
        }
        return;
    }
    if (target == InteractionTarget::Kain)
    {
        tell(game::GameLevels::Chapter().GetState().recordsCompared
                 ? "카인 / 지도에 있던 다리는 사라졌습니다. 아래 수로가 남아 있는지 확인해야겠군요. "
                   "예전 진입로는 기록해 뒀습니다."
                 : "카인 / N-0 외곽에서 근무했습니다. 지금은 기록부터 모읍시다. 길보다 먼저 사람 수를 "
                   "확인해야 합니다.");
        return;
    }
    if (target == InteractionTarget::Lia)
    {
        if (game::GameLevels::Chapter().IsAvailable())
        {
            Dialogue::Start("city/frontier/status",
                            StoryDialogue::FrontierStatus(game::GameLevels::Chapter().GetState()), {}, true);
            return;
        }
        if (state.gateSignalHeard)
        {
            if (game::GameLevels::Mine().GetState().quest.Advance(game::MineQuestStage::ShowRecord))
            {
                tell("리아 / 네가 보낸 적 없는 목소리라고? 수신기 오류일 수도 있어. 광장 수신망에서 이 "
                     "신호가 어디를 거쳤는지 확인해 보자.");
            }
            else
            {
                tell(game::GameLevels::Mine().GetState().quest.GetRiaDialogue());
            }
            return;
        }
        if (state.mainQuestStage < MainQuestStage::MeetLia)
        {
            tell("리아 / 집 단말에 접근 코드를 보냈어. 먼저 확인해 줘.");
            return;
        }
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::EnterObservatory);
        tell("리아 / 광장 동쪽 전망대에 같이 올라가자. 내가 수신기를 볼게. 넌 관측 데크 북동쪽 "
             "콘솔에서 신호를 측정해 줘.");
    }
    if (target == InteractionTarget::Mara)
    {
        if (!state.isSideQuestAccepted)
        {
            state.isSideQuestAccepted = true;
            tell("마라 / 딸아이 방에 불이 안 들어와요. 광장 동쪽 전력 장치를 좀 봐 주시겠어요? [선택 "
                 "의뢰 시작: 남겨 둔 불빛]");
        }
        else if (state.isPowerRestored && !state.isSideQuestComplete)
        {
            state.isSideQuestComplete = true;
            tell("마라 / 불이 돌아왔어요. 아이가 어두우면 혼자 있질 못하거든요. 정말 고마워요. [선택 "
                 "의뢰 완료: 남겨 둔 불빛]");
        }
        else if (state.isSideQuestComplete && state.isPowerRestored)
        {
            if (Dialogue::Danger())
            {
                ShowNotice("마라와의 대화 / 안전을 확보한 뒤 다시 이야기하세요.");
            }
            else
            {
                Dialogue::Start("story/mara/followup",
                                StoryDialogue::MaraFollowUp(state, game::GameLevels::Chapter().GetState()),
                                {}, true);
            }
        }
        else
        {
            tell("마라 / 전력 장치는 광장 동쪽에 있어요. 주황색 표시가 있는 장치예요. 부탁드릴게요.");
        }
    }
    if (target == InteractionTarget::PowerRelay)
    {
        if (state.isSideQuestAccepted)
        {
            state.isPowerRestored = true;
            ShowNotice("전력 장치 / 전력 복구 완료. 주거 구역 창문에 불이 켜집니다. 마라에게 돌아가세요.");
        }
        else
        {
            ShowNotice("전력 장치 / 지역 정전 감지. 먼저 광장의 마라에게 상황을 물어보세요.");
        }
    }
    if (target == InteractionTarget::GateBeacon)
    {
        if (!state.surfaceExploration && state.mainQuestStage < MainQuestStage::EnterObservatory)
        {
            ShowNotice("전망대 / 리아가 접근 코드를 가지고 있습니다. 집 근처의 리아와 먼저 대화하세요.");
            return;
        }
        state.isInObservatory = true;
        state.isInsideHome = false;
        state.playerPosition = CityFacilities::Find(target)->arrival;
        state.StopMotion();
        state.cameraPosition = {0, 0};
        state.surfaceCameraHeight = 0;
        std::fill(state.pressedKeys, state.pressedKeys + 256, false);
        if (!state.gateSignalHeard && (!state.surfaceExploration || state.missions.Active(MissionId::Signal)))
        {
            state.mainQuestStage = MainQuestStage::ReadGateSignal;
        }
        ShowNotice("전망대 / 승강기를 타고 관측 데크에 도착했습니다. 북동쪽의 신호 측정 콘솔로 이동하세요.");
    }
    if (target == InteractionTarget::ObservatorySignal)
    {
        if (state.surfaceExploration && state.gateSignalHeard)
        {
            Dialogue::Start("surface/observatory-archive",
                            {{"archive", "리아", "수신 기록",
                              "전망대에서 받은 기록은 보관돼 있어. 광산 구조 신호와 같은 통신선을 거쳤다는 "
                              "사실도 확인했지."}},
                            {}, true);
            return;
        }
        state.gateSignalHeard = true;
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::DescendObservatory);
        Dialogue::Start("MQ00/observatory",
                        {{"voice", "발신자 미상", "수신 기록", "…수신 확인. 이 목소리가 들리면…", {}},
                         {"observation", "나", "현장", "내 목소리다. 이런 말을 녹음한 기억은 없는데.", {}},
                         {"ria", "리아", "무전", "기록은 저장됐어. 내려오면 같이 확인해 보자.", {}}});
    }
    if (target == InteractionTarget::ObservatoryExit)
    {
        if (!state.surfaceExploration && !state.gateSignalHeard)
        {
            ShowNotice("조사 안내 / 관측 데크 북동쪽 콘솔에서 신호 기록을 받은 뒤 내려가세요.");
            return;
        }
        if (state.surfaceExploration)
        {
            if (ReturnToSurface(kBeaconPosition))
            {
                ShowNotice("헤일로 / 전망대 앞 승강기로 내려왔습니다.");
            }
            return;
        }
        state.isInObservatory = false;
        state.hasDescendedObservatory = true;
        state.playerPosition = kBeaconPosition;
        state.StopMotion();
        state.cameraPosition = kBeaconPosition;
        std::fill(state.pressedKeys, state.pressedKeys + 256, false);
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::MountJetBike);
        ShowNotice("신호 기록 저장 / 집 근처에서 기다리는 리아에게 기록을 보여 주세요.");
    }
    if (target == InteractionTarget::JetBike)
    {
        if ((!state.gateSignalHeard || !state.hasDescendedObservatory) &&
            !game::GameLevels::Mine().GetState().quest.IsRiaJoined())
        {
            ShowNotice("제트 바이크 / 먼저 리아와 대화하고 전망대에서 신호를 측정한 뒤 내려오세요.");
            return;
        }
        game::GameLevels::Mine().GetState().quest.BeginJourney();
        Overworld::ToggleBike(state);
    }
    if (target == InteractionTarget::None)
    {
        ShowNotice("인물이나 빛나는 단말 가까이에서 E 키를 누르세요.");
    }
}

// ============================================================================
// Game — optional surface-story interactions
// ============================================================================
void Game::InteractSurfaceStory(InteractionTarget target)
{
    auto &mine = game::GameLevels::Mine().GetState();
    auto &chapter = game::GameLevels::Chapter();
    auto say = [&](const std::string &key, const std::string &text) {
        Dialogue::Say("surface/story/" + key, text);
    };
    if (target == InteractionTarget::HomeTerminal)
    {
        state.homeMessageRead = true;
        SyncMissionProgress();
        if (state.missions.Status(MissionId::Signal) != MissionStatus::Completed &&
            !state.missions.Active(MissionId::Signal))
        {
            OfferMission(MissionId::Signal, "단말 기록");
        }
        else
        {
            say("home", "리아 · 단말 메시지 / N-0 정기 측정 요청과 전망대 접근 코드를 보관했습니다. 외출과 "
                        "탐사는 자유입니다.");
        }
        return;
    }
    if (target == InteractionTarget::ObservatorySignal)
    {
        bool first = !state.gateSignalHeard;
        state.gateSignalHeard = true;
        SyncMissionProgress();
        if (first)
        {
            Dialogue::Start("MQ00/observatory",
                            {{"voice", "발신자 미상", "수신 기록", "…수신 확인. 이 목소리가 들리면…"},
                             {"observation", "나", "현장", "내 목소리다. 이런 말을 녹음한 기억은 없는데."},
                             {"ria", "리아", "무전", "기록은 저장됐어. 내려오면 같이 확인해 보자."}});
        }
        else
        {
            say("observatory/archive", "전망대 기록 / 음성과 수신 경로는 이미 보관돼 있습니다.");
        }
        return;
    }
    if (target == InteractionTarget::ObservatoryExit)
    {
        if (ReturnToSurface(kBeaconPosition))
        {
            if (state.gateSignalHeard)
            {
                state.hasDescendedObservatory = true;
            }
            SyncMissionProgress();
            ShowNotice(
                "헤일로 / 전망대 앞 승강기로 내려왔습니다. 임무를 받지 않아도 자유롭게 탐사할 수 있습니다.");
        }
        return;
    }
    if (target == InteractionTarget::Mara)
    {
        if (!state.missions.Active(MissionId::Mara) &&
            state.missions.Status(MissionId::Mara) != MissionStatus::Completed)
        {
            OfferMission(MissionId::Mara, "현장");
        }
        else if (CompleteMission(MissionId::Mara))
        {
            say("mara/report", "마라 / 불이 돌아왔어요. 정말 고마워요. [선택 의뢰 완료: 남겨 둔 불빛]");
        }
        else if (state.isSideQuestComplete && state.isPowerRestored)
        {
            if (Dialogue::Danger())
            {
                ShowNotice("마라와의 대화 / 안전을 확보한 뒤 다시 이야기하세요.");
            }
            else
            {
                Dialogue::Start("story/mara/followup", StoryDialogue::MaraFollowUp(state, chapter.GetState()),
                                {}, true);
            }
        }
        else
        {
            say("mara/status/" + std::to_string(state.isSideQuestComplete),
                state.isSideQuestComplete ? "마라 / 언제든 들러요. 따뜻한 차를 준비해 둘게요."
                                          : "마라 / 동쪽 전력 장치를 살펴봐 주세요. 천천히 다녀오세요.");
        }
        return;
    }
    if (target == InteractionTarget::PowerRelay)
    {
        state.isPowerRestored = true;
        ShowNotice(
            "전력 장치 / 주거 구역 전력을 복구했습니다. 복구 사실은 임무 참여 여부와 별도로 유지됩니다.");
        return;
    }
    if (target == InteractionTarget::Lia)
    {
        if (MissionEligible(MissionId::Mine) &&
            state.missions.Status(MissionId::Mine) != MissionStatus::Completed)
        {
            if (!state.missions.Active(MissionId::Mine))
            {
                OfferMission(MissionId::Mine, "현장");
                return;
            }
            if (mine.quest.GetStage() == game::MineQuestStage::TravelToMine && !mine.quest.IsRiaJoined())
            {
                Dialogue::Start(
                    "mission/mine/companion",
                    {{"join",
                      "리아",
                      "동행 제안",
                      "광산 조사에 동행할까? 네가 준비되면 같이 가자.",
                      {{"join", "동행을 요청한다"}, {"later", "아직 혼자 둘러본다"}}}},
                    [this](const std::string &choice) {
                        if (choice == "join" && state.missions.Active(MissionId::Mine))
                        {
                            game::GameLevels::Mine().GetState().quest.BeginJourney();
                        }
                    },
                    true);
                return;
            }
            Dialogue::Start("surface/story/mine/status/" + std::to_string(int(mine.quest.GetStage())),
                            StoryDialogue::MineStatus(mine, StoryContact::Ria));
            return;
        }
        if (MissionEligible(MissionId::Frontier) && !state.missions.Active(MissionId::Frontier) &&
            state.missions.Status(MissionId::Frontier) != MissionStatus::Completed)
        {
            OfferMission(MissionId::Frontier, "현장");
            return;
        }
        if (state.missions.Active(MissionId::Frontier) ||
            state.missions.Status(MissionId::Frontier) == MissionStatus::Completed)
        {
            Dialogue::Start("surface/story/frontier/status",
                            StoryDialogue::FrontierStatus(chapter.GetState()), {}, true);
            return;
        }
        if (state.missions.Active(MissionId::Signal))
        {
            say("signal/status", "리아 / 전망대의 수신 기록을 확인해 줘. 출입과 탐사는 자유야.");
            return;
        }
        if (state.missions.Status(MissionId::Signal) != MissionStatus::Completed)
        {
            OfferMission(MissionId::Signal, "현장");
            return;
        }
        say("lia/free",
            "리아 / 기록은 보관돼 있어. 수신망에서 조사 위치를 확인하거나 네가 원하는 곳을 탐사해.");
        return;
    }
    if (target == InteractionTarget::CityReceiver)
    {
        if (state.missions.Active(MissionId::Mine))
        {
            if (CompleteMission(MissionId::Mine))
            {
                chapter.Unlock();
                say("mine/report", "수신망 / 광산 중계 좌표를 보고했습니다. N-0 외곽 설비와 연결된 세 현장의 "
                                   "조사 제안을 받을 수 있습니다.");
                return;
            }
            if (mine.quest.Advance(game::MineQuestStage::CheckCityRelay))
            {
                state.planetDestination =
                    surface->Stream().Snapshot()->planet->Region("mq00_mine").access.position;
                say("mine/route", "수신망 / 전망대 신호와 광산 통신선의 중계 흔적이 겹칩니다. 리아에게 "
                                  "동행을 요청할 수 있습니다. 입구는 지도에 표시했습니다.");
                return;
            }
            if (mine.quest.GetStage() == game::MineQuestStage::TravelToMine)
            {
                state.planetDestination =
                    surface->Stream().Snapshot()->planet->Region("mq00_mine").access.position;
            }
            Dialogue::Start("surface/story/mine/objective/" + std::to_string(int(mine.quest.GetStage())),
                            StoryDialogue::MineStatus(mine, StoryContact::Receiver));
            return;
        }
        if (MissionEligible(MissionId::Frontier))
        {
            if (state.missions.Active(MissionId::Frontier) ||
                state.missions.Status(MissionId::Frontier) == MissionStatus::Completed)
            {
                chapter.InteractHub();
            }
            else
            {
                OfferMission(MissionId::Frontier, "수신망 기록");
            }
            return;
        }
        if (MissionEligible(MissionId::Mine))
        {
            OfferMission(MissionId::Mine, "수신망 기록");
            return;
        }
        say("receiver/free", "수신망 / 지역 정보는 열려 있습니다. 전망대에서 확보한 음성 기록이 있으면 "
                             "리아와 수신 경로를 조사할 수 있습니다.");
    }
}
// ============================================================================
// RelayTravel — keeps the source live until destination chunks and an unoccupied landing are ready
// ============================================================================
const world::RelayCandidate *RelayTravel::Nearby()
{
    auto &g = Game::Get();
    const auto &s = g.GetState();
    if (!s.surfaceExploration || g.IsSurfaceMine() || s.isInsideHome || s.isInObservatory || !g.Surface() ||
        !g.Surface()->Ready())
    {
        return nullptr;
    }
    return world::RelayNetwork::Nearest(*g.Surface()->Stream().Snapshot()->planet,
                                        g.Surface()->Pose().origin);
}
const char *RelayTravel::Restriction(std::string_view destination)
{
    auto &g = Game::Get();
    const auto &s = g.GetState();
    if (!s.surfaceExploration || !g.Surface() || !g.Surface()->Ready())
    {
        return "행성 지형을 준비 중입니다.";
    }
    if (g.IsSurfaceMine() || s.isInsideHome || s.isInObservatory)
    {
        return "지상의 중계환에서 이용할 수 있습니다.";
    }
    if (s.isBikeMounted)
    {
        return "바이크를 주차하고 내려 주세요. 차량은 출발지에 남습니다.";
    }
    if (HospitalRecovery::Active() || game::GameLevels::Mine().GetState().player.health <= 0)
    {
        return "의료 이송 중에는 이용할 수 없습니다.";
    }
    if (s.isPaused || s.ui.screen != UiScreen::None || s.journalVisible || s.dialogueHistoryVisible ||
        s.weaponWheel.visible || Dialogue::Active() || s.dialogue.suspended)
    {
        return "열린 메뉴나 대화를 마친 뒤 이용해 주세요.";
    }
    if (CivicJustice::State().wanted > 0 || Dialogue::Danger())
    {
        return "수배 또는 교전 중에는 이용할 수 없습니다.";
    }
    const auto *source = Nearby();
    if (!source || !s.relayNetwork.Registered(source->id))
    {
        return "가까운 중계환을 E로 등록한 뒤 이용해 주세요.";
    }
    const auto *target = world::RelayNetwork::Find(*g.Surface()->Stream().Snapshot()->planet, destination);
    if (!target || !s.relayNetwork.Registered(destination))
    {
        return "목적지 중계환에 직접 방문해 등록해야 합니다.";
    }
    if (source->id == target->id)
    {
        return "현재 위치의 중계환입니다. 다른 목적지를 선택하세요.";
    }
    return nullptr;
}
bool RelayTravel::Begin(const std::string &destination)
{
    auto &g = Game::Get();
    auto &s = g.GetState();
    if (s.relayPreparing)
    {
        return false;
    }
    if (auto reason = Restriction(destination))
    {
        s.relayStatus = reason;
        g.ShowNotice(std::string("중계 이동 / ") + reason);
        return false;
    }
    s.relaySource = Nearby()->id;
    s.relayStatus.clear();
    s.relayTarget = destination;
    s.relayElapsed = 0;
    s.relayPreparing = true;
    s.selectedRelay = destination;
    s.mapVisible = true;
    s.StopMotion();
    s.mouseAiming = s.mouseFiring = false;
    std::fill(std::begin(s.pressedKeys), std::end(s.pressedKeys), false);
    g.ShowNotice("중계 이동 / 도착 지형과 착지 공간을 준비합니다. X 또는 Esc로 취소할 수 있습니다.");
    return true;
}
void RelayTravel::Cancel(const std::string &reason)
{
    auto &g = Game::Get();
    auto &s = g.GetState();
    if (!s.relayPreparing)
    {
        return;
    }
    s.relayPreparing = false;
    s.relayStatus = reason;
    s.relaySource.clear();
    s.relayTarget.clear();
    s.relayElapsed = 0;
    s.StopMotion();
    g.ShowNotice(std::string("중계 이동 / ") + reason);
}
void RelayTravel::Interact()
{
    auto &g = Game::Get();
    auto &s = g.GetState();
    const auto *relay = Nearby();
    if (!relay || s.isBikeMounted || s.relayPreparing)
    {
        return;
    }
    if (Dialogue::Active() || s.dialogue.suspended || HospitalRecovery::Active())
    {
        return;
    }
    bool added = s.relayNetwork.Register(*g.Surface()->Stream().Snapshot()->planet, relay->id,
                                         g.Surface()->Location());
    s.selectedRelay = relay->id;
    s.relayStatus.clear();
    s.mapVisible = true;
    s.planetMapSurface = true;
    s.StopMotion();
    g.ShowNotice(
        added ? "국지 중계환 등록 / M 지도에서 등록한 다른 중계환을 선택하세요. 임무 진행은 바뀌지 않습니다."
              : "국지 중계환 / 등록한 다른 목적지를 지도에서 선택하세요.");
}
void RelayTravel::Update(float dt)
{
    auto &g = Game::Get();
    auto &s = g.GetState();
    if (!s.relayPreparing)
    {
        return;
    }
    if (auto reason = Restriction(s.relayTarget))
    {
        Cancel(reason);
        return;
    }
    if (!Nearby() || Nearby()->id != s.relaySource)
    {
        Cancel("출발 중계환을 벗어났습니다.");
        return;
    }
    s.relayElapsed += dt;
    const auto *target = world::RelayNetwork::Find(*g.Surface()->Stream().Snapshot()->planet, s.relayTarget);
    const auto snapshot = g.Surface()->Stream().Snapshot();
    if (g.Surface()->Stream().FailedAt(target->location.position, 96))
    {
        Cancel("도착 지형을 준비하지 못했습니다. 출발지에서 다시 시도해 주세요.");
        return;
    }
    if (s.relayElapsed > 20)
    {
        Cancel("도착 준비 시간이 초과됐습니다. 출발 위치를 유지합니다.");
        return;
    }
    if (s.relayElapsed < .35f || !g.Surface()->Stream().Detailed(target->location.position, 96))
    {
        return;
    }
    const auto destination = target->location.position;
    const auto name = snapshot->planet->Region(target->regionId).definition.name;
    if (!g.LandOnSurface(destination))
    {
        Cancel("도착 공간이 점유돼 있습니다. 출발 위치를 유지합니다.");
        return;
    }
    s.relayPreparing = false;
    s.relaySource.clear();
    s.relayTarget.clear();
    s.relayElapsed = 0;
    s.planetDestination.reset();
    if (g.SurfaceCities())
    {
        g.SurfaceCities()->Update(s, *g.Surface(), 0);
    }
    game::GameLevels::Chapter().Surface().Refresh();
    s.relayNetwork.Observe(*snapshot->planet, g.Surface()->Location());
    g.ShowNotice("중계 이동 / " + name + " 도착. 바이크와 현장 인원·조사·보상 상태는 유지됩니다.");
}
// ============================================================================
// GameCallbacks — window entry points
// ============================================================================
void GameCallbacks::Draw()
{
    GameRenderer::Get().Draw();
}
void GameCallbacks::Resize(int w, int h)
{
    Game::Get().Resize(w, h);
}
void GameCallbacks::KeyDown(unsigned char key, int x, int y)
{
    Game::Get().KeyDown(key, x, y);
}
void GameCallbacks::SpecialKeyDown(int key, int x, int y)
{
    Game::Get().SpecialKeyDown(key, x, y);
}
void GameCallbacks::KeyUp(unsigned char key, int x, int y)
{
    Game::Get().KeyUp(key, x, y);
}
void GameCallbacks::MouseMotion(int x, int y)
{
    Game::Get().MouseMotion(x, y);
}
void GameCallbacks::MouseButton(int button, int state, int x, int y)
{
    Game::Get().MouseButton(button, state, x, y);
}
void GameCallbacks::MouseEntry(int state)
{
    Game::Get().MouseEntry(state);
}
void GameCallbacks::Tick(int timer)
{
    Game::Get().Tick(timer);
}
void GameCallbacks::Shutdown()
{
    WeaponWheel::Close();
    GameRenderer::Get().Shutdown();
}
} // namespace tutorial
