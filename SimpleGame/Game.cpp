#include "stdafx.h"
#include "Game.h"
#include "Civic.h"
#include "Hud.h"
#include "RenderAssets.h"
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
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
    // A mounted rider's body reaction must not disable the vehicle's controls.
    if (!state.isBikeMounted && CivicJustice::IsExterior() &&
        CivicJustice::State().playerImpact.seconds > 0 &&
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
    if (!IsAiming())
    {
        state.facingYaw = std::atan2(worldDeltaY, worldDeltaX);
    }

    Vector2 candidate = {previous.x + worldDeltaX, previous.y + worldDeltaY};
    state.playerPosition =
        BodyCollision::Slide(previous, candidate, [this](Vector2 p) { return IsPositionBlocked(p); });
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
    WeaponWheel::Close();
    game::SceneManager::Reset();
    game::GameLevels::Reset();
    // Keep the current window size when restarting the play session.
    const int viewportWidth = state.viewportWidth;
    const int viewportHeight = state.viewportHeight;
    const UiPreferences preferences = state.uiPreferences;
    state = GameState{};
    state.uiPreferences = preferences;
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
    ShowNotice("리아 · 수신 메시지 / 좋은 아침. 전망대 측정을 좀 도와줄래? 집 단말에 내용을 보냈어. 밖에서 "
               "기다릴게.");
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
    if (stage == TestStage::Observatory)
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
    w.movementAmount = w.movementSpeed = 0;
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
    if (HospitalRecovery::Active())
    {
        return;
    }
    key = NormalizeKey(key);
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
    if (key == 'j' || key == 'h')
    {
        bool opening = key == 'j' ? !state.journalVisible : !state.dialogueHistoryVisible;
        state.journalVisible = key == 'j' && opening;
        state.dialogueHistoryVisible = key == 'h' && opening;
        state.dialogue.historyOffset = 0;
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
    if (HospitalRecovery::Active())
    {
        return;
    }
    if (Hud::HandleSpecialKey(key))
    {
        return;
    }
    if (state.journalVisible || state.dialogueHistoryVisible)
    {
        auto &offset = state.dialogue.historyOffset;
        if (key == GLUT_KEY_PAGE_DOWN)
        {
            ++offset;
        }
        if (key == GLUT_KEY_PAGE_UP && offset > 0)
        {
            --offset;
        }
        return;
    }
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
    if (button == GLUT_RIGHT_BUTTON && buttonState == GLUT_UP)
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
    if (Hud::HandlePointer(button, buttonState == GLUT_DOWN))
    {
        state.mouseFiring = state.mouseAiming = false;
        return;
    }
    if (state.mapVisible)
    {
        state.mouseFiring = state.mouseAiming = false;
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
                            !state.isPaused && state.cursorInsideViewport && !Dialogue::LocksWorld() &&
                            !state.journalVisible && !state.dialogueHistoryVisible;
    }
    if (button == GLUT_RIGHT_BUTTON)
    {
        state.mouseAiming = buttonState == GLUT_DOWN && state.cursorInsideViewport && !state.isPaused &&
                            !state.journalVisible && !state.dialogueHistoryVisible &&
                            !Dialogue::LocksWorld() && !state.isBikeMounted &&
                            game::GameLevels::Mine().GetState().loadout.IsPistolEquipped() &&
                            (game::GameLevels::IsCombatActive() || CivicCombat::Available());
    }
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
    const auto &gun = game::GameLevels::Mine().GetState().pistol;
    pose.reloadProgress = armed && gun.IsReloading() ? gun.GetReloadProgress() : -1;
    if (!armed)
    {
        pose.aimWeight = pose.fireSeconds = 0;
    }
    return pose;
}
void Game::MouseEntry(int pointerState)
{
    if (pointerState == GLUT_LEFT)
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
    deltaSeconds = Clamp(deltaSeconds, 0, kMaximumDeltaSeconds);
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
    const int nowMilliseconds = glutGet(GLUT_ELAPSED_TIME);
    float deltaSeconds =
        Clamp((nowMilliseconds - state.lastUpdateMilliseconds) / 1000.f, 0, kMaximumDeltaSeconds);
    state.lastUpdateMilliseconds = nowMilliseconds;
    Update(deltaSeconds);
    monitoring::GameMonitor::Get().Update();
    glutSetCursor(
        (state.weaponWheel.visible || game::GameLevels::IsCombatActive() || CivicCombat::Available()) &&
                !state.mapVisible && !state.isPaused && state.ui.screen == UiScreen::None &&
                !Dialogue::LocksWorld() && !state.journalVisible && !state.dialogueHistoryVisible
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
    std::vector<std::string> result;
    for (size_t start = 0; start < text.size();)
    {
        size_t end = start, lastSpace = start;
        while (end < text.size() && text[end] != '\n')
        {
            unsigned char lead = static_cast<unsigned char>(text[end]);
            size_t bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
            if (end + bytes > text.size())
            {
                break;
            }
            if (RenderAssets::MeasureUtf8(text.substr(start, end + bytes - start), fontSize) > width &&
                end > start)
            {
                break;
            }
            if (text[end] == ' ')
            {
                lastSpace = end;
            }
            end += bytes;
        }
        if (end == start && end < text.size() && text[end] != '\n')
        {
            ++end;
        }
        if (end < text.size() && text[end] != '\n' && lastSpace > start)
        {
            end = lastSpace;
        }
        result.push_back(text.substr(start, end - start));
        start = end;
        if (start < text.size() && text[start] == '\n')
        {
            ++start;
        }
        while (start < text.size() && text[start] == ' ')
        {
            ++start;
        }
    }
    if (result.empty())
    {
        result.push_back("");
    }
    return result;
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
        if (!chapter.GetState().bullets.empty())
        {
            return true;
        }
        for (const auto &person : chapter.GetState().scrappers)
        {
            if (person.combat.health > 0 && Game::Distance(person.position, world.playerPosition) < 14 &&
                chapter.HasLineOfSight(world.playerPosition, person.position))
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
        world.movementAmount = world.movementSpeed = 0;
        world.bikeSpeed = 0;
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
    const auto &w = Game::Get().GetState();
    const auto &mine = game::GameLevels::Mine().GetState();
    const auto &s = game::GameLevels::Chapter().GetState();
    std::vector<std::string> lines = {"인물: 리아는 헤일로의 정비 담당, 마라는 주거 구역 주민이다.",
                                      "N-0은 도시 가까이의 관문이다."};
    if (s.unlocked)
    {
        lines.push_back("N-0/17은 관문 자체가 아닌 외곽 설비 번호다.");
    }
    if (w.mainQuestStage >= MainQuestStage::MeetLia)
    {
        lines.push_back("집 단말: N-0 정기 측정 요청과 전망대 접근 코드를 받았다.");
    }
    if (w.gateSignalHeard)
    {
        lines.push_back(
            "전망대: 내 목소리와 같은 수신 음성을 저장했다. 녹음한 기억은 없고 발신자는 확인하지 못했다.");
    }
    if (mine.quest.GetStage() >= game::MineQuestStage::TravelToMine)
    {
        lines.push_back(
            "수신망: 전망대 기록과 광산 통신선의 중계 흔적이 겹친다. 같은 발신원인지는 아직 모른다.");
    }
    if (mine.quest.GetStage() >= game::MineQuestStage::InvestigateGlyph)
    {
        lines.push_back("광산 신호: 실종 탐사선 식별자가 남았다. 현재 생존자를 확인했다는 뜻은 아니다.");
    }
    if (mine.quest.GetStage() >= game::MineQuestStage::CollectCoordinates)
    {
        lines.push_back("문양: 나를 닮은 형상을 발견했다. 누가, 언제 새겼는지는 모른다.");
    }
    if (mine.quest.AreCoordinatesFound())
    {
        lines.push_back("광산 좌표: 중계 좌표를 확보했다. 입구로 나와 바이크로 헤일로 수신망에 보고한다.");
    }
    if (s.generatorInspected)
    {
        lines.push_back(
            s.generatorRestored
                ? "발전기: 외곽 선로를 분리했다. 끊어진 선로의 계기판은 여전히 같은 간격으로 움직인다."
                : "발전기: 폐쇄된 외곽 선로로 전력이 빠져나간다. 차단기를 분리해야 한다.");
    }
    if (s.receiverInspected)
    {
        lines.push_back(
            "운송대: 현장 인원은 다섯 명, 무전 응답은 여섯 명이다. 추가 생존자를 확인한 것은 아니다.");
    }
    if (s.convoyRescued)
    {
        lines.push_back(
            "구조: 운송대 다섯 명이 실제 안전 지점에 도착했다. 카인이 인원을 재확인하고 동행했다.");
    }
    if (s.tradeProofFound)
    {
        lines.push_back("거래 기록: 익명의 구매자가 N-0/17 부품을 시세보다 비싸게 샀다.");
    }
    if (s.ledgerFound)
    {
        lines.push_back("기본 장부: 발주처는 지워졌다. 좌표·전력 주기·교신 시각을 확보했다.");
    }
    if (s.detailLedgerFound)
    {
        lines.push_back("상세 장부: 구매자 추적 단서를 보관한다. 발주처의 정체는 미확인이다.");
    }
    if (s.recordsCompared)
    {
        lines.push_back("기록 대조 완료: N-0 외곽 설비의 움직임이 겹친다. 수로 진입로 기록을 얻었지만 2장 "
                        "지역은 아직 진입할 수 없다.");
    }
    lines.push_back(std::string("다음 행동: ") +
                    (s.unlocked ? game::GameLevels::Chapter().Objective()
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
    if (HospitalRecovery::Active() || Dialogue::Active() || state.journalVisible ||
        state.dialogueHistoryVisible)
    {
        return;
    }
    for (auto position : {kLiaPosition, kMaraPosition, kCityKainPosition})
    {
        if (Distance(state.playerPosition, position) < 1.35f && CivicCombat::LandmarkDown(position))
        {
            ShowNotice("의료 지원 / 중상자를 치료하고 있습니다. 회복 후 다시 대화할 수 있습니다.");
            return;
        }
    }
    const InteractionTarget target = FindNearbyInteraction();
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
            if (game::GameLevels::Chapter().GetState().recordsCompared)
            {
                tell("리아 / 세 현장 모두 N-0 외곽 설비와 이어져 있어. 다음에는 카인이 기록한 수로를 "
                     "확인해 보자.");
                return;
            }
            if (game::GameLevels::Chapter().CompletedSites() == 3)
            {
                tell("리아 / 현장 기록은 다 모였어. 광장 수신망에서 광산 좌표와 대조해 보자.");
                return;
            }
            tell(game::GameLevels::Chapter().GetState().generatorRestored
                     ? "리아 / 발전기는 안정됐어. N-0 외곽으로 전력이 빠져나간 기록도 챙겼고. "
                       "수신망에서 출발해서 남은 현장을 확인하자."
                     : "리아 / 광산 좌표가 폐쇄된 N-0 외곽 설비 목록에도 있었어. 그래도 주민들은 지금 "
                       "정전과 운송 지연이 더 급해. 수신망 단말에서 출발하자.");
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
        else
        {
            tell(state.isSideQuestComplete
                     ? "마라 / 언제든 들러요. 따뜻한 차를 준비해 둘게요."
                     : "마라 / 전력 장치는 광장 동쪽에 있어요. 주황색 표시가 있는 장치예요. 부탁드릴게요.");
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
        Dialogue::Start("MQ00/observatory",
                        {{"voice", "발신자 미상", "수신 기록", "…수신 확인. 이 목소리가 들리면…", {}},
                         {"observation", "나", "현장", "내 목소리다. 이런 말을 녹음한 기억은 없는데.", {}},
                         {"ria", "리아", "무전", "기록은 저장됐어. 내려오면 같이 확인해 보자.", {}}});
    }
    if (target == InteractionTarget::ObservatoryExit)
    {
        if (!state.gateSignalHeard)
        {
            ShowNotice("조사 안내 / 관측 데크 북동쪽 콘솔에서 신호 기록을 받은 뒤 내려가세요.");
            return;
        }
        state.isInObservatory = false;
        state.hasDescendedObservatory = true;
        state.playerPosition = kBeaconPosition;
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
    WeaponWheel::Close();
    GameRenderer::Get().Shutdown();
}
} // namespace tutorial
