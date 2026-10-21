#include "stdafx.h"
#include "Overworld.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>

namespace tutorial
{
const std::array<CityInfo, Overworld::kCityCount> &Overworld::Cities()
{
    static const std::array<CityInfo, kCityCount> cities = {
        {{"halo_07", "헤일로", {0, 0}, "익숙한 일상 속 불안", "관문에서 발신자 없는 응답이 들린다."},
         {"cinder", "신더", {256, 0}, "분주한 공업 도시", "광산 교대조의 출근 기록만 남아 있다."},
         {"veil", "베일", {512, 0}, "고요한 관측 도시", "어젯밤 별자리 하나가 사라졌다."},
         {"lumen", "루멘", {512, 256}, "빛으로 버티는 피난 도시", "꺼진 등 아래에서는 이름을 부르지 마라."},
         {"echo", "에코", {256, 256}, "끊어진 통신을 기다리는 도시", "수신기에서 내일의 목소리가 들린다."}}};
    return cities;
}

int Overworld::CityAt(Vector2 position)
{
    for (int i = 0; i < kCityCount; ++i)
    {
        if (Game::Distance(position, Cities()[i].center) <= 30)
        {
            return i;
        }
    }
    return -1;
}

bool Overworld::IsRoad(int x, int y)
{
    for (const auto &city : Cities())
    {
        int dx = x - int(city.center.x), dy = y - int(city.center.y);
        if (std::abs(dx) <= 24 && std::abs(dy) <= 24)
        {
            // Preserve the authored Halo footprints; arterial roads widen outside town.
            return (dy >= -1 && dy <= 0) || (dx >= -1 && dx <= 2) || dy == 7 || dx == -10;
        }
    }
    // Two-way arterial roads form loops; no city requires a scripted transition.
    bool arterial = (x >= -28 && x <= 540 && y >= -2 && y <= 2) ||
                    (x >= 228 && x <= 540 && y >= 254 && y <= 258) ||
                    (((x >= 254 && x <= 258) || (x >= 510 && x <= 514)) && y >= -28 && y <= 284);
    bool mineAccess = x >= 126 && x <= 130 && y >= 0 && y <= 20;
    if (arterial || mineAccess)
    {
        return true;
    }
    return false;
}

models::Palette Overworld::BikePalette()
{
    models::Palette palette;
    palette.body = {.21f, .32f, .39f, 1};
    palette.accent = {.91f, .63f, .27f, 1};
    palette.emission = {.28f, .86f, 1, 1};
    return palette;
}

void Overworld::Populate(GameState &state)
{
    // Preserve each GLB's footprint/aspect instead of stretching imported buildings into old pillars.
    const auto &library = models::ModelLibrary::Get();
    for (auto &building : state.buildings)
    {
        const auto &resource =
            models::ResourceAssets::Get(library.buildings[size_t(building.type)].resourceId);
        building.width = resource.physicalSize.x;
        building.depth = resource.physicalSize.y;
        // Building vertices use nominal height; antennae may extend above normalized z=1.
        building.height = resource.physicalSize.z / resource.maximum.z * 44.f;
    }
    // Keep the authored Halo landmarks; duplicate only its architectural vocabulary.
    const auto haloBuildings = state.buildings;
    for (int cityIndex = 1; cityIndex < kCityCount; ++cityIndex)
    {
        const auto center = Cities()[cityIndex].center;
        for (auto building : haloBuildings)
        {
            if (building.type == models::BuildingType::Observatory)
            {
                continue;
            }
            building.x += center.x;
            building.y += center.y;
            std::rotate(building.palette.body.begin(), building.palette.body.begin() + cityIndex % 3,
                        building.palette.body.begin() + 3);
            state.buildings.push_back(building);
        }
    }
    // Reserve the quest landmark before nearby blocks: its new footprint must not be discarded.
    std::stable_partition(state.buildings.begin(), state.buildings.end(), [](const Building &building) {
        return building.type == models::BuildingType::Observatory;
    });
    CityLayout::ValidateBuildings(state.buildings);
    // Lia + Mara + eighteen residents = twenty Halo citizens. Kain replaces a reserved citizen.
    constexpr Vector2 positions[] = {{-5, 3}, {-1, 3},   {5, 3},    {8, -3},  {-11, 3}, {10, 6},  {-8, 8},
                                     {2, 8},  {-10, -7}, {11, -3},  {11, -5}, {-12, 7}, {-6, -7}, {-2, -7},
                                     {4, 8},  {8, 8},    {-13, -3}, {12, 8},  {-3, 3},  {0, 3}};
    constexpr const char *roles[] = {"배송 기사", "교대 근무자", "정비공", "경비원",    "학생",
                                     "상인",      "주민",        "관측원", "통신 기사", "탐사원"};
    constexpr models::CharacterType appearances[] = {
        models::CharacterType::Pilot,    models::CharacterType::Civilian,   models::CharacterType::Engineer,
        models::CharacterType::Guard,    models::CharacterType::Civilian,   models::CharacterType::Civilian,
        models::CharacterType::Civilian, models::CharacterType::Researcher, models::CharacterType::Engineer,
        models::CharacterType::Explorer};
    state.residents.clear();
    for (int cityIndex = 0; cityIndex < kCityCount; ++cityIndex)
    {
        const auto &city = Cities()[cityIndex];
        int count = cityIndex == 0 ? kPopulationPerCity - 2 : kPopulationPerCity;
        for (int i = 0; i < count; ++i)
        {
            Vector2 position = {city.center.x + positions[i].x, city.center.y + positions[i].y};
            // Place every resident on a genuinely reachable outdoor footprint.
            auto blocked = [&state](Vector2 p) {
                bool building =
                    std::any_of(state.buildings.begin(), state.buildings.end(), [p](const Building &b) {
                        return p.x > b.x - .3f && p.x < b.x + b.width + .3f && p.y > b.y - .3f &&
                               p.y < b.y + b.depth + .3f;
                    });
                return building ||
                       std::any_of(state.residents.begin(), state.residents.end(),
                                   [p](const Resident &r) { return Game::Distance(p, r.position) < .8f; });
            };
            for (int attempt = 0; attempt < 200 && blocked(position); ++attempt)
            {
                position = {city.center.x - 24 + float((attempt + i * 7) % 49),
                            city.center.y + 3.5f + float(attempt / 49) * 1.2f};
            }
            Resident resident;
            resident.position = position;
            resident.name = std::string(city.name) + " " + roles[i % 10] + " " + std::to_string(i + 1);
            resident.dialogue = resident.name + " / " + city.rumor;
            resident.type = appearances[i % 10];
            resident.coat = {.3f + .08f * (i % 4), .36f + .06f * (cityIndex % 4), .46f + .04f * (i % 5), 1};
            resident.gender = i % 2 ? models::Gender::Female : models::Gender::Male;
            resident.hair = models::HairStyle(i % int(models::HairStyle::Count));
            resident.cityIndex = cityIndex;
            state.residents.push_back(resident);
        }
    }
}

void Overworld::UpdateBike(GameState &state, float deltaSeconds)
{
    float steering = float(state.pressedKeys['d']) - float(state.pressedKeys['a']);
    state.bikeYaw = std::remainder(state.bikeYaw + steering * 1.8f * deltaSeconds, 2 * kPi);
    bool accelerating = state.pressedKeys['w'] && !state.pressedKeys['s'];
    float targetSpeed = accelerating ? kBikeCruiseSpeed : 0;
    float acceleration = accelerating ? 32.f : 48.f;
    state.bikeSpeed +=
        Game::Clamp(targetSpeed - state.bikeSpeed, -acceleration * deltaSeconds, acceleration * deltaSeconds);
    // Substeps prevent tunnelling through a narrow building at maximum speed.
    int steps = (std::max)(1, int(std::ceil(state.bikeSpeed * deltaSeconds / .15f)));
    for (int step = 0; step < steps; ++step)
    {
        Vector2 next = {
            state.playerPosition.x + std::cos(state.bikeYaw) * state.bikeSpeed * deltaSeconds / steps,
            state.playerPosition.y + std::sin(state.bikeYaw) * state.bikeSpeed * deltaSeconds / steps};
        bool blocked = false;
        for (float dx : {-.45f, .45f})
        {
            for (float dy : {-.45f, .45f})
            {
                blocked = blocked || Game::Get().IsPositionBlocked({next.x + dx, next.y + dy});
            }
        }
        if (blocked)
        {
            state.bikeSpeed = 0;
            break;
        }
        state.playerPosition = next;
    }
    state.bikePosition = state.playerPosition;
    state.facingYaw = state.bikeYaw;
    state.movementAmount = state.bikeSpeed / kBikeCruiseSpeed;
    state.movementSpeed = state.bikeSpeed;
}

void Overworld::ToggleBike(GameState &state)
{
    if (state.isBikeMounted)
    {
        if (state.bikeSpeed > 1)
        {
            Game::Get().ShowNotice("바이크 / 방향키를 놓고 정차한 뒤 E로 내리세요.");
            return;
        }
        for (Vector2 offset : {Vector2{1.2f, 0}, {-1.2f, 0}, {0, 1.2f}, {0, -1.2f}})
        {
            Vector2 position = {state.bikePosition.x + offset.x, state.bikePosition.y + offset.y};
            if (!Game::Get().IsPositionBlocked(position))
            {
                state.playerPosition = position;
                state.isBikeMounted = false;
                state.bikeSpeed = 0;
                Game::Get().ShowNotice("바이크 / 하차했습니다. 주차 위치는 유지됩니다.");
                return;
            }
        }
        Game::Get().ShowNotice("바이크 / 내릴 공간이 부족합니다. 넓은 곳으로 이동하세요.");
        return;
    }
    state.isBikeMounted = true;
    state.playerPosition = state.bikePosition;
    state.bikeSpeed = 0;
    Game::Get().ShowNotice(
        "바이크 / W 가속 · S 제동 · A/D 조향 · 키를 놓으면 감속 · 정차 후 E 하차. 도시 간 약 20초.");
}
} // namespace tutorial
