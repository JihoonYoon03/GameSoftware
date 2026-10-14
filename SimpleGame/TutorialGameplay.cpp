#include "stdafx.h"
#include "Profiler.h"

#include "Dependencies/freeglut.h"
#include "Tutorial.h"
#include "TutorialState.h"
#include "LevelOne.h"
#include "JetTravel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tutorial
{
float Clamp(float value, float minimum, float maximum)
{
    return (std::max)(minimum, (std::min)(maximum, value));
}

float Distance(Vector2 first, Vector2 second)
{
    return std::sqrt((first.x - second.x) * (first.x - second.x) +
                     (first.y - second.y) * (first.y - second.y));
}

void ShowNotice(const std::string &message)
{
    g_state.noticeText = message;
    g_state.noticeSecondsRemaining = 12;
}

bool IsPositionBlocked(Vector2 position)
{
    profiling::Count(profiling::Counter::CollisionQueries);
    if (levelone::IsActive())
    {
        return levelone::IsBlocked(position);
    }
    if (g_state.isInsideHome)
    {
        if (position.x < -2.8f || position.x > 2.8f || position.y < -2.8f || position.y > 2.8f)
        {
            return true;
        }
        return (position.x < -.6f && position.y < -1.3f) || (position.x > 1.2f && position.y < -1.5f);
    }
    if (g_state.isInObservatory)
    {
        return position.x < -3.8f || position.x > 3.8f || position.y < -3.8f || position.y > 3.8f;
    }
    if (position.x < kWorldMinX || position.x > kWorldMaxX || position.y < kWorldMinY ||
        position.y > kWorldMaxY)
    {
        return true;
    }
    for (auto building : g_state.buildings)
    {
        if (position.x > building.x - .2f && position.x < building.x + building.width + .2f &&
            position.y > building.y - .2f && position.y < building.y + building.depth + .2f)
        {
            return true;
        }
    }
    return false;
}

InteractionTarget FindNearbyInteraction()
{
    if (g_state.isJetTravel)
    {
        return InteractionTarget::None;
    }
    if (g_state.isInObservatory)
    {
        if (Distance(g_state.playerPosition, kObservatoryConsole) < 1.4f)
        {
            return InteractionTarget::ObservatorySignal;
        }
        if (Distance(g_state.playerPosition, kObservatoryStairs) < 1.4f)
        {
            return InteractionTarget::ObservatoryExit;
        }
        return InteractionTarget::None;
    }
    if (g_state.isInsideHome)
    {
        if (Distance(g_state.playerPosition, {1, -1.2f}) < 1.5f)
        {
            return InteractionTarget::HomeTerminal;
        }
        if (Distance(g_state.playerPosition, {2.4f, 2.4f}) < 1.5f)
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
    const InteractionPoint points[] = {
        {kHomeDoor, InteractionTarget::HomeEntrance},     {kLiaPosition, InteractionTarget::Lia},
        {kMaraPosition, InteractionTarget::Mara},         {kRelayPosition, InteractionTarget::PowerRelay},
        {kBeaconPosition, InteractionTarget::GateBeacon}, {kJetBikePosition, InteractionTarget::JetBike}};
    InteractionTarget nearestTarget = InteractionTarget::None;
    float nearestDistance = 1.35f;
    for (const auto &point : points)
    {
        float distance = Distance(g_state.playerPosition, point.position);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestTarget = point.target;
        }
    }
    for (const auto &resident : g_state.residents)
    {
        float distance = Distance(g_state.playerPosition, resident.position);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestTarget = InteractionTarget::Resident;
        }
    }
    return nearestTarget;
}

