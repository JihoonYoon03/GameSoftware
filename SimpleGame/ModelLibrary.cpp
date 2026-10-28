#include "stdafx.h"
#include "ModelLibrary.h"
#include "ResourceAssets.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <stdexcept>
#include <cstdio>
#include <memory>
#include <vector>
#include <map>
#include <utility>
#include <iterator>

namespace models
{

// ============================================================================
// Joint hierarchy
// ============================================================================
namespace
{
constexpr Joint kParents[] = {Joint::Root, Joint::Root,    Joint::Root,        Joint::RightArm,
                              Joint::Root, Joint::LeftArm, Joint::Root,        Joint::RightLeg,
                              Joint::Root, Joint::LeftLeg, Joint::RightForearm};
} // namespace

// ============================================================================
// Transform3D / ModelRig — CPU skinning of rigid limb meshes
// ============================================================================
Point Transform3D::Apply(Point p) const
{
    const float cr = std::cos(roll), sr = std::sin(roll), cp = std::cos(pitch), sp = std::sin(pitch),
                cy = std::cos(yaw), sy = std::sin(yaw);
    p = {p.x, p.y * cr - p.z * sr, p.y * sr + p.z * cr};
    p = {p.x * cp + p.z * sp, p.y, -p.x * sp + p.z * cp};
    return {p.x * cy - p.y * sy + position.x, p.x * sy + p.y * cy + position.y, p.z + position.z};
}
Point CharacterPose::Apply(Point p, Joint joint) const
{
    if (joint == Joint::Weapon)
    {
        // The wrist stabilizes the barrel parallel to the aim plane. Its position
        // comes from the full shoulder -> elbow -> hand chain, including recoil.
        Point wrist = Apply({0, 0, -.27f}, Joint::RightForearm);
        return {p.x + wrist.x, p.y + wrist.y, p.z + wrist.z};
    }
    for (;;)
    {
        p = joints[size_t(joint)].Apply(p);
        if (joint == Joint::Root)
        {
            return p;
        }
        joint = kParents[size_t(joint)];
    }
}
CharacterPose ModelRig::Pose(float phase, bool walking, bool armed, float recoil, bool riding, Gender gender)
{
    CharacterPose p{};
    p.gender = gender;
    p.armed = armed;
    p.walking = walking;
    p.riding = riding;
    p.animationPhase = phase;
    p.recoil = recoil;
    auto &j = p.joints;
    const float shoulder = gender == Gender::Female ? .18f : .225f,
                hip = gender == Gender::Female ? .125f : .105f;
    float step = walking ? std::sin(phase) * .48f : 0;
    j[size_t(Joint::Root)].position.z = walking && !armed ? std::fabs(std::sin(phase)) * .025f : 0;
    j[size_t(Joint::Head)].position = {0, 0, 1.04f};
    j[size_t(Joint::RightArm)].position = {0, -shoulder, .94f};
    j[size_t(Joint::LeftArm)].position = {0, shoulder, .94f};
    j[size_t(Joint::RightForearm)].position = {0, 0, -.29f};
    j[size_t(Joint::LeftForearm)].position = {0, 0, -.29f};
    j[size_t(Joint::RightLeg)].position = {0, -hip, .57f};
    j[size_t(Joint::LeftLeg)].position = {0, hip, .57f};
    j[size_t(Joint::RightShin)].position = {0, 0, -.26f};
    j[size_t(Joint::LeftShin)].position = {0, 0, -.26f};
    j[size_t(Joint::RightLeg)].pitch = step;
    j[size_t(Joint::LeftLeg)].pitch = -step;
    j[size_t(Joint::RightShin)].pitch = (std::max)(0.f, -step) * .6f;
    j[size_t(Joint::LeftShin)].pitch = (std::max)(0.f, step) * .6f;
    j[size_t(Joint::RightArm)].pitch = -step * .65f;
    j[size_t(Joint::LeftArm)].pitch = step * .65f;
    if (armed || riding)
    {
        // Two-bone IK: both hands meet the grip/handlebar while sleeves remain
        // on their own bones, outside the torso and below the face.
        float back = (std::max)(0.f, (std::min)(1.f, recoil)) * .045f;
        for (int side = 0; side < 2; ++side)
        {
            Joint arm = side ? Joint::LeftArm : Joint::RightArm;
            Joint fore = side ? Joint::LeftForearm : Joint::RightForearm;
            auto &a = j[size_t(arm)];
            auto &f = j[size_t(fore)];
            Point target = {riding ? .43f : .36f - back, riding ? (side ? .18f : -.18f) : 0.f,
                            riding ? .80f : 26.f / 44};
            float dx = target.x - a.position.x, dy = target.y - a.position.y, dz = target.z - a.position.z;
            float r = std::sqrt(dx * dx + dy * dy), d = std::sqrt(r * r + dz * dz);
            d = (std::min)(.559f, (std::max)(.03f, d));
            float theta = std::atan2(dz, r);
            float bend = std::acos(
                (std::max)(-1.f, (std::min)(1.f, (.29f * .29f + d * d - .27f * .27f) / (.58f * d))));
            float upper = theta - bend;
            float lower = std::atan2(dz - .29f * std::sin(upper), r - .29f * std::cos(upper));
            a.yaw = std::atan2(dy, dx);
            a.pitch = -1.5707963f - upper;
            f.pitch = upper - lower;
        }
    }
    if (riding)
    {
        j[size_t(Joint::Root)].position.z = -.40f;
        j[size_t(Joint::RightLeg)].pitch = -1.1f;
        j[size_t(Joint::LeftLeg)].pitch = -1.1f;
        j[size_t(Joint::RightShin)].pitch = 1.5f;
        j[size_t(Joint::LeftShin)].pitch = 1.5f;
    }
    return p;
}
Point ModelRig::Muzzle(const CharacterPose &pose)
{
    return ResourceAssets::Muzzle(pose);
}

// ============================================================================
// ModelFactory — validate imported bindings and the remaining effect meshes
// ============================================================================
bool ModelFactory::ValidateModel(const Model &model)
{
    if (model.resourceId)
    {
        const auto &mesh = ResourceAssets::Get(model.resourceId);
        return model.space == CoordinateSpace::World && !mesh.vertices.empty() &&
               mesh.vertices.size() % 3 == 0;
    }
    if (unsigned(model.space) > unsigned(CoordinateSpace::World))
    {
        return false;
    }
    if (model.count == 0 || model.count > model.parts.size())
    {
        return false;
    }
    for (size_t i = 0; i < model.count; ++i)
    {
        const auto &part = model.parts[i];
        if (unsigned(part.joint) >= unsigned(Joint::Count) || part.count > 8 || part.count < 2 ||
            unsigned(part.kind) > 3 || unsigned(part.palette) > 3 || !std::isfinite(part.width) ||
            part.width <= 0 || unsigned(part.surface) > unsigned(Surface::Skin))
        {
            return false;
        }
        if ((part.kind == PartKind::Polygon && part.count < 3) ||
            (part.kind == PartKind::Glass && part.count != 4) ||
            ((part.kind == PartKind::Line || part.kind == PartKind::Ellipse) && part.count != 2))
        {
            return false;
        }
        for (float color : part.color)
        {
            if (!std::isfinite(color) || color < 0 || color > 1)
            {
                return false;
            }
        }
        for (size_t j = 0; j < part.count; ++j)
        {
            auto p = part.points[j];
            auto n = part.normals[j];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(n.x) ||
                !std::isfinite(n.y) || !std::isfinite(n.z))
            {
                return false;
            }
        }
        if (part.kind == PartKind::Ellipse && (part.points[1].x <= 0 || part.points[1].y <= 0))
        {
            return false;
        }
        if (part.kind == PartKind::Polygon || part.kind == PartKind::Glass)
        {

            Point normal = {};
            for (size_t j = 0; j < part.count; ++j)
            {
                auto a = part.points[j], b = part.points[(j + 1) % part.count];
                normal.x += (a.y - b.y) * (a.z + b.z);
                normal.y += (a.z - b.z) * (a.x + b.x);
                normal.z += (a.x - b.x) * (a.y + b.y);
            }
            float n = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
            if (n < 1.e-14f)
            {
                return false;
            }
            for (size_t j = 0; j < part.count; ++j)
            {
                auto a = part.points[j], b = part.points[(j + 1) % part.count],
                     c = part.points[(j + 2) % part.count];
                Point u = {b.x - a.x, b.y - a.y, b.z - a.z}, v = {c.x - b.x, c.y - b.y, c.z - b.z};
                Point cross = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
                if (cross.x * normal.x + cross.y * normal.y + cross.z * normal.z < -1.e-8f)
                {
                    return false;
                }
            }
        }
    }
    return true;
}
// ============================================================================
// ModelLibrary
// ============================================================================
namespace
{

void Generate(Library &library)
{
    library.cube = {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
    for (size_t i = 0; i < library.circle.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 48;
        library.circle[i] = {std::cos(angle), std::sin(angle), 0};
    }
    for (size_t i = 0; i < library.softCircle.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 64;
        library.softCircle[i] = {std::cos(angle), std::sin(angle), 0};
    }
    for (size_t i = 0; i < library.gateRing.size(); ++i)
    {
        float angle = float(i) * 6.2831853f / 96;
        library.gateRing[i] = {std::cos(angle), std::sin(angle), 0};
    }
    constexpr const char *vehicles[] = {"Coupe", "Shuttle", "Hauler", "Patrol", "JetBike"};
    for (size_t i = 0; i < library.vehicles.size(); ++i)
    {
        auto &vehicle = library.vehicles[i];
        ResourceAssets::Bind(vehicle.body, std::string("vehicle/") + vehicles[i]);
        const auto &resource = ResourceAssets::Get(vehicle.body.resourceId);
        std::copy_n(resource.sockets.begin(), 2, vehicle.exhausts.begin());
        std::copy_n(resource.sockets.begin() + 2, 2, vehicle.headlights.begin());
        vehicle.seat = resource.sockets[4];
    }
    constexpr const char *roles[] = {"Explorer", "Engineer",    "Guard", "Medic",   "Researcher",
                                     "Miner",    "HeavyCombat", "Pilot", "Civilian"};
    for (size_t i = 0; i < library.characters.size(); ++i)
    {
        ResourceAssets::Bind(library.characters[i], std::string("character/Male/") + roles[i]);
        ResourceAssets::Bind(library.femaleCharacters[i], std::string("character/Female/") + roles[i]);
    }
    constexpr const char *hairs[] = {"Buzz", "SidePart", "Curls", "Bob", "Ponytail", "Bun"};
    for (size_t i = 0; i < library.hairs.size(); ++i)
    {
        ResourceAssets::Bind(library.hairs[i], std::string("hair/") + hairs[i]);
        ResourceAssets::Bind(library.coveredHairs[i], std::string("hair/covered/") + hairs[i]);
    }
    constexpr const char *buildings[] = {"Habitat",       "Terraced",        "Workshop",   "Dome",
                                         "Observatory",   "SpireHelix",      "TwinBridge", "ArcologyTerrace",
                                         "TransitAtrium", "CourtyardHabitat"};
    static_assert(std::size(buildings) == size_t(BuildingType::Count));
    for (size_t i = 0; i < library.buildings.size(); ++i)
    {
        ResourceAssets::Bind(library.buildings[i], std::string("building/") + buildings[i]);
    }
    constexpr const char *creatures[] = {"RelaySentinel", "RelaySentinel_Variant",
                                         "RiftGrazer",    "RiftGrazer_Variant",
                                         "VeilWalker",    "VeilWalker_Variant"};
    for (size_t i = 0; i < library.creatures.size(); ++i)
    {
        ResourceAssets::Bind(library.creatures[i], std::string("creature/") + creatures[i]);
    }
    ResourceAssets::Bind(library.drone, "creature/RelaySentinel");
    const std::pair<Model *, const char *> props[] = {{&library.box, "box"},
                                                      {&library.salvage, "salvage"},
                                                      {&library.recoveryKit, "recoveryKit"},
                                                      {&library.rock, nullptr},
                                                      {&library.telescope, "telescope"},
                                                      {&library.console, "console"},
                                                      {&library.bulkhead, "bulkhead"},
                                                      {&library.trafficGate, "trafficGate"},
                                                      {&library.bed, "bed"},
                                                      {&library.chair, "chair"},
                                                      {&library.table, "table"},
                                                      {&library.lamp, "lamp"},
                                                      {&library.generator, "generator"},
                                                      {&library.breaker, "breaker"},
                                                      {&library.beacon, "beacon"},
                                                      {&library.relief, "relief"},
                                                      {&library.wall, "wall"},
                                                      {&library.door, "door"},
                                                      {&library.lantern, "lantern"},
                                                      {&library.support, "support"}};
    for (auto prop : props)
    {
        ResourceAssets::Bind(*prop.first, prop.second ? std::string("prop/") + prop.second : "nature/rock");
    }
    constexpr const char *grounds[] = {"concrete", "asphalt", "metal", "glass"};
    for (size_t i = 0; i < library.grounds.size(); ++i)
    {
        ResourceAssets::Bind(library.grounds[i], std::string("ground/") + grounds[i]);
    }
    ResourceAssets::Bind(library.mineGround, "ground/mine");
    ResourceAssets::Bind(library.outdoorGrounds[0], "ground/grass");
    ResourceAssets::Bind(library.outdoorGrounds[1], "ground/sand");
    ResourceAssets::Bind(library.tree, "nature/tree");
    ResourceAssets::Bind(library.fungus, "nature/fungus");
    ResourceAssets::Bind(library.crystal, "nature/crystal");
    ResourceAssets::Bind(library.reeds, "nature/reeds");
    constexpr const char *aircraft[] = {"Kite_Courier", "Manta_AirTaxi", "Mule_Skycrane", "Warden_Survey"};
    for (size_t i = 0; i < library.aircraft.size(); ++i)
    {
        ResourceAssets::Bind(library.aircraft[i],
                             std::string("aircraft/") + aircraft[i / 2] + (i % 2 ? "_Variant" : ""));
    }
    for (size_t i = 0; i < library.scenery.size(); ++i)
    {
        auto &model = library.scenery[i];
        auto type = static_cast<SceneryType>(i);
        ResourceAssets::Bind(model, std::string("scenery/") + ModelLibrary::SceneryName(type));
        model.ground = type <= SceneryType::SpeedTable || type == SceneryType::PedestrianHologram ||
                       type == SceneryType::LandingPad;
    }
    ResourceAssets::Bind(library.signalStates[0], "scenery/City_TrafficSignal_Amber");
    ResourceAssets::Bind(library.signalStates[1], "scenery/City_TrafficSignal_Green");
}

bool Valid(const Library &library)
{
    for (const auto &model : library.aircraft)
    {
        if (!ModelFactory::ValidateModel(model))
        {
            return false;
        }
    }
    for (const auto &model : library.scenery)
    {
        if (!ModelFactory::ValidateModel(model))
        {
            return false;
        }
    }
    for (const auto &model : library.signalStates)
    {
        if (!ModelFactory::ValidateModel(model))
        {
            return false;
        }
    }
    for (size_t i = 0; i < library.hairs.size(); ++i)
    {
        if (!ModelFactory::ValidateModel(library.hairs[i]) ||
            !ModelFactory::ValidateModel(library.coveredHairs[i]))
        {
            return false;
        }
    }

    for (const auto &vehicle : library.vehicles)
    {
        if (!ModelFactory::ValidateModel(vehicle.body))
        {
            return false;
        }
        for (size_t i = 0; i < 2; ++i)
        {
            if (!std::isfinite(vehicle.exhausts[i].x) || !std::isfinite(vehicle.exhausts[i].y) ||
                !std::isfinite(vehicle.exhausts[i].z) || !std::isfinite(vehicle.headlights[i].x) ||
                !std::isfinite(vehicle.headlights[i].y) || !std::isfinite(vehicle.headlights[i].z) ||
                vehicle.exhausts[i].x >= 0 || vehicle.headlights[i].x <= 0)
            {
                return false;
            }
        }
    }
    for (size_t i = 0; i < library.characters.size(); ++i)
    {
        if (!ModelFactory::ValidateModel(library.characters[i]) ||
            !ModelFactory::ValidateModel(library.femaleCharacters[i]))
        {
            return false;
        }
    }
    for (const auto &building : library.buildings)
    {
        if (!ModelFactory::ValidateModel(building))
        {
            return false;
        }
    }
    auto finitePoints = [](const auto &points) {
        for (const Point &point : points)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            {
                return false;
            }
        }
        return true;
    };
    if (!finitePoints(library.circle) || !finitePoints(library.softCircle) || !finitePoints(library.cube) ||
        !finitePoints(library.gateRing))
    {
        return false;
    }
    for (const Model *model :
         {&library.box, &library.drone, &library.salvage, &library.recoveryKit, &library.rock,
          &library.telescope, &library.console, &library.bulkhead, &library.trafficGate})
    {
        if (!ModelFactory::ValidateModel(*model))
        {
            return false;
        }
    }
    for (const auto &ground : library.grounds)
    {
        if (!ModelFactory::ValidateModel(ground))
        {
            return false;
        }
    }
    return true;
}

std::unique_ptr<Library> LoadOrGenerate()
{
    auto library = std::make_unique<Library>();
    Generate(*library);
    if (!Valid(*library))
    {
        throw std::runtime_error("Resource model mappings failed validation");
    }
    return library;
}
} // namespace

