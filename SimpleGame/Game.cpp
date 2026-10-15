#include "stdafx.h"
#include "Game.h"
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
    return (y >= -1 && y <= 0) || (x >= 1 && x <= 2) || y == 7 || x == -10;
}
bool CityLayout::OverlapsRoad(const Building &b, float clearance)
{
    return (b.y < 1 + clearance && b.y + b.depth > -1 - clearance) ||
           (b.y < 8 + clearance && b.y + b.depth > 7 - clearance) ||
           (b.x < 3 + clearance && b.x + b.width > 1 - clearance) ||
           (b.x < -9 + clearance && b.x + b.width > -10 - clearance);
}
void CityLayout::ValidateBuildings(std::vector<Building> &buildings)
{
    std::vector<Building> valid;
    for (const auto &b : buildings)
    {
        if (OverlapsRoad(b))
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
Vector2 CityLayout::TrafficPosition(int index, float seconds)
{
    float direction = index % 2 == 0 ? 1.f : -1.f;
    const float length = kTrafficMaxX - kTrafficMinX;
    float phase = std::fmod(seconds * 2.4f + (index / 2) * 13.333333f + (-20 - kTrafficMinX), length);
    return {direction * (kTrafficMinX + phase), index % 2 == 0 ? -.5f : .5f};
}
float CityLayout::TrafficYaw(int index)
{
    return index % 2 == 0 ? 0.f : kPi;
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
}
void Game::UpdateCamera(float deltaSeconds)
{
    Vector2 target = (state.isInsideHome || state.isInObservatory || state.isJetTravel)
                         ? Vector2{0, 0}
                         : state.playerPosition;
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
    state.buildings = {{-8, -5, 2, 3, 130}, {-5, -5, 2, 3, 180}, {-1, -5, 2, 2, 120}, {3, -6, 2, 2, 160},
                       {-8, 4, 2, 2, 85},   {-4, 5, 2, 2, 110},  {0, 5, 2, 2, 135},   {4, 4, 2, 2, 95},
                       {7, 1, 2, 2, 150},   {-9, -1, 2, 3, 105}};
    for (int x = -16; x <= 16; x += 4)
    {
        state.buildings.push_back({float(x), -11, 2.4f, 2, float(100 + (x + 16) * 3)});
        state.buildings.push_back({float(x), 10, 2.4f, 2, float(95 + (x + 16) * 2)});
    }
    for (int y = -6; y <= 6; y += 4)
    {
        state.buildings.push_back({-16, float(y), 2.5f, 2, 140});
        state.buildings.push_back({13, float(y), 2.5f, 2, 120});
    }
    // Surrounding city blocks are scenery; player collision limits stay unchanged.
    for (int x = CityLayout::kTerrainMinX + 3; x < CityLayout::kTerrainMaxX - 3; x += 6)
    {
        for (int y = CityLayout::kTerrainMinY + 3; y < CityLayout::kTerrainMaxY - 3; y += 6)
        {
            if (x < kWorldMaxX + 3 && x + 3 > kWorldMinX - 3 && y < kWorldMaxY + 3 && y + 3 > kWorldMinY - 3)
            {
                continue;
            }
            unsigned int seed = unsigned((x + 64) * 73 + (y + 64) * 151);
            state.buildings.push_back({float(x), float(y), 2.5f + float(seed % 3) * .3f,
                                       2.3f + float(seed % 4) * .2f, 90.f + float(seed % 95)});
        }
    }
    constexpr models::BuildingType buildingTypes[] = {
        models::BuildingType::Habitat, models::BuildingType::Terraced, models::BuildingType::Workshop,
        models::BuildingType::Dome};
    const std::array<float, 4> buildingColors[] = {{.3f, .42f, .5f, 1},
                                                   {.52f, .37f, .31f, 1},
                                                   {.36f, .47f, .4f, 1},
                                                   {.42f, .35f, .52f, 1},
                                                   {.5f, .49f, .36f, 1}};
    for (size_t i = 0; i < state.buildings.size(); ++i)
    {
        auto &building = state.buildings[i];
        building.type = buildingTypes[i % 4];
        constexpr float heightsMeters[] = {7.2f, 10.8f, 5.4f, 6.5f};
        building.height = heightsMeters[i % 4] * models::WorldScale::pixelsPerMeter;
        building.palette.body = buildingColors[(i / 4 + i) % 5];
        building.palette.accent =
            i % 2 == 0 ? std::array<float, 4>{.32f, .7f, .78f, 1} : std::array<float, 4>{.85f, .62f, .35f, 1};
        building.palette.emission = {.25f, .75f, .85f, 1};
    }
    Building observatory{5, -8, 5, 4, 11.5f * models::WorldScale::pixelsPerMeter};
    observatory.type = models::BuildingType::Observatory;
    observatory.palette.body = {.46f, .55f, .6f, 1};
    observatory.palette.accent = {.3f, .76f, .86f, 1};
    observatory.palette.emission = {.42f, .9f, 1, 1};
    state.buildings.push_back(observatory);
    CityLayout::ValidateBuildings(state.buildings);
    state.residents = {
        {{-5, 0}, "배송 기사", "배송 기사 / 오늘은 관문 쪽 배송이 전부 취소됐어요. 이유도 없이요."},
        {{-1, 3}, "야간 근무자", "야간 근무자 / 밤새 불빛이 깜빡였어. 어제도 같은 아침을 본 것 같은데."},
        {{5, 2}, "정비공", "정비공 / 저건 바퀴가 아니라 자기부상 장치예요. 엔진 소리가 달라졌죠?"},
        {{8, -2}, "관문 경비", "관문 경비 / 전망대까지는 개방되어 있습니다. 울타리 너머로는 가지 마세요."},
        {{-11, 2}, "학생", "학생 / 별들이 어제와 달라요. 선생님은 구름 때문이라고 하지만요."},
        {{10, 6}, "상인", "상인 / 북쪽 광장에서 따뜻한 차를 팔아요. 리아에게도 전해주세요."},
        {{-8, 8}, "주민", "주민 / 이곳에서 삼십 년을 살았지만 관문이 저런 색인 건 처음이야."},
        {{2, 8}, "아이", "아이 / 엄마는 저 빛이 우리를 지켜 준대요. 정말 그럴까요?"},
        {{-10, -7}, "통신 기술자", "통신 기술자 / 발신 기록이 없는 응답이 잡혀요. 수신기가 고장 난 걸까요?"},
        {{11, -3}, "탐사원", "탐사원 / 낯선 곳에 갈 때는 꼭 누군가와 함께 가요."},
        {{11, -5}, "순찰대원", "순찰대원 / 이상한 방송을 들으면 혼자 따라가지 마세요."},
        {{-12, 7}, "퇴역 항해사", "퇴역 항해사 / 저 문이 열리기 전부터 그 너머에는 무언가 있었지."}};
    for (size_t i = 0; i < state.residents.size(); ++i)
    {
        auto &resident = state.residents[i];
        resident.type =
            static_cast<models::CharacterType>(i % static_cast<size_t>(models::CharacterType::Count));
        resident.coat = buildingColors[(i / 5 + i) % 5];
        resident.gender = i % 2 ? models::Gender::Female : models::Gender::Male;
        resident.hair = models::HairStyle(i % size_t(models::HairStyle::Count));
    }
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
    key = NormalizeKey(key);
    if (key == 27) // Escape
    {
        state.isPaused = !state.isPaused;
        state.mouseFiring = false;
        std::fill(state.pressedKeys, state.pressedKeys + 256, false);
        return;
    }
    if (state.isPaused)
    {
        if (key == 'r')
        {
            if (game::GameLevels::Mine().IsActive())
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

    state.pressedKeys[key] = true;
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
    // Direct access for first-level iteration without replaying the tutorial.
    if (key == '1')
    {
        game::GameLevels::Travel().Reset();
        state.isJetTravel = false;
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
    if (button == GLUT_LEFT_BUTTON)
    {
        state.mouseFiring = buttonState == GLUT_DOWN && game::GameLevels::Mine().IsActive() &&
                            !state.isPaused && state.cursorInsideViewport;
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

    if (!state.isPaused)
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
        UpdateCamera(deltaSeconds);
        game::SceneManager::Update(deltaSeconds);
    }

    monitoring::GameMonitor::Get().Update();
    glutSetCursor(game::GameLevels::Mine().IsActive() ? GLUT_CURSOR_NONE : GLUT_CURSOR_INHERIT);
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
        const bool bed = position.x < -1.35f && position.y < -1.6f;
        const bool console = position.x > 1.15f && position.x < 2.06f && position.y < -1.98f;
        return bed || console;
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
    for (auto building : state.buildings)
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
        return state.hasDescendedObservatory && stage == game::MineQuestStage::CheckCityRelay;
    case InteractionTarget::JetBike:
        return state.hasDescendedObservatory && state.gateSignalHeard &&
               stage == game::MineQuestStage::TravelToMine;
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
    if (state.isJetTravel)
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
                                       {kJetBikePosition, InteractionTarget::JetBike},
                                       {kCityReceiverPosition, InteractionTarget::CityReceiver}};
    InteractionTarget nearestTarget = InteractionTarget::None;
    float nearestDistance = 1.35f;
    for (const auto &point : points)
    {
        float distance = Distance(state.playerPosition, point.position);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestTarget = point.target;
        }
    }
    for (const auto &resident : state.residents)
    {
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
    const InteractionTarget target = FindNearbyInteraction();
    if (target == InteractionTarget::Resident)
    {
        const Resident *nearest = nullptr;
        float distance = 1.35f;
        for (const auto &resident : state.residents)
        {
            float candidate = Distance(state.playerPosition, resident.position);
            if (candidate < distance)
            {
                distance = candidate;
                nearest = &resident;
            }
        }
        if (nearest)
        {
            ShowNotice(nearest->dialogue);
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
    if (target == InteractionTarget::Lia)
    {
        if (state.gateSignalHeard)
        {
            if (game::GameLevels::Mine().GetState().quest.Advance(game::MineQuestStage::ShowRecord))
            {
                ShowNotice("리아 / 장비가 거짓말할 수는 있어. 그래도 어디서부터 틀렸는지는 찾아봐야지. 광장 "
                           "남쪽 도시 수신망에서 중계 흔적을 확인하자.");
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
        ShowNotice("리아 / 북동쪽 전망대에서 신호를 측정하고 내려와. 기록을 확인한 뒤 어디로 갈지 정하자.");
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
        if (!state.gateSignalHeard || !state.hasDescendedObservatory)
        {
            ShowNotice("제트 바이크 / 먼저 리아와 대화하고 전망대에서 신호를 측정한 뒤 내려오세요.");
            return;
        }
        if (!game::GameLevels::Mine().GetState().quest.BeginJourney())
        {
            ShowNotice("리아 / 먼저 나에게 신호 기록을 보여 주고 도시 수신망을 확인하자.");
            return;
        }
        game::GameLevels::Travel().Enter();
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
