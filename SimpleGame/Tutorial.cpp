#include "stdafx.h"

#include "Tutorial.h"
#include "TutorialState.h"
#include "Dependencies/freeglut.h"
#include <algorithm>
#include <cmath>

namespace tutorial
{
TutorialState g_state;

namespace
{
constexpr float kMovementSpeed = 3.2f;
constexpr float kMaximumDeltaSeconds = 0.05f;
constexpr float kCameraFollowRate = 5.0f;
constexpr int kUpdateIntervalMilliseconds = 16;

unsigned char NormalizeKey(unsigned char key)
{
    if (key >= 'A' && key <= 'Z')
    {
        return key + ('a' - 'A');
    }
    return key;
}

void UpdatePlayerMovement(float deltaSeconds)
{
    float screenX = (g_state.pressedKeys['d'] ? 1.f : 0) - (g_state.pressedKeys['a'] ? 1.f : 0);
    float screenY = (g_state.pressedKeys['s'] ? 1.f : 0) - (g_state.pressedKeys['w'] ? 1.f : 0);
    float inputLength = std::sqrt(screenX * screenX + screenY * screenY);
    g_state.movementAmount = inputLength;
    if (inputLength <= 0)
    {
        return;
    }

    screenX /= inputLength;
    screenY /= inputLength;
    if (std::fabs(screenX) > std::fabs(screenY))
    {
        g_state.facingDirection = screenX < 0 ? 1 : 2;
    }
    else
    {
        g_state.facingDirection = screenY < 0 ? 3 : 0;
    }
    // Invert the 2:1 isometric projection so WASD follows screen directions.
    float worldDeltaX = (screenX * .5f + screenY) * kMovementSpeed * deltaSeconds;
    float worldDeltaY = (screenY - screenX * .5f) * kMovementSpeed * deltaSeconds;

    // Resolve axes separately to allow sliding along a building edge.
    Vector2 candidate = {g_state.playerPosition.x + worldDeltaX, g_state.playerPosition.y};
    if (!IsPositionBlocked(candidate))
    {
        g_state.playerPosition = candidate;
    }
    candidate = {g_state.playerPosition.x, g_state.playerPosition.y + worldDeltaY};
    if (!IsPositionBlocked(candidate))
    {
        g_state.playerPosition = candidate;
    }
}

void UpdateCamera(float deltaSeconds)
{
    Vector2 target = g_state.isInsideHome ? Vector2{0, 0} : g_state.playerPosition;
    float followAmount = 1 - std::exp(-kCameraFollowRate * deltaSeconds);
    g_state.cameraPosition.x += (target.x - g_state.cameraPosition.x) * followAmount;
    g_state.cameraPosition.y += (target.y - g_state.cameraPosition.y) * followAmount;
}
} // namespace

void Reset()
{
    // Keep the current window size when restarting the play session.
    const int viewportWidth = g_state.viewportWidth;
    const int viewportHeight = g_state.viewportHeight;
    g_state = TutorialState{};
    g_state.viewportWidth = viewportWidth;
    g_state.viewportHeight = viewportHeight;
    g_state.lastUpdateMilliseconds = glutGet(GLUT_ELAPSED_TIME);
    g_state.buildings = {{-8, -5, 2, 3, 130}, {-5, -5, 2, 3, 180}, {-1, -5, 2, 2, 120}, {3, -6, 2, 2, 160},
                         {-8, 4, 2, 2, 85},   {-4, 5, 2, 2, 110},  {0, 5, 2, 2, 135},   {4, 4, 2, 2, 95},
                         {7, 1, 2, 2, 150},   {-9, -1, 2, 3, 105}};
    for (int x = -16; x <= 16; x += 4)
    {
        g_state.buildings.push_back({float(x), -11, 2.4f, 2, float(100 + (x + 16) * 3)});
        g_state.buildings.push_back({float(x), 10, 2.4f, 2, float(95 + (x + 16) * 2)});
    }
    for (int y = -6; y <= 6; y += 4)
    {
        g_state.buildings.push_back({-16, float(y), 2.5f, 2, 140});
        g_state.buildings.push_back({13, float(y), 2.5f, 2, 120});
    }
    g_state.residents = {
        {{-5, 0}, "배송 기사", "배송 기사 / 오늘은 관문 쪽 배송이 전부 취소됐어요. 이유도 없이요."},
        {{-1, 3}, "야간 근무자", "야간 근무자 / 밤새 불빛이 깜빡였어. 어제도 같은 아침을 본 것 같은데."},
        {{5, 2}, "정비공", "정비공 / 저건 바퀴가 아니라 자기부상 장치예요. 엔진 소리가 달라졌죠?"},
        {{8, -2}, "관문 경비", "관문 경비 / 전망대까지는 개방되어 있습니다. 울타리 너머로는 가지 마세요."},
        {{-11, 2}, "학생", "학생 / 별들이 어제와 달라요. 선생님은 구름 때문이라고 하지만요."},
        {{10, 6}, "상인", "상인 / 북쪽 광장에서 따뜻한 차를 팔아요. 리아에게도 전해주세요."},
        {{-8, 8}, "주민", "주민 / 이곳에서 삼십 년을 살았지만 관문이 저런 색인 건 처음이야."},
        {{2, 8}, "아이", "아이 / 엄마는 저 빛이 우리를 지켜 준대요. 정말 그럴까요?"},
        {{-10, -7}, "통신 기술자", "통신 기술자 / 발신 기록이 없는 응답이 잡혀요. 수신기가 고장 난 걸까요?"},
        {{6, -8}, "탐사원", "탐사원 / 낯선 곳에 갈 때는 꼭 누군가와 함께 가요."},
        {{11, -5}, "순찰대원", "순찰대원 / 이상한 방송을 들으면 혼자 따라가지 마세요."},
        {{-12, 7}, "퇴역 항해사", "퇴역 항해사 / 저 문이 열리기 전부터 그 너머에는 무언가 있었지."}};
    ShowNotice("리아 / 좋은 아침. 집 단말이 또 깜빡이네. 확인하고 밖에서 만나자.");
}

void Resize(int width, int height)
{
    g_state.viewportWidth = (std::max)(width, 1);
    g_state.viewportHeight = (std::max)(height, 1);
}

void KeyDown(unsigned char key, int, int)
{
    key = NormalizeKey(key);
    if (key == 27) // Escape
    {
        g_state.isPaused = !g_state.isPaused;
        std::fill(g_state.pressedKeys, g_state.pressedKeys + 256, false);
        return;
    }
    if (g_state.isPaused)
    {
        if (key == 'r')
        {
            Reset();
        }
        if (key == 'q')
        {
            glutLeaveMainLoop();
        }
        return;
    }

    g_state.pressedKeys[key] = true;
    if (key == 'e')
    {
        InteractWithNearbyTarget();
    }
}

void KeyUp(unsigned char key, int, int)
{
    g_state.pressedKeys[NormalizeKey(key)] = false;
}

void Tick(int)
{
    const int nowMilliseconds = glutGet(GLUT_ELAPSED_TIME);
    float deltaSeconds =
        Clamp((nowMilliseconds - g_state.lastUpdateMilliseconds) / 1000.f, 0, kMaximumDeltaSeconds);
    g_state.lastUpdateMilliseconds = nowMilliseconds;

    if (!g_state.isPaused)
    {
        g_state.animationTimeSeconds += deltaSeconds;
        if (!g_state.isComplete)
        {
            g_state.playTimeSeconds += deltaSeconds;
        }
        g_state.noticeSecondsRemaining -= deltaSeconds;
        UpdatePlayerMovement(deltaSeconds);
        UpdateCamera(deltaSeconds);
    }

    glutPostRedisplay();
    glutTimerFunc(kUpdateIntervalMilliseconds, Tick, 0);
}
} // namespace tutorial