const Library &ModelLibrary::Get()
{
    static const auto library = LoadOrGenerate();
    return *library;
}
const Model &ModelLibrary::Character(CharacterType type, Gender gender)
{
    const auto &library = Get();
    return gender == Gender::Female ? library.femaleCharacters.at(size_t(type))
                                    : library.characters.at(size_t(type));
}
const Model &ModelLibrary::Hair(HairStyle style, bool covered)
{
    return covered ? Get().coveredHairs.at(size_t(style)) : Get().hairs.at(size_t(style));
}
const Model &ModelLibrary::Aircraft(AircraftType type, bool alternate)
{
    return Get().aircraft.at(size_t(type) * 2 + (alternate ? 1 : 0));
}
const Model &ModelLibrary::Scenery(SceneryType type)
{
    return Get().scenery.at(size_t(type));
}
const Model &ModelLibrary::TrafficSignal(unsigned int state)
{
    return state == 0 ? Scenery(SceneryType::TrafficSignal) : Get().signalStates.at(state - 1);
}
const Model &ModelLibrary::Transit(const std::string &name)
{
    // Stable heap addresses are the mesh-cache identity; no large Model values on the stack.
    static std::map<std::string, std::unique_ptr<Model>> models;
    auto &model = models[name];
    if (!model)
    {
        model = std::make_unique<Model>();
        ResourceAssets::Bind(*model, "transit/" + name);
        // Elevated slabs, rails and piers cast real shadows; flat road paint/paving do not.
        model->ground = name.find("Deck") == std::string::npos && name.find("Ramp_2L") == std::string::npos &&
                        name.find("Pier_") == std::string::npos;
    }
    return *model;
}
const char *ModelLibrary::SceneryName(SceneryType type)
{
    constexpr const char *names[] = {"Road_2Lane_4m",         "Road_4Lane_4m",
                                     "Road_6Lane_4m",         "Road_Crossroad_12m",
                                     "Road_TJunction_12m",    "Road_Curve90_R9m",
                                     "City_Sidewalk_2x4m",    "Road_SpeedTable_4m",
                                     "Road_CrashBarrier_4m",  "City_Streetlight_4m",
                                     "City_TrafficSignal",    "City_PedestrianHologram_6m",
                                     "City_Bollard",          "City_Bench",
                                     "City_Planter",          "City_TransitShelter",
                                     "City_Wayfinding",       "City_ChargePost",
                                     "Utility_Transformer",   "Utility_CargoContainer_6m",
                                     "Utility_HVAC",          "Utility_PipeRack_4m",
                                     "Utility_CommRelay",     "Utility_SolarArray",
                                     "Utility_WaterTank",     "Airfield_LandingPad_16m",
                                     "Airfield_Beacon",       "Frontier_SurveyTripod",
                                     "Frontier_FieldShelter", "Frontier_MiningConveyor",
                                     "Ruin_ObserverMonolith", "Ruin_FracturedArch",
                                     "Nature_PrismBloom",     "Nature_SailTree"};
    static_assert(std::size(names) == size_t(SceneryType::Count));
    return names[size_t(type)];
}
} // namespace models
