#include "stdafx.h"
#include "ModelCatalog.h"
#include "../ModelLibrary.h"
#include "../ModelVariants.h"
#include "../AssetCache.h"
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace monitoring
{
namespace
{
void PointJson(std::ostream &out, models::Point point)
{
    out << '[' << point.x << ',' << point.y << ',' << point.z << ']';
}
void ModelJson(std::ostream &out, const models::Model &model, const char *category, const char *name,
               size_t type, const models::VehicleModel *vehicle = nullptr)
{
    out << "{\"category\":\"" << category << "\",\"name\":\"" << name << "\",\"type\":" << type
        << ",\"space\":\"" << (model.space == models::CoordinateSpace::World ? "world" : "screen")
        << "\",\"validated\":" << (models::ValidateModel(model) ? "true" : "false") << ",\"parts\":[";
    for (size_t i = 0; i < model.count; ++i)
    {
        const auto &part = model.parts[i];
        out << (i ? "," : "") << "{\"kind\":" << unsigned(part.kind)
            << ",\"palette\":" << unsigned(part.palette) << ",\"width\":" << part.width << ",\"color\":[";
        for (size_t j = 0; j < 4; ++j)
        {
            out << (j ? "," : "") << part.color[j];
        }
        out << "],\"points\":[";
        for (size_t j = 0; j < part.count; ++j)
        {
            if (j)
            {
                out << ',';
            }
            PointJson(out, part.points[j]);
        }
        out << "]}";
    }
    out << ']';
    if (vehicle)
    {
        out << ",\"exhausts\":[";
        for (size_t i = 0; i < 2; ++i)
        {
            if (i)
            {
                out << ',';
            }
            PointJson(out, vehicle->exhausts[i]);
        }
        out << "],\"headlights\":[";
        for (size_t i = 0; i < 2; ++i)
        {
            if (i)
            {
                out << ',';
            }
            PointJson(out, vehicle->headlights[i]);
        }
        out << ']';
    }
    out << '}';
}
} // namespace
std::string BuildModelCatalog()
{
    const auto &library = models::Get();
    constexpr const char *kVehicleNames[] = {"쿠페", "여객 셔틀", "화물 수송차", "순찰차",
                                             "개인용 제트 오토바이"};
    constexpr const char *kCharacterNames[] = {"탐험가", "정비사", "경비원", "의무관", "연구원"};
    constexpr const char *kBuildingNames[] = {"발코니 주거동", "계단식 테라스", "박공 작업장", "돔 시설",
                                              "관문 전망대"};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(5) << "{\"schema\":\"game_models_v1\",\"models\":[";
    bool first = true;
    for (size_t i = 0; i < library.vehicles.size(); ++i)
    {
        if (!first)
        {
            out << ',';
        }
        first = false;
        ModelJson(out, library.vehicles[i].body, "vehicle", kVehicleNames[i], i, &library.vehicles[i]);
    }
    for (size_t i = 0; i < library.characters.size(); ++i)
    {
        out << ',';
        ModelJson(out, library.characters[i], "character", kCharacterNames[i], i);
    }
    for (size_t i = 0; i < library.buildings.size(); ++i)
    {
        out << ',';
        ModelJson(out, library.buildings[i], "building", kBuildingNames[i], i);
    }
    out << "],\"character_base\":";
    constexpr int atlasWidth = 384, atlasHeight = 256, frameWidth = 48, frameHeight = 64;
    std::vector<unsigned char> pixels;
    if (assetcache::Load("characters.rgba", 1, atlasWidth * atlasHeight * 4, pixels))
    {
        out << "{\"width\":48,\"height\":64,\"rgba\":[";
        bool firstPixel = true;
        for (int y = 0; y < frameHeight; ++y)
        {
            for (int x = 0; x < frameWidth; ++x)
            {
                size_t offset = (y * atlasWidth + x) * 4;
                for (int c = 0; c < 4; ++c)
                {
                    out << (firstPixel ? "" : ",") << unsigned(pixels[offset + c]);
                    firstPixel = false;
                }
            }
        }
        out << "]}";
    }
    else
    {
        out << "null";
    }
    out << '}';
    return out.str();
}
} // namespace monitoring
