#include "stdafx.h"
#include "FieldMinigame.h"
#include "Drawing.h"
#include "Game.h"
#include "RenderAssets.h"
#include "RenderQueue.h"
#include "Renderer.h"
#include "Window.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace tutorial
{
namespace
{
FieldMinigameState gState;
std::function<void(const FieldMinigameOutcome &)> gOnComplete;
std::function<void()> gOnMiss;

unsigned NextHash(unsigned &seed)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

float NextUnit(unsigned &seed)
{
    return float(NextHash(seed) & 0xFFFFu) / 65535.f;
}

float WrapPhase(float phase)
{
    const float tau = kPi * 2.f;
    float p = std::fmod(phase, tau);
    if (p < 0.f)
    {
        p += tau;
    }
    return p;
}

float PhaseDiff(float a, float b)
{
    const float tau = kPi * 2.f;
    float d = std::fabs(WrapPhase(a) - WrapPhase(b));
    return (std::min)(d, tau - d);
}

void Finish(bool succeeded, bool perfect, bool bypassed)
{
    auto cb = std::move(gOnComplete);
    const FieldMinigameOutcome outcome{succeeded, perfect, bypassed, gState.misses};
    gState.active = false;
    gOnComplete = {};
    gOnMiss = {};
    if (cb && succeeded)
    {
        cb(outcome);
    }
}

void DrawTextUtf8(float x, float y, const std::string &text, int size, const ColorRGBA &color,
                  bool outlined = true)
{
    Drawing::SetColor(color);
    GameRenderer::Get().Backend().DrawUiText(x, y, text, size, outlined);
}

void DrawBox(float x, float y, float w, float h, const ColorRGBA &fill, const ColorRGBA &border)
{
    Drawing::DrawRectangle(x, y, w, h, fill);
    Drawing::DrawRectangle(x, y, w, 2.f, border);
    Drawing::DrawRectangle(x, y + h - 2.f, w, 2.f, border);
    Drawing::DrawRectangle(x, y, 2.f, h, border);
    Drawing::DrawRectangle(x + w - 2.f, y, 2.f, h, border);
}

void DrawRingPolygon(float cx, float cy, float radius, int segments, float thickness, const ColorRGBA &color)
{
    const float tau = kPi * 2.f;
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = tau * float(i) / float(segments);
        const float a1 = tau * float(i + 1) / float(segments);
        Drawing::DrawLine({cx + std::cos(a0) * radius, cy + std::sin(a0) * radius},
                          {cx + std::cos(a1) * radius, cy + std::sin(a1) * radius}, color, thickness);
    }
}

void TriggerExtractionStrike()
{
    const float dist = std::fabs(gState.needlePos - gState.zoneCenter);
    if (dist <= gState.zoneHalfWidth)
    {
        const bool crit = dist <= gState.perfectHalfWidth;
        if (crit)
        {
            ++gState.perfectHits;
        }
        ++gState.strikesDone;
        gState.flashSeconds = 0.28f;
        gState.lastFlashGood = true;
        if (gState.strikesDone >= gState.strikesNeeded)
        {
            Finish(true, gState.perfectHits > 0 && gState.misses == 0, false);
            return;
        }
        unsigned s = unsigned(gState.strikesDone * 7919 + int(gState.elapsedSeconds * 1000.f) + 37);
        gState.zoneCenter = 0.24f + NextUnit(s) * 0.52f;
        gState.needleVelocity = (gState.needleVelocity > 0.f ? -1.f : 1.f) *
                                (1.12f + 0.18f * float(gState.strikesDone));
    }
    else
    {
        ++gState.misses;
        gState.flashSeconds = 0.34f;
        gState.lastFlashGood = false;
        if (gOnMiss)
        {
            gOnMiss();
        }
    }
}
} // namespace

