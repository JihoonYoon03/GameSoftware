#include "stdafx.h"
#include "Game.h"
#include "Civic.h"
#include "Hud.h"
#include "Overworld.h"
#include "ResidentAI.h"
#include "TransitLayout.h"
#include "Level.h"
#include "Scene.h"
#include "Renderer.h"
#include "Profiler.h"
#include "Monitoring/Monitor.h"
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include <algorithm>
#include <cmath>

namespace tutorial
{
namespace
{
constexpr float kMovementSpeed = 3.2f;
constexpr float kMaximumDeltaSeconds = 0.05f;
constexpr float kCameraFollowRate = 5.0f;
constexpr int kUpdateIntervalMilliseconds = 16;
} // namespace
Game &Game::Get()
{
    static Game game;
    return game;
}
GameState &Game::GetState()
{
    return state;
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
    if (CivicJustice::IsExterior() && CivicJustice::State().playerImpact.seconds > 0 &&
        CivicJustice::State().playerImpact.response == BodyResponse::Knockdown)
    {
        state.movementAmount = state.movementSpeed = 0;
        return;
    }
    state.movementSpeed = 0;
    if (state.isBikeMounted && !game::GameLevels::Active() && !state.isInsideHome && !state.isInObservatory)
    {
        Overworld::UpdateBike(state, deltaSeconds);
        return;
    }
    float screenX = (state.pressedKeys['d'] ? 1.f : 0) - (state.pressedKeys['a'] ? 1.f : 0);
    float screenY = (state.pressedKeys['s'] ? 1.f : 0) - (state.pressedKeys['w'] ? 1.f : 0);
    float inputLength = std::sqrt(screenX * screenX + screenY * screenY);
    state.movementAmount = inputLength;
    if (inputLength <= 0)
    {
        return;
    }

    screenX /= inputLength;
    screenY /= inputLength;
    if (std::fabs(screenX) > std::fabs(screenY))
    {
        state.facingDirection = screenX < 0 ? 1 : 2;
    }
    else
    {
        state.facingDirection = screenY < 0 ? 3 : 0;
    }
    // Invert the 2:1 isometric projection so WASD follows screen directions.
    float worldDeltaX = (screenX * .5f + screenY) * kMovementSpeed * deltaSeconds;
    float worldDeltaY = (screenY - screenX * .5f) * kMovementSpeed * deltaSeconds;
    state.facingYaw = std::atan2(worldDeltaY, worldDeltaX);

    // Resolve axes separately to allow sliding along a building edge.
    Vector2 candidate = {state.playerPosition.x + worldDeltaX, state.playerPosition.y};
    if (!IsPositionBlocked(candidate))
    {
        state.playerPosition = candidate;
    }
    candidate = {state.playerPosition.x, state.playerPosition.y + worldDeltaY};
    if (!IsPositionBlocked(candidate))
    {
        state.playerPosition = candidate;
    }
    state.playerPosition = BodyCollision::MovePlayer(previous, state.playerPosition);
    state.movementSpeed = deltaSeconds > 0 ? Distance(previous, state.playerPosition) / deltaSeconds : 0;
    state.movementAmount = state.movementSpeed > .001f ? inputLength : 0;
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
    game::SceneManager::Reset();
    game::GameLevels::Reset();
    // Keep the current window size when restarting the play session.
    const int viewportWidth = state.viewportWidth;
    const int viewportHeight = state.viewportHeight;
    state = GameState{};
    state.viewportWidth = viewportWidth;
    state.viewportHeight = viewportHeight;
    state.lastUpdateMilliseconds = glutGet(GLUT_ELAPSED_TIME);
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
    ShowNotice("리아 / 좋은 아침. 집 단말이 또 깜빡이네. 확인하고 밖에서 만나자.");
}
void Game::Resize(int width, int height)
{
    state.viewportWidth = (std::max)(width, 1);
    state.viewportHeight = (std::max)(height, 1);
    MouseMotion(int(state.cursorWindowPosition.x), int(state.cursorWindowPosition.y));
}
void Game::KeyDown(unsigned char key, int, int)
{
    if (HospitalRecovery::Active())
    {
        return;
    }
    key = NormalizeKey(key);
    if (key == 27) // Escape
    {
        if (state.mapVisible)
        {
            state.mapVisible = false;
            return;
        }
        state.isPaused = !state.isPaused;
        state.mouseFiring = false;
        std::fill(state.pressedKeys, state.pressedKeys + 256, false);
        return;
    }
    if (state.isPaused)
    {
        if (key == 'r')
        {
            if (game::GameLevels::Chapter().IsActive())
            {
                game::GameLevels::Chapter().Leave();
                state.isPaused = false;
            }
            else if (game::GameLevels::Mine().IsActive())
            {
                game::GameLevels::Mine().Restart();
                state.isPaused = false;
            }
            else
            {
                Reset();
            }
        }
        if (key == 'q')
        {
            glutLeaveMainLoop();
        }
        return;
    }

    if (key == 'e')
    {
        if (state.interactionHeld)
        {
            return;
        }
        state.interactionHeld = true;
    }
    bool repeated = state.pressedKeys[key];
    state.pressedKeys[key] = true;
    if (repeated && (key == 'j' || key == 'm'))
    {
        return;
    }
    if (key == 'm')
    {
        state.mapVisible = !state.mapVisible;
        state.mouseFiring = false;
        std::fill(state.pressedKeys, state.pressedKeys + 256, false);
        state.pressedKeys['m'] = true;
        game::GameLevels::Chapter().GetState().journalVisible = false;
        return;
    }
    if (state.mapVisible)
    {
        return;
    }
    if (key == 'v' && !repeated)
    {
        CivicCombat::Melee();
        return;
    }
    if (key == 'r' && CivicCombat::Available())
    {
        game::GameLevels::Mine().Combat().Reload();
        return;
    }
    auto &chapter = game::GameLevels::Chapter();
    if (key == 'j' && chapter.IsAvailable())
    {
        chapter.GetState().journalVisible = !chapter.GetState().journalVisible;
        state.mouseFiring = false;
        return;
    }
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
        if (key == 'q')
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
        if (key == 'q')
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
        chapter.Enter(true);
        return;
    }
    // Direct access for first-level iteration without replaying the tutorial.
    if (key == '1')
    {
        state.isInObservatory = false;
        game::GameLevels::Mine().Enter(true);
        return;
    }
    if (key == 'e')
    {
        InteractWithNearbyTarget();
    }
}
void Game::SpecialKeyDown(int key, int, int)
{
    if (key != GLUT_KEY_F7 && key != GLUT_KEY_F8)
    {
        return;
    }
    auto &renderer = GameRenderer::Get().Backend();
    auto style = renderer.GetStyle();
    if (key == GLUT_KEY_F7)
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
        state.mouseFiring = false;
    }
}
void Game::MouseButton(int button, int buttonState, int x, int y)
{
    MouseMotion(x, y);
    if (HospitalRecovery::Active())
    {
        state.mouseFiring = false;
        return;
    }
    if (state.mapVisible)
    {
        state.mouseFiring = false;
        if (buttonState == GLUT_DOWN && state.cursorInsideViewport && !state.isPaused)
        {
            if (button == GLUT_LEFT_BUTTON || button == GLUT_RIGHT_BUTTON)
            {
                MapHud::HandleClick(state.cursorPosition, button == GLUT_RIGHT_BUTTON);
            }
        }
        return;
    }
    if (button == GLUT_LEFT_BUTTON)
    {
        state.mouseFiring = buttonState == GLUT_DOWN &&
                            (game::GameLevels::IsCombatActive() || CivicCombat::Available()) &&
                            !state.isPaused && state.cursorInsideViewport &&
                            !game::GameLevels::Chapter().GetState().journalVisible;
    }
}
void Game::MouseEntry(int pointerState)
{
    if (pointerState == GLUT_LEFT)
    {
        state.mouseFiring = false;
    }
}
void Game::Tick(int)
{
    profiling::Scope profileTimer(profiling::Timer::Update);
    const int nowMilliseconds = glutGet(GLUT_ELAPSED_TIME);
    float deltaSeconds =
        Clamp((nowMilliseconds - state.lastUpdateMilliseconds) / 1000.f, 0, kMaximumDeltaSeconds);
    state.lastUpdateMilliseconds = nowMilliseconds;

    HospitalRecovery::Update(deltaSeconds);
    if (!state.isPaused && !state.mapVisible && !HospitalRecovery::Active())
    {
        state.animationTimeSeconds += deltaSeconds;
        if (!state.isComplete)
        {
            state.playTimeSeconds += deltaSeconds;
        }
        state.noticeSecondsRemaining -= deltaSeconds;
        game::Level *activeLevel = game::GameLevels::Active();
        if (!activeLevel || activeLevel->UsesWalkingControls())
        {
            UpdatePlayerMovement(deltaSeconds);
        }
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
        UpdateCamera(deltaSeconds);
        game::SceneManager::Update(deltaSeconds);
    }

    monitoring::GameMonitor::Get().Update();
    glutSetCursor((game::GameLevels::IsCombatActive() || CivicCombat::Available()) && !state.mapVisible &&
                          !state.isPaused
                      ? GLUT_CURSOR_NONE
                      : GLUT_CURSOR_INHERIT);
    glutPostRedisplay();
    glutTimerFunc(kUpdateIntervalMilliseconds, GameCallbacks::Tick, 0);
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
void Game::ShowNotice(const std::string &message)
{
    state.noticeText = message;
    state.noticeSecondsRemaining = 12;
}
bool Game::IsPositionBlocked(Vector2 position)
{
    profiling::Count(profiling::Counter::CollisionQueries);
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
    if (Overworld::Scenery().IsBlocked(position))
    {
        return true;
    }
    for (const auto &building : state.buildings)
    {
        if (position.x > building.x - .2f && position.x < building.x + building.width + .2f &&
            position.y > building.y - .2f && position.y < building.y + building.depth + .2f)
        {
            return true;
        }
    }
    return false;
}
bool Game::IsQuestInteraction(InteractionTarget target) const
{
    auto stage = game::GameLevels::Mine().GetState().quest.GetStage();
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
        if (Distance(state.playerPosition, kObservatoryStairs) < 1.4f)
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
        if (Distance(state.playerPosition, {2.4f, 2.4f}) < 1.5f)
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
    const InteractionPoint points[] = {{kHomeDoor, InteractionTarget::HomeEntrance},
                                       {kLiaPosition, InteractionTarget::Lia},
                                       {kMaraPosition, InteractionTarget::Mara},
                                       {kRelayPosition, InteractionTarget::PowerRelay},
                                       {kBeaconPosition, InteractionTarget::GateBeacon},
                                       {kCityReceiverPosition, InteractionTarget::CityReceiver}};
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
    if (HospitalRecovery::Active())
    {
        return;
    }
    const InteractionTarget target = FindNearbyInteraction();
    if (target == InteractionTarget::MineEntrance)
    {
        if (game::GameLevels::Mine().GetState().quest.GetStage() < game::MineQuestStage::TravelToMine)
        {
            ShowNotice("광산 / 리아에게 기록을 보여 주고 헤일로 수신망에서 접근 경로를 확인하세요.");
            return;
        }
        state.mineReturnPosition = state.playerPosition;
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
            float candidate = Distance(state.playerPosition, resident.position);
            if (candidate < distance)
            {
                distance = candidate;
                nearest = &resident;
            }
        }
        if (nearest)
        {
            nearest->conversationSeconds = 3;
            ShowNotice(nearest->dialogue + (nearest->profession == ResidentProfession::Merchant
                                                ? " / 보안망 신용에 따른 거래가 배율 " +
                                                      std::to_string(CivicJustice::PriceMultiplier())
                                                : ""));
        }
        return;
    }
    if (target == InteractionTarget::HomeTerminal)
    {
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::MeetLia);
        ShowNotice("단말 / 관문 N-0 정기 보정 중. 리아가 전망대에서 관문 신호를 측정해 달라고 요청했습니다.");
    }
    if (target == InteractionTarget::ApartmentExit)
    {
        if (state.mainQuestStage == MainQuestStage::ReadHomeTerminal)
        {
            ShowNotice("외출하기 전에 집 단말을 확인하세요. 청록색 불빛을 찾으세요.");
            return;
        }
        state.isInsideHome = false;
        state.playerPosition = kHomeDoor;
        state.cameraPosition = state.playerPosition;
        ShowNotice(
            "헤일로 / 주거 구역 07. 머리 위로 자기부상 차량이 지나갑니다. 새벽부터 관문이 낮게 울립니다.");
    }
    if (target == InteractionTarget::HomeEntrance)
    {
        state.isInsideHome = true;
        state.playerPosition = {2, 2};
        state.cameraPosition = {0, 0};
        ShowNotice("집 / 문이 닫히자 도시의 웅성거림이 멀어집니다.");
    }
    if (target == InteractionTarget::CityReceiver)
    {
        if (game::GameLevels::Mine().GetState().quest.Advance(game::MineQuestStage::ReturnToHalo))
        {
            state.isComplete = true;
            state.mainQuestStage = MainQuestStage::Complete;
            game::GameLevels::Chapter().Unlock();
            ShowNotice("낙하한 신호 완료 / 헤일로에 광산 좌표를 보고했습니다. 수신망에서 1장 현장 조사를 "
                       "시작하세요.");
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
            ShowNotice("도시 수신망 / 관문 신호에 광산 통신선 주소가 겹칩니다. 실종 탐사선의 구조 요청도 "
                       "같은 시각입니다. 리아와 바이크로 광산에 가세요.");
        }
        else
        {
            ShowNotice(game::GameLevels::Mine().GetState().quest.GetStage() <
                               game::MineQuestStage::CheckCityRelay
                           ? "수신망 / 전망대 신호 기록을 먼저 리아에게 보여 주세요."
                           : "수신망 / 광산 통신선의 중계 흔적을 확인했습니다. 신호들의 동일 발신 여부는 "
                             "미확인입니다.");
        }
        return;
    }
    if (target == InteractionTarget::Kain)
    {
        ShowNotice(game::GameLevels::Chapter().GetState().recordsCompared
                       ? "카인 / 옛 지도에 있는 다리는 사라졌지만, 아래로 지나던 수로는 남아 있을 겁니다. "
                         "돌아올 표식을 남기며 갑시다."
                       : "카인 / N-0 외곽에서 근무했습니다. 지금은 기록부터 모읍시다. 길보다 먼저 사람 수를 "
                         "확인해야 합니다.");
        return;
    }
    if (target == InteractionTarget::Lia)
    {
        if (game::GameLevels::Chapter().IsAvailable())
        {
            ShowNotice(game::GameLevels::Chapter().GetState().generatorRestored
                           ? "리아 / 도시 전력이 안정됐어. 아직 N-0 외곽으로 흐르던 주기는 기록에 남아 있어. "
                             "나머지 현장도 확인하자."
                           : "리아 / 광산 좌표가 폐쇄된 N-0 외곽 설비 목록에도 있었어. 그래도 주민들은 지금 "
                             "정전과 운송 지연이 더 급해. 수신망 단말에서 출발하자.");
            return;
        }
        if (state.gateSignalHeard)
        {
            if (game::GameLevels::Mine().GetState().quest.Advance(game::MineQuestStage::ShowRecord))
            {
                ShowNotice(
                    "리아 / 장비가 거짓말할 수는 있어. 그래도 어디서부터 틀렸는지는 찾아봐야지. 광장의 "
                    "도시 수신망에서 중계 흔적을 확인하자.");
            }
            else
            {
                ShowNotice(std::string("리아 / ") + game::GameLevels::Mine().GetState().quest.GetObjective());
            }
            return;
        }
        if (state.mainQuestStage < MainQuestStage::MeetLia)
        {
            ShowNotice("리아 / 집 단말에 접근 코드를 보냈어. 먼저 확인해 줘.");
            return;
        }
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::EnterObservatory);
        ShowNotice(
            "리아 / 광장 동쪽 전망대에서 신호를 측정하고 내려와. 기록을 확인한 뒤 어디로 갈지 정하자.");
    }
    if (target == InteractionTarget::Mara)
    {
        if (!state.isSideQuestAccepted)
        {
            state.isSideQuestAccepted = true;
            ShowNotice(
                "마라 / 딸아이 방에 불이 안 들어와요. 동쪽 전력 장치를 봐 주실래요? [서브퀘스트 수락]");
        }
        else if (state.isPowerRestored && !state.isSideQuestComplete)
        {
            state.isSideQuestComplete = true;
            ShowNotice("마라 / 이제 아이가 편히 잘 수 있겠어요. 고마워요. [남겨 둔 불빛: 완료]");
        }
        else
        {
            ShowNotice(state.isSideQuestComplete
                           ? "마라 / 언제든 들러요. 따뜻한 차를 준비해 둘게요."
                           : "마라 / 광장 동쪽의 주황색 전력 장치를 복구하고 돌아와 주세요.");
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
        if (state.mainQuestStage < MainQuestStage::EnterObservatory)
        {
            ShowNotice("전망대 / 리아가 접근 코드를 가지고 있습니다. 집 근처의 리아와 먼저 대화하세요.");
            return;
        }
        state.isInObservatory = true;
        state.isInsideHome = false;
        state.playerPosition = kObservatoryStairs;
        state.cameraPosition = {0, 0};
        std::fill(state.pressedKeys, state.pressedKeys + 256, false);
        if (!state.gateSignalHeard)
        {
            state.mainQuestStage = MainQuestStage::ReadGateSignal;
        }
        ShowNotice("전망대 / 승강기를 타고 관측 데크에 도착했습니다. 북동쪽의 신호 측정 콘솔로 이동하세요.");
    }
    if (target == InteractionTarget::ObservatorySignal)
    {
        state.gateSignalHeard = true;
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::DescendObservatory);
        ShowNotice("알 수 없음 / …수신 확인… 이 목소리가 들리면… 리아 / 방금 네 목소리였어. 넌 아무 말도 안 "
                   "했잖아.");
    }
    if (target == InteractionTarget::ObservatoryExit)
    {
        if (!state.gateSignalHeard)
        {
            ShowNotice("리아 / 먼저 관측 데크 북동쪽의 콘솔에서 신호를 확인해 줘.");
            return;
        }
        state.isInObservatory = false;
        state.hasDescendedObservatory = true;
        state.playerPosition = kBeaconPosition;
        state.cameraPosition = kBeaconPosition;
        std::fill(state.pressedKeys, state.pressedKeys + 256, false);
        state.mainQuestStage = (std::max)(state.mainQuestStage, MainQuestStage::MountJetBike);
        ShowNotice("리아 / 이상 신호 기록을 집 근처의 나에게 보여 줘. 도시 수신망에서 발신 경로를 대조하자.");
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
// GameCallbacks — GLUT entry points
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
    GameRenderer::Get().Shutdown();
}
} // namespace tutorial