void InteractWithNearbyTarget()
{
    const InteractionTarget target = FindNearbyInteraction();
    if (target == InteractionTarget::Resident)
    {
        const Resident *nearest = nullptr;
        float distance = 1.35f;
        for (const auto &resident : g_state.residents)
        {
            float candidate = Distance(g_state.playerPosition, resident.position);
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
        g_state.mainQuestStage = (std::max)(g_state.mainQuestStage, MainQuestStage::MeetLia);
        ShowNotice("단말 / 관문 N-0 정기 보정 중. 리아가 전망대에서 관문 신호를 측정해 달라고 요청했습니다.");
    }
    if (target == InteractionTarget::ApartmentExit)
    {
        if (g_state.mainQuestStage == MainQuestStage::ReadHomeTerminal)
        {
            ShowNotice("외출하기 전에 집 단말을 확인하세요. 청록색 불빛을 찾으세요.");
            return;
        }
        g_state.isInsideHome = false;
        g_state.playerPosition = kHomeDoor;
        g_state.cameraPosition = g_state.playerPosition;
        ShowNotice(
            "헤일로 / 주거 구역 07. 머리 위로 자기부상 차량이 지나갑니다. 새벽부터 관문이 낮게 울립니다.");
    }
    if (target == InteractionTarget::HomeEntrance)
    {
        g_state.isInsideHome = true;
        g_state.playerPosition = {2, 2};
        g_state.cameraPosition = {0, 0};
        ShowNotice("집 / 문이 닫히자 도시의 웅성거림이 멀어집니다.");
    }
    if (target == InteractionTarget::Lia)
    {
        if (g_state.mainQuestStage < MainQuestStage::MeetLia)
        {
            ShowNotice("리아 / 집 단말에 접근 코드를 보냈어. 먼저 확인해 줘.");
            return;
        }
        g_state.mainQuestStage = (std::max)(g_state.mainQuestStage, MainQuestStage::EnterObservatory);
        ShowNotice("리아 / 북동쪽 전망대 입구에서 E로 올라가. 신호 측정 뒤 내려오면 네 제트 바이크로 격리 "
                   "구역에 가자.");
    }
    if (target == InteractionTarget::Mara)
    {
        if (!g_state.isSideQuestAccepted)
        {
            g_state.isSideQuestAccepted = true;
            ShowNotice(
                "마라 / 딸아이 방에 불이 안 들어와요. 동쪽 전력 장치를 봐 주실래요? [서브퀘스트 수락]");
        }
        else if (g_state.isPowerRestored && !g_state.isSideQuestComplete)
        {
            g_state.isSideQuestComplete = true;
            ShowNotice("마라 / 이제 아이가 편히 잘 수 있겠어요. 고마워요. [남겨 둔 불빛: 완료]");
        }
        else
        {
            ShowNotice(g_state.isSideQuestComplete
                           ? "마라 / 언제든 들러요. 따뜻한 차를 준비해 둘게요."
                           : "마라 / 광장 동쪽의 주황색 전력 장치를 복구하고 돌아와 주세요.");
        }
    }
    if (target == InteractionTarget::PowerRelay)
    {
        if (g_state.isSideQuestAccepted)
        {
            g_state.isPowerRestored = true;
            ShowNotice("전력 장치 / 전력 복구 완료. 주거 구역 창문에 불이 켜집니다. 마라에게 돌아가세요.");
        }
        else
        {
            ShowNotice("전력 장치 / 지역 정전 감지. 먼저 광장의 마라에게 상황을 물어보세요.");
        }
    }
    if (target == InteractionTarget::GateBeacon)
    {
        if (g_state.mainQuestStage < MainQuestStage::EnterObservatory)
        {
            ShowNotice("전망대 / 리아가 접근 코드를 가지고 있습니다. 집 근처의 리아와 먼저 대화하세요.");
            return;
        }
        g_state.isInObservatory = true;
        g_state.isInsideHome = false;
        g_state.playerPosition = kObservatoryStairs;
        g_state.cameraPosition = {0, 0};
        std::fill(g_state.pressedKeys, g_state.pressedKeys + 256, false);
        if (!g_state.gateSignalHeard)
        {
            g_state.mainQuestStage = MainQuestStage::ReadGateSignal;
        }
        ShowNotice("전망대 / 승강기를 타고 관측 데크에 도착했습니다. 북동쪽의 신호 측정 콘솔로 이동하세요.");
    }
    if (target == InteractionTarget::ObservatorySignal)
    {
        g_state.gateSignalHeard = true;
        g_state.mainQuestStage = (std::max)(g_state.mainQuestStage, MainQuestStage::DescendObservatory);
        ShowNotice("알 수 없음 / …여긴 아직 빛이 있어… 리아 / 방금 네 목소리였어. 넌 아무 말도 안 했잖아.");
    }
    if (target == InteractionTarget::ObservatoryExit)
    {
        if (!g_state.gateSignalHeard)
        {
            ShowNotice("리아 / 먼저 관측 데크 북동쪽의 콘솔에서 신호를 확인해 줘.");
            return;
        }
        g_state.isInObservatory = false;
        g_state.hasDescendedObservatory = true;
        g_state.playerPosition = kBeaconPosition;
        g_state.cameraPosition = kBeaconPosition;
        std::fill(g_state.pressedKeys, g_state.pressedKeys + 256, false);
        g_state.mainQuestStage = (std::max)(g_state.mainQuestStage, MainQuestStage::MountJetBike);
        ShowNotice("리아 / 아래 주차장에 네 제트 바이크가 있어. 바이크에서 E로 탑승해 격리 구역으로 가자.");
    }
    if (target == InteractionTarget::JetBike)
    {
        if (!g_state.gateSignalHeard || !g_state.hasDescendedObservatory)
        {
            ShowNotice("제트 바이크 / 먼저 리아와 대화하고 전망대에서 신호를 측정한 뒤 내려오세요.");
            return;
        }
        jettravel::Begin();
    }
    if (target == InteractionTarget::None)
    {
        ShowNotice("인물이나 빛나는 단말 가까이에서 E 키를 누르세요.");
    }
}
} // namespace tutorial