void FieldMinigame::Begin(FieldMinigameKind kind, const std::string &title, const std::string &subtitle,
                          unsigned seed, std::function<void(const FieldMinigameOutcome &)> onComplete,
                          std::function<void()> onMiss, int strikesNeeded)
{
    gState = {};
    gState.active = true;
    gState.kind = kind;
    gState.title = title;
    gState.subtitle = subtitle;
    gState.strikesNeeded = (std::max)(1, (std::min)(4, strikesNeeded));
    gOnComplete = std::move(onComplete);
    gOnMiss = std::move(onMiss);

    unsigned s = seed == 0 ? 0x9E3779B9u : seed;
    if (kind == FieldMinigameKind::CircuitBalance)
    {
        for (int i = 0; i < 3; ++i)
        {
            gState.railTarget[size_t(i)] = 28.f + std::floor(NextUnit(s) * 9.f) * 5.f;
            float start = gState.railTarget[size_t(i)] + (NextUnit(s) > 0.5f ? 26.f : -26.f);
            gState.railValue[size_t(i)] = Game::Clamp(start, 10.f, 90.f);
        }
        gState.railTolerance = 8.5f;
        gState.selectedRail = 0;
    }
    else if (kind == FieldMinigameKind::SignalTuner)
    {
        gState.targetFreq = 1.6f + std::floor(NextUnit(s) * 10.f) * 0.22f;
        gState.targetPhase = WrapPhase(0.5f + NextUnit(s) * 5.2f);
        gState.playerFreq = Game::Clamp(gState.targetFreq + (NextUnit(s) > 0.5f ? 0.9f : -0.8f), 1.2f, 4.2f);
        gState.playerPhase = WrapPhase(gState.targetPhase + 2.1f);
        gState.syncHoldSeconds = 0.f;
    }
    else if (kind == FieldMinigameKind::GlyphCipher)
    {
        for (int i = 0; i < 3; ++i)
        {
            gState.ringAngle[size_t(i)] = 1 + int(NextHash(s) % 7u);
        }
        gState.selectedRing = 0;
    }
    else
    {
        gState.needlePos = 0.08f;
        gState.needleVelocity = 1.06f;
        gState.zoneCenter = 0.34f + NextUnit(s) * 0.36f;
        gState.zoneHalfWidth = 0.135f;
        gState.perfectHalfWidth = 0.048f;
        gState.strikesDone = 0;
    }

    auto &w = Game::Get().GetState();
    w.StopMotion();
    w.mouseFiring = false;
    w.mouseAiming = false;
    std::fill(std::begin(w.pressedKeys), std::end(w.pressedKeys), false);
}

bool FieldMinigame::Active()
{
    return gState.active;
}

void FieldMinigame::Cancel()
{
    if (!gState.active)
    {
        return;
    }
    gState.active = false;
    gOnComplete = {};
    gOnMiss = {};
}

void FieldMinigame::Reset()
{
    gState = {};
    gOnComplete = {};
    gOnMiss = {};
}

float FieldMinigame::SignalSyncRatio()
{
    const float freqErr = std::fabs(gState.playerFreq - gState.targetFreq) / 1.5f;
    const float phaseErr = PhaseDiff(gState.playerPhase, gState.targetPhase) / kPi;
    return Game::Clamp(1.f - (freqErr * 0.58f + phaseErr * 0.42f), 0.f, 1.f);
}

bool FieldMinigame::IsCircuitBalanced()
{
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(gState.railValue[size_t(i)] - gState.railTarget[size_t(i)]) > gState.railTolerance)
        {
            return false;
        }
    }
    return true;
}

bool FieldMinigame::IsCipherAligned()
{
    return gState.ringAngle[0] == 0 && gState.ringAngle[1] == 0 && gState.ringAngle[2] == 0;
}

const FieldMinigameState &FieldMinigame::State()
{
    return gState;
}

void FieldMinigame::CompleteForTest(bool perfect)
{
    if (!gState.active)
    {
        return;
    }
    Finish(true, perfect, false);
}

void FieldMinigame::Update(float deltaSeconds)
{
    if (!gState.active)
    {
        return;
    }
    gState.elapsedSeconds += deltaSeconds;
    if (gState.flashSeconds > 0.f)
    {
        gState.flashSeconds = (std::max)(0.f, gState.flashSeconds - deltaSeconds);
    }

    if (gState.kind == FieldMinigameKind::ExtractionTiming)
    {
        gState.needlePos += gState.needleVelocity * deltaSeconds;
        if (gState.needlePos >= 0.98f)
        {
            gState.needlePos = 0.98f;
            gState.needleVelocity = -std::fabs(gState.needleVelocity);
        }
        else if (gState.needlePos <= 0.02f)
        {
            gState.needlePos = 0.02f;
            gState.needleVelocity = std::fabs(gState.needleVelocity);
        }
    }
    else if (gState.kind == FieldMinigameKind::SignalTuner)
    {
        const float sync = SignalSyncRatio();
        if (sync >= 0.90f)
        {
            gState.syncHoldSeconds += deltaSeconds;
            if (gState.syncHoldSeconds >= 0.55f)
            {
                Finish(true, gState.misses == 0, false);
            }
        }
        else
        {
            gState.syncHoldSeconds = (std::max)(0.f, gState.syncHoldSeconds - deltaSeconds * 1.4f);
        }
    }
}

