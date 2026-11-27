#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Game.h"
#include "PlayerCombat.h"
#include "Story.h"
#include "StoryArc.h"
#include "StoryJournal.h"
#include "WorldEncounters.h"
#include "FieldMinigame.h"
#include "CityFacilities.h"
#include "FacilityApproaches.h"
#include "CityInfrastructure.h"
#include "Civic.h"
#include "Hud.h"
#include "Save.h"
#include "RenderAssets.h"
#include "ResourceAssets.h"
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
#include <fstream>
#include <limits>
#include <thread>

namespace tutorial
{
namespace
{
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
        const float aspect = client.bottom > 0 ? float(client.right) / float(client.bottom) : 1.6f;
        RECT next{};
        if (aspect >= float(kCanvasWidth) / float(kCanvasHeight) - 1e-4f)
        {
            next = {origin.x, origin.y, origin.x + (std::max)(1L, client.right),
                    origin.y + (std::max)(1L, client.bottom)};
        }
        else
        {
            float scale = (std::min)(client.right / float(kCanvasWidth), client.bottom / float(kCanvasHeight));
            int width = (std::max)(1, int(kCanvasWidth * scale));
            int height = (std::max)(1, int(kCanvasHeight * scale));
            next = {origin.x + (client.right - width) / 2, origin.y + (client.bottom - height) / 2, 0, 0};
            next.right = next.left + width;
            next.bottom = next.top + height;
        }
        const int width = next.right - next.left;
        const int height = next.bottom - next.top;
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
WorldMode Game::Mode() const
{
    if (story::StoryArc::Get().IsActive())
    {
        return WorldMode::StoryOperation;
    }
    if (IsSurfaceMine())
    {
        return WorldMode::SurfaceMine;
    }
    if (state.surfaceExploration)
    {
        if (state.isInsideHome)
        {
            return WorldMode::SurfaceInterior;
        }
        if (state.isInObservatory)
        {
            return WorldMode::SurfaceObservatory;
        }
        if (state.facilityInterior.Active())
        {
            return WorldMode::SurfaceFacility;
        }
        return WorldMode::SurfaceExterior;
    }
    if (game::GameLevels::Chapter().IsActive())
    {
        return WorldMode::LegacyFrontier;
    }
    if (game::GameLevels::Mine().IsActive())
    {
        return WorldMode::LegacyMine;
    }
    if (state.isInObservatory)
    {
        return WorldMode::LegacyObservatory;
    }
    if (state.isInsideHome)
    {
        return WorldMode::LegacyInterior;
    }
    return WorldMode::LegacyCity;
}
bool Game::IsSurfaceMode() const
{
    const auto mode = Mode();
    return mode == WorldMode::SurfaceExterior || mode == WorldMode::SurfaceInterior ||
           mode == WorldMode::SurfaceObservatory || mode == WorldMode::SurfaceFacility ||
           mode == WorldMode::SurfaceMine;
}
bool Game::IsInteriorMode() const
{
    const auto mode = Mode();
    return mode == WorldMode::LegacyInterior || mode == WorldMode::LegacyObservatory ||
           mode == WorldMode::SurfaceInterior || mode == WorldMode::SurfaceObservatory ||
           mode == WorldMode::SurfaceFacility;
}
bool Game::IsExteriorMode() const
{
    const auto mode = Mode();
    return mode == WorldMode::LegacyCity || mode == WorldMode::SurfaceExterior;
}
game::PlayerStats &Game::Player()
{
    return game::GameLevels::Mine().GetState().player;
}
const game::PlayerStats &Game::Player() const
{
    return game::GameLevels::Mine().GetState().player;
}
combat::PlayerLoadout &Game::Loadout()
{
    return game::GameLevels::Mine().GetState().loadout;
}
const combat::PlayerLoadout &Game::Loadout() const
{
    return game::GameLevels::Mine().GetState().loadout;
}
combat::Pistol &Game::Sidearm()
{
    return game::GameLevels::Mine().GetState().pistol;
}
const combat::Pistol &Game::Sidearm() const
{
    return game::GameLevels::Mine().GetState().pistol;
}
combat::PulseCarbine &Game::PrimaryWeapon()
{
    return game::GameLevels::Mine().GetState().carbine;
}
const combat::PulseCarbine &Game::PrimaryWeapon() const
{
    return game::GameLevels::Mine().GetState().carbine;
}
combat::Firearm &Game::ActiveFirearm()
{
    return Loadout().IsPrimaryEquipped() ? static_cast<combat::Firearm &>(PrimaryWeapon())
                                         : static_cast<combat::Firearm &>(Sidearm());
}
const combat::Firearm &Game::ActiveFirearm() const
{
    return Loadout().IsPrimaryEquipped() ? static_cast<const combat::Firearm &>(PrimaryWeapon())
                                         : static_cast<const combat::Firearm &>(Sidearm());
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
    return MissionObjective(FocusedMission());
}
const char *Game::MissionObjective(MissionId focused) const
{
    if (!IsStoryWorld())
    {
        return "행성 자유 탐사 · 중계환 발견·등록 / M 지도";
    }
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
    if (story::StoryArc::Get().IsActive())
    {
        goal.title = story::StoryArc::Get().CurrentObjectiveTitle();
        goal.action = story::StoryArc::Get().CurrentObjectiveAction();
        goal.location = story::StoryArc::Get().CurrentObjectiveLocation();
        return goal;
    }
    if (!w.surfaceExploration || !g.Surface() || !g.Surface()->Ready())
    {
        if (story::StoryArc::Get().IsActTwoUnlocked())
        {
            goal.title = story::StoryArc::Get().CurrentObjectiveTitle();
            goal.action = story::StoryArc::Get().CurrentObjectiveAction();
            goal.location = story::StoryArc::Get().CurrentObjectiveLocation();
        }
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
        auto city = [&](Vector2 point) {
            goal.location = "헤일로";
            if (g.SurfaceCities())
            {
                goal.surfaceTarget = g.SurfaceCities()->Point(*g.Surface(), 0, point);
            }
        };
        if ((goal.mission == MissionId::Count || goal.mission == MissionId::Frontier) && g.IsStoryWorld() &&
            story::StoryArc::Get().IsActTwoUnlocked())
        {
            const auto &arc = story::StoryArc::Get().State();
            goal.title = story::StoryArc::Get().CurrentObjectiveTitle();
            goal.action = story::StoryArc::Get().CurrentObjectiveAction();
            goal.location = story::StoryArc::Get().CurrentObjectiveLocation();
            if (arc.sectorLossTriggered && !arc.chapter4Complete)
            {
                city(!arc.safetyLineChecked ? Vector2{25.f, 7.f} : kMaraPosition);
            }
            else if (arc.chapter4Complete)
            {
                if (!arc.epilogueShelterVisited)
                {
                    city(kMaraPosition);
                }
                else if (!arc.epilogueClinicVisited)
                {
                    city({11.f, -2.f});
                }
                else if (!arc.epilogueWorkshopVisited)
                {
                    city({19.f, 2.f});
                }
                else
                {
                    city(kBeaconPosition);
                }
            }
            else if (arc.veilHypothesisFound && !arc.chapter2Reported)
            {
                city(kLiaPosition);
            }
            else if (arc.chapter2Reported && !arc.chapter3Committed)
            {
                city(kLiaPosition);
            }
            else
            {
                goal.surfaceTarget = plan->Region("n0_outskirts").access.position;
            }
            return goal;
        }
        if (goal.mission == MissionId::Count)
        {
            if (g.IsSurfaceMine())
            {
                goal.action = "광산 자유 탐사 · 기록 보관";
            }
            return goal;
        }
        goal.title = QuestParticipation::Name(goal.mission);
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
            case game::MineQuestStage::CheckCityRelay:
                city(kLiaPosition);
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
                    city(kLiaPosition);
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
                city(kLiaPosition);
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
std::optional<world::SurfacePoint> ActivityJournal::MissionGiverTarget(MissionId id)
{
    auto &g = Game::Get();
    const auto &w = g.GetState();
    if (!w.surfaceExploration || !g.Surface() || !g.Surface()->Ready() || !g.IsStoryWorld() || !g.SurfaceCities())
    {
        return std::nullopt;
    }
    switch (id)
    {
    case MissionId::Signal:
        return g.SurfaceCities()->Point(*g.Surface(), 0, !w.homeMessageRead ? kHomeDoor : kLiaPosition);
    case MissionId::Mara:
        return g.SurfaceCities()->Point(*g.Surface(), 0, kMaraPosition);
    case MissionId::Mine:
    case MissionId::Frontier:
        return g.SurfaceCities()->Point(*g.Surface(), 0, kLiaPosition);
    default:
        return std::nullopt;
    }
}
std::optional<world::SurfacePoint> ActivityJournal::GuidanceTarget(std::string *labelOut)
{
    auto &g = Game::Get();
    const auto &w = g.GetState();
    if (!w.surfaceExploration || !g.Surface() || !g.Surface()->Ready())
    {
        return std::nullopt;
    }
    if (w.guidanceOverrideTarget)
    {
        if (labelOut)
        {
            *labelOut = w.guidanceLabel.empty() ? "퀘스트 제안자" : w.guidanceLabel;
        }
        return w.guidanceOverrideTarget;
    }
    const auto goal = Current();
    if (goal.surfaceTarget)
    {
        if (labelOut)
        {
            *labelOut = !goal.title.empty() ? goal.title : goal.location;
        }
        return goal.surfaceTarget;
    }
    if (w.planetDestination)
    {
        if (labelOut)
        {
            *labelOut = "지정 목적지";
        }
        return w.planetDestination;
    }
    if (g.IsStoryWorld())
    {
        for (int i = 0; i < int(MissionId::Count); ++i)
        {
            const auto id = MissionId(i);
            if (w.missions.Status(id) == MissionStatus::NotAccepted && g.MissionEligible(id))
            {
                if (auto pt = MissionGiverTarget(id))
                {
                    if (labelOut)
                    {
                        *labelOut = std::string(QuestParticipation::Name(id)) + " 제안자";
                    }
                    return pt;
                }
            }
        }
    }
    return std::nullopt;
}
bool ActivityJournal::TriggerGuidance(std::optional<world::SurfacePoint> overrideTarget, const std::string &label)
{
    auto &g = Game::Get();
    auto &w = g.GetState();
    if (g.IsSurfaceMine() || game::GameLevels::Mine().IsActive())
    {
        const auto &mine = game::GameLevels::Mine().GetState();
        g.ShowNotice("경로 안내 [V] / " + std::string(mine.quest.GetObjective()) + " (F 스캔 · 입구 E 귀환)");
        return true;
    }
    if (w.IsInsideAny())
    {
        const auto goal = Current();
        g.ShowNotice("경로 안내 [V] / " + (goal.action.empty() ? std::string("야외로 나가면 3D 경로가 표시됩니다")
                                                               : goal.action + " · 야외로 나가면 3D 경로 표시"));
        return true;
    }
    if (!w.surfaceExploration || !g.Surface() || !g.Surface()->Ready())
    {
        return false;
    }
    if (overrideTarget)
    {
        w.guidanceOverrideTarget = overrideTarget;
        w.guidanceLabel = label;
        w.planetDestination = overrideTarget;
    }
    else
    {
        w.guidanceOverrideTarget.reset();
        w.guidanceLabel.clear();
    }
    std::string targetLabel;
    const auto target = GuidanceTarget(&targetLabel);
    if (!target)
    {
        g.ShowNotice("경로 안내 [V] / 추적 중인 임무나 수령 가능한 제안자가 없습니다. J 또는 M에서 목표를 지정하세요.");
        return false;
    }
    w.guidanceDurationSeconds = 8.f;
    w.guidanceSecondsRemaining = 8.f;
    const double meters = world::SurfaceGeometry::Distance(g.Surface()->Planet().radiusMeters,
                                                           g.Surface()->Pose().origin, *target);
    g.ShowNotice("경로 안내 [V] / " + (targetLabel.empty() ? std::string("목표 지점") : targetLabel) + " · 약 " +
                 std::to_string(int(std::round(meters))) + "m");
    return true;
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
        return;
    }
    if (state.missions.Status(id) == MissionStatus::NotAccepted)
    {
        // Remote acceptance from J is disabled; instead guide the player directly to the in-world quest giver.
        state.journalVisible = false;
        if (auto giver = ActivityJournal::MissionGiverTarget(id))
        {
            ActivityJournal::TriggerGuidance(giver, std::string(QuestParticipation::Name(id)) + " 제안자");
            ShowNotice(std::string("현장 수락 필요 / ") + QuestParticipation::Name(id) +
                       " 제안자에게 직접 방문해 수락하세요 · 3D 경로 안내 표시 [V]");
        }
        else
        {
            ShowNotice("현장 수락 필요 / 헤일로 현장의 제안자에게 직접 방문해 수락하세요.");
        }
        return;
    }
    OfferMission(id);
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
float WorldCamera::zoom = WorldCamera::kDefaultZoom;
float WorldCamera::targetZoom = WorldCamera::kDefaultZoom;
float WorldCamera::topViewBlend = 0.f;

float WorldCamera::IsoYScale()
{
    return 18.f + 16.f * topViewBlend;
}
float WorldCamera::IsoZScale()
{
    return 44.f - 28.f * topViewBlend;
}
float WorldCamera::LogicalWidth()
{
    const auto &w = Game::Get().GetState();
    if (w.viewportWidth <= 0 || w.viewportHeight <= 0)
    {
        return float(kCanvasWidth);
    }
    const float aspect = float(w.viewportWidth) / float(w.viewportHeight);
    return (std::max)(float(kCanvasWidth), float(kCanvasHeight) * aspect);
}
float WorldCamera::CenterOffsetX()
{
    return (LogicalWidth() - float(kCanvasWidth)) * .5f;
}
float WorldCamera::RightOffsetX()
{
    return LogicalWidth() - float(kCanvasWidth);
}
void WorldCamera::StepZoom(int direction)
{
    if (direction == 0)
    {
        return;
    }
    const float factor = direction > 0 ? 1.18f : (1.f / 1.18f);
    SetZoom(targetZoom * factor, false);
}
void WorldCamera::SetZoom(float value, bool immediate)
{
    targetZoom = Game::Clamp(value, kMinZoom, kMaxZoom);
    if (immediate)
    {
        zoom = targetZoom;
    }
}
void WorldCamera::Update(float deltaSeconds, bool aircraftMounted)
{
    if (deltaSeconds <= 0.f)
    {
        return;
    }
    const float zoomFollow = 1.f - std::exp(-10.f * deltaSeconds);
    zoom += (targetZoom - zoom) * zoomFollow;
    if (std::fabs(targetZoom - zoom) < 0.0005f)
    {
        zoom = targetZoom;
    }
    const float targetTop = aircraftMounted ? 1.f : 0.f;
    const float topFollow = 1.f - std::exp(-2.6f * deltaSeconds);
    topViewBlend += (targetTop - topViewBlend) * topFollow;
    if (std::fabs(targetTop - topViewBlend) < 0.0005f)
    {
        topViewBlend = targetTop;
    }
}
void WorldCamera::Reset()
{
    zoom = targetZoom = kDefaultZoom;
    topViewBlend = 0.f;
}
Vector2 WorldCamera::ToViewport(Vector2 p)
{
    const float cx = LogicalWidth() * .5f;
    return {cx + (p.x - cx) * zoom, kCanvasHeight * .57f + (p.y - kCanvasHeight * .57f) * zoom};
}
Vector2 WorldCamera::FromViewport(Vector2 p)
{
    const float cx = LogicalWidth() * .5f;
    return {cx + (p.x - cx) / zoom, kCanvasHeight * .57f + (p.y - kCanvasHeight * .57f) / zoom};
}
void MovementFacing::Update(Vector2 from, Vector2 to, float dt)
{
    const float dx = to.x - from.x, dy = to.y - from.y;
    walking = dx * dx + dy * dy > 1.e-10f;
    if (dt <= 0)
    {
        return;
    }
    const float now = Game::Get().GetState().animationTimeSeconds;
    const float idle = (std::max)(0.f, now - lastMotionSeconds - dt);
    const float speed = std::hypot(dx, dy) / dt;
    motionSpeed = motionSpeed * std::exp(-14 * (idle + dt)) + speed * (1 - std::exp(-14 * dt));
    const float metersPerSecond = speed * models::WorldScale::metersPerUnit;
    const float run = Game::Clamp((metersPerSecond - 1.08f) / (2.46f - 1.08f), 0, 1);
    const float stride = (1.08f + (2.46f - 1.08f) * run) / (1 + .5f * run);
    motionPhase =
        std::fmod(motionPhase + std::hypot(dx, dy) * models::WorldScale::metersPerUnit / stride, 1.f);
    lastMotionSeconds = now + dt;
    if (walking)
    {
        yaw = std::atan2(dy, dx);
    }
}
bool MovementFacing::Follow(float distance, float stopDistance, bool visible)
{
    if (following)
    {
        following = !visible || distance > stopDistance + .04f;
    }
    else
    {
        following = !visible || distance > stopDistance + .6f;
    }
    return following;
}
void MovementFacing::ApplyMotion(models::CharacterPose &pose) const
{
    const float age = (std::max)(0.f, Game::Get().GetState().animationTimeSeconds - lastMotionSeconds);
    const float speed = motionSpeed * std::exp(-14 * age);
    pose.blended = true;
    pose.locomotionSpeed = speed;
    pose.locomotionPhase = motionPhase;
    pose.moveLeft = 0;
    pose.moveForward = speed * models::WorldScale::metersPerUnit;
    pose.aimWeight = pose.armed ? 1.f : 0.f;
    pose.fireSeconds = pose.recoil * .4f;
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
void PlayerMotion::Stop()
{
    velocity = {};
    forwardLean = sideLean = 0;
    pivoting = false;
    mobility.Cancel();
}
void GameState::StopMotion()
{
    playerMotion.Stop();
    playerAnimation.InterruptDodge();
    bikeVelocity = {};
    bikeYawSpeed = bikeSpeed = movementAmount = movementSpeed = 0;
    car.velocity = {};
    car.speed = 0;
    aircraft.Stop();
}
void GameState::InterruptDodge()
{
    if (playerMotion.mobility.Dodging())
    {
        playerMotion.mobility.Cancel();
        playerMotion.velocity = {};
    }
    playerAnimation.InterruptDodge();
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
bool Game::HasWalkingControls() const
{
    if (state.car.mounted || state.aircraft.mounted)
    {
        return false;
    }
    if (state.surfaceExploration)
    {
        return state.IsInsideAny() || (surface && surface->Ready());
    }
    const auto *level = game::GameLevels::Active();
    return !level || level->UsesWalkingControls();
}
void Game::UpdatePlayerMovement(float deltaSeconds)
{
    if (state.aircraft.mounted)
    {
        PlayerAircraftController::Drive(*this, deltaSeconds);
        return;
    }
    profiling::Scope profileTimer(profiling::Timer::Movement);
    const Vector2 previous = state.playerPosition;
    if (state.car.mounted)
    {
        UpdateGroundVehicle(deltaSeconds);
        return;
    }
    // A mounted rider's body reaction must not disable the vehicle's controls.
    if (!state.isBikeMounted && CivicJustice::IsExterior() &&
        CivicJustice::State().playerImpact.seconds > 0 &&
        CivicJustice::State().playerImpact.response == BodyResponse::Knockdown)
    {
        state.playerMotion.mobility.Update(deltaSeconds, {}, state.facingYaw, false, false, false, false);
        state.StopMotion();
        return;
    }
    state.movementSpeed = 0;
    if (state.isBikeMounted &&
        (!game::GameLevels::Active() || (state.surfaceExploration && !IsSurfaceMine())) &&
        !state.IsInsideAny())
    {
        Overworld::UpdateBike(state, deltaSeconds);
        state.playerMotion.mobility.Update(deltaSeconds, {}, state.facingYaw, false, false, false, false);
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
    const auto combatContext = combat::PlayerCombat::Current();
    const bool mobilityAllowed =
        combat::PlayerCombat::ControlBlock(combatContext) == combat::CombatBlock::Ready;
    auto &mobility = state.playerMotion.mobility;
    const bool wasDodging = mobility.Dodging();
    const bool wasMoving = mobility.Moving();
    const bool aiming = IsAiming();
    float dodgeFacing = state.facingYaw;
    if (wasDodging && !wasMoving && aiming)
    {
        const auto aim = game::MineLevel::GetAimDirection(game::GameLevels::AimAngle());
        dodgeFacing = std::atan2(aim.y, aim.x);
    }
    const bool sprintHeld =
        state.pressedKeys[platform::SprintKey] && combatContext.area != combat::CombatArea::Inactive;
    const auto dash = mobility.Update(
        deltaSeconds, {target.x, target.y, 0}, dodgeFacing, sprintHeld, aiming, mobilityAllowed,
        combatContext.reloading || combatContext.fireCooldown > 0 || combatContext.meleeRemaining > 0);
    if (!wasMoving && mobility.Moving())
    {
        state.playerAnimation.StartDodge(mobility.Direction(), dodgeFacing);
    }
    if (mobility.PopDodgeTriggered() && Loadout().IsFirearmEquipped())
    {
        ActiveFirearm().TopUpRounds(2);
    }
    if (wasDodging && !state.playerMotion.mobility.Dodging())
    {
        if (!sprintHeld || std::hypot(target.x, target.y) <= .001f)
        {
            state.playerMotion.velocity = {};
        }
    }
    float length = std::hypot(target.x, target.y);
    const float speed =
        kMovementSpeed * (state.playerMotion.mobility.Sprinting() ? PlayerMobility::SprintMultiplier : 1);
    if (length > 0)
    {
        target.x *= speed / length;
        target.y *= speed / length;
    }
    if (state.playerMotion.mobility.Dodging() && deltaSeconds > 0)
    {
        state.playerMotion.velocity = {dash.x / deltaSeconds, dash.y / deltaSeconds};
        state.playerMotion.pivoting = false;
    }
    else
    {
        const bool wasPivoting = state.playerMotion.pivoting;
        const float prevYaw = state.facingYaw;
        state.playerMotion.Update(target, state.facingYaw, aiming, deltaSeconds);
        if (!wasPivoting && state.playerMotion.pivoting && !aiming)
        {
            const float targetYaw = std::atan2(target.y, target.x);
            float diff = targetYaw - prevYaw;
            while (diff > kPi) diff -= 2 * kPi;
            while (diff < -kPi) diff += 2 * kPi;
            state.playerAnimation.TriggerTurn(true, diff < 0);
        }
        else if (!state.playerMotion.pivoting && !aiming && length > .1f &&
                 std::hypot(state.playerMotion.velocity.x, state.playerMotion.velocity.y) > .6f)
        {
            const float targetYaw = std::atan2(target.y, target.x);
            float diff = targetYaw - prevYaw;
            while (diff > kPi) diff -= 2 * kPi;
            while (diff < -kPi) diff += 2 * kPi;
            if (std::fabs(diff) > 1.25f && std::fabs(diff) < 2.2f)
            {
                state.playerAnimation.TriggerTurn(false, diff < 0);
            }
        }
    }
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
    if (state.playerMotion.mobility.Contact(std::hypot(requested.x, requested.y),
                                            Distance(previous, state.playerPosition)))
    {
        state.playerMotion.velocity = {};
        state.playerAnimation.InterruptDodge();
    }
    state.movementAmount = Game::Clamp(state.movementSpeed / kMovementSpeed, 0, 1);
}
void Game::UpdateCamera(float deltaSeconds)
{
    WorldCamera::Update(deltaSeconds, state.surfaceExploration && state.aircraft.mounted);
    Vector2 target = state.IsInsideAny() ? Vector2{0, 0} : state.playerPosition;
    float followAmount = 1 - std::exp(-kCameraFollowRate * deltaSeconds);
    state.cameraPosition.x += (target.x - state.cameraPosition.x) * followAmount;
    state.cameraPosition.y += (target.y - state.cameraPosition.y) * followAmount;
}
world::SurfaceView Game::SurfaceTerrainView() const
{
    // Invert the isometric projection at the camera's ground height. At altitude the visible
    // terrain is behind the aircraft, and can be several chunk widths from the pilot.
    world::SurfaceView view;
    if (!surface || !surface->Ready())
    {
        return view;
    }
    auto &cache = terrainViewCache;
    const auto snapshot = surface->Stream().Snapshot();
    const auto &origin = surface->Origin().origin.normal;
    if (cache.valid && cache.session == surface.get() && snapshot &&
        cache.epoch == snapshot->statistics.epoch && cache.revision == snapshot->statistics.revision &&
        std::fabs(cache.originX - origin.x) < 1.e-11 && std::fabs(cache.originY - origin.y) < 1.e-11 &&
        std::fabs(cache.originZ - origin.z) < 1.e-11 &&
        Game::Distance(cache.camera, state.cameraPosition) < .25f &&
        std::fabs(cache.camHeight - state.surfaceCameraHeight) < .1f &&
        std::fabs(cache.zoom - WorldCamera::zoom) < .001f &&
        std::fabs(cache.topView - WorldCamera::topViewBlend) < .002f)
    {
        return cache.result;
    }
    const double right = state.cameraPosition.x * 2, forward = state.cameraPosition.y * 2;
    const double isoY = WorldCamera::IsoYScale(), isoZ = WorldCamera::IsoZScale();
    auto project = [&](double screenX, double screenY) {
        const double baseX = right + screenX / 36 + screenY / isoY;
        const double baseY = forward - screenX / 36 + screenY / isoY;
        double x = baseX, y = baseY;
        for (unsigned i = 0; i < 6; ++i)
        {
            const double ground =
                (std::max)(world::PlanetTerrain::SeaLevelMeters, surface->ContactAt(x, y).heightMeters);
            const double height =
                surface->Origin().origin.altitudeMeters + state.surfaceCameraHeight * 2 - ground;
            x = baseX - height * (isoZ / (2. * isoY));
            y = baseY - height * (isoZ / (2. * isoY));
        }
        auto point = surface->PointAt(x, y);
        point.altitudeMeters = surface->ContactAt(x, y).heightMeters;
        return point;
    };
    view.center = project(0, 0);
    double radius = 0;
    const double halfLogicalW = WorldCamera::LogicalWidth() * .5;
    for (double x : {-halfLogicalW / WorldCamera::zoom, halfLogicalW / WorldCamera::zoom})
    {
        for (double y : {-kCanvasHeight * .57 / WorldCamera::zoom, kCanvasHeight * .43 / WorldCamera::zoom})
        {
            radius = (std::max)(radius, world::SurfaceGeometry::Distance(surface->Planet().radiusMeters,
                                                                         view.center, project(x, y)));
        }
    }
    // Quantize coverage so small camera/terrain changes do not rebuild the fallback every frame.
    view.radiusMeters = (std::min)(512., (std::max)(96., std::ceil((radius + 64) / 16) * 16));
    if (snapshot)
    {
        cache.session = surface.get();
        cache.epoch = snapshot->statistics.epoch;
        cache.revision = snapshot->statistics.revision;
        cache.originX = origin.x;
        cache.originY = origin.y;
        cache.originZ = origin.z;
        cache.camera = state.cameraPosition;
        cache.camHeight = state.surfaceCameraHeight;
        cache.zoom = WorldCamera::zoom;
        cache.topView = WorldCamera::topViewBlend;
        cache.result = view;
        cache.valid = true;
    }
    return view;
}
void Game::Reset()
{
    WorldCamera::Reset();
    SaveSystem::ResetCampaign();
    StopSurface();
    WeaponWheel::Close();
    game::SceneManager::Reset();
    game::GameLevels::Reset();
    story::StoryArc::Get().Reset();
    encounters::WorldEncounters::Get().Reset();
    // Keep the current window size when restarting the play session.
    const int viewportWidth = state.viewportWidth;
    const int viewportHeight = state.viewportHeight;
    const UiPreferences preferences = state.uiPreferences;
    const DisplayPreferences displayPrefs = state.displayPreferences;
    state = GameState{};
    frameSchedule = {};
    state.uiPreferences = preferences;
    state.displayPreferences = displayPrefs;
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
    FacilityApproaches::Publish({});
    CityInfrastructure::Publish({});
    Overworld::Populate(state);
    CivicJustice::Reset(state);
    FacilityApproaches::Publish(FacilityApproaches::Prepare(state));
    CityInfrastructure::Publish(CityInfrastructure::Prepare(state, FacilityApproaches::Get()));
    CityResidents::Get().RefreshSidewalks();
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
    ShowNotice("리아 · 수신 메시지 / 좋은 아침. 전망대에 이상한 응답이 잡혔어. 집 단말부터 확인하고 밖으로 나와 줘.");
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
        return "서장 · 헤일로 거주지";
    case TestStage::Observatory:
        return "서장 · 전망대 수신 조사";
    case TestStage::BikeUnlocked:
        return "서장 · 바이크 광산 출발";
    case TestStage::WeaponUnlocked:
        return "서장 · 권총 해금 · 광산 입구";
    case TestStage::Mine:
        return "서장 · 오르딘 광산 내부";
    case TestStage::ChapterOne:
        return "1장 · 빛나는 개척지";
    case TestStage::ActTwoWaterway:
        return "2장 · 외곽 수로 조사";
    case TestStage::ActThreeCrisis:
        return "3장 · 외곽 차단 및 대피";
    case TestStage::ActFourGateCore:
        return "4장 · 관문 N-0 핵심 폐쇄";
    case TestStage::EpilogueRecovery:
        return "에필로그 · 복구된 헤일로";
    case TestStage::SurfaceSurvey:
        return "자유 탐사 · 에르마 전역";
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

    auto waitSurfaceReady = [&]() {
        if (!surface)
        {
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!surface->Ready() && std::chrono::steady_clock::now() < deadline)
        {
            UpdateSurface(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (surface->Ready())
        {
            UpdateSurface(0);
            return true;
        }
        return false;
    };

    // Every chapter/stage jump boots directly into the unified Erma open-world surface.
    StartSurface(stage == TestStage::Prologue);
    if (stage != TestStage::Prologue && stage != TestStage::SurfaceSurvey)
    {
        waitSurfaceReady();
    }

    if (stage == TestStage::Observatory)
    {
        state.isInsideHome = false;
        state.isInObservatory = true;
        state.playerPosition = {0, 2};
        state.cameraPosition = {0, 2};
    }
    else if (stage == TestStage::BikeUnlocked)
    {
        if (surface && surface->Ready() && surfaceBike)
        {
            LandOnSurface(*surfaceBike, 0);
            state.isBikeMounted = true;
            state.playerPosition = state.bikePosition;
            state.planetDestination = surface->Stream().Snapshot()->planet->Region("mq00_mine").access.position;
            if (surfaceParty)
            {
                surfaceParty->Update(0);
            }
        }
    }
    else if (stage == TestStage::WeaponUnlocked || stage == TestStage::Mine)
    {
        if (surface && surface->Ready())
        {
            const auto &mineAccess = surface->Stream().Snapshot()->planet->Region("mq00_mine").access.position;
            RelocateSurfaceSurvey(mineAccess);
            if (surfaceParty)
            {
                surfaceParty->Update(0);
            }
            if (stage == TestStage::Mine)
            {
                EnterSurfaceMine();
            }
        }
    }
    else if (stage == TestStage::ChapterOne)
    {
        if (surface && surface->Ready())
        {
            const auto &frontierAccess = surface->Stream().Snapshot()->planet->Region("frontier").access.position;
            RelocateSurfaceSurvey(frontierAccess);
            game::GameLevels::Chapter().Surface().Refresh();
            if (surfaceParty)
            {
                surfaceParty->Update(0);
            }
        }
    }
    else if (stage == TestStage::ActTwoWaterway || stage == TestStage::ActThreeCrisis ||
             stage == TestStage::ActFourGateCore)
    {
        if (surface && surface->Ready())
        {
            const auto &n0Access = surface->Stream().Snapshot()->planet->Region("n0_outskirts").access.position;
            RelocateSurfaceSurvey(n0Access);
        }
        if (stage == TestStage::ActTwoWaterway)
        {
            story::StoryArc::Get().PrepareActTwoEntry();
        }
        else if (stage == TestStage::ActThreeCrisis)
        {
            story::StoryArc::Get().PrepareActThreeEntry();
        }
        else
        {
            story::StoryArc::Get().PrepareActFourEntry();
        }
    }
    else if (stage == TestStage::EpilogueRecovery)
    {
        story::StoryArc::Get().PrepareEpilogueEntry();
        if (surface && surface->Ready())
        {
            if (surfaceParty)
            {
                surfaceParty->Update(0);
            }
            if (surfaceCities)
            {
                surfaceCities->Update(state, *surface, 0);
            }
        }
    }
    state.cameraPosition = state.playerPosition;
    ShowNotice(std::string("오픈월드 챕터 진입 / ") + StageName(stage) + " · 이전 필수 진행 완료");
    return true;
}

// ============================================================================
// WeaponWheel
// ============================================================================
bool WeaponWheel::Open()
{
    auto &w = Game::Get().GetState();
    if (w.weaponWheel.visible || w.isPaused || w.mapVisible || w.journalVisible || w.dialogueHistoryVisible ||
        Dialogue::Active() || w.dialogue.suspended || HospitalRecovery::Active() || w.Mounted() ||
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
    if (FieldMinigame::Active())
    {
        FieldMinigame::HandleKey(key);
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
    if (repeated && (key == 'e' || key == 13 || key == 'j' || key == 'h' || key == 'm' || key == 'v'))
    {
        return;
    }
    if (state.mapVisible && MapHud::HandleMapKey(key))
    {
        return;
    }
    if (!state.isPaused && state.ui.screen == UiScreen::None && !Dialogue::Active() && !state.journalVisible &&
        !state.dialogueHistoryVisible && !state.mapVisible)
    {
        if (key == '+' || key == '=')
        {
            WorldCamera::StepZoom(+1);
            return;
        }
        if (key == '-' || key == '_')
        {
            WorldCamera::StepZoom(-1);
            return;
        }
    }
    if (key == ' ' && !state.Mounted())
    {
        const auto context = combat::PlayerCombat::Current();
        if (!repeated && HasWalkingControls() && context.area != combat::CombatArea::Inactive &&
            combat::PlayerCombat::ControlBlock(context) == combat::CombatBlock::Ready)
        {
            state.playerMotion.mobility.RequestDodge();
        }
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
                 !state.IsInsideAny() && surface && surface->Ready())
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
        else if (key == 'e' && !state.mapVisible && (state.IsInsideAny() || (surface && surface->Ready())))
        {
            InteractWithNearbyTarget();
        }
        else if (key == 'v' && !repeated && !state.mapVisible)
        {
            ActivityJournal::TriggerGuidance();
        }
        else if (key == 'r' && (game::GameLevels::Chapter().IsActive() || CivicCombat::Available()))
        {
            game::GameLevels::Mine().Combat().Reload();
        }
        else if (key == 'g' && !repeated && (game::GameLevels::Chapter().IsActive() || CivicCombat::Available()))
        {
            game::GameLevels::Mine().UseRecoveryKit();
        }
        else if (key == 'q' && !repeated)
        {
            WeaponWheel::Open();
        }
        else if (key == 'f' && !repeated && !state.mapVisible && !state.IsInsideAny())
        {
            encounters::WorldEncounters::Get().TriggerScannerPing();
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
        ActivityJournal::TriggerGuidance();
        return;
    }
    if (story::StoryArc::Get().IsActive())
    {
        if (key == 'e')
        {
            story::StoryArc::Get().Interact();
        }
        if (key == 'f' && !repeated)
        {
            story::StoryArc::Get().TriggerScanner();
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
    if (key == 'r' && CivicCombat::Available())
    {
        game::GameLevels::Mine().Combat().Reload();
        return;
    }
    if (key == 'g' && !repeated && CivicCombat::Available())
    {
        game::GameLevels::Mine().UseRecoveryKit();
        return;
    }
    auto &chapter = game::GameLevels::Chapter();
    if (chapter.IsActive())
    {
        if (key == 'e')
        {
            chapter.InteractInteractive();
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
            game::GameLevels::Mine().InteractInteractive();
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
    if (key == 'e')
    {
        InteractWithNearbyTarget();
    }
    if (key == 'f' && !repeated && !state.IsInsideAny())
    {
        encounters::WorldEncounters::Get().TriggerScannerPing();
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
    if (FieldMinigame::Active())
    {
        FieldMinigame::HandleSpecialKey(key);
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
    const float logicalW = WorldCamera::LogicalWidth();
    const float aspect = state.viewportHeight > 0 ? float(state.viewportWidth) / float(state.viewportHeight) : 1.6f;
    if (aspect >= float(kCanvasWidth) / float(kCanvasHeight) - 1e-4f)
    {
        const int width = (std::max)(1, state.viewportWidth);
        const int height = (std::max)(1, state.viewportHeight);
        state.cursorInsideViewport = x >= 0 && y >= 0 && x < width && y < height;
        state.cursorPosition = {Clamp(float(x) * logicalW / float(width), 0, logicalW),
                                Clamp(float(y) * float(kCanvasHeight) / float(height), 0, float(kCanvasHeight))};
    }
    else
    {
        float scale =
            (std::min)(state.viewportWidth / float(kCanvasWidth), state.viewportHeight / float(kCanvasHeight));
        int width = (std::max)(1, int(kCanvasWidth * scale));
        int height = (std::max)(1, int(kCanvasHeight * scale));
        int left = (state.viewportWidth - width) / 2, top = (state.viewportHeight - height) / 2;
        state.cursorInsideViewport = x >= left && y >= top && x < left + width && y < top + height;
        state.cursorPosition = {Clamp((x - left) * float(kCanvasWidth) / width, 0, float(kCanvasWidth)),
                                Clamp((y - top) * float(kCanvasHeight) / height, 0, float(kCanvasHeight))};
    }
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
    if (FieldMinigame::Active())
    {
        state.mouseFiring = state.mouseAiming = false;
        FieldMinigame::HandlePointer(button, buttonState == GLFW_PRESS);
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
                            !Dialogue::LocksWorld() && !state.Mounted() &&
                            Loadout().IsFirearmEquipped() &&
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
        if (state.facilityInterior.Active())
        {
            const auto &fi = state.facilityInterior;
            location.position = world::SurfaceGeometry::FromLocal(
                world::SurfaceGeometry::Frame(fi.returnPoint),
                {(state.playerPosition.x - 2.5f) * 2, (state.playerPosition.y - 1.9f) * 2, 0.},
                surface->Planet().radiusMeters);
            location.regionId = fi.regionId;
            location.space = world::SpaceKind::Interior;
            location.layer = 3;
            return location;
        }
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
    if (state.IsInsideAny() || game::GameLevels::Active())
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
    return state.mouseAiming && !state.Mounted() && !state.isPaused && !state.mapVisible &&
           !state.weaponWheel.visible && !state.journalVisible && !state.dialogueHistoryVisible &&
           !HospitalRecovery::Active() && !Dialogue::LocksWorld() && Loadout().IsFirearmEquipped() &&
           (game::GameLevels::IsCombatActive() || CivicJustice::IsExterior());
}
models::CharacterPose Game::PlayerPose(float recoil) const
{
    bool armed = Loadout().IsFirearmEquipped() && !state.isBikeMounted &&
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
        if (state.playerMotion.mobility.Dodging())
        {
            const auto direction = state.playerMotion.mobility.Direction();
            const float cy = std::cos(state.facingYaw), sy = std::sin(state.facingYaw);
            pose.motionForwardLean += (direction.x * cy + direction.y * sy) * .12f;
            pose.motionSideLean += (-direction.x * sy + direction.y * cy) * .12f;
        }
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
    if (FieldMinigame::Active())
    {
        FieldMinigame::Update(deltaSeconds);
        state.animationTimeSeconds += deltaSeconds;
        return;
    }
    if ((state.ui.screen == UiScreen::FacilityShop || state.ui.screen == UiScreen::Wardrobe ||
         state.ui.screen == UiScreen::HomeStorage) &&
        !HospitalRecovery::Active())
    {
        state.animationTimeSeconds += deltaSeconds;
        state.facilityReactionSeconds = (std::max)(0.f, state.facilityReactionSeconds - deltaSeconds);
    }
    if (story::StoryArc::Get().IsActive())
    {
        HospitalRecovery::Update(deltaSeconds);
        if (!state.weaponWheel.visible && !state.isPaused && !state.mapVisible && !HospitalRecovery::Active() &&
            !Dialogue::LocksWorld())
        {
            state.animationTimeSeconds += deltaSeconds;
            state.playTimeSeconds += deltaSeconds;
            if (!Dialogue::Active())
            {
                state.noticeSecondsRemaining -= deltaSeconds;
            }
            auto previous = state.playerPosition;
            UpdatePlayerMovement(deltaSeconds);
            if (IsAiming())
            {
                auto direction = game::MineLevel::GetAimDirection(game::GameLevels::AimAngle());
                state.facingYaw = std::atan2(direction.y, direction.x);
            }
            models::Point velocity = deltaSeconds > 0
                                         ? models::Point{(state.playerPosition.x - previous.x) / deltaSeconds,
                                                         (state.playerPosition.y - previous.y) / deltaSeconds, 0}
                                         : models::Point{};
            state.playerAnimation.Update(deltaSeconds, velocity, state.facingYaw, IsAiming(), false);
            if (Loadout().IsFirearmEquipped())
            {
                ActiveFirearm().Update(deltaSeconds);
            }
            if (state.mouseFiring && Loadout().IsFirearmEquipped())
            {
                const Vector2 aimDir = game::MineLevel::GetAimDirection(game::GameLevels::AimAngle());
                story::StoryArc::Get().TryAttack(
                    state.playerPosition, {state.playerPosition.x + aimDir.x * 12.f, state.playerPosition.y + aimDir.y * 12.f});
            }
            story::StoryArc::Get().Update(deltaSeconds);
            HospitalRecovery::Update(0);
            UpdateCamera(deltaSeconds);
            game::SceneManager::Update(deltaSeconds);
        }
        return;
    }
    if (story::StoryArc::Get().State().epilogueTransitionSeconds > 0.f)
    {
        story::StoryArc::Get().Update(deltaSeconds);
    }
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
        if (!activeLevel && !state.IsInsideAny())
        {
            BodyCollision::Update(deltaSeconds);
            Overworld::Traffic().Update(state, deltaSeconds);
            CityResidents::Get().Update(state, deltaSeconds);
            Overworld::Flights().Update(state, deltaSeconds);
            encounters::WorldEncounters::Get().Update(deltaSeconds);
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
    terrainViewCache.valid = false;
    state.car.mounted = false;
    state.aircraft.mounted = false;
    state.aircraft.Stop();
    groundVehicleSpawnRetry = 0;
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
    if (state.relayPreparing || state.car.mounted || state.aircraft.mounted)
    {
        return false;
    }
    if (surfaceParty)
    {
        surfaceParty->Capture();
    }
    const auto travelers = surfaceParty ? surfaceParty->NearbyTravelers() : std::array<bool, 2>{};
    if (!state.surfaceExploration || IsSurfaceMine() || state.IsInsideAny() ||
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
        game::GameLevels::Active() || state.IsInsideAny() || IsSurfaceMine() ||
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
    if (state.Mounted())
    {
        ShowNotice("광산 / 차량을 주차하고 내린 뒤 입장하세요.");
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
    if (surfaceParty)
    {
        surfaceParty->Update(0);
        const auto &m = surfaceParty->State().members;
        surfaceParty->Arrive(
            {m[0].joined && m[0].combat.health > 0 && m[0].place == CompanionPlace::Mine,
             m[1].joined && m[1].combat.health > 0 && m[1].place == CompanionPlace::Mine});
    }
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
// ============================================================================
// GroundVehicle — surface position, driving and safe entry/exit
// ============================================================================
namespace
{
const models::ResourceMesh &ServiceCarMesh()
{
    return models::ResourceAssets::Get(
        models::ModelLibrary::Get().vehicles[size_t(models::VehicleType::Coupe)].body.resourceId);
}
} // namespace
void GroundVehicle::SetYaw(float yaw, const world::TangentFrame &origin)
{
    using G = world::SurfaceGeometry;
    const auto frame = G::Frame(*position);
    const auto local = G::TransformDirection({std::cos(yaw), std::sin(yaw), 0}, origin, frame);
    heading = G::Unit({frame.right.x * local.x + frame.forward.x * local.y,
                       frame.right.y * local.x + frame.forward.y * local.y,
                       frame.right.z * local.x + frame.forward.z * local.y});
}
float GroundVehicle::Yaw(const world::TangentFrame &frame) const
{
    return float(std::atan2(world::SurfaceGeometry::Dot(heading, frame.forward),
                            world::SurfaceGeometry::Dot(heading, frame.right)));
}
bool GroundVehicle::Contains(const world::SurfaceSession &session, const world::SurfacePoint &point,
                             float padding, bool projectile) const
{
    using G = world::SurfaceGeometry;
    if (!position || G::Distance(session.Planet().radiusMeters, *position, point) > 8)
    {
        return false;
    }
    const auto frame = G::Frame(*position);
    const auto local = G::ToLocal(frame, point, session.Planet().radiusMeters);
    const auto &mesh = ServiceCarMesh();
    const float yaw = Yaw(frame), c = std::cos(yaw), s = std::sin(yaw);
    const double x = (local.x * c + local.y * s) / 2, y = (-local.x * s + local.y * c) / 2;
    const double z = local.z / 2;
    return x >= mesh.minimum.x - padding && x <= mesh.maximum.x + padding && y >= mesh.minimum.y - padding &&
           y <= mesh.maximum.y + padding && z >= (projectile ? mesh.minimum.z : -.7) &&
           z <= (projectile ? mesh.maximum.z : .8);
}

bool Game::GroundVehicleBlocked(Vector2 position, float yaw, bool includePeople) const
{
    const auto &mesh = ServiceCarMesh();
    const float c = std::cos(yaw), s = std::sin(yaw);
    // Check the centerline and both edges along the actual model bounds.
    for (float front : {mesh.minimum.x, (mesh.minimum.x + mesh.maximum.x) * .5f, mesh.maximum.x})
    {
        for (float side : {mesh.minimum.y, (mesh.minimum.y + mesh.maximum.y) * .5f, mesh.maximum.y})
        {
            const Vector2 probe{position.x + c * front - s * side, position.y + s * front + c * side};
            auto point = surface->PointAt(probe.x * 2, probe.y * 2);
            const auto contact = surface->ContactAt(probe.x * 2, probe.y * 2);
            point.altitudeMeters = contact.heightMeters;
            if (!contact.land || contact.slope > .58 || state.aircraft.Contains(*surface, point, .32) ||
                (!state.car.mounted && surfaceBike &&
                 world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, point, *surfaceBike) < .65) ||
                surface->Blocked(probe.x * 2, probe.y * 2, false) ||
                (surfaceCities && surfaceCities->Occupied(state, *surface, probe, -1, includePeople)) ||
                (surfaceTraffic && surfaceTraffic->Blocked(point, .16)) ||
                (includePeople && (!SurfaceLandingClear(point, -1, true) ||
                                   game::GameLevels::Chapter().Surface().Blocked(probe, false))))
            {
                return true;
            }
        }
    }
    return false;
}

bool Game::IsGroundVehicleUnlocked() const
{
    if (state.car.position.has_value() || !IsStoryWorld())
    {
        return true;
    }
    return state.isComplete || state.missions.Status(MissionId::Mine) == MissionStatus::Completed ||
           game::GameLevels::Mine().GetState().quest.GetStage() == game::MineQuestStage::Complete ||
           game::GameLevels::Chapter().IsAvailable();
}

bool Game::IsAircraftUnlocked() const
{
    if (state.aircraft.position.has_value() || !IsStoryWorld())
    {
        return true;
    }
    return state.missions.Status(MissionId::Frontier) == MissionStatus::Completed ||
           game::GameLevels::Chapter().GetState().waterwayUnlocked ||
           story::StoryArc::Get().IsActTwoUnlocked();
}

void Game::UpdateGroundVehicleLifecycle(float dt)
{
    using G = world::SurfaceGeometry;
    auto &car = state.car;
    if (car.position)
    {
        if (car.health > 0)
        {
            return;
        }
        if (car.mounted)
        {
            if (!ExitGroundVehicle())
            {
                car.mounted = false;
                car.velocity = {};
                car.speed = 0;
                game::GameLevels::Mine().GetState().player.health = 0;
                HospitalRecovery::Begin();
            }
            return;
        }
        car.wreckSeconds += dt;
        const bool hidden =
            G::Distance(surface->Planet().radiusMeters, *car.position, surface->Pose().origin) > 180;
        if (VehicleWreck::ShouldRetire(car.wreckSeconds, hidden, true, VehicleWreck::WorldCleanupSeconds))
        {
            car = {};
            groundVehicleSpawnRetry = 3;
        }
        return;
    }
    if (!IsGroundVehicleUnlocked() || state.IsInsideAny() || IsSurfaceMine() ||
        (groundVehicleSpawnRetry -= dt) > 0)
    {
        return;
    }
    groundVehicleSpawnRetry = 1;
    const auto plan = surface->Stream().Snapshot()->planet;
    const auto hero = surface->Pose().origin;
    const world::GeneratedRegion *spawnRegion = nullptr;
    std::optional<world::SurfacePoint> anchor;
    // Allow service vehicle availability near Halo home OR any nearby settlement/transit hub access point.
    if (IsStoryWorld() && surfaceCities)
    {
        const auto &halo = plan->Region("halo_07");
        const auto homeAnchor = surfaceCities->Point(*surface, 0, kHomeDoor);
        if (homeAnchor && G::Distance(surface->Planet().radiusMeters, *homeAnchor, hero) <= 40)
        {
            spawnRegion = &halo;
            anchor = homeAnchor;
        }
    }
    if (!anchor)
    {
        for (const auto &reg : plan->regions)
        {
            if (reg.center.space != world::SpaceKind::Surface ||
                (reg.definition.kind != world::RegionKind::Settlement &&
                 reg.definition.id != "landing_zone"))
            {
                continue;
            }
            if (G::Distance(surface->Planet().radiusMeters, reg.access.position, hero) <= 60)
            {
                spawnRegion = &reg;
                anchor = reg.access.position;
                break;
            }
        }
    }
    if (!anchor || !spawnRegion)
    {
        return;
    }
    const auto &region = *spawnRegion;
    const auto frame = G::Frame(*anchor);
    const auto direction = G::TransformDirection({0, -1, 0}, region.frame, surface->Origin());
    const float yaw = float(std::atan2(direction.y, direction.x));
    for (double meters : {5., 8., 12., 16.})
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            const double angle = i * G::Pi / 4;
            auto candidate = G::FromLocal(frame, {meters * std::cos(angle), meters * std::sin(angle), 0},
                                          surface->Planet().radiusMeters);
            const auto local = surface->Local(candidate);
            if (IsStoryWorld() && region.definition.id == "halo_07")
            {
                const auto city = G::ToLocal(region.frame, candidate, surface->Planet().radiusMeters);
                const auto center = Overworld::Cities()[0].center;
                if (Overworld::IsRoad(int(std::floor(center.x + city.x / 2)),
                                      int(std::floor(center.y + city.y / 2))))
                {
                    continue;
                }
            }
            if (Distance({float(local.x / 2), float(local.y / 2)}, state.playerPosition) < 1.8f ||
                GroundVehicleBlocked({float(local.x / 2), float(local.y / 2)}, yaw, true))
            {
                continue;
            }
            candidate.altitudeMeters = surface->ContactAt(local.x, local.y).heightMeters;
            car.position = candidate;
            car.SetYaw(yaw, surface->Origin());
            return;
        }
    }
}

void Game::UpdateGroundVehicle(float dt)
{
    auto &car = state.car;
    const float oldYaw = car.Yaw(surface->Origin());
    const float steering = float(state.pressedKeys['d']) - float(state.pressedKeys['a']);
    const bool reverse = state.pressedKeys['s'] && !state.pressedKeys['w'];
    const bool forward = state.pressedKeys['w'] && !state.pressedKeys['s'];
    const bool boost = forward && state.pressedKeys[' '];
    // Rugged armored utility vehicle: faster highway/overdrive speed (up to 119 km/h), all-terrain slope climb,
    // bullet-proof cabin, and companion boarding.
    const float targetSpeed = forward ? (boost ? 16.5f : 12.5f) : reverse ? -2.8f : 0.f;
    const float rate = (car.speed * targetSpeed < 0 || targetSpeed == 0 ? 20.f : (boost ? 15.f : 13.f)) *
                       MotionMath::kResponse;
    const float turnFactor = std::fabs(car.speed) > .2f ? Clamp(car.speed / 2.2f, -1.f, 1.f)
                             : forward                  ? .55f
                             : reverse                  ? -.55f
                                                        : .35f;
    const float yawChange = steering * 1.75f * turnFactor * dt;
    float yaw = oldYaw + yawChange;
    const auto start = state.playerPosition;
    if (yawChange != 0 && GroundVehicleBlocked(start, yaw, false))
    {
        yaw = oldYaw;
    }
    const Vector2 axis{std::cos(yaw), std::sin(yaw)};
    float along = car.velocity.x * axis.x + car.velocity.y * axis.y;
    float side = car.velocity.y * axis.x - car.velocity.x * axis.y;
    along += Clamp(targetSpeed - along, -rate * dt, rate * dt);
    side *= std::exp(-6.f * dt);
    car.velocity = {axis.x * along - axis.y * side, axis.y * along + axis.x * side};
    if (std::hypot(car.velocity.x, car.velocity.y) > .001f)
    {
        const float preSpeed = std::hypot(car.velocity.x, car.velocity.y);
        const Vector2 requested{car.velocity.x * dt, car.velocity.y * dt};
        const float reqLen = std::hypot(requested.x, requested.y);
        state.playerPosition = BodyCollision::Slide(
            start, {start.x + requested.x, start.y + requested.y},
            [&](Vector2 p) { return GroundVehicleBlocked(p, yaw, false); }, .08f);
        const Vector2 actual{state.playerPosition.x - start.x, state.playerPosition.y - start.y};
        const float actLen = std::hypot(actual.x, actual.y);
        // Apply high-speed collision damage & bounce recoil only on severe impacts (armored bumper absorbs normal bumps).
        if (preSpeed > 12.5f && reqLen > .001f && actLen < reqLen * .45f)
        {
            const int impactDamage = (std::max)(4, int((preSpeed - 12.5f) * 2.5f));
            car.health = (std::max)(0, car.health - impactDamage);
            along = -along * .25f;
            car.velocity = {axis.x * along, axis.y * along};
            if (car.health == 0)
            {
                const float z = PlanetSecurity::Elevation(surface->Pose().origin) + .5f;
                for (int ring = 0; ring < 6; ++ring)
                {
                    const float a = ring * (kPi / 3.f);
                    PlanetSecurity::Hit({state.playerPosition.x + std::cos(a) * 1.8f,
                                         state.playerPosition.y + std::sin(a) * 1.8f, z},
                                        40, true);
                }
                ShowNotice("차량 충돌 파괴 / 충격 폭발이 발생했습니다!");
            }
        }
        else
        {
            car.velocity = MotionMath::ContactVelocity(car.velocity, requested, actual);
        }
    }
    car.speed = along;
    state.facingYaw = yaw;
    state.playerMotion.mobility.Update(dt, {}, yaw, false, false, false, false);
}

bool Game::ExitGroundVehicle()
{
    auto &car = state.car;
    const float yaw = car.Yaw(surface->Origin()), c = std::cos(yaw), s = std::sin(yaw);
    const auto &mesh = ServiceCarMesh();
    const auto local = surface->Local(surface->Pose().origin);
    for (const Vector2 offset : {Vector2{0, mesh.minimum.y - .32f},
                                 {0, mesh.maximum.y + .32f},
                                 {mesh.minimum.x - .32f, 0},
                                 {mesh.maximum.x + .32f, 0}})
    {
        const Vector2 delta{c * offset.x - s * offset.y, s * offset.x + c * offset.y};
        const double x = local.x + delta.x * 2, y = local.y + delta.y * 2;
        auto point = surface->PointAt(x, y);
        const auto contact = surface->ContactAt(x, y);
        point.altitudeMeters = contact.heightMeters;
        const Vector2 exit{float(x / 2), float(y / 2)};
        if (std::abs(point.altitudeMeters - car.position->altitudeMeters) > 1 ||
            !SurfaceLandingClear(point, -1) || PlanetSecurity::OfficerBlocked(point, .32) ||
            game::GameLevels::Chapter().Surface().Blocked(exit, false) ||
            surface->Blocked(local.x + delta.x * 1.6, local.y + delta.y * 1.6, false) ||
            !surface->RestorePose(world::SurfaceGeometry::Frame(point), surface->Origin(), false))
        {
            continue;
        }
        car.mounted = false;
        state.StopMotion();
        state.playerPosition = exit;
        state.facingYaw = std::atan2(delta.y, delta.x);
        return true;
    }
    return false;
}

void Game::InteractGroundVehicle()
{
    auto &car = state.car;
    if (car.mounted)
    {
        if (std::abs(car.speed) > .45f)
        {
            ShowNotice("차량을 정차한 뒤 내려 주세요.");
        }
        else if (!ExitGroundVehicle())
        {
            ShowNotice("차량 주변이 막혀 있습니다. 공간이 있는 곳에 주차해 주세요.");
        }
        return;
    }
    if (state.Mounted())
    {
        return;
    }
    const bool nearOwnedCar =
        car.position && car.health > 0 &&
        world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, surface->Pose().origin,
                                         *car.position) < 4.6;
    bool commandeered = false;
    bool commandeeredOccupied = false;
    if (!nearOwnedCar && IsGroundVehicleUnlocked() && surfaceTraffic)
    {
        world::SurfacePoint pos{};
        world::Vector3d heading{};
        int hp = GroundVehicle::MaxHealth;
        if (surfaceTraffic->CommandeerNearest(surface->Pose().origin, 4.8, pos, heading, hp,
                                              commandeeredOccupied))
        {
            car = {};
            car.position = pos;
            car.heading = heading;
            car.health = hp;
            commandeered = true;
        }
    }
    if (!car.position || car.health <= 0)
    {
        return;
    }
    const auto local = surface->Local(*car.position);
    const Vector2 center{float(local.x / 2), float(local.y / 2)};
    const float yaw = car.Yaw(surface->Origin());
    const auto approach = [&](float fraction) {
        const Vector2 p{state.playerPosition.x + (center.x - state.playerPosition.x) * fraction,
                        state.playerPosition.y + (center.y - state.playerPosition.y) * fraction};
        return surface->Blocked(p.x * 2, p.y * 2, false);
    };
    if (!commandeered &&
        (approach(.25f) || approach(.5f) || approach(.75f) || GroundVehicleBlocked(center, yaw, true) ||
         !surface->RestorePose(world::SurfaceGeometry::Frame(*car.position), surface->Origin(), false)))
    {
        ShowNotice("차량 주변이 막혀 탑승할 수 없습니다.");
        return;
    }
    if (commandeered)
    {
        if (!surface->RestorePose(world::SurfaceGeometry::Frame(*car.position), surface->Origin(), false))
        {
            surface->Relocate(*car.position, .32, 3.0);
            car.position = surface->Pose().origin;
        }
        if (commandeeredOccupied)
        {
            PlanetSecurity::Report(Offense::Assault, *car.position);
        }
    }
    const auto syncedLocal = surface->Local(surface->Pose().origin);
    state.StopMotion();
    car.mounted = true;
    state.playerPosition = {float(syncedLocal.x / 2), float(syncedLocal.y / 2)};
    state.facingYaw = yaw;
    if (commandeered)
    {
        ShowNotice("차량 탈취 탑승 · W 가속 · S 제동/후진 · A/D 조향 · 정차 후 E 하차");
    }
    else
    {
        ShowNotice("공용 차량 탑승 · W 가속 · S 제동/후진 · A/D 조향 · Space 부스트 · 정차 후 E 하차");
    }
    state.mouseFiring = state.mouseAiming = false;
}

void Game::ToggleSurfaceBike()
{
    if (!surface || IsSurfaceMine() || state.IsInsideAny())
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
                const bool connectedWalkway = !FacilityApproaches::Connected(facility->target) ||
                                              FacilityApproaches::Contains(local) ||
                                              CityPlan::IsWalkway(local);
                return connectedWalkway && point && SurfaceLandingClear(*point, city);
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
bool Game::SurfaceLandingClear(const world::SurfacePoint &point, int city, bool ignoreServiceCar,
                               bool ignoreAircraft) const
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
    return (ignoreServiceCar || !state.car.Contains(*surface, point, .18f)) &&
           (ignoreAircraft || !state.aircraft.Contains(*surface, point, .36)) &&
           !(surfaceParty && surfaceParty->Blocked(point, .32)) &&
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
    const auto travelers = surfaceParty && !IsSurfaceMine() && !state.IsInsideAny()
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
    state.facilityInterior.Clear();
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
    if (id == CompanionId::Ria && story::StoryArc::Get().IsRiaMissing())
    {
        return false;
    }
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
    auto velocity = state.car.mounted     ? state.car.velocity
                    : state.isBikeMounted ? state.bikeVelocity
                                          : state.playerMotion.velocity;
    const auto streamVelocity =
        state.aircraft.mounted ? state.aircraft.velocity : world::Vector3d{velocity.x * 2, velocity.y * 2, 0};
    surface->Update(
        IsSurfaceMine() || state.IsInsideAny() ? world::Vector3d{} : streamVelocity,
        relayDestination, state.aircraft.mounted,
        surface->Ready() ? std::optional<world::SurfaceView>(SurfaceTerrainView()) : std::nullopt);
    if (!surface->Ready())
    {
        if (state.IsInsideAny())
        {
            const bool advancing = !state.isPaused && !state.mapVisible && !state.weaponWheel.visible &&
                                   !state.journalVisible && !state.dialogueHistoryVisible &&
                                   !Dialogue::LocksWorld();
            if (advancing)
            {
                state.playTimeSeconds += dt;
                state.animationTimeSeconds += dt;
                if (!Dialogue::Active())
                {
                    state.noticeSecondsRemaining -= dt;
                }
                UpdatePlayerMovement(dt);
                UpdateCamera(dt);
                state.surfaceCameraHeight = 0;
                state.playerAnimation.Update(
                    dt, {state.playerMotion.velocity.x, state.playerMotion.velocity.y, 0}, state.facingYaw,
                    false, false);
                game::SceneManager::Update(dt);
            }
            return;
        }
        state.StopMotion();
        return;
    }
    const int healthBefore = game::GameLevels::Mine().GetState().player.health;
    if (state.relayNetwork.Registrations().empty() && surface->Stream().Snapshot() &&
        surface->Stream().Snapshot()->planet)
    {
        state.relayNetwork.RegisterPublicHubs(*surface->Stream().Snapshot()->planet);
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
    if (!IsSurfaceMine() && !state.IsInsideAny())
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
        if (advancing && (IsSurfaceMine() || state.IsInsideAny()))
        {
            surfaceCities->Update(state, *surface, dt);
        }
    }
    if (advancing)
    {
        if (surfaceCommunity)
        {
            surfaceCommunity->Update(dt, !IsSurfaceMine() && !state.IsInsideAny());
        }
        if (!IsSurfaceMine() && !state.IsInsideAny())
        {
            encounters::WorldEncounters::Get().Update(dt);
        }
        state.playTimeSeconds += dt;
        CivicJustice::Update(dt);
        PlanetSecurity::UpdateCompanions(dt);
        CivicCombat::Update(dt);
        UpdateGroundVehicleLifecycle(dt);
        PlayerAircraftController::Update(*this, dt);
        if (surfaceTraffic && !HospitalRecovery::Active())
        {
            surfaceTraffic->Update(state, *surface, dt, !IsSurfaceMine() && !state.IsInsideAny());
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
    if (advancing && state.IsInsideAny())
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
        if (state.guidanceSecondsRemaining > 0.f)
        {
            state.guidanceSecondsRemaining = (std::max)(0.f, state.guidanceSecondsRemaining - dt);
        }
        if (!Dialogue::Active())
        {
            state.noticeSecondsRemaining -= dt;
        }
        auto before = state.playerPosition;
        const auto surfaceFrom = surface->Pose().origin;
        const float previousVehicleYaw = state.isBikeMounted ? state.bikeYaw : state.facingYaw;
        UpdatePlayerMovement(dt);
        auto desired = state.playerPosition;
        if (!state.aircraft.mounted)
        {
            surface->Move((desired.x - before.x) * 2, (desired.y - before.y) * 2, state.isBikeMounted);
        }
        auto position = surface->Local(surface->Pose().origin);
        if (state.Mounted() && !state.aircraft.mounted)
        {
            const float speed = state.car.mounted ? state.car.speed : state.bikeSpeed;
            auto contact = BodyCollision::SurfaceVehicleContacts(surfaceFrom, surface->Pose().origin,
                                                                 previousVehicleYaw, std::fabs(speed));
            if (std::hypot(contact.x * 2 - position.x, contact.y * 2 - position.y) > .0001)
            {
                surface->Move(contact.x * 2 - position.x, contact.y * 2 - position.y, state.isBikeMounted);
                position = surface->Local(surface->Pose().origin);
                if (state.isBikeMounted)
                {
                    state.bikeSafePosition = {float(position.x / 2), float(position.y / 2)};
                    state.bikeSafeYaw = state.bikeYaw;
                    state.bikeSafePoseValid = true;
                }
            }
        }
        state.playerPosition = {float(position.x / 2), float(position.y / 2)};
        Vector2 requested{desired.x - before.x, desired.y - before.y},
            actual{state.playerPosition.x - before.x, state.playerPosition.y - before.y};
        if (state.car.mounted)
        {
            auto &car = state.car;
            car.velocity = MotionMath::ContactVelocity(car.velocity, requested, actual);
            car.speed =
                car.velocity.x * std::cos(state.facingYaw) + car.velocity.y * std::sin(state.facingYaw);
        }
        else if (state.isBikeMounted)
        {
            state.bikeVelocity = MotionMath::ContactVelocity(state.bikeVelocity, requested, actual);
            float direction = state.bikeSpeed < 0 ? -1.f : 1.f;
            state.bikeSpeed = direction * std::hypot(state.bikeVelocity.x, state.bikeVelocity.y);
        }
        else if (!state.aircraft.mounted)
        {
            state.playerMotion.velocity =
                MotionMath::ContactVelocity(state.playerMotion.velocity, requested, actual);
            if (state.playerMotion.mobility.Contact(std::hypot(requested.x, requested.y),
                                                    std::hypot(actual.x, actual.y)))
            {
                state.playerMotion.velocity = {};
                state.playerAnimation.InterruptDodge();
            }
        }
        state.movementSpeed = dt > 0 ? Distance(before, state.playerPosition) / dt : 0;
        state.movementAmount = Clamp(state.movementSpeed / (state.Mounted() ? 12.8f : kMovementSpeed), 0, 1);
        auto animationVelocity = state.isBikeMounted ? state.bikeVelocity : state.playerMotion.velocity;
        if (IsAiming())
        {
            auto direction = game::MineLevel::GetAimDirection(game::GameLevels::Mine().GetAimAngle());
            state.facingYaw = std::atan2(direction.y, direction.x);
        }
        state.playerAnimation.Update(dt, {animationVelocity.x, animationVelocity.y, 0}, state.facingYaw,
                                     IsAiming(), state.Mounted());
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
            const auto dodgeDirection = state.playerMotion.mobility.Direction();
            const auto rotatedDodge = rotate({dodgeDirection.x, dodgeDirection.y});
            state.playerMotion.mobility.RotateDirection(rotatedDodge.x, rotatedDodge.y);
            state.bikeVelocity = rotate(state.bikeVelocity);
            state.car.velocity = rotate(state.car.velocity);
            const auto airVelocity =
                world::SurfaceGeometry::TransformDirection(state.aircraft.velocity, *old, surface->Origin());
            state.aircraft.velocity = airVelocity;
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
        if (state.car.mounted)
        {
            state.car.position = surface->Pose().origin;
            state.car.SetYaw(state.facingYaw, surface->Origin());
        }
        if (surfaceCities)
        {
            surfaceCities->Update(state, *surface, dt);
            // Contacts ran in a city frame; apply their correction to the authoritative sphere too.
            const auto current = surface->Local(surface->Pose().origin);
            const double dx = state.playerPosition.x * 2 - current.x,
                         dy = state.playerPosition.y * 2 - current.y;
            if (!state.aircraft.mounted && std::hypot(dx, dy) > .0001)
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
            if (state.car.mounted)
            {
                state.car.position = surface->Pose().origin;
                state.car.SetYaw(state.facingYaw, surface->Origin());
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
        const int healthAfter = game::GameLevels::Mine().GetState().player.health;
        if (healthAfter < healthBefore && !HospitalRecovery::Active())
        {
            ShowNotice("피해 / 체력 -" + std::to_string(healthBefore - healthAfter));
        }
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
    using TickClock = std::chrono::steady_clock;
    static TickClock::time_point lastTick;
    static bool hasLastTick = false;
    static TickClock::time_point lastMaintenance;
    static bool hasMaintenance = false;
    const auto nowTime = TickClock::now();
    const int nowMilliseconds = platform::ElapsedMilliseconds();
    float deltaSeconds = 0.f;
    if (hasLastTick)
    {
        const double elapsed = std::chrono::duration<double>(nowTime - lastTick).count();
        deltaSeconds = Clamp(float(elapsed), 0.f, kMaximumDeltaSeconds);
    }
    else
    {
        deltaSeconds = Clamp((nowMilliseconds - state.lastUpdateMilliseconds) / 1000.f, 0.f, kMaximumDeltaSeconds);
        hasLastTick = true;
    }
    lastTick = nowTime;
    state.lastUpdateMilliseconds = nowMilliseconds;
    // Rate-limit Win32 focus/cursor-clip polling and monitor publishing to ~60Hz so 144+ FPS
    // frames do not multiply OS/monitoring overhead.
    if (!hasMaintenance || std::chrono::duration<double>(nowTime - lastMaintenance).count() >= 0.016)
    {
        hasMaintenance = true;
        lastMaintenance = nowTime;
        WeaponWheel::RefreshCursorCapture();
        HWND activeWindow = GetActiveWindow();
        if (activeWindow && IsWindowVisible(activeWindow) && GetForegroundWindow() != activeWindow)
        {
            state.mouseFiring = state.mouseAiming = false;
            std::fill(std::begin(state.pressedKeys), std::end(state.pressedKeys), false);
        }
    }
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
    if (story::StoryArc::Get().IsActive())
    {
        return story::StoryArc::Get().HasNearbyHostiles(world.playerPosition, 9.f);
    }
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
                     std::function<void(const std::string &)> completion, bool repeatable, bool blocking)
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
    if (record.phase == DialoguePhase::InProgress)
    {
        for (size_t i = 0; i < d.pages[d.page].choices.size(); ++i)
        {
            if (d.pages[d.page].choices[i].id == record.pendingChoice)
            {
                d.selection = int(i);
            }
        }
    }
    d.active = true;
    d.suspended = false;
    d.blocking = blocking && !Danger();
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
    record.pendingChoice.clear();
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
void Dialogue::Say(const std::string &id, const std::string &message, bool blocking)
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
        Start(id, std::move(lines), {}, false, blocking);
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
    if (story::StoryArc::Get().IsActive())
    {
        return story::StoryArc::Get().IsPositionBlocked(position, .35f);
    }
    if (state.surfaceExploration && surface && surface->Ready() && state.aircraft.position &&
        !state.aircraft.mounted && !IsSurfaceMine() && !state.IsInsideAny())
    {
        auto point = PlanetSecurity::Point(position);
        point.altitudeMeters = surface->Stream().Contact(point).heightMeters;
        if (state.aircraft.Contains(*surface, point, .4))
        {
            return true;
        }
    }
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
    if (state.surfaceExploration && !IsSurfaceMine() && !state.IsInsideAny())
    {
        if (!surface)
        {
            return true;
        }
        const auto point = surface->PointAt(position.x * 2, position.y * 2);
        if (state.car.position && !state.car.mounted &&
            world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, *state.car.position,
                                             surface->Pose().origin) < 25)
        {
            auto carPoint = point;
            carPoint.altitudeMeters = surface->Stream().Contact(point).heightMeters;
            if (state.car.Contains(*surface, carPoint, .16f))
            {
                return true;
            }
        }
        const double clearance = state.isBikeMounted ? .5 : .32;
        return surface->Blocked(position.x * 2, position.y * 2, state.isBikeMounted) ||
               game::GameLevels::Chapter().Surface().Blocked(position, state.isBikeMounted) ||
               (surfaceCities &&
                surfaceCities->OccupiedAt(state, *surface, point, -1, !state.isBikeMounted)) ||
               (!state.isBikeMounted && surfaceParty && surfaceParty->Blocked(point, clearance)) ||
               (!state.isBikeMounted && PlanetSecurity::OfficerBlocked(point, .32)) ||
               (surfaceTraffic && surfaceTraffic->Blocked(point, clearance)) ||
               (surfaceCommunity && surfaceCommunity->Blocked(point, clearance, !state.isBikeMounted));
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
        const bool storage =
            position.x > 1.82f && position.x < 2.68f && position.y > -.58f && position.y < .28f;
        return bed || console || table || storage;
    }
    if (state.isInObservatory)
    {
        return position.x < -3.8f || position.x > 3.8f || position.y < -3.8f || position.y > 3.8f;
    }
    if (state.facilityInterior.Active())
    {
        if (position.x < -3.2f || position.x > 3.2f || position.y < -2.4f || position.y > 2.4f)
        {
            return true;
        }
        // Service counter desk across the back of the facility room (walkable in front, clerk behind).
        const bool counter =
            position.x > -2.35f && position.x < 2.35f && position.y > -2.15f && position.y < -1.15f;
        const bool sideCrate =
            position.x < -2.45f && position.y > .75f && position.y < 1.95f;
        return counter || sideCrate;
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
    if ((target == InteractionTarget::Lia &&
         (story::StoryArc::Get().IsRiaMissing() || CivicCombat::LandmarkDown(kLiaPosition))) ||
        (target == InteractionTarget::Mara && CivicCombat::LandmarkDown(kMaraPosition)) ||
        (target == InteractionTarget::Kain && CivicCombat::LandmarkDown(kCityKainPosition)))
    {
        return false;
    }
    if (target == InteractionTarget::CityReceiver)
    {
        return false;
    }
    const auto &arc = story::StoryArc::Get().State();
    auto stage = game::GameLevels::Mine().GetState().quest.GetStage();
    if (state.surfaceExploration)
    {
        if (arc.chapter4Complete && !arc.nereusSignalReceived && arc.epilogueShelterVisited &&
            arc.epilogueClinicVisited && arc.epilogueWorkshopVisited &&
            (target == InteractionTarget::GateBeacon || target == InteractionTarget::ObservatorySignal))
        {
            return true;
        }
        if (arc.sectorLossTriggered && !arc.chapter4Complete && arc.safetyLineChecked &&
            target == InteractionTarget::Mara)
        {
            return true;
        }
        if (story::StoryArc::Get().IsActTwoUnlocked() && !arc.chapter3Committed &&
            (arc.veilHypothesisFound || arc.chapter2Reported) && target == InteractionTarget::Lia)
        {
            return true;
        }
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
                return stage == game::MineQuestStage::ShowRecord || stage == game::MineQuestStage::CheckCityRelay ||
                       CanCompleteMission(MissionId::Mine) ||
                       (stage == game::MineQuestStage::TravelToMine &&
                        !game::GameLevels::Mine().GetState().quest.IsRiaJoined());
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
        return target == InteractionTarget::Lia && CanCompleteMission(MissionId::Frontier);
    }
    if (game::GameLevels::Chapter().IsAvailable() &&
        (target == InteractionTarget::HomeTerminal || target == InteractionTarget::ApartmentExit ||
         target == InteractionTarget::HomeEntrance || target == InteractionTarget::GateBeacon ||
         target == InteractionTarget::JetBike || target == InteractionTarget::ObservatorySignal ||
         target == InteractionTarget::ObservatoryExit))
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
               (state.hasDescendedObservatory &&
                (stage == game::MineQuestStage::ShowRecord || stage == game::MineQuestStage::CheckCityRelay)) ||
               stage == game::MineQuestStage::ReturnToHalo ||
               (game::GameLevels::Chapter().IsAvailable() && !state.isInsideHome && !state.isInObservatory &&
                !game::GameLevels::Chapter().GetState().recordsCompared);
    case InteractionTarget::Mara:
        return (!state.isSideQuestComplete && (!state.isSideQuestAccepted || state.isPowerRestored)) ||
               (arc.sectorLossTriggered && !arc.chapter4Complete && arc.safetyLineChecked);
    case InteractionTarget::PowerRelay:
        return state.isSideQuestAccepted && !state.isPowerRestored;
    case InteractionTarget::GateBeacon:
        return (state.mainQuestStage == MainQuestStage::EnterObservatory && !state.gateSignalHeard) ||
               (arc.chapter4Complete && !arc.nereusSignalReceived && arc.epilogueShelterVisited &&
                arc.epilogueClinicVisited && arc.epilogueWorkshopVisited);
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
    if (story::StoryArc::Get().IsActive())
    {
        return story::StoryArc::Get().NearbyNode(state.playerPosition) ? InteractionTarget::StoryOperationNode
                                                                       : InteractionTarget::None;
    }
    if (state.surfaceExploration && !IsSurfaceMine() && !state.IsInsideAny())
    {
        if (state.aircraft.mounted)
        {
            const auto &a = state.aircraft;
            return a.health > 0 && (a.grounded || a.landingRequested ||
                                    (a.landingAvailable && std::hypot(a.velocity.x, a.velocity.y) <= 2))
                       ? InteractionTarget::Aircraft
                       : InteractionTarget::None;
        }
        if (state.car.mounted)
        {
            return InteractionTarget::GroundVehicle;
        }
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
            if (story::StoryArc::Get().IsActTwoUnlocked() && !story::StoryArc::Get().State().sectorLossTriggered)
            {
                const auto &n0 = surface->Stream().Snapshot()->planet->Region("n0_outskirts");
                if (world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, surface->Pose().origin,
                                                     n0.access.position) <= 7.0)
                {
                    return InteractionTarget::StoryOperationPortal;
                }
            }
        }
        const bool fixedFacility = target == InteractionTarget::HomeEntrance ||
                                   target == InteractionTarget::GateBeacon ||
                                   target == InteractionTarget::CityReceiver ||
                                   target == InteractionTarget::PowerRelay ||
                                   ((target == InteractionTarget::Lia || target == InteractionTarget::Kain ||
                                     target == InteractionTarget::Mara) &&
                                    IsQuestInteraction(target));
        float bikeDistance = Distance(state.playerPosition, state.bikePosition);
        if (!fixedFacility && bikeDistance < 1.8f && bikeDistance < distance)
        {
            distance = bikeDistance;
            target = InteractionTarget::JetBike;
        }
        const bool vehicleAllowed = !fixedFacility && target != InteractionTarget::JetBike &&
                                    target != InteractionTarget::Community;
        if (vehicleAllowed && surface && surface->Ready() && state.car.position && state.car.health > 0 &&
            std::abs(surface->Pose().origin.altitudeMeters - state.car.position->altitudeMeters) < 1.3)
        {
            const float carDistance =
                float(world::SurfaceGeometry::Distance(surface->Planet().radiusMeters, surface->Pose().origin,
                                                       *state.car.position) *
                      .5);
            if (carDistance < 2.3f && (target == InteractionTarget::None || carDistance < distance + .6f))
            {
                distance = carDistance;
                target = InteractionTarget::GroundVehicle;
            }
        }
        if (vehicleAllowed && IsGroundVehicleUnlocked() && target != InteractionTarget::GroundVehicle &&
            surface && surface->Ready() && surfaceTraffic)
        {
            if (auto npcCarMeters = surfaceTraffic->NearestGroundDistance(surface->Pose().origin, 4.8))
            {
                const float npcCarDistance = float(*npcCarMeters * .5);
                if (target == InteractionTarget::None || npcCarDistance < distance + .6f)
                {
                    distance = npcCarDistance;
                    target = InteractionTarget::GroundVehicle;
                }
            }
        }
        if (vehicleAllowed && target != InteractionTarget::GroundVehicle &&
            PlayerAircraftController::Nearby(*this))
        {
            const float aircraftDistance =
                state.aircraft.position
                    ? float(world::SurfaceGeometry::Distance(surface->Planet().radiusMeters,
                                                             surface->Pose().origin,
                                                             *state.aircraft.position) *
                            .5)
                    : 0.f;
            if (target == InteractionTarget::None || aircraftDistance < distance + .8f)
            {
                distance = aircraftDistance;
                target = InteractionTarget::Aircraft;
            }
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
        if (target == InteractionTarget::None && encounters::WorldEncounters::Get().HasNearbyInteraction())
        {
            return InteractionTarget::WorldEncounter;
        }
        // CQ-04 C2: Discover walkable urban facility entrances (Market / Clinic / Workshop / CivicHall)
        // on all 5 legacy cities and all procedural settlements without overriding higher-priority interactions.
        if (target == InteractionTarget::None && surface && surface->Ready())
        {
            const auto snapshot = surface->Stream().Snapshot();
            if (snapshot && snapshot->planet)
            {
                const double planetR = surface->Planet().radiusMeters;
                const auto playerOrigin = surface->Pose().origin;
                double bestDoorMeters = 4.6;
                ActiveFacilityInterior bestEntry;
                for (const auto *item : surface->NearbyStructures())
                {
                    if (!item)
                    {
                        continue;
                    }
                    const auto kind = EnterableUrbanFacility(*item, snapshot->planet.get());
                    if (kind == UrbanFacilityKind::None)
                    {
                        continue;
                    }
                    const world::GeneratedRegion *reg = nullptr;
                    for (const auto &candidateReg : snapshot->planet->regions)
                    {
                        if (candidateReg.definition.id == item->regionId)
                        {
                            reg = &candidateReg;
                            break;
                        }
                    }
                    if (!reg)
                    {
                        continue;
                    }
                    const auto localP = world::SurfaceGeometry::ToLocal(item->frame, playerOrigin, planetR);
                    const double cy = std::cos(item->yaw), sy = std::sin(item->yaw);
                    const double lx = localP.x * cy + localP.y * sy;
                    const double ly = -localP.x * sy + localP.y * cy;
                    const double hx = item->sizeMeters.x * .5, hy = item->sizeMeters.y * .5;
                    // Distance from player to the closest perimeter facade of this building.
                    const double cx = std::clamp(lx, -hx, hx);
                    const double cyClamped = std::clamp(ly, -hy, hy);
                    const double facadeDist = std::hypot(lx - cx, ly - cyClamped);
                    if (facadeDist < bestDoorMeters)
                    {
                        bestDoorMeters = facadeDist;
                        bestEntry.kind = kind;
                        bestEntry.regionId = reg->definition.id;
                        bestEntry.regionName = reg->definition.name;
                        bestEntry.facilityId = item->id;
                        bestEntry.label = std::string(FacilityKindName(kind));
                        bestEntry.returnPoint = playerOrigin;
                        bestEntry.returnYaw = state.facingYaw;
                    }
                }
                if (bestEntry.Active())
                {
                    state.nearbyFacilityEntrance = std::move(bestEntry);
                    return InteractionTarget::FacilityEntrance;
                }
            }
        }
        state.nearbyFacilityEntrance.Clear();
        return target;
    }
    if (HospitalRecovery::Active())
    {
        return InteractionTarget::None;
    }
    if (state.facilityInterior.Active())
    {
        if (Distance(state.playerPosition, {2.5f, 1.9f}) < 1.45f)
        {
            return InteractionTarget::FacilityExit;
        }
        // Purchase/service interaction starts by talking to the facility NPC across the counter.
        if (Distance(state.playerPosition, {-.2f, -1.05f}) < 2.05f)
        {
            return InteractionTarget::FacilityNpc;
        }
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
        struct HomePoint
        {
            Vector2 position;
            InteractionTarget target;
            float radius;
        };
        const HomePoint points[] = {
            {{1, -1.2f}, InteractionTarget::HomeTerminal, 1.5f},
            {CityFacilities::Find(InteractionTarget::HomeEntrance)->exit, InteractionTarget::ApartmentExit,
             1.5f},
            {kHomeBedInteraction, InteractionTarget::HomeBed, 1.45f},
            {kHomeWorkbenchInteraction, InteractionTarget::HomeWorkbench, 1.45f},
            {kHomeStorageInteraction, InteractionTarget::HomeStorage, 1.45f}};
        InteractionTarget best = InteractionTarget::None;
        float bestDist = 1000.f;
        for (const auto &pt : points)
        {
            const float d = Distance(state.playerPosition, pt.position);
            if (d < pt.radius && d < bestDist)
            {
                bestDist = d;
                best = pt.target;
            }
        }
        return best;
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

    if (game::GameLevels::Chapter().GetState().kainJoined &&
        Distance(state.playerPosition, kCityKainPosition) < 1.1f &&
        !CivicCombat::LandmarkDown(kCityKainPosition))
    {
        return InteractionTarget::Kain;
    }
    {
        const auto &arc = story::StoryArc::Get().State();
        if (arc.sectorLossTriggered && !arc.chapter4Complete &&
            Distance(state.playerPosition, {25.f, 7.f}) < 2.2f)
        {
            return InteractionTarget::StorySafetyLine;
        }
        if (arc.chapter4Complete)
        {
            if (game::GameLevels::Chapter().GetState().portableLightRepaired &&
                Distance(state.playerPosition, {kMaraPosition.x + 1.6f, kMaraPosition.y}) < 1.3f)
            {
                return InteractionTarget::StoryShelterLamp;
            }
            if (Distance(state.playerPosition, {11.f, -2.f}) < 1.8f)
            {
                return InteractionTarget::StoryEpilogueClinic;
            }
            if (Distance(state.playerPosition, {19.f, 2.f}) < 1.8f)
            {
                return InteractionTarget::StoryEpilogueWorkshop;
            }
        }
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
        if (point.target == InteractionTarget::Lia && story::StoryArc::Get().IsRiaMissing())
        {
            continue;
        }
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
    if (nearestTarget == InteractionTarget::None && encounters::WorldEncounters::Get().HasNearbyInteraction())
    {
        return InteractionTarget::WorldEncounter;
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
    if (target == InteractionTarget::WorldEncounter)
    {
        encounters::WorldEncounters::Get().Interact();
        return;
    }
    if (target == InteractionTarget::StoryOperationNode)
    {
        story::StoryArc::Get().Interact();
        return;
    }
    if (target == InteractionTarget::StoryOperationPortal)
    {
        if (!story::StoryArc::Get().InteractHaloReceiver())
        {
            story::StoryArc::Get().EnterZone(story::OperationZone::Waterway);
        }
        return;
    }
    if (target == InteractionTarget::StorySafetyLine)
    {
        story::StoryArc::Get().InteractHaloSafetyLine();
        return;
    }
    if (target == InteractionTarget::StoryShelterLamp)
    {
        story::StoryArc::Get().InteractHaloShelterLamp();
        return;
    }
    if (target == InteractionTarget::StoryEpilogueClinic)
    {
        story::StoryArc::Get().InteractHaloEpilogueClinic();
        return;
    }
    if (target == InteractionTarget::StoryEpilogueWorkshop)
    {
        story::StoryArc::Get().InteractHaloEpilogueWorkshop();
        return;
    }
    if (target == InteractionTarget::FacilityEntrance)
    {
        EnterFacilityInterior(state.nearbyFacilityEntrance);
        return;
    }
    if (target == InteractionTarget::FacilityExit)
    {
        ExitFacilityInterior();
        return;
    }
    if (target == InteractionTarget::FacilityNpc)
    {
        state.StopMotion();
        state.facingYaw = -kPi * .5f;
        state.facilityReactionSeconds = 1.15f;
        state.noticeSecondsRemaining = 0.f;
        Hud::OpenMenu(UiScreen::FacilityShop);
        return;
    }
    if (target == InteractionTarget::FacilityServicePrimary ||
        target == InteractionTarget::FacilityServiceSecondary)
    {
        InteractFacilityStation(target);
        return;
    }
    if (target == InteractionTarget::HomeBed || target == InteractionTarget::HomeWorkbench ||
        target == InteractionTarget::HomeStorage)
    {
        InteractResidence(target);
        return;
    }
    if (target == InteractionTarget::Aircraft)
    {
        PlayerAircraftController::Interact(*this);
        return;
    }
    if (target == InteractionTarget::GroundVehicle)
    {
        InteractGroundVehicle();
        return;
    }
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
         target == InteractionTarget::Mara || target == InteractionTarget::Kain ||
         target == InteractionTarget::PowerRelay || target == InteractionTarget::CityReceiver ||
         target == InteractionTarget::ObservatorySignal || target == InteractionTarget::ObservatoryExit))
    {
        InteractSurfaceStory(target);
        return;
    }
    if (target == InteractionTarget::FrontierSite)
    {
        game::GameLevels::Chapter().InteractInteractive();
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
            nearest->conversationSeconds = 8.f;
            nearest->facing.walking = false;
            nearest->activity = ResidentActivity::Talking;
            if (const auto rpos = ResidentPosition(*nearest))
            {
                const float dx = state.playerPosition.x - rpos->x;
                const float dy = state.playerPosition.y - rpos->y;
                if (dx * dx + dy * dy > 1.e-4f)
                {
                    nearest->facing.yaw = std::atan2(dy, dx);
                }
            }
            // Passerby NPC conversation runs live without pausing the game; the NPC pauses and faces the player.
            Dialogue::Say("resident/" + std::to_string(nearest - state.residents.data()),
                          nearest->dialogue + (nearest->profession == ResidentProfession::Merchant
                                                   ? " [거래 안내] 악명에 따른 가격 배율: " +
                                                         std::to_string(CivicJustice::PriceMultiplier())
                                                   : ""),
                          false);
        }
        return;
    }
    if (target == InteractionTarget::HomeTerminal)
    {
        if (state.mainQuestStage == MainQuestStage::ReadHomeTerminal)
        {
            state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::MeetLia);
            state.homeMessageRead = true;
            tell("리아 · 단말 메시지 / 잘 잤어? 오늘 N-0 정기 측정 날인데 전망대 수신기에 정체 모를 "
                 "응답이 잡혔어. 점검표엔 없는 주파수야. 접근 코드 붙여 둘 테니 밖에서 만나자.");
        }
        else
        {
            InteractResidence(InteractionTarget::HomeTerminal);
        }
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
            if (!surface || !surface->Ready())
            {
                ShowNotice("야외 지형을 준비하는 중입니다. 잠시 후 다시 나가세요.");
                return;
            }
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
        if (story::StoryArc::Get().InteractHaloKain())
        {
            return;
        }
        if (game::GameLevels::Chapter().IsAvailable() &&
            (game::GameLevels::Chapter().CompletedSites() == 3 ||
             game::GameLevels::Chapter().GetState().recordsCompared))
        {
            game::GameLevels::Chapter().InteractHub();
            return;
        }
        tell(game::GameLevels::Chapter().GetState().recordsCompared
                 ? "카인 / 지도에 있던 다리는 사라졌습니다. 아래 수로가 남아 있는지 확인해야겠군요. "
                   "예전 진입로는 기록해 뒀습니다."
                 : "카인 / N-0 외곽에서 근무했습니다. 지금은 기록부터 모읍시다. 길보다 먼저 사람 수를 "
                   "확인해야 합니다.");
        return;
    }
    if (target == InteractionTarget::Lia)
    {
        if (story::StoryArc::Get().IsRiaMissing())
        {
            return;
        }
        if (story::StoryArc::Get().IsActTwoUnlocked() && story::StoryArc::Get().InteractHaloReceiver())
        {
            return;
        }
        if (game::GameLevels::Mine().GetState().quest.Advance(game::MineQuestStage::ReturnToHalo))
        {
            state.isComplete = true;
            state.mainQuestStage = MainQuestStage::Complete;
            game::GameLevels::Chapter().Unlock();
            tell("리아 / 광산 좌표가 N-0 외곽 설비 목록이랑 딱 맞아떨어져! 마침 그쪽 발전기·운송대·폐설비에 "
                 "이상이 겹쳤어. 준비되면 개척지로 바로 출발하자.");
            ShowNotice("보상 해금 / 오르딘 광산 공로로 장갑 공용 차량이 헤일로에 배치되었습니다!");
            return;
        }
        if (game::GameLevels::Chapter().IsAvailable())
        {
            game::GameLevels::Chapter().InteractHub();
            return;
        }
        if (state.gateSignalHeard)
        {
            auto &mq = game::GameLevels::Mine().GetState().quest;
            if (mq.Advance(game::MineQuestStage::ShowRecord))
            {
                mq.Advance(game::MineQuestStage::CheckCityRelay);
                tell("리아 / 네가 보낸 적 없는 목소리라고? 잠깐만, 단말로 주파수 경로를 뜯어보니 인근 "
                     "오르딘 광산 통신선을 거쳤어! 같은 시각 실종 탐사선 구조 신호도 거기서 잡혔어. "
                     "바이크 타고 광산으로 가자.");
            }
            else if (mq.Advance(game::MineQuestStage::CheckCityRelay))
            {
                tell("리아 / 경로를 확인했어. 오르딘 광산 통신선을 거친 신호야. 바이크로 이동해서 "
                     "입구에서 하차한 뒤 안쪽을 조사하자.");
            }
            else
            {
                tell(mq.GetRiaDialogue());
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
        if (story::StoryArc::Get().InteractHaloMara())
        {
            return;
        }
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
        if (state.isPowerRestored)
        {
            ShowNotice(story::StoryArc::Get().State().chapter4Complete
                           ? "전력 장치 / 대피소와 주거 구역 비상 배선이 안정적으로 유지되고 있습니다."
                           : "전력 장치 / 주거 구역 전력 공급이 정상 작동 중입니다.");
            return;
        }
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
        const auto &arc = story::StoryArc::Get().State();
        if (!state.gateSignalHeard)
        {
            ShowNotice("전망대 / 승강기를 타고 관측 데크에 도착했습니다. 북동쪽의 신호 측정 콘솔로 이동하세요.");
        }
        else if (arc.chapter4Complete && !arc.nereusSignalReceived && arc.epilogueShelterVisited &&
                 arc.epilogueClinicVisited && arc.epilogueWorkshopVisited)
        {
            ShowNotice("전망대 / 관측 데크에 도착했습니다. 콘솔에서 17일간의 수신 기록을 정리할 수 있습니다.");
        }
        else
        {
            ShowNotice("전망대 / 관측 데크에 도착했습니다.");
        }
    }
    if (target == InteractionTarget::ObservatorySignal)
    {
        if (story::StoryArc::Get().State().chapter4Complete && story::StoryArc::Get().InteractHaloReceiver())
        {
            return;
        }
        const bool first = !state.gateSignalHeard;
        state.gateSignalHeard = true;
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::DescendObservatory);
        if (first)
        {
            Dialogue::Start("MQ00/observatory",
                            {{"voice", "발신자 미상", "수신 기록", "…수신 확인. 이 목소리가 들리면—", {}},
                             {"observation", "나", "현장",
                              "숨이 멎을 뻔했다. 방금 끊긴 끝음까지 틀림없는 내 목소리다. 그런데 이런 말을 녹음한 "
                              "기억은 없다.",
                              {}},
                             {"ria", "리아", "무전",
                              "음성 기록은 일단 저장했어. 얼굴이 하얗게 질렸네… 내려와, 광장에서 주파수 경로를 "
                              "같이 뜯어보자.",
                              {}}});
        }
        else
        {
            Dialogue::Say("observatory/archive",
                          "전망대 기록 / 전망대에서 수신한 음성과 경로 기록은 단말에 보관되어 있다.");
        }
    }
    if (target == InteractionTarget::ObservatoryExit)
    {
        if (!state.surfaceExploration && !state.gateSignalHeard)
        {
            ShowNotice("조사 안내 / 관측 데크 북동쪽 콘솔에서 신호 기록을 받은 뒤 내려가세요.");
            return;
        }
        const bool firstDescent = !state.hasDescendedObservatory;
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
        const bool showRiaHint = firstDescent && !story::StoryArc::Get().IsRiaMissing() &&
                                 game::GameLevels::Mine().GetState().quest.GetStage() ==
                                     game::MineQuestStage::ShowRecord;
        ShowNotice(showRiaHint ? "신호 기록 저장 / 집 근처에서 기다리는 리아에게 기록을 보여 주세요."
                               : "헤일로 / 전망대 앞 승강기로 내려왔습니다.");
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
        const bool firstRead = !state.homeMessageRead;
        state.homeMessageRead = true;
        SyncMissionProgress();
        if (firstRead || (state.missions.Status(MissionId::Signal) != MissionStatus::Completed &&
                          !state.missions.Active(MissionId::Signal)))
        {
            OfferMission(MissionId::Signal, "단말 기록");
        }
        else
        {
            InteractResidence(InteractionTarget::HomeTerminal);
        }
        return;
    }
    if (target == InteractionTarget::ObservatorySignal)
    {
        if (story::StoryArc::Get().State().chapter4Complete && story::StoryArc::Get().InteractHaloReceiver())
        {
            return;
        }
        bool first = !state.gateSignalHeard;
        state.gateSignalHeard = true;
        SyncMissionProgress();
        if (first)
        {
            Dialogue::Start("MQ00/observatory",
                            {{"voice", "발신자 미상", "수신 기록", "…수신 확인. 이 목소리가 들리면—"},
                             {"observation", "나", "현장",
                              "숨이 멎을 뻔했다. 방금 끊긴 끝음까지 틀림없는 내 목소리다. 그런데 이런 말을 "
                              "녹음한 기억은 없다."},
                             {"ria", "리아", "무전",
                              "음성 기록은 일단 저장했어. 얼굴이 하얗게 질렸네… 내려와, 광장에서 나랑 "
                              "주파수 경로를 같이 뜯어보자."}});
        }
        else
        {
            say("observatory/archive", "전망대 기록 / 전망대에서 수신한 음성과 경로 기록은 단말에 보관되어 있다.");
        }
        return;
    }
    if (target == InteractionTarget::ObservatoryExit)
    {
        const bool firstDescent = !state.hasDescendedObservatory;
        if (ReturnToSurface(kBeaconPosition))
        {
            if (state.gateSignalHeard)
            {
                state.hasDescendedObservatory = true;
            }
            SyncMissionProgress();
            const bool showRiaHint = firstDescent && state.gateSignalHeard &&
                                     !story::StoryArc::Get().IsRiaMissing() &&
                                     state.missions.Status(MissionId::Mine) != MissionStatus::Completed &&
                                     mine.quest.GetStage() == game::MineQuestStage::ShowRecord;
            ShowNotice(showRiaHint
                           ? "헤일로 광장 / 전망대에서 내려왔습니다. 광장의 리아에게 음성 기록을 들려주세요."
                           : "헤일로 광장 / 전망대 앞 승강기로 내려왔습니다.");
        }
        return;
    }
    if (target == InteractionTarget::Mara)
    {
        if (story::StoryArc::Get().InteractHaloMara())
        {
            return;
        }
        if (!state.missions.Active(MissionId::Mara) &&
            state.missions.Status(MissionId::Mara) != MissionStatus::Completed)
        {
            OfferMission(MissionId::Mara, "현장");
        }
        else if (CompleteMission(MissionId::Mara))
        {
            say("mara/report",
                "마라 / 불이 돌아왔어요! 어두운 방에 아이 혼자 둘 생각에 밤마다 잠을 설쳤는데… 정말 고마워요.");
            ShowNotice("선택 의뢰 완료 / 남겨 둔 불빛");
        }
        else if (state.isSideQuestComplete && state.isPowerRestored)
        {
            if (Dialogue::Danger())
            {
                ShowNotice("마라와의 대화 / 주변이 잠잠해진 뒤에 다시 이야기하세요.");
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
                state.isSideQuestComplete
                    ? "마라 / 언제든 들르세요. 따뜻한 차를 끓여 둘게요."
                    : "마라 / 동쪽 전력 장치가 말썽이에요. 바쁘지 않으실 때 한 번만 살펴봐 주실래요?");
        }
        return;
    }
    if (target == InteractionTarget::Kain)
    {
        if (story::StoryArc::Get().InteractHaloKain())
        {
            return;
        }
        if (chapter.IsAvailable() &&
            (chapter.CompletedSites() == 3 || chapter.GetState().recordsCompared))
        {
            chapter.InteractHub();
            return;
        }
        say("kain/halo/" + std::to_string(chapter.GetState().recordsCompared),
            chapter.GetState().recordsCompared
                ? "카인 / 지도에 있던 다리는 사라졌습니다. 아래 수로가 남아 있는지 확인해야겠군요. 예전 "
                  "진입로는 기록해 두었습니다."
                : "카인 / N-0 외곽에서 근무했습니다. 길보다 먼저 사람 수부터 확인합시다.");
        return;
    }
    if (target == InteractionTarget::PowerRelay)
    {
        if (state.isPowerRestored)
        {
            ShowNotice(story::StoryArc::Get().State().chapter4Complete
                           ? "전력 장치 / 대피소와 주거 구역 비상 배선이 안정적으로 유지되고 있습니다."
                           : "전력 장치 / 주거 구역 전력 공급이 정상 작동 중입니다.");
            return;
        }
        state.isPowerRestored = true;
        ShowNotice("전력 장치 / 주거 구역 전력을 복구했습니다. 창문에 불이 들어오니 마라에게 알려 주세요.");
        return;
    }
    if (target == InteractionTarget::Lia)
    {
        if (story::StoryArc::Get().IsRiaMissing())
        {
            return;
        }
        if (story::StoryArc::Get().IsActTwoUnlocked() && story::StoryArc::Get().InteractHaloReceiver())
        {
            return;
        }
        if (MissionEligible(MissionId::Mine) &&
            state.missions.Status(MissionId::Mine) != MissionStatus::Completed)
        {
            if (!state.missions.Active(MissionId::Mine))
            {
                OfferMission(MissionId::Mine, "현장");
                return;
            }
            if (CompleteMission(MissionId::Mine))
            {
                chapter.Unlock();
                say("mine/report",
                    "리아 / 광산 중계 좌표가 N-0 외곽 개척지 세 현장의 전력·통신선과 딱 겹쳐! 마침 "
                    "거기서 정전이랑 운송대 실종이 같이 터졌거든. 준비되면 개척지로 움직이자.");
                ShowNotice("보상 해금 / 오르딘 광산 공로로 장갑 공용 차량이 헤일로에 배치되었습니다!");
                return;
            }
            if (mine.quest.GetStage() == game::MineQuestStage::ShowRecord ||
                mine.quest.GetStage() == game::MineQuestStage::CheckCityRelay)
            {
                mine.quest.Advance(game::MineQuestStage::ShowRecord);
                mine.quest.Advance(game::MineQuestStage::CheckCityRelay);
                if (surface && surface->Ready())
                {
                    state.planetDestination =
                        surface->Stream().Snapshot()->planet->Region("mq00_mine").access.position;
                }
                Dialogue::Start(
                    "mission/mine/companion",
                    {{"trace",
                      "리아",
                      "현장",
                      "전망대 잡음 밑바닥을 뜯어보니까 인근 오르딘 광산 통신선 주소가 겹쳐 있어. "
                      "광산 입구 좌표는 지도에 찍어 뒀어."},
                     {"join",
                      "리아",
                      "동행 제안",
                      "안쪽은 장비도 낡고 길도 험해. 나도 같이 내려갈게, 준비됐어?",
                      {{"join", "같이 내려간다"}, {"later", "잠깐만 더 둘러본다"}}}},
                    [this](const std::string &choice) {
                        if (choice == "join" && state.missions.Active(MissionId::Mine))
                        {
                            game::GameLevels::Mine().GetState().quest.BeginJourney();
                        }
                    },
                    true);
                return;
            }
            if (mine.quest.GetStage() == game::MineQuestStage::TravelToMine && !mine.quest.IsRiaJoined())
            {
                Dialogue::Start(
                    "mission/mine/companion",
                    {{"join",
                      "리아",
                      "동행 제안",
                      "광산 안쪽은 장비도 낡고 길도 험해. 나도 같이 내려갈게, 준비됐어?",
                      {{"join", "같이 내려간다"}, {"later", "잠깐만 더 둘러본다"}}}},
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
            if (chapter.CompletedSites() == 3 || chapter.GetState().recordsCompared)
            {
                chapter.InteractHub();
                return;
            }
            Dialogue::Start("surface/story/frontier/status",
                            StoryDialogue::FrontierStatus(chapter.GetState()), {}, true);
            return;
        }
        if (state.missions.Active(MissionId::Signal))
        {
            say("signal/status", "리아 / 전망대 수신 기록 먼저 들어 봐 줘. 난 여기 광장에서 기다리고 있을게.");
            return;
        }
        if (state.missions.Status(MissionId::Signal) != MissionStatus::Completed)
        {
            OfferMission(MissionId::Signal, "현장");
            return;
        }
        say("lia/free",
            "리아 / 전망대 기록은 그대로 보관해 뒀어. 준비되면 나랑 같이 광산 쪽 경로를 짚어 보자.");
        return;
    }
    if (target == InteractionTarget::CityReceiver)
    {
        if (story::StoryArc::Get().InteractHaloReceiver())
        {
            return;
        }
        if (state.missions.Active(MissionId::Mine))
        {
            if (CompleteMission(MissionId::Mine))
            {
                chapter.Unlock();
                say("mine/report", "수신망 / 광산 중계 좌표를 대조 완료했습니다. N-0 외곽 개척지 세 현장의 "
                                   "신호선과 겹치는 경로가 잡힙니다.");
                return;
            }
            if (mine.quest.Advance(game::MineQuestStage::CheckCityRelay))
            {
                state.planetDestination =
                    surface->Stream().Snapshot()->planet->Region("mq00_mine").access.position;
                say("mine/route", "수신망 / 전망대 신호와 오르딘 광산 통신선의 중계 흔적이 겹칩니다. 광산 "
                                  "입구 좌표를 지도에 표시했습니다.");
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
        say("receiver/free", "수신망 / 헤일로 관제 채널이 열려 있습니다. 전망대 음성 기록을 확보하면 "
                             "발신 경로를 역추적할 수 있습니다.");
    }
}
// ============================================================================
// RelayTravel — keeps the source live until destination chunks and an unoccupied landing are ready
// ============================================================================
const world::RelayCandidate *RelayTravel::Nearby()
{
    auto &g = Game::Get();
    const auto &s = g.GetState();
    if (!s.surfaceExploration || g.IsSurfaceMine() || s.IsInsideAny() || !g.Surface() ||
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
    if (g.IsSurfaceMine() || s.IsInsideAny())
    {
        return "지상의 중계환에서 이용할 수 있습니다.";
    }
    if (s.Mounted())
    {
        return "차량을 주차하고 내려 주세요. 차량은 출발지에 남습니다.";
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
    if (!relay || s.Mounted() || s.relayPreparing)
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
// Game — CQ-09 player residence hub (rest, crafting/upgrades, stash, terminal)
// ============================================================================
bool Game::ResidenceRest(bool saveCheckpoint)
{
    if (!state.isInsideHome || HospitalRecovery::Active())
    {
        return false;
    }
    auto &p = Player();
    p.health = p.maxHealth;
    state.playerMotion.mobility.RestoreStamina(PlayerMobility::MaxStamina, 0.f);
    auto &law = CivicJustice::State();
    law.playerImpact = {};
    if (surfaceParty)
    {
        surfaceParty->RecoverAll();
    }
    auto &mine = game::GameLevels::Mine().GetState();
    mine.riaCombat.health = (std::max)(mine.riaCombat.health, 100);
    mine.riaCombat.downedSeconds = 0;
    auto &chapter = game::GameLevels::Chapter().GetState();
    chapter.ria.combat.health = (std::max)(chapter.ria.combat.health, 100);
    chapter.ria.combat.downedSeconds = 0;
    chapter.kain.combat.health = (std::max)(chapter.kain.combat.health, 100);
    chapter.kain.combat.downedSeconds = 0;
    if (mine.loadout.HasPistol())
    {
        mine.pistol.TopUpRounds(mine.pistol.GetMagazineSize());
    }
    if (mine.loadout.HasPrimary())
    {
        mine.carbine.TopUpRounds(mine.carbine.GetMagazineSize());
    }
    bool calmedWanted = false;
    const bool highWantedBlocked = law.wanted >= 3;
    if (law.wanted > 0 && law.wanted <= 2)
    {
        law.wanted = 0;
        law.heat = 0;
        law.unseenSeconds = 0;
        law.sinceCrime = 1000;
        law.responseStarted = false;
        law.identified = false;
        law.officers.clear();
        PlanetSecurity::State().officers.clear();
        calmedWanted = true;
    }
    ++state.residence.restCount;
    state.residence.lastRestPlayTime = state.playTimeSeconds;
    bool saved = false;
    if (saveCheckpoint && state.surfaceExploration)
    {
        saved = SaveSystem::Save();
    }
    std::string note = "거주지 휴식 / 체력·스태미너·동료 컨디션과 탄창을 정비했습니다.";
    if (calmedWanted)
    {
        note += " 실내 은신으로 1~2단계 수배가 진정되었습니다.";
    }
    else if (highWantedBlocked)
    {
        note += " 3단계 이상 고위험 수배는 은신 휴식만으로 해제되지 않습니다.";
    }
    if (saved)
    {
        note += " 안전 체크포인트 저장 완료.";
    }
    ShowNotice(note);
    return true;
}
bool Game::ResidenceCraftKit()
{
    if (!state.isInsideHome || HospitalRecovery::Active())
    {
        return false;
    }
    auto &mine = game::GameLevels::Mine().GetState();
    if (mine.loadout.HasPistol())
    {
        mine.pistol.TopUpRounds(mine.pistol.GetMagazineSize());
    }
    if (mine.loadout.HasPrimary())
    {
        mine.carbine.TopUpRounds(mine.carbine.GetMagazineSize());
    }
    auto &p = Player();
    if (p.recoveryKits >= ResidenceState::kMaxCarryKits)
    {
        ShowNotice("작업대 / 휴대 회복 키트가 최대치(" + std::to_string(ResidenceState::kMaxCarryKits) +
                   "개)입니다. 보유 총기 탄창만 보충했습니다.");
        return false;
    }
    if (p.salvage >= 1)
    {
        --p.salvage;
        ++p.recoveryKits;
        ShowNotice("작업대 / 고철 1개로 회복 키트를 제작했습니다. (보유 " + std::to_string(p.recoveryKits) +
                   "개 · 탄창 보충 완료)");
        return true;
    }
    if (p.credits >= 40)
    {
        p.credits -= 40;
        ++p.recoveryKits;
        ShowNotice("작업대 / 40 크레딧으로 회복 키트를 보급했습니다. (보유 " +
                   std::to_string(p.recoveryKits) + "개 · 탄창 보충 완료)");
        return true;
    }
    ShowNotice("작업대 / 회복 키트 제작에는 고철 1개 또는 40 크레딧이 필요합니다.");
    return false;
}
bool Game::ResidenceUpgradeArmor()
{
    if (!state.isInsideHome || HospitalRecovery::Active())
    {
        return false;
    }
    auto &r = state.residence;
    if (r.armorTier >= ResidenceState::kMaxUpgradeTier)
    {
        ShowNotice("작업대 / 전술 방호복 보강이 최고 단계(3단계)에 도달했습니다.");
        return false;
    }
    const int salvageCost = ResidenceState::ArmorSalvageCost(r.armorTier);
    const int creditCost = ResidenceState::ArmorCreditCost(r.armorTier);
    auto &p = Player();
    if (p.salvage < salvageCost || p.credits < creditCost)
    {
        ShowNotice("작업대 / 방호복 보강에 고철 " + std::to_string(salvageCost) + "개와 " +
                   std::to_string(creditCost) + " 크레딧이 필요합니다.");
        return false;
    }
    p.salvage -= salvageCost;
    p.credits -= creditCost;
    ++r.armorTier;
    p.maxHealth += 20;
    p.health = (std::min)(p.maxHealth, p.health + 20);
    p.defense += 2;
    ShowNotice("작업대 / 전술 방호복 보강 " + std::to_string(r.armorTier) + "단계 완료 · 최대 체력 " +
               std::to_string(p.maxHealth) + " · 방어력 " + std::to_string(p.defense));
    return true;
}
bool Game::ResidenceUpgradeWeapon()
{
    if (!state.isInsideHome || HospitalRecovery::Active())
    {
        return false;
    }
    auto &r = state.residence;
    if (r.weaponTier >= ResidenceState::kMaxUpgradeTier)
    {
        ShowNotice("작업대 / 무기 정밀 튜닝이 최고 단계(3단계)에 도달했습니다.");
        return false;
    }
    const int salvageCost = ResidenceState::WeaponSalvageCost(r.weaponTier);
    const int creditCost = ResidenceState::WeaponCreditCost(r.weaponTier);
    auto &p = Player();
    if (p.salvage < salvageCost || p.credits < creditCost)
    {
        ShowNotice("작업대 / 무기 튜닝에 고철 " + std::to_string(salvageCost) + "개와 " +
                   std::to_string(creditCost) + " 크레딧이 필요합니다.");
        return false;
    }
    p.salvage -= salvageCost;
    p.credits -= creditCost;
    ++r.weaponTier;
    p.attack += 4;
    ShowNotice("작업대 / 무기 정밀 튜닝 " + std::to_string(r.weaponTier) + "단계 완료 · 공격력 " +
               std::to_string(p.attack));
    return true;
}
bool Game::ResidenceRepairVehicles()
{
    if (!state.isInsideHome || HospitalRecovery::Active())
    {
        return false;
    }
    const bool carNeeds = state.car.position && state.car.health < GroundVehicle::MaxHealth;
    const bool airNeeds = state.aircraft.position && state.aircraft.health < PlayerAircraft::MaxHealth;
    if (!carNeeds && !airNeeds)
    {
        ShowNotice("작업대 / 정비할 손상 차량이나 비행체가 없습니다.");
        return false;
    }
    auto &p = Player();
    if (p.salvage >= 1)
    {
        --p.salvage;
    }
    else if (p.credits >= 80)
    {
        p.credits -= 80;
    }
    else
    {
        ShowNotice("작업대 / 원격 기체 정비에는 고철 1개 또는 80 크레딧이 필요합니다.");
        return false;
    }
    if (carNeeds)
    {
        state.car.health = GroundVehicle::MaxHealth;
        state.car.wreckSeconds = 0;
        state.car.velocity = {};
        state.car.speed = 0;
    }
    if (airNeeds)
    {
        state.aircraft.health = PlayerAircraft::MaxHealth;
        state.aircraft.wreckSeconds = 0;
        state.aircraft.Stop();
        state.aircraft.grounded = true;
    }
    ShowNotice("작업대 / 등록된 차량·비행체 내구도를 100%로 원격 복구했습니다.");
    return true;
}
bool Game::ResidenceRecallVehicles()
{
    if (!state.isInsideHome || HospitalRecovery::Active())
    {
        return false;
    }
    state.bikePosition = kJetBikePosition;
    state.bikeYaw = kJetBikeYaw;
    state.bikeSpeed = 0;
    state.bikeVelocity = {};
    state.bikeSafePosition = kJetBikePosition;
    state.bikeSafeYaw = kJetBikeYaw;
    state.bikeSafePoseValid = true;
    if (state.surfaceExploration && surface && surface->Ready() && surfaceCities)
    {
        if (auto homeBike = surfaceCities->Point(*surface, 0, kJetBikePosition))
        {
            surfaceBike = *homeBike;
            const auto frame = world::SurfaceGeometry::Frame(*homeBike);
            surfaceBikeHeading = world::SurfaceGeometry::Unit(
                {frame.right.x * std::cos(kJetBikeYaw) + frame.forward.x * std::sin(kJetBikeYaw),
                 frame.right.y * std::cos(kJetBikeYaw) + frame.forward.y * std::sin(kJetBikeYaw),
                 frame.right.z * std::cos(kJetBikeYaw) + frame.forward.z * std::sin(kJetBikeYaw)});
        }
        if (state.car.position && state.car.health > 0)
        {
            if (auto pad = surfaceCities->Point(*surface, 0, {kHomeDoor.x + 3.5f, kHomeDoor.y - 3.f}))
            {
                state.car.position = *pad;
                state.car.SetYaw(0.f, world::SurfaceGeometry::Frame(*pad));
                state.car.velocity = {};
                state.car.speed = 0;
            }
        }
    }
    ShowNotice("거주지 단말 / 개인 바이크와 가동 중인 차량을 헤일로 거주지 앞 주차 구역으로 호출했습니다.");
    return true;
}
bool Game::ResidenceDepositCredits(int amount)
{
    if (!state.isInsideHome || amount <= 0)
    {
        return false;
    }
    auto &p = Player();
    auto &r = state.residence;
    const int move = (std::min)({amount, p.credits, ResidenceState::kMaxStashCredits - r.stashCredits});
    if (move <= 0)
    {
        ShowNotice("보관함 / 예치할 크레딧이 없거나 금고가 가득 찼습니다.");
        return false;
    }
    p.credits -= move;
    r.stashCredits += move;
    ShowNotice("보관함 / " + std::to_string(move) + " 크레딧을 금고에 예치했습니다. (금고 " +
               std::to_string(r.stashCredits) + " · 휴대 " + std::to_string(p.credits) + ")");
    return true;
}
bool Game::ResidenceWithdrawCredits(int amount)
{
    if (!state.isInsideHome || amount <= 0)
    {
        return false;
    }
    auto &p = Player();
    auto &r = state.residence;
    const int move = (std::min)({amount, r.stashCredits, game::PlayerStats::kMaximumCredits - p.credits});
    if (move <= 0)
    {
        ShowNotice("보관함 / 인출할 크레딧이 금고에 없습니다.");
        return false;
    }
    r.stashCredits -= move;
    p.credits += move;
    ShowNotice("보관함 / " + std::to_string(move) + " 크레딧을 금고에서 인출했습니다. (금고 " +
               std::to_string(r.stashCredits) + " · 휴대 " + std::to_string(p.credits) + ")");
    return true;
}
bool Game::ResidenceDepositKit()
{
    if (!state.isInsideHome)
    {
        return false;
    }
    auto &p = Player();
    auto &r = state.residence;
    if (p.recoveryKits <= 0 || r.stashRecoveryKits >= ResidenceState::kMaxStashKits)
    {
        ShowNotice("보관함 / 보관할 회복 키트가 없거나 보관함이 가득 찼습니다.");
        return false;
    }
    --p.recoveryKits;
    ++r.stashRecoveryKits;
    ShowNotice("보관함 / 회복 키트 1개를 보관했습니다. (보관 " + std::to_string(r.stashRecoveryKits) +
               " · 휴대 " + std::to_string(p.recoveryKits) + ")");
    return true;
}
bool Game::ResidenceWithdrawKit()
{
    if (!state.isInsideHome)
    {
        return false;
    }
    auto &p = Player();
    auto &r = state.residence;
    if (r.stashRecoveryKits <= 0 || p.recoveryKits >= ResidenceState::kMaxCarryKits)
    {
        ShowNotice("보관함 / 인출할 키트가 없거나 휴대 한도(" + std::to_string(ResidenceState::kMaxCarryKits) +
                   "개)입니다.");
        return false;
    }
    --r.stashRecoveryKits;
    ++p.recoveryKits;
    ShowNotice("보관함 / 회복 키트 1개를 꺼냈습니다. (보관 " + std::to_string(r.stashRecoveryKits) +
               " · 휴대 " + std::to_string(p.recoveryKits) + ")");
    return true;
}
bool Game::ResidenceDepositSalvage()
{
    if (!state.isInsideHome)
    {
        return false;
    }
    auto &p = Player();
    auto &r = state.residence;
    const int move = (std::min)(p.salvage, ResidenceState::kMaxStashSalvage - r.stashSalvage);
    if (move <= 0)
    {
        ShowNotice("보관함 / 보관할 고철이 없거나 보관함이 가득 찼습니다.");
        return false;
    }
    p.salvage -= move;
    r.stashSalvage += move;
    ShowNotice("보관함 / 고철 " + std::to_string(move) + "개를 보관했습니다. (보관 " +
               std::to_string(r.stashSalvage) + " · 휴대 " + std::to_string(p.salvage) + ")");
    return true;
}
bool Game::ResidenceWithdrawSalvage()
{
    if (!state.isInsideHome)
    {
        return false;
    }
    auto &p = Player();
    auto &r = state.residence;
    if (r.stashSalvage <= 0)
    {
        ShowNotice("보관함 / 보관된 고철이 없습니다.");
        return false;
    }
    const int move = r.stashSalvage;
    r.stashSalvage = 0;
    p.salvage += move;
    ShowNotice("보관함 / 고철 " + std::to_string(move) + "개를 모두 꺼냈습니다. (휴대 " +
               std::to_string(p.salvage) + ")");
    return true;
}
void Game::ResidenceCycleGender()
{
    state.playerGender =
        state.playerGender == models::Gender::Male ? models::Gender::Female : models::Gender::Male;
    ShowNotice(std::string("옷장 / 캐릭터 체형을 ") +
               (state.playerGender == models::Gender::Male ? "남성" : "여성") + "으로 변경했습니다.");
}
void Game::ResidenceCycleHair()
{
    const unsigned next = (unsigned(state.playerHair) + 1u) % unsigned(models::HairStyle::Count);
    state.playerHair = models::HairStyle(next);
    static constexpr const char *kHairNames[] = {"버즈컷", "사이드파트", "컬", "단발", "포니테일", "번"};
    ShowNotice(std::string("옷장 / 헤어스타일을 '") + kHairNames[next] + "'으로 변경했습니다.");
}
void Game::InteractResidence(InteractionTarget target)
{
    if (!state.isInsideHome || HospitalRecovery::Active())
    {
        return;
    }
    const auto &p = Player();
    const auto &r = state.residence;
    if (target == InteractionTarget::HomeBed)
    {
        const auto &law = CivicJustice::State();
        std::string status = "체력 " + std::to_string(p.health) + "/" + std::to_string(p.maxHealth) +
                             " · 누적 휴식 " + std::to_string(r.restCount) + "회";
        if (law.wanted > 0)
        {
            status += law.wanted <= 2 ? " · 수배 " + std::to_string(law.wanted) + "단계(은신 진정 가능)"
                                      : " · 수배 " + std::to_string(law.wanted) + "단계(고위험 봉쇄)";
        }
        std::vector<DialogueChoice> choices = {{"rest", "짧은 휴식 (체력·스태미너·동료·탄창 회복)"}};
        if (state.surfaceExploration)
        {
            choices.push_back({"rest_save", "휴식 및 안전 체크포인트 저장"});
        }
        choices.push_back({"cancel", "일어나기"});
        Dialogue::Start("residence/bed",
                        {{"bed", "침대", "거주지 휴식", status, std::move(choices)}},
                        [this](const std::string &choice) {
                            if (choice == "rest")
                            {
                                ResidenceRest(false);
                            }
                            else if (choice == "rest_save")
                            {
                                ResidenceRest(true);
                            }
                        },
                        true);
        return;
    }
    if (target == InteractionTarget::HomeWorkbench)
    {
        const std::string header = "휴대 고철 " + std::to_string(p.salvage) + " · 크레딧 " +
                                   std::to_string(p.credits) + " · 회복 키트 " +
                                   std::to_string(p.recoveryKits) + "/" +
                                   std::to_string(ResidenceState::kMaxCarryKits) + " · 방호복 " +
                                   std::to_string(r.armorTier) + "/3 · 무기 " +
                                   std::to_string(r.weaponTier) + "/3";
        std::vector<DialogueChoice> choices = {
            {"kit", "회복 키트 제작 & 탄창 보충 (고철 1 또는 40 Cr)"},
            {"armor", r.armorTier < ResidenceState::kMaxUpgradeTier
                          ? "전술 방호복 보강 +" + std::to_string(r.armorTier + 1) + " (고철 " +
                                std::to_string(ResidenceState::ArmorSalvageCost(r.armorTier)) + " + " +
                                std::to_string(ResidenceState::ArmorCreditCost(r.armorTier)) +
                                " Cr → 최대체력+20·방어+2)"
                          : "전술 방호복 보강 (최고 3단계 완료)"},
            {"weapon", r.weaponTier < ResidenceState::kMaxUpgradeTier
                           ? "무기 정밀 튜닝 +" + std::to_string(r.weaponTier + 1) + " (고철 " +
                                 std::to_string(ResidenceState::WeaponSalvageCost(r.weaponTier)) + " + " +
                                 std::to_string(ResidenceState::WeaponCreditCost(r.weaponTier)) +
                                 " Cr → 공격+4)"
                           : "무기 정밀 튜닝 (최고 3단계 완료)"},
            {"vehicle", "차량·비행체 원격 정비 (고철 1 또는 80 Cr)"},
            {"cancel", "작업대 닫기"}};
        Dialogue::Start("residence/workbench",
                        {{"bench", "작업대", "장비 정비 · 강화", header, std::move(choices)}},
                        [this](const std::string &choice) {
                            if (choice == "kit")
                            {
                                ResidenceCraftKit();
                            }
                            else if (choice == "armor")
                            {
                                ResidenceUpgradeArmor();
                            }
                            else if (choice == "weapon")
                            {
                                ResidenceUpgradeWeapon();
                            }
                            else if (choice == "vehicle")
                            {
                                ResidenceRepairVehicles();
                            }
                        },
                        true);
        return;
    }
    if (target == InteractionTarget::HomeStorage)
    {
        state.StopMotion();
        state.noticeSecondsRemaining = 0.f;
        Hud::OpenMenu(UiScreen::HomeStorage);
        return;
    }
    if (target == InteractionTarget::HomeTerminal)
    {
        const auto &law = CivicJustice::State();
        const std::string briefing =
            "금고 " + std::to_string(r.stashCredits) + " Cr · 방호 " + std::to_string(r.armorTier) +
            "/3 · 무기 " + std::to_string(r.weaponTier) + "/3 · 등록 중계환 " +
            std::to_string(state.relayNetwork.Registrations().size()) + "개 · 수배 " +
            std::to_string(law.wanted) + "성 · 악명 " + std::to_string(int(std::round(law.infamy)));
        const auto &arc = story::StoryArc::Get().State();
        std::vector<DialogueChoice> choices = {
            {"brief", "거점·탐사 현황 확인"},
            {"recall", "개인 바이크·차량 거주지 앞으로 호출"},
            {"message", arc.nereusSignalReceived ? "네레우스 수신 기록 다시 듣기"
                        : story::StoryArc::Get().IsRiaMissing() ? "리아가 남긴 마지막 메시지 보기"
                                                                : "리아의 보관 메시지 다시 보기"},
            {"cancel", "단말 닫기"}};
        Dialogue::Start(
            "residence/terminal",
            {{"terminal", "거주지 단말", "헤일로 07 거점", briefing, std::move(choices)}},
            [this, briefing](const std::string &choice) {
                if (choice == "brief")
                {
                    ShowNotice("거점 브리핑 / " + briefing);
                }
                else if (choice == "recall")
                {
                    ResidenceRecallVehicles();
                }
                else if (choice == "message")
                {
                    const auto &a = story::StoryArc::Get().State();
                    if (a.nereusSignalReceived)
                    {
                        Dialogue::Say("residence/nereus-replay",
                                      "수신 기록 · 리아의 음성 (네레우스 · 송신 시각 미확인) / 수신 확인. "
                                      "여기 조명이 하나 켜졌어. 누가 듣고 있으면…");
                    }
                    else if (story::StoryArc::Get().IsRiaMissing())
                    {
                        Dialogue::Say("residence/lia-last",
                                      "리아 · 마지막 수신 / …여긴, 아직 빛이 있어.");
                    }
                    else
                    {
                        Dialogue::Say("residence/lia-archive",
                                      "리아 · 보관 메시지 / N-0 정기 측정 중에 이상한 응답이 잡혔어. "
                                      "전망대 접근 코드 같이 보내 둘게.");
                    }
                }
            },
            true);
    }
}
// ============================================================================
// CQ-04 C2 — Urban Facility Interiors (Market / Clinic / Workshop / CivicHall)
// ============================================================================
UrbanFacilityKind Game::ClassifyUrbanFacility(const world::SurfaceStructure &structure)
{
    if (!structure.solid || structure.sizeMeters.x < 5. || structure.sizeMeters.y < 5. ||
        structure.sizeMeters.z < 3.5)
    {
        return UrbanFacilityKind::None;
    }
    // Strict whitelist of actual architectural building forms (never street props, consoles, crates, or aircraft).
    switch (structure.form)
    {
    case world::ContentForm::MeasuredBuilding:
    case world::ContentForm::Building:
    case world::ContentForm::Habitat:
    case world::ContentForm::Terrace:
    case world::ContentForm::Workshop:
    case world::ContentForm::Dome:
    case world::ContentForm::Spire:
    case world::ContentForm::TwinBridge:
    case world::ContentForm::Arcology:
    case world::ContentForm::Transit:
    case world::ContentForm::Courtyard:
        break;
    default:
        return UrbanFacilityKind::None;
    }
    if (structure.use == world::ContentUse::Administration)
    {
        return UrbanFacilityKind::CivicHall;
    }
    if (structure.use == world::ContentUse::Industry)
    {
        return UrbanFacilityKind::Workshop;
    }
    if (structure.use == world::ContentUse::Services)
    {
        // Deterministic split between Market and Clinic based on building variant/id.
        if (structure.form == world::ContentForm::MeasuredBuilding && structure.variant == 2)
        {
            return UrbanFacilityKind::Clinic;
        }
        std::uint64_t h = 14695981039346656037ULL;
        for (unsigned char ch : structure.id)
        {
            h = (h ^ ch) * 1099511628211ULL;
        }
        return (h % 3 == 0) ? UrbanFacilityKind::Clinic : UrbanFacilityKind::Market;
    }
    return UrbanFacilityKind::None;
}
UrbanFacilityKind Game::EnterableUrbanFacility(const world::SurfaceStructure &structure,
                                               const world::GeneratedPlanet *planet)
{
    const auto kind = ClassifyUrbanFacility(structure);
    if (kind == UrbanFacilityKind::None || !planet)
    {
        return UrbanFacilityKind::None;
    }
    const world::GeneratedRegion *reg = nullptr;
    for (const auto &candidate : planet->regions)
    {
        if (candidate.definition.id == structure.regionId)
        {
            reg = &candidate;
            break;
        }
    }
    if (!reg || reg->definition.kind != world::RegionKind::Settlement)
    {
        return UrbanFacilityKind::None;
    }
    const double planetR = planet->terrain.Planet().radiusMeters;
    const auto local = world::SurfaceGeometry::ToLocal(reg->frame, structure.frame.origin, planetR);
    // Never turn Halo's own Home or Observatory landmarks into generic urban facilities.
    if (structure.regionId == "halo_07")
    {
        if (std::hypot(local.x - kHomeBuildingPosition.x * 2.,
                       local.y - kHomeBuildingPosition.y * 2.) < 16. ||
            std::hypot(local.x - kObservatoryBuildingPosition.x * 2.,
                       local.y - kObservatoryBuildingPosition.y * 2.) < 16.)
        {
            return UrbanFacilityKind::None;
        }
    }
    // Unique capital/metropolis civic landmarks are always enterable.
    if (structure.id.find(".landmark.") != std::string::npos)
    {
        return kind;
    }
    // Spatial thinning per facility kind: partition each settlement into kind-specific sectors with a
    // deterministic interior anchor ([0.25, 0.75] of the sector) and tight capture radius so duplicate
    // facilities do not cluster in a small area while remaining independent of chunk load order.
    const double cellMeters = kind == UrbanFacilityKind::Market     ? 180.
                              : kind == UrbanFacilityKind::Workshop ? 200.
                              : kind == UrbanFacilityKind::Clinic   ? 220.
                                                                    : 260.;
    const double captureRadius = kind == UrbanFacilityKind::CivicHall ? 24. : 20.;
    const auto gx = std::int64_t(std::floor(local.x / cellMeters));
    const auto gy = std::int64_t(std::floor(local.y / cellMeters));
    std::uint64_t seed = 14695981039346656037ULL;
    auto mix = [&](std::uint64_t v) {
        seed = (seed ^ v) * 1099511628211ULL;
        seed ^= seed >> 33;
    };
    for (unsigned char ch : reg->definition.id)
    {
        mix(ch);
    }
    mix(std::uint64_t(kind) + 0x9e3779b97f4a7c15ULL);
    mix(std::uint64_t(gx));
    mix(std::uint64_t(gy));
    const double u = .25 + .50 * (double(seed & 0xFFFFu) / 65535.);
    const double v = .25 + .50 * (double((seed >> 16) & 0xFFFFu) / 65535.);
    const double ax = (double(gx) + u) * cellMeters;
    const double ay = (double(gy) + v) * cellMeters;
    const double dx = local.x - ax;
    const double dy = local.y - ay;
    if (dx * dx + dy * dy > captureRadius * captureRadius)
    {
        return UrbanFacilityKind::None;
    }
    return kind;
}
const char *Game::FacilityKindName(UrbanFacilityKind kind)
{
    switch (kind)
    {
    case UrbanFacilityKind::Market:
        return "상점 · 보급소";
    case UrbanFacilityKind::Clinic:
        return "의료소";
    case UrbanFacilityKind::Workshop:
        return "정비소 · 공방";
    case UrbanFacilityKind::CivicHall:
        return "행정 · 의뢰소";
    default:
        return "";
    }
}
const char *Game::FacilityShortLabel(UrbanFacilityKind kind)
{
    switch (kind)
    {
    case UrbanFacilityKind::Market:
        return "상점";
    case UrbanFacilityKind::Clinic:
        return "의료소";
    case UrbanFacilityKind::Workshop:
        return "정비소";
    case UrbanFacilityKind::CivicHall:
        return "행정소";
    default:
        return "";
    }
}
world::SurfacePoint Game::FacilityDoorPoint(const world::SurfaceStructure &structure, double planetRadius)
{
    const double cy = std::cos(structure.yaw), sy = std::sin(structure.yaw);
    const double forward = structure.sizeMeters.y * .5 + 1.1;
    // Front door offset along structure's local +Y facade.
    auto pt = world::SurfaceGeometry::FromLocal(
        structure.frame, {-sy * forward, cy * forward, 0}, planetRadius);
    pt.altitudeMeters = structure.frame.origin.altitudeMeters;
    return pt;
}
bool Game::EnterFacilityInterior(const ActiveFacilityInterior &entry)
{
    if (!state.surfaceExploration || !entry.Active() || state.Mounted() || IsSurfaceMine() ||
        state.isInsideHome || state.isInObservatory || HospitalRecovery::Active())
    {
        return false;
    }
    state.facilityInterior = entry;
    state.isInsideHome = false;
    state.isInObservatory = false;
    state.StopMotion();
    state.playerPosition = {2.05f, 1.55f};
    state.cameraPosition = {0, 0};
    state.surfaceCameraHeight = 0;
    state.facingYaw = -kPi * .75f;
    ShowNotice(entry.regionName + " · " + entry.label + " 입장 / E 카운터 이용 · 출입문으로 퇴장");
    return true;
}
bool Game::ExitFacilityInterior()
{
    if (!state.facilityInterior.Active())
    {
        return false;
    }
    const auto exitPoint = state.facilityInterior.returnPoint;
    const float exitYaw = state.facilityInterior.returnYaw;
    const std::string label = state.facilityInterior.label;
    state.facilityInterior.Clear();
    state.StopMotion();
    if (surface && surface->Ready())
    {
        surface->Relocate(exitPoint, .32, 0);
        const auto local = surface->Local(surface->Pose().origin);
        state.playerPosition = {float(local.x / 2), float(local.y / 2)};
        state.cameraPosition = state.playerPosition;
    }
    state.facingYaw = exitYaw;
    ShowNotice(label + "에서 거리로 나왔습니다.");
    return true;
}
bool Game::InteractFacilityStation(InteractionTarget target)
{
    if (!state.facilityInterior.Active() || HospitalRecovery::Active())
    {
        return false;
    }
    constexpr int kMaxRecoveryKits = 999;
    auto &player = Player();
    const auto kind = state.facilityInterior.kind;
    if (kind == UrbanFacilityKind::Market)
    {
        if (target == InteractionTarget::FacilityServicePrimary)
        {
            // Buy recovery kit (40 Cr) or refill ammo (25 Cr).
            if (player.recoveryKits < kMaxRecoveryKits)
            {
                if (player.credits < 40)
                {
                    ShowNotice("상점 · 보급소 / 회복 키트 구매에는 40 Cr가 필요합니다.");
                    return false;
                }
                player.credits -= 40;
                ++player.recoveryKits;
                ShowNotice("상점 · 보급소 / 회복 키트 1개 구매 (-40 Cr · 보유 " +
                           std::to_string(player.recoveryKits) + "개)");
                return true;
            }
            if (Loadout().IsFirearmEquipped())
            {
                auto &gun = ActiveFirearm();
                if (gun.GetRounds() < gun.GetMagazineSize())
                {
                    if (player.credits < 25)
                    {
                        ShowNotice("상점 · 보급소 / 탄창 보충에는 25 Cr가 필요합니다.");
                        return false;
                    }
                    player.credits -= 25;
                    gun.TopUpRounds(gun.GetMagazineSize());
                    ShowNotice("상점 · 보급소 / 탄창 완충 (-25 Cr)");
                    return true;
                }
            }
            ShowNotice("상점 · 보급소 / 회복 키트와 탄약이 모두 충분합니다.");
            return true;
        }
        // Secondary: sell salvage (+35 Cr per salvage)
        if (player.salvage <= 0)
        {
            ShowNotice("고철 매입 단말 / 매각할 고철이 없습니다.");
            return false;
        }
        const int room = game::PlayerStats::kMaximumCredits - player.credits;
        if (room < 35)
        {
            ShowNotice("고철 매입 단말 / 소지 크레딧이 한도에 도달해 매각할 수 없습니다.");
            return false;
        }
        --player.salvage;
        player.credits += 35;
        ShowNotice("고철 매입 단말 / 고철 1개 매각 (+35 Cr · 잔여 고철 " +
                   std::to_string(player.salvage) + ")");
        return true;
    }
    if (kind == UrbanFacilityKind::Clinic)
    {
        if (target == InteractionTarget::FacilityServicePrimary)
        {
            if (player.health >= player.maxHealth)
            {
                ShowNotice("의료소 / 체력이 이미 최대 상태입니다.");
                return true;
            }
            const int cost = player.credits >= 30 ? 30 : 0;
            player.credits -= cost;
            player.health = player.maxHealth;
            state.playerMotion.mobility.RestoreStamina(PlayerMobility::MaxStamina, 0.f);
            ShowNotice(cost > 0 ? "의료소 / 응급 치료 완료 (-30 Cr · 체력 완전 회복)"
                                : "의료소 / 긴급 무상 처치 완료 (체력 완전 회복)");
            return true;
        }
        if (player.recoveryKits >= kMaxRecoveryKits)
        {
            ShowNotice("구급 보급함 / 회복 키트가 이미 최대 보유량입니다.");
            return true;
        }
        if (player.credits < 35)
        {
            ShowNotice("구급 보급함 / 회복 키트 구매에는 35 Cr가 필요합니다.");
            return false;
        }
        player.credits -= 35;
        ++player.recoveryKits;
        ShowNotice("구급 보급함 / 회복 키트 1개 보급 (-35 Cr)");
        return true;
    }
    if (kind == UrbanFacilityKind::Workshop)
    {
        if (target == InteractionTarget::FacilityServicePrimary)
        {
            bool repaired = false;
            if (state.car.position && state.car.health < GroundVehicle::MaxHealth)
            {
                repaired = true;
            }
            if (state.aircraft.position && state.aircraft.health < PlayerAircraft::MaxHealth)
            {
                repaired = true;
            }
            if (!repaired)
            {
                ShowNotice("정비소 · 공방 / 현재 파손된 차량·비행체가 없습니다.");
                return true;
            }
            if (player.salvage > 0)
            {
                --player.salvage;
            }
            else if (player.credits >= 35)
            {
                player.credits -= 35;
            }
            else
            {
                ShowNotice("정비소 · 공방 / 수리에는 고철 1개 또는 35 Cr가 필요합니다.");
                return false;
            }
            if (state.car.position)
            {
                state.car.health = GroundVehicle::MaxHealth;
                state.car.wreckSeconds = 0;
            }
            if (state.aircraft.position)
            {
                state.aircraft.health = PlayerAircraft::MaxHealth;
                state.aircraft.wreckSeconds = 0;
            }
            ShowNotice("정비소 · 공방 / 차량 및 비행체 정비를 완료했습니다.");
            return true;
        }
        // Secondary: craft recovery kit from 1 salvage
        if (player.recoveryKits >= kMaxRecoveryKits)
        {
            ShowNotice("개조 작업대 / 회복 키트가 최대 보유량입니다.");
            return false;
        }
        if (player.salvage < 1)
        {
            ShowNotice("개조 작업대 / 회복 키트 제작에는 고철 1개가 필요합니다.");
            return false;
        }
        --player.salvage;
        ++player.recoveryKits;
        ShowNotice("개조 작업대 / 고철 1개로 회복 키트 제작 완료.");
        return true;
    }
    if (kind == UrbanFacilityKind::CivicHall)
    {
        if (target == InteractionTarget::FacilityServicePrimary)
        {
            auto &law = CivicJustice::State();
            if (law.wanted > 0 && law.wanted <= 2)
            {
                const int fine = law.wanted * 60;
                if (player.credits < fine)
                {
                    ShowNotice("행정 · 치안 창구 / 수배 해제 벌금 " + std::to_string(fine) +
                               " Cr가 부족합니다.");
                    return false;
                }
                player.credits -= fine;
                law.wanted = 0;
                law.heat = 0;
                ShowNotice("행정 · 치안 창구 / 벌금 " + std::to_string(fine) +
                           " Cr 납부 · 수배가 해제되었습니다.");
                return true;
            }
            if (law.wanted >= 3)
            {
                ShowNotice("행정 · 치안 창구 / 중범죄(3성 이상)는 자진 납부로 해제할 수 없습니다.");
                return false;
            }
            ShowNotice(state.facilityInterior.regionName +
                       " 행정 · 의뢰소 / 현재 수배 없음 · 주변 지역 의뢰는 옆 게시판에서 확인하세요.");
            return true;
        }
        // Secondary: open Journal on Local Activities tab or highlight nearby active site.
        state.journalVisible = true;
        ShowNotice("지역 의뢰 게시판 / J 기록에서 수락된 의뢰와 주변 현장 활동을 확인할 수 있습니다.");
        return true;
    }
    return false;
}
std::vector<FacilityServiceItem> Game::FacilityServices() const
{
    std::vector<FacilityServiceItem> items;
    if (!state.facilityInterior.Active())
    {
        return items;
    }
    const auto &player = game::GameLevels::Mine().GetState().player;
    const auto kind = state.facilityInterior.kind;
    if (kind == UrbanFacilityKind::Market)
    {
        items.push_back({"응급 회복 키트",
                         "즉시 체력 +45 회복 (G 사용) · 보유 " + std::to_string(player.recoveryKits) + "개",
                         "40 Cr",
                         player.credits >= 40 && player.recoveryKits < 999,
                         "heal",
                         "의료 보급",
                         1});
        const bool firearmReady = Game::Get().Loadout().IsFirearmEquipped();
        const int rounds = firearmReady ? Game::Get().ActiveFirearm().GetRounds() : 0;
        const int magSize = firearmReady ? Game::Get().ActiveFirearm().GetMagazineSize() : 0;
        const bool needsAmmo = firearmReady && rounds < magSize;
        items.push_back({"전술 탄약 팩",
                         firearmReady ? ("장착 총기 탄창 완충 (현재 " + std::to_string(rounds) + "/" +
                                         std::to_string(magSize) + ")")
                                      : "총기 장착 시 탄약 보급 가능",
                         "25 Cr",
                         needsAmmo && player.credits >= 25,
                         "magazine",
                         "화기 탄약",
                         3});
        items.push_back({"고철 부품 매각",
                         "정비 고철 1개를 현금으로 정산 · 보유 " + std::to_string(player.salvage) + "개",
                         "+35 Cr",
                         player.salvage >= 1,
                         "parts",
                         "자원 매각",
                         2});
    }
    else if (kind == UrbanFacilityKind::Clinic)
    {
        const bool needsHeal = player.health < player.maxHealth;
        items.push_back({"집중 치료 · 체력 완복",
                         "체력 " + std::to_string(player.health) + "/" + std::to_string(player.maxHealth) +
                             " → 최대 회복 (크레딧 부족 시 응급 지원)",
                         player.credits >= 30 ? "30 Cr" : "무료 지원",
                         needsHeal,
                         "medical",
                         "클리닉 시술",
                         0});
        items.push_back({"구급 회복 키트",
                         "의료소 전용 회복 키트 +1 (G 사용) · 보유 " + std::to_string(player.recoveryKits) + "개",
                         "35 Cr",
                         player.credits >= 35 && player.recoveryKits < 999,
                         "heal",
                         "의료 보급",
                         1});
    }
    else if (kind == UrbanFacilityKind::Workshop)
    {
        const bool carDamaged = state.car.position && state.car.health < GroundVehicle::MaxHealth;
        const bool airDamaged = state.aircraft.position && state.aircraft.health < PlayerAircraft::MaxHealth;
        items.push_back({"차량 · 비행체 종합 정비",
                         (carDamaged || airDamaged) ? "손상된 탑승 장비의 장갑·엔진을 최대 내구도로 복구"
                                                    : "현재 모든 차량과 비행체가 정상 상태입니다",
                         player.salvage > 0 ? "고철 1" : "35 Cr",
                         (carDamaged || airDamaged) && (player.salvage > 0 || player.credits >= 35),
                         "ground_car",
                         "장비 정비",
                         4});
        items.push_back({"고철 회복 키트 제작",
                         "고철 1개 → 응급 회복 키트 +1 제작 · 보유 고철 " + std::to_string(player.salvage) + "개",
                         "고철 1",
                         player.salvage >= 1 && player.recoveryKits < 999,
                         "parts",
                         "현장 제작",
                         1});
    }
    else if (kind == UrbanFacilityKind::CivicHall)
    {
        const auto &law = CivicJustice::State();
        const int fine = law.wanted * 60;
        items.push_back({"수배 벌금 자진 정산",
                         law.wanted == 0   ? "현재 적용된 수배가 없습니다"
                         : law.wanted <= 2 ? ("수배 " + std::to_string(law.wanted) + "단계 해제 및 치안 기록 정리")
                                           : "3성 이상 중범죄는 자진 정산 불가",
                         (law.wanted > 0 && law.wanted <= 2) ? (std::to_string(fine) + " Cr") : "—",
                         law.wanted > 0 && law.wanted <= 2 && player.credits >= fine,
                         "wanted",
                         "치안 행정",
                         0});
        items.push_back({"지역 의뢰 · 조사 기록",
                         state.facilityInterior.regionName + " 주변 지역 의뢰 및 현장 활동 열기",
                         "무료",
                         true,
                         "record",
                         "행정 게시판",
                         0});
    }
    return items;
}
bool Game::ExecuteFacilityService(int serviceIndex)
{
    if (!state.facilityInterior.Active() || HospitalRecovery::Active())
    {
        return false;
    }
    auto finish = [&](bool ok) {
        if (ok)
        {
            state.facilityReactionSeconds = 1.4f;
        }
        return ok;
    };
    auto &player = Player();
    const auto kind = state.facilityInterior.kind;
    if (kind == UrbanFacilityKind::Market)
    {
        if (serviceIndex == 0)
        {
            if (player.recoveryKits >= 999)
            {
                ShowNotice("보급 상인 / 회복 키트 보유 상한입니다.");
                return false;
            }
            if (player.credits < 40)
            {
                ShowNotice("보급 상인 / 크레딧이 부족합니다 (회복 키트 40 Cr).");
                return false;
            }
            player.credits -= 40;
            ++player.recoveryKits;
            ShowNotice("보급 상인 / 회복 키트 1개를 구매했습니다 (-40 Cr).");
            return finish(true);
        }
        if (serviceIndex == 1)
        {
            if (!Loadout().IsFirearmEquipped())
            {
                ShowNotice("보급 상인 / 총기를 장착한 상태에서만 탄약을 보급할 수 있습니다.");
                return false;
            }
            auto &gun = ActiveFirearm();
            if (gun.GetRounds() >= gun.GetMagazineSize())
            {
                ShowNotice("보급 상인 / 이미 탄창이 가득 차 있습니다.");
                return false;
            }
            if (player.credits < 25)
            {
                ShowNotice("보급 상인 / 탄약 보충에는 25 Cr가 필요합니다.");
                return false;
            }
            player.credits -= 25;
            gun.TopUpRounds(gun.GetMagazineSize());
            ShowNotice("보급 상인 / 탄창을 완충했습니다 (-25 Cr).");
            return finish(true);
        }
        if (serviceIndex == 2)
        {
            return finish(InteractFacilityStation(InteractionTarget::FacilityServiceSecondary));
        }
        return false;
    }
    if (kind == UrbanFacilityKind::Clinic || kind == UrbanFacilityKind::Workshop)
    {
        if (serviceIndex == 0)
        {
            return finish(InteractFacilityStation(InteractionTarget::FacilityServicePrimary));
        }
        if (serviceIndex == 1)
        {
            return finish(InteractFacilityStation(InteractionTarget::FacilityServiceSecondary));
        }
        return false;
    }
    if (kind == UrbanFacilityKind::CivicHall)
    {
        if (serviceIndex == 0)
        {
            return finish(InteractFacilityStation(InteractionTarget::FacilityServicePrimary));
        }
        if (serviceIndex == 1)
        {
            Hud::CloseMenu();
            return finish(InteractFacilityStation(InteractionTarget::FacilityServiceSecondary));
        }
        return false;
    }
    return false;
}
void Game::ApplyDisplayPreferences(bool persist)
{
    const int count = int(platform::kResolutionOptions.size());
    state.displayPreferences.resolutionIndex = std::clamp(state.displayPreferences.resolutionIndex, 0, count - 1);
    const auto &opt = platform::kResolutionOptions[size_t(state.displayPreferences.resolutionIndex)];
    platform::ApplyDisplayMode(opt.width, opt.height, state.displayPreferences.fullscreen);
    if (persist)
    {
        std::ofstream out("display_settings.json", std::ios::binary | std::ios::trunc);
        if (out)
        {
            out << "{\"resolutionIndex\":" << state.displayPreferences.resolutionIndex
                << ",\"width\":" << opt.width << ",\"height\":" << opt.height
                << ",\"fullscreen\":" << (state.displayPreferences.fullscreen ? "true" : "false") << "}\n";
        }
    }
}
void Game::LoadDisplayPreferences()
{
    std::ifstream in("display_settings.json", std::ios::binary);
    if (!in)
    {
        return;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto findInt = [&](const char *key, int fallback) {
        const auto pos = content.find(key);
        if (pos == std::string::npos)
        {
            return fallback;
        }
        const auto colon = content.find(':', pos);
        if (colon == std::string::npos)
        {
            return fallback;
        }
        try
        {
            return std::stoi(content.substr(colon + 1));
        }
        catch (...)
        {
            return fallback;
        }
    };
    const int count = int(platform::kResolutionOptions.size());
    state.displayPreferences.resolutionIndex = std::clamp(findInt("\"resolutionIndex\"", 0), 0, count - 1);
    const auto fsPos = content.find("\"fullscreen\"");
    if (fsPos != std::string::npos)
    {
        const auto truePos = content.find("true", fsPos);
        const auto falsePos = content.find("false", fsPos);
        state.displayPreferences.fullscreen = truePos != std::string::npos &&
                                              (falsePos == std::string::npos || truePos < falsePos);
    }
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
