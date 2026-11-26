#include "stdafx.h"
#include "WorldEncounters.h"
#include "Civic.h"
#include "Level.h"
#include "Overworld.h"
#include "WorldGeneration.h"
#include "WorldStreaming.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace encounters
{
namespace
{
using namespace tutorial;

constexpr float kPi = 3.14159265f;

std::uint64_t HashString(const std::string &s)
{
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

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

Archetype ArchetypeForRegion(world::RegionKind kind, std::uint64_t seed)
{
    switch (kind)
    {
    case world::RegionKind::Outlaw:
        return (seed % 2 == 0) ? Archetype::OutlawBountyRaid : Archetype::SmugglerStandoff;
    case world::RegionKind::AncientRuins:
        return (seed % 2 == 0) ? Archetype::RuinsResonance : Archetype::RelayDefenseHold;
    case world::RegionKind::Mine:
        return (seed % 2 == 0) ? Archetype::MineCollapseRescue : Archetype::CrashedFreighter;
    case world::RegionKind::Outpost:
        return (seed % 3 == 0)   ? Archetype::MissingPatrol
               : (seed % 3 == 1) ? Archetype::ConvoyAmbush
                                 : Archetype::RelayDefenseHold;
    case world::RegionKind::Wilderness:
        return (seed % 3 == 0)   ? Archetype::WildernessAnomaly
               : (seed % 3 == 1) ? Archetype::CrashedFreighter
                                 : Archetype::ConvoyAmbush;
    case world::RegionKind::Settlement:
    default:
        return (seed % 2 == 0) ? Archetype::RogueDroneSwarm : Archetype::SmugglerStandoff;
    }
}
} // namespace

WorldEncounters &WorldEncounters::Get()
{
    static WorldEncounters instance;
    return instance;
}

void WorldEncounters::Reset()
{
    state = {};
    scannerCooldown = 0.f;
    scannerPulseRadius = 0.f;
}

const char *WorldEncounters::ArchetypeName(Archetype a)
{
    switch (a)
    {
    case Archetype::ConvoyAmbush:
        return "조난 호송대 방어 및 구출";
    case Archetype::OutlawBountyRaid:
        return "불법 중계기 삼각측량 · 기지 소탕";
    case Archetype::RuinsResonance:
        return "고대 석비 3중 공명 · 수호체 각성";
    case Archetype::MineCollapseRescue:
        return "노천 광맥 붕괴 생존자 구조";
    case Archetype::RogueDroneSwarm:
        return "폭주 드론 제어 노드 해킹";
    case Archetype::CrashedFreighter:
        return "추락 화물선 블랙박스 인양";
    case Archetype::WildernessAnomaly:
        return "야생 지질 이상 · 포식자 소탕";
    case Archetype::MissingPatrol:
        return "실종 순찰대 추적 및 탈출";
    case Archetype::SmugglerStandoff:
        return "밀수 거래 현장 급습 · 분기";
    case Archetype::RelayDefenseHold:
        return "임시 중계 비컨 웨이브 방어전";
    default:
        return "현장 인카운터";
    }
}

const char *WorldEncounters::ArchetypeKey(Archetype a)
{
    switch (a)
    {
    case Archetype::ConvoyAmbush:
        return "convoy_ambush";
    case Archetype::OutlawBountyRaid:
        return "outlaw_bounty_raid";
    case Archetype::RuinsResonance:
        return "ruins_resonance";
    case Archetype::MineCollapseRescue:
        return "mine_collapse_rescue";
    case Archetype::RogueDroneSwarm:
        return "rogue_drone_swarm";
    case Archetype::CrashedFreighter:
        return "crashed_freighter";
    case Archetype::WildernessAnomaly:
        return "wilderness_anomaly";
    case Archetype::MissingPatrol:
        return "missing_patrol";
    case Archetype::SmugglerStandoff:
        return "smuggler_standoff";
    case Archetype::RelayDefenseHold:
        return "relay_defense_hold";
    default:
        return "unknown";
    }
}

const char *WorldEncounters::PhaseKey(Phase p)
{
    switch (p)
    {
    case Phase::Dormant:
        return "dormant";
    case Phase::Stage1:
        return "stage1";
    case Phase::Stage2:
        return "stage2";
    case Phase::Stage3:
        return "stage3";
    case Phase::Stage4:
        return "stage4";
    case Phase::Completed:
        return "completed";
    default:
        return "dormant";
    }
}

int WorldEncounters::StageNumber(Phase p)
{
    switch (p)
    {
    case Phase::Stage1:
        return 1;
    case Phase::Stage2:
        return 2;
    case Phase::Stage3:
        return 3;
    case Phase::Stage4:
    case Phase::Completed:
        return 4;
    default:
        return 0;
    }
}

int WorldEncounters::AliveEnemies(const EncounterInstance &enc, int waveFilter) const
{
    int count = 0;
    for (const auto &e : enc.enemies)
    {
        if (!e.active || e.health <= 0)
        {
            continue;
        }
        if (waveFilter > 0 && e.wave != waveFilter)
        {
            continue;
        }
        ++count;
    }
    return count;
}

EncounterInstance *WorldEncounters::FindLive(const std::string &id)
{
    for (auto &enc : state.live)
    {
        if (enc.id == id)
        {
            return &enc;
        }
    }
    return nullptr;
}

Vector2 WorldEncounters::ToLegacyWorld(const EncounterInstance &enc, Vector2 offset) const
{
    return {enc.legacyAnchor.x + offset.x, enc.legacyAnchor.y + offset.y};
}

world::SurfacePoint WorldEncounters::ToSurfaceWorld(const EncounterInstance &enc, Vector2 offsetMeters) const
{
    const auto *session = Game::Get().Surface();
    if (!session || !session->Ready())
    {
        return enc.surfaceAnchor;
    }
    const auto frame = world::SurfaceGeometry::Frame(enc.surfaceAnchor);
    auto pt = world::SurfaceGeometry::FromLocal(frame, {double(offsetMeters.x), double(offsetMeters.y), 0.0},
                                                session->Planet().radiusMeters);
    pt.altitudeMeters = session->Stream().Contact(pt).heightMeters;
    return pt;
}

std::optional<Vector2> WorldEncounters::PlayerOffsetIn(const EncounterInstance &enc) const
{
    const auto &w = Game::Get().GetState();
    if (enc.surfaceSpace)
    {
        const auto *session = Game::Get().Surface();
        if (!w.surfaceExploration || !session || !session->Ready())
        {
            return std::nullopt;
        }
        const double dist =
            world::SurfaceGeometry::Distance(session->Planet().radiusMeters, session->Pose().origin, enc.surfaceAnchor);
        if (dist > 420.0)
        {
            return std::nullopt;
        }
        const auto frame = world::SurfaceGeometry::Frame(enc.surfaceAnchor);
        const auto rel = world::SurfaceAtlas::Project(frame, session->Pose().origin, session->Planet().radiusMeters);
        return Vector2{float(rel.x), float(rel.y)};
    }
    if (w.surfaceExploration)
    {
        return std::nullopt;
    }
    return Vector2{w.playerPosition.x - enc.legacyAnchor.x, w.playerPosition.y - enc.legacyAnchor.y};
}

void WorldEncounters::BuildArchetypeLayout(EncounterInstance &enc, std::uint64_t seed)
{
    enc.nodes.clear();
    enc.enemies.clear();
    enc.npcs.clear();
    enc.bullets.clear();
    enc.tracers.clear();
    const float sc = enc.surfaceSpace ? 2.0f : 1.0f; // meters on surface vs legacy world units
    enc.title = ArchetypeName(enc.archetype);
    enc.stageObjectivesDone = 0;
    enc.wave2Spawned = false;
    enc.defenseHoldSeconds = 0.f;
    enc.beaconHealth = enc.beaconMaxHealth = 100;

    auto addEnemy = [&](int id, const char *label, Vector2 off, int hp, int wave, bool boss, bool melee) {
        EncounterEnemy e;
        e.id = id;
        e.label = label;
        e.offset = {off.x * sc, off.y * sc};
        e.home = e.offset;
        e.radius = boss ? .62f : .48f;
        e.health = e.maxHealth = hp;
        e.wave = wave;
        e.isBoss = boss;
        e.meleeOnly = melee;
        e.active = false;
        enc.enemies.push_back(std::move(e));
    };
    auto addNode = [&](int idx, int reqStage, Vector2 off, const char *label, ColorRGBA glow, bool hidden = false) {
        EncounterNode n;
        n.index = idx;
        n.requiredStage = reqStage;
        n.offset = {off.x * sc, off.y * sc};
        n.radius = 2.6f * sc;
        n.label = label;
        n.glow = glow;
        n.hiddenUntilScanned = hidden;
        n.revealed = !hidden;
        enc.nodes.push_back(std::move(n));
    };

    int eid = 1;
    switch (enc.archetype)
    {
    case Archetype::ConvoyAmbush:
        enc.subtitle = "1차 매복 격퇴 → 호송 차량 2곳 수리 · 증원 방어 → 안전 지점 호위";
        enc.stageObjectivesTotal = 2;
        enc.npcs.push_back({"호송대장", models::CharacterType::Guard, models::Gender::Male,
                            models::HairStyle::SidePart, {.72f, .52f, .28f, 1.f}, {0.f, 0.f}, {16.f * sc, -4.f * sc}});
        enc.npcs.push_back({"운송 정비원", models::CharacterType::Engineer, models::Gender::Female,
                            models::HairStyle::Ponytail, {.36f, .64f, .72f, 1.f}, {-1.4f * sc, 1.2f * sc},
                            {15.f * sc, -5.f * sc}});
        addNode(0, 0, {0.f, 0.f}, "호송대장 / 조난 브리핑 · 정산", {1.f, .82f, .36f, 1.f});
        addNode(1, 2, {4.5f, -3.5f}, "호송 차량 구동계 수리 (1/2)", {.36f, .92f, .96f, 1.f});
        addNode(2, 2, {-4.5f, 3.8f}, "호송 차량 차폐기 수리 (2/2)", {.36f, .92f, .96f, 1.f});
        addEnemy(eid++, "약탈자 척후", {9.f, -5.f}, 60, 1, false, false);
        addEnemy(eid++, "약탈자 돌격수", {10.f, 4.f}, 68, 1, false, false);
        addEnemy(eid++, "약탈자 산탄병", {-8.f, -6.f}, 65, 1, false, false);
        addEnemy(eid++, "증원 약탈자 A", {12.f, -8.f}, 75, 2, false, false);
        addEnemy(eid++, "약탈자 습격대장", {13.f, 6.f}, 115, 2, true, false);
        break;

    case Archetype::OutlawBountyRaid:
        enc.subtitle = "위장 안테나 2곳 스캔·파괴 → 무법자 두목 및 호위 소탕 → 암호 단말 해독";
        enc.stageObjectivesTotal = 2;
        enc.npcs.push_back({"피습 상인", models::CharacterType::Civilian, models::Gender::Male,
                            models::HairStyle::Buzz, {.64f, .48f, .36f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        addNode(0, 0, {0.f, 0.f}, "현장 제보자 / 교란 추적 · 현상금 정산", {1.f, .66f, .28f, 1.f});
        addNode(1, 2, {11.f, -7.f}, "위장 교란 안테나 북부 파괴 (1/2)", {1.f, .42f, .32f, 1.f}, true);
        addNode(2, 2, {10.f, 8.f}, "위장 교란 안테나 남부 파괴 (2/2)", {1.f, .42f, .32f, 1.f}, true);
        addNode(3, 4, {16.f, 0.f}, "무법자 본거지 암호 단말 해독", {.38f, .96f, .74f, 1.f});
        addEnemy(eid++, "무법자 경비 A", {10.f, -5.f}, 65, 1, false, false);
        addEnemy(eid++, "무법자 경비 B", {9.f, 6.f}, 65, 1, false, false);
        addEnemy(eid++, "무법자 호위 A", {15.f, -4.f}, 78, 2, false, false);
        addEnemy(eid++, "무법자 호위 B", {15.f, 4.f}, 78, 2, false, false);
        addEnemy(eid++, "무법자 중장갑 두목", {17.5f, 0.f}, 145, 2, true, false);
        break;

    case Archetype::RuinsResonance:
        enc.subtitle = "고대 석비 3곳 공명 동기화 → 각성 수호체 5기 격퇴 → 공명 코어 회수";
        enc.stageObjectivesTotal = 3;
        enc.npcs.push_back({"유적 학자", models::CharacterType::Researcher, models::Gender::Female,
                            models::HairStyle::Bun, {.44f, .52f, .86f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        addNode(0, 0, {0.f, 0.f}, "유적 탐사 학자 / 공명 의뢰 · 보고", {.56f, .72f, 1.f, 1.f});
        addNode(1, 2, {0.f, -11.f}, "북부 고대 석비 공명 동기화 (1/3)", {.42f, .88f, 1.f, 1.f});
        addNode(2, 2, {11.f, 4.f}, "동부 고대 석비 공명 동기화 (2/3)", {.42f, .88f, 1.f, 1.f});
        addNode(3, 2, {-10.f, 5.f}, "서부 고대 석비 공명 동기화 (3/3)", {.42f, .88f, 1.f, 1.f});
        addEnemy(eid++, "유적 수호 드론 A", {6.f, -8.f}, 65, 2, false, false);
        addEnemy(eid++, "유적 수호 드론 B", {8.f, 6.f}, 65, 2, false, false);
        addEnemy(eid++, "유적 수호 드론 C", {-7.f, 6.f}, 65, 2, false, false);
        addEnemy(eid++, "유적 파수꾼", {-6.f, -6.f}, 85, 2, false, true);
        addEnemy(eid++, "고대 핵심 수호체", {2.f, -3.f}, 140, 2, true, false);
        break;

    case Archetype::MineCollapseRescue:
        enc.subtitle = "붕괴 잔해 2곳 해체 → 광산 포식자 4기 소탕 → 고립 작업자 3명 인계";
        enc.stageObjectivesTotal = 2;
        enc.npcs.push_back({"채굴 작업반장", models::CharacterType::Engineer, models::Gender::Male,
                            models::HairStyle::Buzz, {.76f, .56f, .28f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        for (int i = 0; i < 3; ++i)
        {
            enc.npcs.push_back({"고립 작업자 " + std::to_string(i + 1), models::CharacterType::Civilian,
                                i == 1 ? models::Gender::Female : models::Gender::Male, models::HairStyle::Bob,
                                {.52f, .60f, .56f, 1.f}, {(11.f + i * 1.4f) * sc, (-2.f + i * 2.f) * sc},
                                {1.5f * sc, 1.f * sc}});
        }
        addNode(0, 0, {0.f, 0.f}, "작업반장 / 구조 브리핑 · 완료 보고", {1.f, .76f, .32f, 1.f});
        addNode(1, 2, {6.5f, -3.f}, "북쪽 붕괴 잔해 해체 (1/2)", {1.f, .64f, .28f, 1.f});
        addNode(2, 2, {7.5f, 4.f}, "남쪽 붕괴 잔해 해체 (2/2)", {1.f, .64f, .28f, 1.f});
        addEnemy(eid++, "광산 포식자 A", {9.f, -5.f}, 68, 2, false, true);
        addEnemy(eid++, "광산 포식자 B", {11.f, 5.f}, 68, 2, false, true);
        addEnemy(eid++, "변이 굴착체 A", {14.f, -2.f}, 80, 2, false, true);
        addEnemy(eid++, "변이 우두머리", {15.f, 2.f}, 120, 2, true, true);
        break;

    case Archetype::RogueDroneSwarm:
        enc.subtitle = "제어 노드 3곳 해킹 · 폭주 드론 방어 → 지휘 중계 드론 격추";
        enc.stageObjectivesTotal = 3;
        enc.npcs.push_back({"도시 제어 기술자", models::CharacterType::Engineer, models::Gender::Female,
                            models::HairStyle::SidePart, {.32f, .74f, .86f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        addNode(0, 0, {0.f, 0.f}, "제어 기술자 / 드론 폭주 대응 · 정산", {.34f, .92f, 1.f, 1.f});
        addNode(1, 2, {8.f, -6.f}, "외곽 제어 노드 A 해킹 (1/3)", {.34f, .96f, .74f, 1.f});
        addNode(2, 2, {11.f, 4.f}, "외곽 제어 노드 B 해킹 (2/3)", {.34f, .96f, .74f, 1.f});
        addNode(3, 2, {-6.f, 7.f}, "외곽 제어 노드 C 해킹 (3/3)", {.34f, .96f, .74f, 1.f});
        addEnemy(eid++, "폭주 순찰 드론 A", {7.f, -4.f}, 55, 1, false, false);
        addEnemy(eid++, "폭주 순찰 드론 B", {9.f, 5.f}, 55, 1, false, false);
        addEnemy(eid++, "폭주 순찰 드론 C", {-5.f, 5.f}, 55, 1, false, false);
        addEnemy(eid++, "지휘 호위 드론", {5.f, 8.f}, 72, 2, false, false);
        addEnemy(eid++, "지휘 중계 드론 (엘리트)", {12.f, -1.f}, 135, 2, true, false);
        break;

    case Archetype::CrashedFreighter:
        enc.subtitle = "잔해 3구역 블랙박스·항법칩·물자 인양 → 난입 약탈단 격퇴";
        enc.stageObjectivesTotal = 3;
        enc.npcs.push_back({"생존 항법사", models::CharacterType::Explorer, models::Gender::Female,
                            models::HairStyle::Ponytail, {.76f, .44f, .34f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        addNode(0, 0, {0.f, 0.f}, "생존 항법사 / 추락 인양 · 분배", {1.f, .74f, .38f, 1.f});
        addNode(1, 2, {9.f, -6.f}, "기수 잔해 · 블랙박스 회수 (1/3)", {1.f, .86f, .38f, 1.f});
        addNode(2, 2, {13.f, 1.f}, "화물칸 잔해 · 항법 칩 회수 (2/3)", {1.f, .86f, .38f, 1.f});
        addNode(3, 2, {7.f, 8.f}, "엔진실 잔해 · 비상 물자 회수 (3/3)", {1.f, .86f, .38f, 1.f});
        addEnemy(eid++, "고철 약탈자 A", {12.f, -8.f}, 65, 2, false, false);
        addEnemy(eid++, "고철 약탈자 B", {15.f, 4.f}, 65, 2, false, false);
        addEnemy(eid++, "고철 약탈자 C", {6.f, 10.f}, 70, 2, false, false);
        addEnemy(eid++, "약탈단 현장 지휘관", {14.f, -2.f}, 130, 2, true, false);
        break;

    case Archetype::WildernessAnomaly:
        enc.subtitle = "지질 이상 3지점 표본 채취 → 진동원 변이 포식자 5기 소탕";
        enc.stageObjectivesTotal = 3;
        enc.npcs.push_back({"생태 연구원", models::CharacterType::Researcher, models::Gender::Male,
                            models::HairStyle::SidePart, {.34f, .72f, .52f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        addNode(0, 0, {0.f, 0.f}, "생태 연구원 / 지질 이상 조사 · 정산", {.38f, .96f, .64f, 1.f});
        addNode(1, 2, {-8.f, -7.f}, "서북 지질 이상 표본 채취 (1/3)", {.44f, .94f, .76f, 1.f});
        addNode(2, 2, {9.f, -6.f}, "동북 지질 이상 표본 채취 (2/3)", {.44f, .94f, .76f, 1.f});
        addNode(3, 2, {5.f, 9.f}, "남부 진동원 표본 채취 (3/3)", {.44f, .94f, .76f, 1.f});
        addEnemy(eid++, "야생 변이 포식자 A", {6.f, 11.f}, 64, 2, false, true);
        addEnemy(eid++, "야생 변이 포식자 B", {9.f, 8.f}, 64, 2, false, true);
        addEnemy(eid++, "야생 변이 포식자 C", {2.f, 12.f}, 64, 2, false, true);
        addEnemy(eid++, "야생 변이 포식자 D", {11.f, 5.f}, 72, 2, false, true);
        addEnemy(eid++, "둥지 알파 변이체", {7.f, 10.f}, 135, 2, true, true);
        break;

    case Archetype::MissingPatrol:
        enc.subtitle = "순찰대 흔적 3곳 추적 → 포위 무법자 5명 격파 → 순찰대원 2명 귀환 호위";
        enc.stageObjectivesTotal = 3;
        enc.npcs.push_back({"전초 지휘관", models::CharacterType::Guard, models::Gender::Male,
                            models::HairStyle::Buzz, {.34f, .46f, .62f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        enc.npcs.push_back({"실종 순찰대원 A", models::CharacterType::Guard, models::Gender::Female,
                            models::HairStyle::Ponytail, {.38f, .54f, .70f, 1.f}, {16.f * sc, 2.f * sc},
                            {1.5f * sc, -1.f * sc}});
        enc.npcs.push_back({"실종 순찰대원 B", models::CharacterType::Guard, models::Gender::Male,
                            models::HairStyle::SidePart, {.38f, .54f, .70f, 1.f}, {17.f * sc, 3.5f * sc},
                            {1.5f * sc, 1.f * sc}});
        addNode(0, 0, {0.f, 0.f}, "전초 지휘관 / 실종 순찰대 수색 · 보고", {.42f, .86f, 1.f, 1.f});
        addNode(1, 2, {5.f, -3.f}, "흔적 1/3 · 버려진 순찰 바이크 조사", {1.f, .82f, .38f, 1.f});
        addNode(2, 2, {10.f, -1.f}, "흔적 2/3 · 교전 탄피 흔적 조사", {1.f, .82f, .38f, 1.f});
        addNode(3, 2, {13.f, 2.f}, "흔적 3/3 · 응급 키트 및 포위 지점 확인", {1.f, .82f, .38f, 1.f});
        addEnemy(eid++, "포위 무법자 A", {14.f, -2.f}, 68, 2, false, false);
        addEnemy(eid++, "포위 무법자 B", {19.f, 1.f}, 68, 2, false, false);
        addEnemy(eid++, "포위 무법자 C", {18.f, 6.f}, 68, 2, false, false);
        addEnemy(eid++, "포위 무법자 D", {14.f, 6.f}, 72, 2, false, false);
        addEnemy(eid++, "매복 지휘자", {20.f, 3.5f}, 125, 2, true, false);
        break;

    case Archetype::SmugglerStandoff:
        enc.subtitle = "밀수 화물 단서 2곳 확보 → 밀수 조직장 대면(강경 압수 / 협상 / 정보 거래)";
        enc.stageObjectivesTotal = 2;
        enc.npcs.push_back({"익명 제보자", models::CharacterType::Civilian, models::Gender::Female,
                            models::HairStyle::Bob, {.54f, .44f, .64f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        enc.npcs.push_back({"밀수 조직장", models::CharacterType::HeavyCombat, models::Gender::Male,
                            models::HairStyle::SidePart, {.52f, .30f, .28f, 1.f}, {13.f * sc, 0.f}, {13.f * sc, 0.f}});
        addNode(0, 0, {0.f, 0.f}, "제보자 / 밀수 현장 정보 · 사후 정산", {.86f, .62f, 1.f, 1.f});
        addNode(1, 2, {6.5f, -5.f}, "밀수 화물 적하 목록 확보 (1/2)", {1.f, .78f, .36f, 1.f});
        addNode(2, 2, {7.5f, 5.f}, "발주 암호 단서 확보 (2/2)", {1.f, .78f, .36f, 1.f});
        addNode(3, 3, {12.f, 0.f}, "밀수 조직장 대면 · 압수 / 협상 / 거래 선택", {.98f, .52f, .36f, 1.f});
        addEnemy(eid++, "밀수 경호원 A", {11.f, -3.f}, 70, 2, false, false);
        addEnemy(eid++, "밀수 경호원 B", {11.f, 3.f}, 70, 2, false, false);
        addEnemy(eid++, "밀수 경호원 C", {15.f, -2.f}, 75, 2, false, false);
        addEnemy(eid++, "밀수 조직장 (전투)", {14.f, 1.f}, 135, 2, true, false);
        break;

    case Archetype::RelayDefenseHold:
    default:
        enc.subtitle = "냉각 밸브 2곳 조작 · 1파 저지 → 임시 비컨 26초 2웨이브 실시간 방어";
        enc.stageObjectivesTotal = 2;
        enc.defenseHoldTarget = 26.f;
        enc.npcs.push_back({"통신 엔지니어", models::CharacterType::Engineer, models::Gender::Male,
                            models::HairStyle::SidePart, {.34f, .78f, .68f, 1.f}, {0.f, 0.f}, {0.f, 0.f}});
        addNode(0, 0, {0.f, 0.f}, "통신 엔지니어 / 임시 비컨 가동 · 정산", {.32f, .96f, .86f, 1.f});
        addNode(1, 2, {6.f, -5.5f}, "과열 냉각 밸브 A 개방 (1/2)", {.36f, .92f, 1.f, 1.f});
        addNode(2, 2, {-6.f, 5.5f}, "과열 냉각 밸브 B 개방 (2/2)", {.36f, .92f, 1.f, 1.f});
        addEnemy(eid++, "교란 습격 드론 A", {10.f, -6.f}, 60, 1, false, false);
        addEnemy(eid++, "교란 습격 드론 B", {-10.f, 6.f}, 60, 1, false, false);
        addEnemy(eid++, "교란 습격 드론 C", {11.f, 5.f}, 60, 1, false, false);
        addEnemy(eid++, "2파 강습병 A", {12.f, -8.f}, 75, 2, false, false);
        addEnemy(eid++, "2파 강습병 B", {-12.f, -6.f}, 75, 2, false, false);
        addEnemy(eid++, "2파 강습병 C", {12.f, 8.f}, 75, 2, false, false);
        addEnemy(eid++, "비컨 파괴자 (엘리트)", {-12.f, 8.f}, 130, 2, true, false);
        break;
    }
    (void)seed;
}

void WorldEncounters::SpawnWaveEnemies(EncounterInstance &enc, int wave)
{
    for (auto &e : enc.enemies)
    {
        if (e.wave == wave && e.health > 0)
        {
            e.active = true;
            e.alertSeconds = 12.f;
        }
    }
    if (wave == 2)
    {
        enc.wave2Spawned = true;
    }
}

void WorldEncounters::BuildLegacyOutskirtsEncounters()
{
    state.worldKey = "legacy_halo";
    state.live.clear();
    struct SeedSpec
    {
        const char *id;
        Archetype type;
        Vector2 anchor;
    };
    const SeedSpec specs[] = {
        {"legacy_convoy_east", Archetype::ConvoyAmbush, {68.f, -14.f}},
        {"legacy_drones_north", Archetype::RogueDroneSwarm, {-44.f, -62.f}},
        {"legacy_smuggler_south", Archetype::SmugglerStandoff, {32.f, 68.f}},
        {"legacy_relay_west", Archetype::RelayDefenseHold, {-78.f, 18.f}},
    };
    for (const auto &sp : specs)
    {
        EncounterInstance enc;
        enc.id = sp.id;
        enc.regionId = "halo_07";
        enc.archetype = sp.type;
        enc.surfaceSpace = false;
        enc.legacyAnchor = sp.anchor;
        BuildArchetypeLayout(enc, HashString(enc.id));
        state.live.push_back(std::move(enc));
    }
    ApplySavedRecords();
}

void WorldEncounters::BuildPlanetSurfaceEncounters()
{
    const auto *session = Game::Get().Surface();
    if (!session || !session->Ready() || !session->Stream().Snapshot() || !session->Stream().Snapshot()->planet)
    {
        return;
    }
    const auto &planet = *session->Stream().Snapshot()->planet;
    world::RelayNetwork keyMaker;
    keyMaker.Bind(planet.terrain.Planet());
    const std::string key = keyMaker.WorldKey();
    if (state.worldKey == key && !state.live.empty())
    {
        return;
    }
    state.worldKey = key;
    state.live.clear();

    std::set<Archetype> usedTypes;
    const double R = planet.terrain.Planet().radiusMeters;
    // 1) Region-anchored rich 4-stage encounters across every non-story & outer region
    for (const auto &reg : planet.regions)
    {
        if (reg.center.space != world::SpaceKind::Surface || reg.definition.id == "frontier" ||
            reg.definition.id == "mq00_mine")
        {
            continue;
        }
        if (state.live.size() >= 18)
        {
            break;
        }
        const std::uint64_t h = HashString(reg.definition.id + ":" + key);
        Archetype type = ArchetypeForRegion(reg.definition.kind, h);
        // Ensure all 10 archetypes are strongly represented across the planet!
        if (usedTypes.size() < size_t(Archetype::Count) && usedTypes.count(type))
        {
            for (int a = 0; a < int(Archetype::Count); ++a)
            {
                if (!usedTypes.count(Archetype(a)))
                {
                    type = Archetype(a);
                    break;
                }
            }
        }
        usedTypes.insert(type);
        EncounterInstance enc;
        enc.id = "enc_" + reg.definition.id;
        enc.regionId = reg.definition.id;
        enc.archetype = type;
        enc.surfaceSpace = true;
        // Offset cleanly outside settlement plaza so it never crowds core city quest NPCs
        const double offsetDist = reg.definition.kind == world::RegionKind::Settlement ? 120.0 : 48.0;
        const double ang = double(h % 360) * (kPi / 180.0);
        const auto frame = world::SurfaceGeometry::Frame(reg.access.position);
        enc.surfaceAnchor = world::SurfaceGeometry::FromLocal(
            frame, {std::cos(ang) * offsetDist, std::sin(ang) * offsetDist, 0.0}, R);
        enc.surfaceAnchor.altitudeMeters = session->Stream().Contact(enc.surfaceAnchor).heightMeters;
        BuildArchetypeLayout(enc, h);
        state.live.push_back(std::move(enc));
    }

    // 2) Roaming roadside & wilderness dynamic encounter pockets around Halo & travel corridors
    const auto *halo = planet.regions.empty() ? nullptr : &planet.regions.front();
    if (halo)
    {
        const Vector2 roamOffsets[] = {{210.f, -85.f}, {-195.f, -140.f}, {165.f, 220.f}, {-240.f, 155.f},
                                       {340.f, 45.f},  {-320.f, -20.f}};
        const Archetype roamTypes[] = {Archetype::ConvoyAmbush,     Archetype::RogueDroneSwarm,
                                       Archetype::SmugglerStandoff, Archetype::RelayDefenseHold,
                                       Archetype::CrashedFreighter, Archetype::MissingPatrol};
        const auto baseFrame = world::SurfaceGeometry::Frame(halo->access.position);
        for (size_t i = 0; i < 6; ++i)
        {
            EncounterInstance enc;
            enc.id = "roam_" + std::to_string(i);
            enc.regionId = halo->definition.id;
            enc.archetype = roamTypes[i];
            enc.surfaceSpace = true;
            enc.surfaceAnchor = world::SurfaceGeometry::FromLocal(
                baseFrame, {double(roamOffsets[i].x), double(roamOffsets[i].y), 0.0}, R);
            enc.surfaceAnchor.altitudeMeters = session->Stream().Contact(enc.surfaceAnchor).heightMeters;
            BuildArchetypeLayout(enc, HashString(enc.id + key));
            state.live.push_back(std::move(enc));
        }
    }
    ApplySavedRecords();
}

void WorldEncounters::PopulateForCurrentWorld()
{
    const auto &w = Game::Get().GetState();
    if (w.surfaceExploration)
    {
        BuildPlanetSurfaceEncounters();
    }
    else if (state.worldKey != "legacy_halo" || state.live.empty())
    {
        BuildLegacyOutskirtsEncounters();
    }
}

void WorldEncounters::SyncSaveRecords()
{
    state.saved.clear();
    for (const auto &enc : state.live)
    {
        if (enc.phase == Phase::Dormant && enc.stageObjectivesDone == 0 && !enc.rewardClaimed)
        {
            continue;
        }
        state.saved.push_back({enc.id, enc.archetype, enc.phase, enc.choice, enc.stageObjectivesDone, enc.rewardClaimed});
    }
}

void WorldEncounters::ApplySavedRecords()
{
    for (const auto &rec : state.saved)
    {
        if (auto *enc = FindLive(rec.id))
        {
            enc->phase = rec.phase;
            enc->choice = rec.choice;
            enc->stageObjectivesDone = rec.stageObjectivesDone;
            enc->rewardClaimed = rec.rewardClaimed;
            if (enc->phase == Phase::Completed)
            {
                for (auto &n : enc->nodes)
                {
                    n.completed = true;
                }
                for (auto &e : enc->enemies)
                {
                    e.active = false;
                    e.health = 0;
                }
            }
            else if (enc->phase == Phase::Stage2)
            {
                int marked = 0;
                for (auto &n : enc->nodes)
                {
                    if (n.requiredStage == 2 && marked < enc->stageObjectivesDone)
                    {
                        n.completed = true;
                        ++marked;
                    }
                }
            }
            else if (enc->phase == Phase::Stage3)
            {
                for (auto &n : enc->nodes)
                {
                    if (n.requiredStage <= 2)
                    {
                        n.completed = true;
                    }
                }
                if (enc->archetype != Archetype::SmugglerStandoff)
                {
                    SpawnWaveEnemies(*enc, 2);
                }
            }
            else if (enc->phase == Phase::Stage4)
            {
                for (auto &n : enc->nodes)
                {
                    if (n.requiredStage <= 3)
                    {
                        n.completed = true;
                    }
                }
            }
        }
    }
}

void WorldEncounters::Update(float dt)
{
    scannerCooldown = (std::max)(0.f, scannerCooldown - dt);
    if (scannerPulseRadius > 0.f)
    {
        scannerPulseRadius += dt * 42.f;
        if (scannerPulseRadius > 140.f)
        {
            scannerPulseRadius = 0.f;
        }
    }
    const auto &w = Game::Get().GetState();
    if (w.IsInsideAny() || game::GameLevels::Mine().IsActive() || game::GameLevels::Chapter().IsActive())
    {
        return;
    }
    PopulateForCurrentWorld();

    for (auto &enc : state.live)
    {
        const auto playerOffOpt = PlayerOffsetIn(enc);
        if (!playerOffOpt)
        {
            enc.activeInRange = false;
            continue;
        }
        const Vector2 pOff = *playerOffOpt;
        const float distToCenter = std::hypot(pOff.x, pOff.y);
        const float wakeRange = enc.surfaceSpace ? 190.f : 42.f;
        enc.activeInRange = distToCenter <= wakeRange;
        if (!enc.activeInRange)
        {
            continue;
        }
        if (!enc.discoveredNotified && enc.phase == Phase::Dormant && distToCenter <= (enc.surfaceSpace ? 130.f : 26.f))
        {
            enc.discoveredNotified = true;
            Game::Get().ShowNotice("[현장 인카운터] " + enc.title + " 신호 포착 — 현장 인원에게 접근해 확인 [E]");
        }
        if (enc.phase == Phase::Completed)
        {
            continue;
        }

        // RelayDefenseHold 26-second live wave defense timer!
        if (enc.archetype == Archetype::RelayDefenseHold && enc.phase == Phase::Stage3)
        {
            enc.defenseHoldSeconds = (std::min)(enc.defenseHoldTarget, enc.defenseHoldSeconds + dt);
            if (!enc.wave2Spawned && enc.defenseHoldSeconds >= 10.f)
            {
                SpawnWaveEnemies(enc, 2);
                Game::Get().ShowNotice("임시 중계 비컨 방어: 2파 강습 부대 출현! 비컨을 지켜내세요!");
            }
            if (enc.defenseHoldSeconds >= enc.defenseHoldTarget && AliveEnemies(enc) == 0)
            {
                enc.phase = Phase::Stage4;
                Game::Get().ShowNotice("중계 비컨 동기화 100% 완료! 통신 엔지니어에게 보상을 수령하세요.");
                SyncSaveRecords();
            }
        }

        // Enemy AI (combat against player, and against beacon during RelayDefenseHold)
        const float unitScale = enc.surfaceSpace ? 2.f : 1.f;
        for (auto &e : enc.enemies)
        {
            if (!e.active || e.health <= 0)
            {
                continue;
            }
            e.hitFlash = (std::max)(0.f, e.hitFlash - dt * 4.f);
            const float dPlayer = Game::Distance(e.offset, pOff);
            if (dPlayer < 20.f * unitScale)
            {
                e.alertSeconds = 8.f;
            }
            else
            {
                e.alertSeconds = (std::max)(0.f, e.alertSeconds - dt);
            }
            if (e.alertSeconds <= 0.f)
            {
                continue;
            }
            const bool attackBeacon = enc.archetype == Archetype::RelayDefenseHold && enc.phase == Phase::Stage3 &&
                                      std::hypot(e.offset.x, e.offset.y) < dPlayer;
            const Vector2 targetPos = attackBeacon ? Vector2{0.f, 0.f} : pOff;
            const float dist = Game::Distance(e.offset, targetPos);
            const float aim = std::atan2(targetPos.y - e.offset.y, targetPos.x - e.offset.x);
            e.yaw = aim;
            const float desired = (e.meleeOnly ? 1.1f : 6.2f) * unitScale;
            const float speed = (e.meleeOnly ? 3.1f : 2.3f) * unitScale;
            if (dist > desired)
            {
                e.offset.x += std::cos(aim) * speed * dt;
                e.offset.y += std::sin(aim) * speed * dt;
            }
            e.fireCooldown -= dt;
            if (e.meleeOnly)
            {
                if (dist <= 1.45f * unitScale && e.fireCooldown <= 0.f)
                {
                    e.fireCooldown = 1.1f;
                    if (attackBeacon)
                    {
                        enc.beaconHealth = (std::max)(10, enc.beaconHealth - 6);
                    }
                    else
                    {
                        DamagePlayer(e.isBoss ? 14 : 10);
                    }
                }
            }
            else if (dist <= 15.f * unitScale && e.fireCooldown <= 0.f)
            {
                e.fireCooldown = e.isBoss ? 0.95f : 1.35f;
                if (attackBeacon)
                {
                    enc.beaconHealth = (std::max)(10, enc.beaconHealth - 4);
                    enc.tracers.push_back({e.offset, {0.f, 0.f}, {1.f, .44f, .32f, .9f}, .14f});
                }
                else
                {
                    const float bSpd = 14.f * unitScale;
                    enc.bullets.push_back(
                        {e.offset, {std::cos(aim) * bSpd, std::sin(aim) * bSpd}, 1.2f, e.isBoss ? 12 : 8, false});
                    enc.tracers.push_back({e.offset,
                                           {e.offset.x + std::cos(aim) * 2.4f * unitScale,
                                            e.offset.y + std::sin(aim) * 2.4f * unitScale},
                                           {1.f, .44f, .34f, .9f},
                                           .12f});
                }
            }
        }

        // Escort NPCs moving toward safety point during Stage 3
        bool anyEscortPending = false;
        for (auto &npc : enc.npcs)
        {
            if (!npc.escorting || npc.arrived)
            {
                continue;
            }
            const float d = Game::Distance(npc.offset, npc.escortTarget);
            if (d > .9f * unitScale)
            {
                const float ang = std::atan2(npc.escortTarget.y - npc.offset.y, npc.escortTarget.x - npc.offset.x);
                npc.yaw = ang;
                npc.offset.x += std::cos(ang) * 2.8f * unitScale * dt;
                npc.offset.y += std::sin(ang) * 2.8f * unitScale * dt;
                anyEscortPending = true;
            }
            else
            {
                npc.arrived = true;
                npc.rescued = true;
                npc.escorting = false;
            }
        }
        if (enc.phase == Phase::Stage3 && !anyEscortPending && AliveEnemies(enc) == 0 &&
            (enc.archetype == Archetype::ConvoyAmbush || enc.archetype == Archetype::MineCollapseRescue ||
             enc.archetype == Archetype::MissingPatrol))
        {
            enc.phase = Phase::Stage4;
            Game::Get().ShowNotice(enc.title + " / 안전 호위 완료! 의뢰인에게 최종 보상을 정산하세요.");
            SyncSaveRecords();
        }

        // Bullets & tracers
        for (auto &b : enc.bullets)
        {
            if (b.life <= 0.f)
            {
                continue;
            }
            b.life -= dt;
            b.offset.x += b.velocity.x * dt;
            b.offset.y += b.velocity.y * dt;
            if (!b.playerOwned && Game::Distance(b.offset, pOff) <= .55f * unitScale &&
                !w.playerMotion.mobility.Dodging())
            {
                DamagePlayer(b.damage);
                b.life = 0.f;
            }
        }
        enc.bullets.erase(std::remove_if(enc.bullets.begin(), enc.bullets.end(),
                                         [](const EncounterBullet &b) { return b.life <= 0.f; }),
                          enc.bullets.end());
        for (auto &tr : enc.tracers)
        {
            tr.life -= dt;
        }
        enc.tracers.erase(std::remove_if(enc.tracers.begin(), enc.tracers.end(),
                                         [](const EncounterTracer &t) { return t.life <= 0.f; }),
                          enc.tracers.end());
    }
}

void WorldEncounters::DamagePlayer(int amount)
{
    if (HospitalRecovery::Active())
    {
        return;
    }
    auto &player = Game::Get().Player();
    const int reduced = (std::max)(1, amount - player.defense / 3);
    player.health = (std::max)(0, player.health - reduced);
    if (player.health <= 0)
    {
        HospitalRecovery::Begin();
    }
}

void WorldEncounters::DamageEnemy(EncounterInstance &enc, EncounterEnemy &enemy, int damage, bool playerOwned)
{
    if (!enemy.active || enemy.health <= 0 || !playerOwned)
    {
        return;
    }
    enemy.health = (std::max)(0, enemy.health - damage);
    enemy.hitFlash = 1.f;
    enemy.alertSeconds = 12.f;
    if (enemy.health <= 0)
    {
        enemy.active = false;
        auto &player = Game::Get().Player();
        player.experience += enemy.isBoss ? 55 : 22;
        player.credits += enemy.isBoss ? 40 : 12;
        if (enemy.isBoss)
        {
            player.salvage = (std::min)(9999, player.salvage + 1);
            Game::Get().ShowNotice(enemy.label + " 격파! (+55 XP · +40 Cr · 고철 +1)");
        }
        AdvanceStageIfReady(enc);
    }
}

void WorldEncounters::AdvanceStageIfReady(EncounterInstance &enc)
{
    if (enc.phase == Phase::Stage1 && AliveEnemies(enc, 1) == 0)
    {
        enc.phase = Phase::Stage2;
        Game::Get().ShowNotice(enc.title + " [2/4단계] 1차 위협 격퇴! 현장 목표를 완료하세요.");
        SyncSaveRecords();
        return;
    }
    if (enc.phase == Phase::Stage3 && AliveEnemies(enc) == 0)
    {
        if (enc.archetype == Archetype::ConvoyAmbush || enc.archetype == Archetype::MineCollapseRescue ||
            enc.archetype == Archetype::MissingPatrol)
        {
            // Trigger escort walk to safety!
            for (size_t i = 0; i < enc.npcs.size(); ++i)
            {
                if (enc.archetype == Archetype::ConvoyAmbush || i > 0)
                {
                    enc.npcs[i].escorting = true;
                }
            }
            Game::Get().ShowNotice(enc.title + " [3/4단계] 위협 섬멸 완료 — 생존 인원이 안전 지점으로 이동합니다!");
            return;
        }
        if (enc.archetype == Archetype::RelayDefenseHold && enc.defenseHoldSeconds < enc.defenseHoldTarget)
        {
            return;
        }
        enc.phase = Phase::Stage4;
        Game::Get().ShowNotice(enc.title + " [4/4단계] 현장 제압 완료! 의뢰인/단말에서 최종 분기 정산을 진행하세요.");
        SyncSaveRecords();
    }
}

bool WorldEncounters::HitAtWorldPoint(const world::SurfacePoint &point, int damage, bool playerOwned)
{
    if (!playerOwned)
    {
        return false;
    }
    const auto *session = Game::Get().Surface();
    if (!session || !session->Ready())
    {
        return false;
    }
    for (auto &enc : state.live)
    {
        if (!enc.surfaceSpace || !enc.activeInRange || enc.phase == Phase::Completed)
        {
            continue;
        }
        const auto frame = world::SurfaceGeometry::Frame(enc.surfaceAnchor);
        const auto rel = world::SurfaceAtlas::Project(frame, point, session->Planet().radiusMeters);
        const Vector2 hitOff{float(rel.x), float(rel.y)};
        for (auto &e : enc.enemies)
        {
            if (e.active && e.health > 0 && Game::Distance(hitOff, e.offset) <= e.radius * 2.f + .8f)
            {
                DamageEnemy(enc, e, damage, true);
                return true;
            }
        }
    }
    return false;
}

bool WorldEncounters::HitAtLegacyPoint(Vector2 pos, int damage, bool playerOwned)
{
    if (!playerOwned)
    {
        return false;
    }
    for (auto &enc : state.live)
    {
        if (enc.surfaceSpace || !enc.activeInRange || enc.phase == Phase::Completed)
        {
            continue;
        }
        const Vector2 hitOff{pos.x - enc.legacyAnchor.x, pos.y - enc.legacyAnchor.y};
        for (auto &e : enc.enemies)
        {
            if (e.active && e.health > 0 && Game::Distance(hitOff, e.offset) <= e.radius + .45f)
            {
                DamageEnemy(enc, e, damage, true);
                return true;
            }
        }
    }
    return false;
}

bool WorldEncounters::TryMelee(Vector2 playerPos, float yaw, bool surfaceMode)
{
    bool hit = false;
    for (auto &enc : state.live)
    {
        if (enc.surfaceSpace != surfaceMode || !enc.activeInRange || enc.phase == Phase::Completed)
        {
            continue;
        }
        const auto pOffOpt = PlayerOffsetIn(enc);
        if (!pOffOpt)
        {
            continue;
        }
        const float reach = surfaceMode ? 3.8f : 1.9f;
        for (auto &e : enc.enemies)
        {
            if (!e.active || e.health <= 0)
            {
                continue;
            }
            const float d = Game::Distance(*pOffOpt, e.offset);
            if (d <= reach + e.radius)
            {
                const float ang = std::atan2(e.offset.y - pOffOpt->y, e.offset.x - pOffOpt->x);
                if (std::fabs(WrapAngle(ang - yaw)) <= 1.35f)
                {
                    DamageEnemy(enc, e, 36, true);
                    hit = true;
                }
            }
        }
    }
    (void)playerPos;
    return hit;
}

bool WorldEncounters::TriggerScannerPing()
{
    if (scannerCooldown > 0.f)
    {
        return false;
    }
    PopulateForCurrentWorld();
    const auto &w = Game::Get().GetState();
    scannerCooldown = 4.f;
    scannerPulseRadius = 1.f;
    scannerSurfaceSpace = w.surfaceExploration;
    if (w.surfaceExploration && Game::Get().Surface() && Game::Get().Surface()->Ready())
    {
        scannerOriginSurface = Game::Get().Surface()->Pose().origin;
    }
    else
    {
        scannerOriginLegacy = w.playerPosition;
    }

    int revealedCount = 0;
    const EncounterInstance *nearest = nullptr;
    float nearestDist = 1e9f;
    Vector2 nearestDelta{};
    for (auto &enc : state.live)
    {
        if (enc.surfaceSpace != w.surfaceExploration || enc.phase == Phase::Completed)
        {
            continue;
        }
        const auto pOff = PlayerOffsetIn(enc);
        if (!pOff)
        {
            continue;
        }
        const float d = std::hypot(pOff->x, pOff->y);
        if (d < nearestDist)
        {
            nearestDist = d;
            nearest = &enc;
            nearestDelta = {-pOff->x, -pOff->y};
        }
        if (d <= (enc.surfaceSpace ? 180.f : 40.f))
        {
            for (auto &n : enc.nodes)
            {
                if (n.hiddenUntilScanned && !n.revealed)
                {
                    n.revealed = true;
                    ++revealedCount;
                }
            }
        }
    }
    if (revealedCount > 0)
    {
        Game::Get().ShowNotice("스캐너 펄스: 은폐된 현장 목표 " + std::to_string(revealedCount) +
                               "곳의 좌표를 포착했습니다!");
        return true;
    }
    if (nearest)
    {
        const int meters = int(std::lround(nearest->surfaceSpace ? nearestDist : nearestDist * 2.f));
        const char *dir = std::fabs(nearestDelta.x) > std::fabs(nearestDelta.y)
                              ? (nearestDelta.x > 0 ? "동쪽" : "서쪽")
                              : (nearestDelta.y > 0 ? "남쪽" : "북쪽");
        Game::Get().ShowNotice("스캐너 펄스: [" + nearest->title + "] 신호 감지 (" + dir + " 약 " +
                               std::to_string(meters) + "m)");
        return true;
    }
    return false;
}

bool WorldEncounters::HasNearbyInteraction() const
{
    return !NearbyInteractionLabel().empty();
}

std::string WorldEncounters::NearbyInteractionLabel() const
{
    const auto &w = Game::Get().GetState();
    for (const auto &enc : state.live)
    {
        if (enc.surfaceSpace != w.surfaceExploration || !enc.activeInRange || enc.phase == Phase::Completed)
        {
            continue;
        }
        const auto pOffOpt = PlayerOffsetIn(enc);
        if (!pOffOpt)
        {
            continue;
        }
        for (const auto &n : enc.nodes)
        {
            if (!n.revealed || n.completed)
            {
                continue;
            }
            const bool usable = (n.requiredStage == 0 && (enc.phase == Phase::Dormant || enc.phase == Phase::Stage4)) ||
                                (n.requiredStage == int(enc.phase));
            if (usable && Game::Distance(*pOffOpt, n.offset) <= n.radius + (enc.surfaceSpace ? 1.8f : 0.9f))
            {
                return n.label;
            }
        }
    }
    return "";
}

void WorldEncounters::OpenBriefingDialogue(EncounterInstance &enc)
{
    const std::string encId = enc.id;
    Dialogue::Start(
        "enc/brief/" + enc.id,
        {{"b1", enc.npcs.empty() ? "현장 단말" : enc.npcs.front().name, "현장 인카운터 · 1/4단계",
          enc.title + " — " + enc.subtitle,
          {{"accept", "의뢰를 수락하고 즉시 투입한다"}, {"later", "잠시 정비한 뒤 다시 온다"}}}},
        [this, encId](const std::string &choice) {
            auto *live = FindLive(encId);
            if (!live || choice != "accept")
            {
                return;
            }
            state.activeEncounterId = live->id;
            if (live->archetype == Archetype::ConvoyAmbush)
            {
                live->phase = Phase::Stage1;
                SpawnWaveEnemies(*live, 1);
                Game::Get().ShowNotice(live->title + " [1/4단계] 1차 약탈자 매복대를 격퇴하세요!");
            }
            else if (live->archetype == Archetype::OutlawBountyRaid || live->archetype == Archetype::RogueDroneSwarm ||
                     live->archetype == Archetype::RelayDefenseHold)
            {
                live->phase = Phase::Stage2;
                SpawnWaveEnemies(*live, 1);
                if (live->archetype == Archetype::OutlawBountyRaid)
                {
                    Game::Get().ShowNotice(live->title + " [2/4단계] F 스캔으로 은폐된 위장 안테나 2곳을 찾아 파괴하세요!");
                }
                else
                {
                    Game::Get().ShowNotice(live->title + " [2/4단계] 1파 위협을 막으며 현장 목표를 완료하세요!");
                }
            }
            else
            {
                live->phase = Phase::Stage2;
                Game::Get().ShowNotice(live->title + " [2/4단계] 현장의 모든 탐색·조사 목표를 완료하세요!");
            }
            SyncSaveRecords();
        },
        true);
}

void WorldEncounters::OpenStage4ResolutionDialogue(EncounterInstance &enc)
{
    const std::string encId = enc.id;
    std::string prompt = "모든 현장 단계를 마쳤습니다. 최종 처리 방식을 선택하세요.";
    std::vector<DialogueChoice> choices = {{"a", "공식 인도 · 치안/학계 보고 (크레딧·경험치 중심)"},
                                           {"b", "현장 부품 분배 · 고철 회수 (고철·실용 보상 중심)"}};
    if (enc.archetype == Archetype::RelayDefenseHold)
    {
        const bool perfect = enc.beaconHealth >= 60;
        prompt = perfect ? "비컨 내구도 60% 이상 완벽 방어에 성공했습니다! 통신망 보너스를 수령하세요."
                         : "비컨 동기화를 완료했습니다. 현장 방어 보상을 수령하세요.";
        choices = {{"a", "전역 통신망 보급 수령"}, {"b", "예비 부품 · 고철 우선 수령"}};
    }
    Dialogue::Start(
        "enc/resolve/" + enc.id,
        {{"r1", enc.npcs.empty() ? "현장 단말" : enc.npcs.front().name, "현장 인카운터 · 4/4단계 정산", prompt,
          std::move(choices)}},
        [this, encId](const std::string &c) {
            if (auto *live = FindLive(encId))
            {
                GrantReward(*live, c == "b" ? ResolutionChoice::OptionB : ResolutionChoice::OptionA);
            }
        },
        true);
}

void WorldEncounters::GrantReward(EncounterInstance &enc, ResolutionChoice choice)
{
    if (enc.rewardClaimed)
    {
        return;
    }
    enc.rewardClaimed = true;
    enc.choice = choice;
    enc.phase = Phase::Completed;
    ++state.totalCompleted;
    if (state.activeEncounterId == enc.id)
    {
        state.activeEncounterId.clear();
    }
    for (auto &n : enc.nodes)
    {
        n.completed = true;
    }
    for (auto &e : enc.enemies)
    {
        e.active = false;
        e.health = 0;
    }

    auto &player = Game::Get().Player();
    int cr = 260, xp = 130, salvage = 2, kits = 1;
    if (choice == ResolutionChoice::OptionB)
    {
        cr = 185;
        xp = 110;
        salvage = 5;
        kits = 1;
    }
    else if (choice == ResolutionChoice::OptionC)
    {
        cr = 350;
        xp = 95;
        salvage = 4;
        kits = 0;
    }
    else if (enc.archetype == Archetype::RelayDefenseHold && enc.beaconHealth >= 60)
    {
        cr = 360;
        xp = 170;
        salvage = 4;
        kits = 2;
    }
    player.credits = (std::min)(game::PlayerStats::kMaximumCredits, player.credits + cr);
    player.experience += xp;
    player.salvage = (std::min)(9999, player.salvage + salvage);
    player.recoveryKits = (std::min)(999, player.recoveryKits + kits);
    if (choice == ResolutionChoice::OptionA &&
        (enc.archetype == Archetype::OutlawBountyRaid || enc.archetype == Archetype::RogueDroneSwarm ||
         enc.archetype == Archetype::SmugglerStandoff))
    {
        CivicJustice::State().infamy = (std::max)(0.f, CivicJustice::State().infamy - 2.5f);
    }
    if (choice == ResolutionChoice::OptionB && enc.archetype == Archetype::RuinsResonance)
    {
        player.health = player.maxHealth;
    }
    Game::Get().ShowNotice("인카운터 완료 [" + enc.title + "] +" + std::to_string(cr) + " Cr · +" +
                           std::to_string(xp) + " XP · 고철 +" + std::to_string(salvage));
    SyncSaveRecords();
}

bool WorldEncounters::Interact()
{
    const auto &w = Game::Get().GetState();
    for (auto &enc : state.live)
    {
        if (enc.surfaceSpace != w.surfaceExploration || !enc.activeInRange || enc.phase == Phase::Completed)
        {
            continue;
        }
        const auto pOffOpt = PlayerOffsetIn(enc);
        if (!pOffOpt)
        {
            continue;
        }
        for (auto &n : enc.nodes)
        {
            if (!n.revealed || n.completed)
            {
                continue;
            }
            const float dist = Game::Distance(*pOffOpt, n.offset);
            if (dist > n.radius + (enc.surfaceSpace ? 1.8f : 0.9f))
            {
                continue;
            }
            if (n.requiredStage == 0)
            {
                if (enc.phase == Phase::Dormant)
                {
                    OpenBriefingDialogue(enc);
                    return true;
                }
                if (enc.phase == Phase::Stage4)
                {
                    OpenStage4ResolutionDialogue(enc);
                    return true;
                }
                continue;
            }
            if (n.requiredStage == 2 && enc.phase == Phase::Stage2)
            {
                n.completed = true;
                ++enc.stageObjectivesDone;
                if (enc.stageObjectivesDone >= enc.stageObjectivesTotal)
                {
                    enc.phase = Phase::Stage3;
                    if (enc.archetype == Archetype::SmugglerStandoff)
                    {
                        Game::Get().ShowNotice(enc.title + " [3/4단계] 증거 확보 완료! 밀수 조직장과 대면하세요.");
                    }
                    else if (enc.archetype == Archetype::RelayDefenseHold)
                    {
                        Game::Get().ShowNotice(enc.title + " [3/4단계] 비컨 26초 실시간 동기화 시작! 몰려오는 파도를 막으세요!");
                    }
                    else
                    {
                        SpawnWaveEnemies(enc, 2);
                        Game::Get().ShowNotice(enc.title + " [3/4단계] 핵심 위협/보스 출현! 전원을 격퇴하세요!");
                    }
                }
                else
                {
                    Game::Get().ShowNotice(n.label + " 완료 (" + std::to_string(enc.stageObjectivesDone) + "/" +
                                           std::to_string(enc.stageObjectivesTotal) + ")");
                }
                SyncSaveRecords();
                return true;
            }
            if (n.requiredStage == 3 && enc.phase == Phase::Stage3 && enc.archetype == Archetype::SmugglerStandoff)
            {
                const std::string encId = enc.id;
                n.completed = true;
                Dialogue::Start(
                    "enc/smuggler/" + enc.id,
                    {{"sm1", "밀수 조직장", "현장 대면 · 3/4단계",
                      "우리 적하 목록까지 뒤졌군. 여기서 어떻게 끝낼 셈이지?",
                      {{"raid", "강경 압수 — 밀수단을 무력으로 제압한다"},
                       {"negotiate", "증거 제시 협상 — 무혈 해산하고 화물을 넘긴다"},
                       {"deal", "묵인 및 정보 거래 — 암시장 대가를 받고 물러난다"}}}},
                    [this, encId](const std::string &choice) {
                        auto *live = FindLive(encId);
                        if (!live)
                        {
                            return;
                        }
                        if (choice == "raid")
                        {
                            SpawnWaveEnemies(*live, 2);
                            Game::Get().ShowNotice("밀수단 교전 돌입! 밀수 조직장과 경호원을 제압하세요!");
                        }
                        else if (choice == "negotiate")
                        {
                            live->phase = Phase::Stage4;
                            live->choice = ResolutionChoice::OptionB;
                            GrantReward(*live, ResolutionChoice::OptionB);
                        }
                        else
                        {
                            live->phase = Phase::Stage4;
                            live->choice = ResolutionChoice::OptionC;
                            GrantReward(*live, ResolutionChoice::OptionC);
                        }
                    },
                    true);
                return true;
            }
            if (n.requiredStage == 4 && enc.phase == Phase::Stage4)
            {
                OpenStage4ResolutionDialogue(enc);
                return true;
            }
        }
    }
    return false;
}

const EncounterInstance *WorldEncounters::ActiveEncounter() const
{
    if (!state.activeEncounterId.empty())
    {
        for (const auto &enc : state.live)
        {
            if (enc.id == state.activeEncounterId && enc.phase != Phase::Completed)
            {
                return &enc;
            }
        }
    }
    for (const auto &enc : state.live)
    {
        if (enc.activeInRange && enc.phase != Phase::Dormant && enc.phase != Phase::Completed)
        {
            return &enc;
        }
    }
    return nullptr;
}

const EncounterInstance *WorldEncounters::NearestEncounter(float maxMeters) const
{
    const auto &w = Game::Get().GetState();
    const EncounterInstance *best = nullptr;
    float bestDist = maxMeters;
    for (const auto &enc : state.live)
    {
        if (enc.surfaceSpace != w.surfaceExploration || enc.phase == Phase::Completed)
        {
            continue;
        }
        const auto pOff = PlayerOffsetIn(enc);
        if (!pOff)
        {
            continue;
        }
        const float d = std::hypot(pOff->x, pOff->y);
        if (d < bestDist)
        {
            bestDist = d;
            best = &enc;
        }
    }
    return best;
}

std::string WorldEncounters::CurrentObjectiveTitle() const
{
    const auto *enc = ActiveEncounter();
    if (!enc)
    {
        return "";
    }
    return "[인카운터 " + std::to_string(StageNumber(enc->phase)) + "/4] " + enc->title;
}

std::string WorldEncounters::CurrentObjectiveAction() const
{
    const auto *enc = ActiveEncounter();
    if (!enc)
    {
        return "";
    }
    if (enc->phase == Phase::Stage1)
    {
        return "1차 위협 격퇴 (잔여 " + std::to_string(AliveEnemies(*enc, 1)) + "명)";
    }
    if (enc->phase == Phase::Stage2)
    {
        return "현장 목표 완료 (" + std::to_string(enc->stageObjectivesDone) + "/" +
               std::to_string(enc->stageObjectivesTotal) + ") · F 스캔 지원";
    }
    if (enc->phase == Phase::Stage3)
    {
        if (enc->archetype == Archetype::RelayDefenseHold)
        {
            const int rem = (std::max)(0, int(std::ceil(enc->defenseHoldTarget - enc->defenseHoldSeconds)));
            return "임시 비컨 방어 (남은 " + std::to_string(rem) + "초 · 내구도 " + std::to_string(enc->beaconHealth) +
                   "% · 적 " + std::to_string(AliveEnemies(*enc)) + ")";
        }
        if (enc->archetype == Archetype::SmugglerStandoff && !enc->wave2Spawned)
        {
            return "밀수 조직장 대면 · 압수 / 협상 / 거래 선택 [E]";
        }
        const int alive = AliveEnemies(*enc);
        if (alive > 0)
        {
            return "핵심 위협/보스 격퇴 (잔여 " + std::to_string(alive) + "명)";
        }
        return "생존 인원 안전 지점 호위 진행 중";
    }
    if (enc->phase == Phase::Stage4)
    {
        return "의뢰인/단말에서 최종 분기 정산 [E]";
    }
    return enc->subtitle;
}

std::vector<std::string> WorldEncounters::JournalLines() const
{
    std::vector<std::string> lines;
    lines.push_back("완료한 현장 인카운터: " + std::to_string(state.totalCompleted) + "건");
    for (const auto &enc : state.live)
    {
        if (enc.phase == Phase::Dormant)
        {
            continue;
        }
        lines.push_back(enc.title + " — " +
                        (enc.phase == Phase::Completed ? "완료"
                                                       : ("진행 " + std::to_string(StageNumber(enc.phase)) + "/4단계")));
    }
    return lines;
}
} // namespace encounters