bool FieldMinigame::HandleKey(unsigned char key)
{
    if (!gState.active)
    {
        return false;
    }
    if (key == 27)
    {
        Cancel();
        return true;
    }
    if (key == '\t')
    {
        Finish(true, false, true);
        return true;
    }

    if (gState.kind == FieldMinigameKind::CircuitBalance)
    {
        if (key == 'w')
        {
            gState.selectedRail = (gState.selectedRail + 2) % 3;
        }
        else if (key == 's')
        {
            gState.selectedRail = (gState.selectedRail + 1) % 3;
        }
        else if (key == 'a' || key == 'd')
        {
            const float delta = (key == 'd' ? 6.f : -6.f);
            const int sel = gState.selectedRail;
            gState.railValue[size_t(sel)] = Game::Clamp(gState.railValue[size_t(sel)] + delta, 5.f, 95.f);
            const int next = (sel + 1) % 3;
            gState.railValue[size_t(next)] = Game::Clamp(gState.railValue[size_t(next)] - delta * 0.25f, 5.f, 95.f);
        }
        else if (key == ' ' || key == 13 || key == 'e')
        {
            if (IsCircuitBalanced())
            {
                Finish(true, gState.misses == 0, false);
            }
            else
            {
                ++gState.misses;
                gState.flashSeconds = 0.28f;
                gState.lastFlashGood = false;
                if (gOnMiss)
                {
                    gOnMiss();
                }
            }
        }
        return true;
    }

    if (gState.kind == FieldMinigameKind::SignalTuner)
    {
        if (key == 'a')
        {
            gState.playerFreq = Game::Clamp(gState.playerFreq - 0.14f, 1.2f, 4.2f);
        }
        else if (key == 'd')
        {
            gState.playerFreq = Game::Clamp(gState.playerFreq + 0.14f, 1.2f, 4.2f);
        }
        else if (key == 'w')
        {
            gState.playerPhase = WrapPhase(gState.playerPhase + 0.32f);
        }
        else if (key == 's')
        {
            gState.playerPhase = WrapPhase(gState.playerPhase - 0.32f);
        }
        else if (key == ' ' || key == 13 || key == 'e')
        {
            if (SignalSyncRatio() >= 0.87f)
            {
                Finish(true, SignalSyncRatio() >= 0.94f && gState.misses == 0, false);
            }
            else
            {
                ++gState.misses;
                gState.flashSeconds = 0.26f;
                gState.lastFlashGood = false;
                if (gOnMiss)
                {
                    gOnMiss();
                }
            }
        }
        return true;
    }

    if (gState.kind == FieldMinigameKind::GlyphCipher)
    {
        if (key == 'w')
        {
            gState.selectedRing = (gState.selectedRing + 2) % 3;
        }
        else if (key == 's')
        {
            gState.selectedRing = (gState.selectedRing + 1) % 3;
        }
        else if (key == 'a' || key == 'd')
        {
            const int step = (key == 'd' ? 1 : 7);
            gState.ringAngle[size_t(gState.selectedRing)] =
                (gState.ringAngle[size_t(gState.selectedRing)] + step) % 8;
            if (IsCipherAligned())
            {
                gState.flashSeconds = 0.25f;
                gState.lastFlashGood = true;
            }
        }
        else if (key == ' ' || key == 13 || key == 'e')
        {
            if (IsCipherAligned())
            {
                Finish(true, gState.misses == 0, false);
            }
            else
            {
                ++gState.misses;
                gState.flashSeconds = 0.26f;
                gState.lastFlashGood = false;
                if (gOnMiss)
                {
                    gOnMiss();
                }
            }
        }
        return true;
    }

    if (gState.kind == FieldMinigameKind::ExtractionTiming)
    {
        if (key == ' ' || key == 13 || key == 'e')
        {
            TriggerExtractionStrike();
        }
        return true;
    }
    return true;
}

bool FieldMinigame::HandleSpecialKey(int key)
{
    if (!gState.active)
    {
        return false;
    }
    if (key == GLFW_KEY_UP)
    {
        return HandleKey('w');
    }
    if (key == GLFW_KEY_DOWN)
    {
        return HandleKey('s');
    }
    if (key == GLFW_KEY_LEFT)
    {
        return HandleKey('a');
    }
    if (key == GLFW_KEY_RIGHT)
    {
        return HandleKey('d');
    }
    if (key == GLFW_KEY_ENTER)
    {
        return HandleKey(13);
    }
    return true;
}

bool FieldMinigame::HandlePointer(int button, bool down)
{
    if (!gState.active)
    {
        return false;
    }
    if (down && button == GLFW_MOUSE_BUTTON_LEFT && gState.kind == FieldMinigameKind::ExtractionTiming)
    {
        TriggerExtractionStrike();
    }
    return true;
}

void FieldMinigame::Draw()
{
    if (!gState.active)
    {
        return;
    }
    const float logicalW = WorldCamera::LogicalWidth();
    const float cx = logicalW * 0.5f;
    const float panelW = 580.f;
    const float panelH = 406.f;
    const float px = cx - panelW * 0.5f;
    const float py = 186.f;

    Drawing::DrawRectangle(0.f, 0.f, logicalW, float(kCanvasHeight), {0.02f, 0.04f, 0.06f, 0.62f});

    ColorRGBA accent = {.40f, .90f, .82f, .95f};
    if (gState.flashSeconds > 0.f)
    {
        accent = gState.lastFlashGood ? ColorRGBA{.45f, 1.f, .68f, 1.f} : ColorRGBA{1.f, .38f, .32f, 1.f};
    }
    DrawBox(px, py, panelW, panelH, {.05f, .09f, .13f, .95f}, accent);
    Drawing::DrawRectangle(px + 2.f, py + 2.f, panelW - 4.f, 54.f, {.09f, .16f, .21f, .92f});
    Drawing::DrawRectangle(px + 2.f, py + 54.f, panelW - 4.f, 2.f, {.36f, .82f, .80f, .55f});

    const char *badge = gState.kind == FieldMinigameKind::CircuitBalance   ? "CIRCUIT · 전력 회로"
                        : gState.kind == FieldMinigameKind::SignalTuner    ? "SIGNAL · 파형 동조"
                        : gState.kind == FieldMinigameKind::GlyphCipher    ? "CIPHER · 문양 해독"
                                                                           : "EXTRACT · 정밀 채굴";
    DrawTextUtf8(px + 20.f, py + 24.f, badge, 13, {.48f, .92f, .86f, 1.f}, true);
    DrawTextUtf8(px + 20.f, py + 45.f, gState.title, 20, {.94f, .98f, .99f, 1.f}, true);
    DrawTextUtf8(px + 20.f, py + 80.f, gState.subtitle, 14, {.76f, .86f, .90f, 1.f}, true);

    const float bodyX = px + 26.f;
    const float bodyY = py + 102.f;
    const float bodyW = panelW - 52.f;

    if (gState.kind == FieldMinigameKind::CircuitBalance)
    {
        const char *railNames[3] = {"배선 A (주전원)", "배선 B (안정기)", "배선 C (보조망)"};
        for (int i = 0; i < 3; ++i)
        {
            const float ry = bodyY + float(i) * 68.f;
            const bool sel = (i == gState.selectedRail);
            const float val = gState.railValue[size_t(i)];
            const float tgt = gState.railTarget[size_t(i)];
            const bool locked = std::fabs(val - tgt) <= gState.railTolerance;

            DrawBox(bodyX, ry, bodyW, 56.f,
                    sel ? ColorRGBA{.11f, .20f, .27f, .92f} : ColorRGBA{.07f, .12f, .16f, .85f},
                    locked ? ColorRGBA{.42f, .95f, .64f, .95f}
                    : sel  ? ColorRGBA{.45f, .90f, .96f, .95f}
                           : ColorRGBA{.26f, .38f, .46f, .70f});

            DrawTextUtf8(bodyX + 12.f, ry + 20.f, railNames[i], 14,
                                   sel ? ColorRGBA{.96f, .99f, 1.f, 1.f} : ColorRGBA{.78f, .86f, .90f, 1.f}, true);
            const std::string pct = std::to_string(int(std::round(val))) + "% / 목표 " +
                                    std::to_string(int(std::round(tgt))) + "%";
            DrawTextUtf8(bodyX + bodyW - 148.f, ry + 20.f, pct, 13,
                                   locked ? ColorRGBA{.48f, .98f, .68f, 1.f} : ColorRGBA{.96f, .82f, .44f, 1.f},
                                   true);

            const float trackX = bodyX + 14.f;
            const float trackY = ry + 32.f;
            const float trackW = bodyW - 28.f;
            const float trackH = 14.f;
            Drawing::DrawRectangle(trackX, trackY, trackW, trackH, {.03f, .06f, .09f, .95f});
            const float zoneL = Game::Clamp((tgt - gState.railTolerance) / 100.f, 0.f, 1.f);
            const float zoneR = Game::Clamp((tgt + gState.railTolerance) / 100.f, 0.f, 1.f);
            Drawing::DrawRectangle(trackX + zoneL * trackW, trackY - 2.f, (zoneR - zoneL) * trackW, trackH + 4.f,
                                   locked ? ColorRGBA{.28f, .86f, .54f, .52f} : ColorRGBA{.96f, .78f, .30f, .38f});
            const float fillW = Game::Clamp(val / 100.f, 0.f, 1.f) * trackW;
            Drawing::DrawRectangle(trackX, trackY + 2.f, fillW, trackH - 4.f,
                                   locked ? ColorRGBA{.44f, .98f, .66f, .95f} : ColorRGBA{.36f, .86f, .94f, .92f});
            Drawing::SetColor({1.f, 1.f, 1.f, 1.f});
            if (!GameRenderer::Get().Backend().DrawUiSprite("Components/minigame_circuit_node",
                                                            trackX + fillW - 11.f, trackY - 4.f, 22.f, 22.f, 1.f,
                                                            false))
            {
                Drawing::DrawRectangle(trackX + fillW - 2.f, trackY - 3.f, 4.f, trackH + 6.f,
                                       {.98f, .99f, 1.f, 1.f});
            }
        }
        const bool ready = IsCircuitBalanced();
        DrawTextUtf8(bodyX, bodyY + 218.f,
                               ready ? "모든 배선 균형 완료! [Space / Enter]로 회로 고정"
                                     : "W/S 배선 선택 · A/D 출력 조절 (인접 배선 미세 연동)",
                               15, ready ? ColorRGBA{.48f, 1.f, .68f, 1.f} : ColorRGBA{.86f, .92f, .94f, 1.f},
                               true);
    }
    else if (gState.kind == FieldMinigameKind::SignalTuner)
    {
        const float scopeH = 148.f;
        DrawBox(bodyX, bodyY, bodyW, scopeH, {.02f, .06f, .08f, .98f}, {.30f, .68f, .72f, .85f});
        Drawing::SetColor({1.f, 1.f, 1.f, .86f});
        if (!GameRenderer::Get().Backend().DrawUiSprite("Components/minigame_oscilloscope_grid", bodyX + 2.f,
                                                        bodyY + 2.f, bodyW - 4.f, scopeH - 4.f, 1.f, false))
        {
            for (int g = 1; g < 4; ++g)
            {
                const float gy = bodyY + scopeH * float(g) * 0.25f;
                Drawing::DrawLine({bodyX + 4.f, gy}, {bodyX + bodyW - 4.f, gy}, {.16f, .32f, .36f, .45f}, 1.f);
            }
        }
        const float midY = bodyY + scopeH * 0.5f;
        const float amp = scopeH * 0.34f;
        const int samples = 72;
        for (int i = 0; i < samples; ++i)
        {
            const float u0 = float(i) / float(samples);
            const float u1 = float(i + 1) / float(samples);
            const float x0 = bodyX + 8.f + u0 * (bodyW - 16.f);
            const float x1 = bodyX + 8.f + u1 * (bodyW - 16.f);
            const float ty0 = midY - std::sin(u0 * kPi * 2.f * gState.targetFreq + gState.targetPhase) * amp;
            const float ty1 = midY - std::sin(u1 * kPi * 2.f * gState.targetFreq + gState.targetPhase) * amp;
            if ((i % 2) == 0)
            {
                Drawing::DrawLine({x0, ty0}, {x1, ty1}, {.98f, .80f, .34f, .88f}, 2.2f);
            }
            const float py0 = midY - std::sin(u0 * kPi * 2.f * gState.playerFreq + gState.playerPhase) * amp;
            const float py1 = midY - std::sin(u1 * kPi * 2.f * gState.playerFreq + gState.playerPhase) * amp;
            Drawing::DrawLine({x0, py0}, {x1, py1}, {.38f, .96f, .92f, .98f}, 2.8f);
        }
        const float sync = SignalSyncRatio();
        const int syncPct = int(std::round(sync * 100.f));
        const float barY = bodyY + scopeH + 18.f;
        Drawing::DrawRectangle(bodyX, barY, bodyW, 16.f, {.03f, .06f, .09f, .95f});
        Drawing::DrawRectangle(bodyX + 2.f, barY + 2.f, (bodyW - 4.f) * sync, 12.f,
                               sync >= 0.88f ? ColorRGBA{.44f, .98f, .66f, .95f} : ColorRGBA{.36f, .84f, .94f, .92f});
        std::ostringstream oss;
        oss << "동조율 " << syncPct << "%  ·  주파수 " << std::fixed << std::setprecision(2) << gState.playerFreq
            << " Hz (A/D)  ·  위상 조절 (W/S)";
        DrawTextUtf8(bodyX, barY + 38.f, oss.str(), 14,
                               sync >= 0.88f ? ColorRGBA{.48f, 1.f, .68f, 1.f} : ColorRGBA{.86f, .92f, .95f, 1.f},
                               true);
        DrawTextUtf8(bodyX, barY + 60.f,
                               sync >= 0.88f ? "신호 포착! 잠시 유지하거나 [Space / Enter]로 잠금"
                                             : "금색 기준 파형에 시안 파형을 맞추세요",
                               14, {.74f, .86f, .90f, 1.f}, true);
    }
    else if (gState.kind == FieldMinigameKind::GlyphCipher)
    {
        const float gcx = cx;
        const float gcy = bodyY + 112.f;
        Drawing::SetColor({1.f, 1.f, 1.f, .72f});
        GameRenderer::Get().Backend().DrawUiSprite("Components/minigame_cipher_ring", gcx - 108.f, gcy - 108.f,
                                                   216.f, 216.f, 1.f, false);
        const float radii[3] = {96.f, 68.f, 40.f};
        Drawing::DrawRectangle(gcx - 5.f, gcy - 112.f, 10.f, 18.f, {.98f, .84f, .36f, 1.f});
        Drawing::DrawLine({gcx, gcy - 102.f}, {gcx, gcy - 26.f}, {.98f, .84f, .36f, .42f}, 1.5f);

        for (int r = 0; r < 3; ++r)
        {
            const bool sel = (r == gState.selectedRing);
            const bool aligned = (gState.ringAngle[size_t(r)] == 0);
            const ColorRGBA ringCol = aligned ? ColorRGBA{.46f, .98f, .66f, .95f}
                                      : sel   ? ColorRGBA{.44f, .92f, 1.f, 1.f}
                                              : ColorRGBA{.40f, .56f, .64f, .78f};
            DrawRingPolygon(gcx, gcy, radii[r], 32, sel ? 3.2f : 2.f, ringCol);
            for (int k = 0; k < 8; ++k)
            {
                const float ang = -kPi * 0.5f + float((k + gState.ringAngle[size_t(r)]) % 8) * (kPi * 0.25f);
                const float x0 = gcx + std::cos(ang) * (radii[r] - 6.f);
                const float y0 = gcy + std::sin(ang) * (radii[r] - 6.f);
                const float x1 = gcx + std::cos(ang) * (radii[r] + 6.f);
                const float y1 = gcy + std::sin(ang) * (radii[r] + 6.f);
                const bool isKey = (k == 0);
                Drawing::DrawLine({x0, y0}, {x1, y1},
                                  isKey ? (aligned ? ColorRGBA{.48f, 1.f, .68f, 1.f}
                                                   : ColorRGBA{.98f, .82f, .34f, 1.f})
                                        : ColorRGBA{.50f, .64f, .72f, .60f},
                                  isKey ? 4.5f : 1.6f);
            }
        }
        const bool alignedAll = IsCipherAligned();
        DrawTextUtf8(bodyX, bodyY + 232.f,
                               alignedAll ? "3중 관측 문양 정렬 완료! [Space / Enter]로 해독 확정"
                                          : "W/S 고리 선택 · A/D 회전 — 금색 표식을 12시 기준선에 정렬",
                               14,
                               alignedAll ? ColorRGBA{.48f, 1.f, .68f, 1.f} : ColorRGBA{.86f, .92f, .94f, 1.f},
                               true);
    }
    else
    {
        const float barY = bodyY + 72.f;
        const float barH = 38.f;
        Drawing::SetColor({1.f, 1.f, 1.f, 1.f});
        if (!GameRenderer::Get().Backend().DrawUiSprite("Components/minigame_timing_track", bodyX, barY, bodyW,
                                                        barH, 1.f, true))
        {
            DrawBox(bodyX, barY, bodyW, barH, {.03f, .06f, .09f, .96f}, {.34f, .66f, .72f, .88f});
        }
        const float zL = Game::Clamp(gState.zoneCenter - gState.zoneHalfWidth, 0.f, 1.f);
        const float zR = Game::Clamp(gState.zoneCenter + gState.zoneHalfWidth, 0.f, 1.f);
        Drawing::DrawRectangle(bodyX + 2.f + zL * (bodyW - 4.f), barY + 4.f, (zR - zL) * (bodyW - 4.f),
                               barH - 8.f, {.24f, .76f, .82f, .52f});
        const float pL = Game::Clamp(gState.zoneCenter - gState.perfectHalfWidth, 0.f, 1.f);
        const float pR = Game::Clamp(gState.zoneCenter + gState.perfectHalfWidth, 0.f, 1.f);
        Drawing::SetColor({1.f, 1.f, 1.f, .96f});
        if (!GameRenderer::Get().Backend().DrawUiSprite("Components/minigame_timing_target",
                                                        bodyX + 2.f + pL * (bodyW - 4.f), barY + 2.f,
                                                        (pR - pL) * (bodyW - 4.f), barH - 4.f, 1.f, false))
        {
            Drawing::DrawRectangle(bodyX + 2.f + pL * (bodyW - 4.f), barY + 2.f, (pR - pL) * (bodyW - 4.f),
                                   barH - 4.f, {.98f, .82f, .32f, .85f});
        }
        const float nx = bodyX + 2.f + gState.needlePos * (bodyW - 4.f);
        Drawing::DrawRectangle(nx - 3.f, barY - 7.f, 6.f, barH + 14.f, {.99f, .99f, 1.f, 1.f});

        for (int i = 0; i < gState.strikesNeeded; ++i)
        {
            const float pipX = cx - float(gState.strikesNeeded) * 20.f + float(i) * 40.f;
            const bool done = i < gState.strikesDone;
            DrawBox(pipX, barY + 64.f, 26.f, 14.f,
                    done ? ColorRGBA{.44f, .98f, .66f, .95f} : ColorRGBA{.10f, .16f, .22f, .90f},
                    done ? ColorRGBA{.62f, 1.f, .78f, 1.f} : ColorRGBA{.36f, .54f, .62f, .75f});
        }
        DrawTextUtf8(bodyX, bodyY + 184.f,
                               "[Space / 클릭 / E] 금색 핵심 구간에 맞춰 정밀 추출 (완벽 적중 시 보상 2배)",
                               14, {.92f, .96f, .98f, 1.f}, true);
        DrawTextUtf8(bodyX, bodyY + 208.f,
                               "진행 " + std::to_string(gState.strikesDone) + "/" +
                                   std::to_string(gState.strikesNeeded) + "  ·  정밀 적중 " +
                                   std::to_string(gState.perfectHits) + "  ·  오차 " +
                                   std::to_string(gState.misses),
                               14, {.70f, .86f, .90f, 1.f}, true);
    }

    Drawing::DrawRectangle(px + 2.f, py + panelH - 40.f, panelW - 4.f, 38.f, {.04f, .07f, .10f, .94f});
    DrawTextUtf8(px + 20.f, py + panelH - 16.f,
                           "[Space / Enter] 확정   ·   [Tab] 현장 우회(보너스 없음)   ·   [Esc] 취소", 13,
                           {.66f, .80f, .84f, 1.f}, true);
}
} // namespace tutorial
