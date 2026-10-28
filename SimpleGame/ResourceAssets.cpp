#include "stdafx.h"
#include "ResourceAssets.h"
#include "Profiler.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace models
{
namespace
{
static_assert(sizeof(ResourceVertex) == 21 * sizeof(float), "Resource vertex layout changed");
static_assert(sizeof(Point) == 3 * sizeof(float), "Resource point layout changed");
class Reader
{
  public:
    explicit Reader(const std::filesystem::path &path) : file(path, std::ios::binary | std::ios::ate)
    {
        // Detailed meshes plus all authored human clips can exceed the old 128 MiB cache limit.
        if (!file || file.tellg() < 12 || file.tellg() > 512 * 1024 * 1024)
        {
            throw std::runtime_error("Resource pack missing/invalid at " + path.u8string() +
                                     "; rebuild or run node tools/ensure-resources.mjs");
        }
        remaining = static_cast<size_t>(file.tellg());
        file.seekg(0);
    }
    template <typename T> void Read(T *destination, size_t count = 1)
    {
        static_assert(std::is_trivially_copyable<T>::value, "Binary field must be POD");
        if (count > remaining / sizeof(T))
        {
            throw std::runtime_error("Truncated resource pack");
        }
        const size_t bytes = count * sizeof(T);
        file.read(reinterpret_cast<char *>(destination), static_cast<std::streamsize>(bytes));
        if (!file)
        {
            throw std::runtime_error("Cannot read resource pack");
        }
        remaining -= bytes;
    }
    unsigned int Count(unsigned int maximum)
    {
        unsigned int value;
        Read(&value);
        if (value > maximum)
        {
            throw std::runtime_error("Resource pack field exceeds limit");
        }
        return value;
    }
    std::string String()
    {
        std::string text(Count(256), '\0');
        Read(text.data(), text.size());
        return text;
    }
    size_t remaining = 0;

  private:
    std::ifstream file;
};

std::filesystem::path FindPack()
{
    // Installed output wins; ancestor discovery also supports Visual Studio and CPU test executables.
    wchar_t path[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    auto directory = length && length < 32768 ? std::filesystem::path(path).parent_path()
                                              : std::filesystem::current_path();
    for (int depth = 0; depth < 10; ++depth)
    {
        auto candidate = directory / L"Resource" / L"Runtime" / L"game-assets.bin";
        if (std::filesystem::is_regular_file(candidate))
        {
            return candidate;
        }
        if (directory == directory.parent_path())
        {
            break;
        }
        directory = directory.parent_path();
    }
    return std::filesystem::current_path() / L"Resource" / L"Runtime" / L"game-assets.bin";
}
struct Store
{
    std::vector<ResourceMesh> meshes;
    std::vector<std::unique_ptr<ResourceMesh>> assemblies;
    std::map<std::string, unsigned int> names;
    Store()
    {
        profiling::Scope timer(profiling::Timer::DiskCacheRead);
        const auto packPath = FindPack();
        Reader reader(packPath);
        unsigned int magic, version;
        reader.Read(&magic);
        reader.Read(&version);
        if (magic != 0x41524d45 || version != 3)
        {
            throw std::runtime_error(
                "Unsupported resource pack version: rebuild or run node tools/ensure-resources.mjs");
        }
        const auto count = reader.Count(512);
        const auto bankCount = reader.Count(512);
        struct Bank
        {
            unsigned bones;
            std::shared_ptr<const std::vector<ResourceClip>> clips;
            std::shared_ptr<const AnimationRig> rig;
        };
        std::vector<Bank> banks;
        size_t animationClips = 0;
        for (unsigned bank = 0; bank < bankCount; ++bank)
        {
            unsigned bones = reader.Count(31), clipCount = reader.Count(128);
            if (!bones || !clipCount)
            {
                throw std::runtime_error("Invalid resource animation bank");
            }
            auto rig = std::make_shared<AnimationRig>();
            for (unsigned j = 0; j < bones; ++j)
            {
                rig->names.push_back(reader.String());
                int parent = int(reader.Count(bones)) - 1;
                if (parent == int(j) || (bones == 23 && parent >= int(j)))
                {
                    throw std::runtime_error("Invalid animation hierarchy");
                }
                rig->parents.push_back(parent);
                AnimationMatrix bind{};
                reader.Read(bind.data(), bind.size());
                if (std::any_of(bind.begin(), bind.end(), [](float value) { return !std::isfinite(value); }))
                {
                    throw std::runtime_error("Invalid animation bind transform");
                }
                rig->bind.push_back(bind);
                rig->inverseBind.push_back(AnimationMath::Inverse(bind));
                if (rig->names.back() == "muzzle")
                {
                    rig->muzzle = int(j);
                }
            }
            unsigned sampleCount = reader.Count(128);
            for (unsigned j = 0; j < sampleCount; ++j)
            {
                AnimationSample sample;
                sample.clip = reader.String();
                reader.Read(&sample.left);
                reader.Read(&sample.forward);
                if (!std::isfinite(sample.left) || !std::isfinite(sample.forward))
                {
                    throw std::runtime_error("Invalid animation blend sample");
                }
                rig->samples.push_back(std::move(sample));
            }
            unsigned triangleCount = reader.Count(512);
            for (unsigned j = 0; j < triangleCount; ++j)
            {
                std::array<unsigned, 3> triangle{};
                for (auto &index : triangle)
                {
                    index = reader.Count(sampleCount);
                    if (index >= sampleCount)
                    {
                        throw std::runtime_error("Invalid blend triangle");
                    }
                }
                rig->triangles.push_back(triangle);
            }
            unsigned soleCount = reader.Count(16);
            for (unsigned j = 0; j < soleCount; ++j)
            {
                SolePoint sole;
                sole.bone = reader.Count(bones - 1);
                reader.Read(&sole.position);
                rig->soles.push_back(sole);
            }
            auto clips = std::make_shared<std::vector<ResourceClip>>();
            clips->reserve(clipCount);
            for (unsigned index = 0; index < clipCount; ++index)
            {
                ResourceClip clip;
                clip.name = reader.String();
                clip.frames = reader.Count(10000);
                reader.Read(&clip.duration);
                if (clip.frames < 2 || !std::isfinite(clip.duration) || clip.duration <= 0)
                {
                    throw std::runtime_error("Invalid resource animation");
                }
                clip.matrices.resize(size_t(clip.frames) * bones * 16);
                reader.Read(clip.matrices.data(), clip.matrices.size());
                clip.muzzles.resize(clip.frames);
                reader.Read(clip.muzzles.data(), clip.muzzles.size());
                if (std::any_of(clip.matrices.begin(), clip.matrices.end(),
                                [](float v) { return !std::isfinite(v); }))
                {
                    throw std::runtime_error("Invalid resource skin palette");
                }
                clips->push_back(std::move(clip));
                const auto &name = clips->back().name;
                int clipIndex = int(index);
                if (name == "Idle")
                {
                    rig->idle = clipIndex;
                }
                if (name == "Walk")
                {
                    rig->walk = clipIndex;
                }
                if (name == "Run")
                {
                    rig->run = clipIndex;
                }
                if (name == "Aim_Base")
                {
                    rig->aim = clipIndex;
                }
                if (name == "Attack_Fire")
                {
                    rig->fire = clipIndex;
                }
                if (name == "Attack_Melee")
                {
                    rig->melee = clipIndex;
                }
                if (name == "Pistol_Carry_Idle")
                {
                    rig->carryIdle = clipIndex;
                }
                if (name == "Pistol_Carry_Walk")
                {
                    rig->carryWalk = clipIndex;
                }
                if (name == "Pistol_Carry_Run")
                {
                    rig->carryRun = clipIndex;
                }
                if (name == "Reload_Pistol")
                {
                    rig->reload = clipIndex;
                }
                for (auto &sample : rig->samples)
                {
                    if (sample.clip == name)
                    {
                        sample.clipIndex = clipIndex;
                    }
                }
                ++animationClips;
            }
            for (const auto &sample : rig->samples)
            {
                if (sample.clipIndex < 0)
                {
                    throw std::runtime_error("Missing animation blend clip");
                }
            }
            if (bones == 23 && (rig->idle < 0 || rig->walk < 0 || rig->run < 0 || rig->aim < 0 ||
                                rig->fire < 0 || rig->melee < 0 || rig->muzzle < 0))
            {
                throw std::runtime_error("Missing required character animation");
            }
            if (bones == 23)
            {
                LayeredAnimation::PrepareGraph(*clips, *rig);
            }
            banks.push_back({bones, std::move(clips), std::move(rig)});
        }
        meshes.reserve(count);
        size_t vertices = 0;
        for (unsigned int index = 0; index < count; ++index)
        {
            meshes.emplace_back();
            auto &mesh = meshes.back();
            mesh.name = reader.String();
            mesh.source = reader.String();
            if (!names.emplace(mesh.name, index + 1).second)
            {
                throw std::runtime_error("Duplicate resource asset name");
            }
            auto vertexCount = reader.Count(2000000);
            mesh.boneCount = reader.Count(31);
            auto bank = reader.Count(bankCount);
            if (bank)
            {
                if (banks[bank - 1].bones != mesh.boneCount)
                {
                    throw std::runtime_error("Animation bank skeleton mismatch");
                }
                mesh.animations = banks[bank - 1].clips;
                mesh.rig = banks[bank - 1].rig;
            }
            reader.Read(&mesh.minimum);
            reader.Read(&mesh.maximum);
            reader.Read(&mesh.physicalSize);
            reader.Read(mesh.sockets.data(), mesh.sockets.size());
            if (vertexCount == 0 || vertexCount % 3)
            {
                throw std::runtime_error("Resource asset is not a triangle list");
            }
            mesh.vertices.resize(vertexCount);
            reader.Read(mesh.vertices.data(), vertexCount);
            for (const auto &vertex : mesh.vertices)
            {
                // Validate each named field without reading past a scalar object's bounds.
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) ||
                    !std::isfinite(vertex.nx) || !std::isfinite(vertex.ny) || !std::isfinite(vertex.nz))
                {
                    throw std::runtime_error("Nonfinite resource vertex");
                }
                float sum = 0;
                for (size_t j = 0; j < 4; ++j)
                {
                    const float joint = vertex.joints[j], weight = vertex.weights[j];
                    if (!std::isfinite(joint) || joint < 0 || joint > 31 || joint != std::floor(joint) ||
                        (joint >= mesh.boneCount && joint != 31) || !std::isfinite(weight) || weight < 0)
                    {
                        throw std::runtime_error("Invalid resource skin binding");
                    }
                    sum += weight;
                }
                if (std::fabs(sum - 1) > .001f)
                {
                    throw std::runtime_error("Unnormalized resource skin");
                }
            }
            vertices += vertexCount;
        }
        if (reader.remaining)
        {
            throw std::runtime_error("Unexpected trailing resource pack data");
        }
        profiling::Count(profiling::Counter::DiskCacheHits);
        std::printf("{\"event\":\"resource_pack_loaded\",\"asset_count\":%zu,\"triangles\":%zu,\"skin_"
                    "palette_limit\":32,\"animation_clips\":%zu,\"pack_path\":\"%s\"}\n",
                    meshes.size(), vertices / 3, animationClips, packPath.generic_u8string().c_str());
    }
};
Store &Assets()
{
    static const auto store = std::make_unique<Store>();
    return *store;
}
struct Selection
{
    const ResourceClip *clip = nullptr;
    unsigned int frame = 0;
};
Selection Select(const ResourceMesh &mesh, const CharacterPose *pose, bool upper = false)
{
    const bool aircraft = mesh.name.find("aircraft/") == 0;
    std::string name = aircraft                          ? "Aircraft_Reference"
                       : mesh.name.find("vehicle/") == 0 ? "Hover_Idle"
                                                         : "Idle";
    float time = pose ? pose->animationPhase / 10 : 0;
    if (pose)
    {
        if (aircraft || mesh.name.find("vehicle/") == 0)
        {
            name = pose->walking ? "Cruise" : aircraft ? "Aircraft_Reference" : "Hover_Idle";
        }
        else if (pose->riding)
        {
            name = "Ride";
        }
        else if (upper && (pose->armed || pose->meleeSeconds > 0))
        {
            name = pose->meleeSeconds > 0 ? "Attack_Melee" : pose->recoil > 0 ? "Attack_Fire" : "Aim_Base";
            time = pose->meleeSeconds > 0 ? (1 - std::clamp(pose->meleeSeconds / .65f, 0.f, 1.f)) * .65f
                   : pose->recoil > 0     ? (1 - std::clamp(pose->recoil, 0.f, 1.f)) * .4f
                                          : 0;
        }
        else if (pose->walking)
        {
            const bool creature = mesh.name.find("creature/") == 0;
            const bool walk = !creature && pose->locomotionSpeed < .9f;
            name = walk ? "Walk" : "Run";
            const float referenceSpeed =
                creature ? (mesh.name.find("RelaySentinel") != std::string::npos ? 2.1f : 1.98f)
                : walk   ? 1.08f
                         : 2.46f;
            time *= pose->locomotionSpeed * WorldScale::metersPerUnit / referenceSpeed;
        }
    }
    for (const auto &clip : mesh.Clips())
    {
        if (clip.name == name)
        {
            const bool looping =
                name == "Run" || name == "Walk" || name == "Idle" || name == "Cruise" || name == "Hover_Idle";
            // Play the whole strike/recovery within the gameplay cooldown, for every clip duration.
            if (name == "Attack_Melee" && pose)
            {
                time = (1 - std::clamp(pose->meleeSeconds / .65f, 0.f, 1.f)) * clip.duration;
            }
            float t = looping ? std::fmod((std::max)(0.f, time), clip.duration)
                              : std::clamp(time, 0.f, clip.duration);
            return {&clip,
                    (std::min)(clip.frames - 1, unsigned(t / clip.duration * (clip.frames - 1) + .5f))};
        }
    }
    return {};
}
} // namespace

unsigned int ResourceAssets::Find(const std::string &name)
{
    const auto &names = Assets().names;
    auto found = names.find(name);
    if (found == names.end())
    {
        throw std::runtime_error("Resource mapping missing: " + name);
    }
    return found->second;
}
const ResourceMesh &ResourceAssets::Get(unsigned int id)
{
    auto &store = Assets();
    if (!id || id > store.meshes.size() + store.assemblies.size())
    {
        throw std::out_of_range("Invalid resource model id");
    }
    return id <= store.meshes.size() ? store.meshes[id - 1] : *store.assemblies[id - store.meshes.size() - 1];
}
unsigned int ResourceAssets::RegisterStatic(ResourceMesh mesh)
{
    auto &store = Assets();
    auto found = store.names.find(mesh.name);
    if (found != store.names.end())
    {
        return found->second;
    }
    mesh.boneCount = 0;
    mesh.animations.reset();
    mesh.minimum = {1.e9f, 1.e9f, 1.e9f};
    mesh.maximum = {-1.e9f, -1.e9f, -1.e9f};
    for (auto &v : mesh.vertices)
    {
        mesh.minimum.x = (std::min)(mesh.minimum.x, v.x);
        mesh.maximum.x = (std::max)(mesh.maximum.x, v.x);
        mesh.minimum.y = (std::min)(mesh.minimum.y, v.y);
        mesh.maximum.y = (std::max)(mesh.maximum.y, v.y);
        mesh.minimum.z = (std::min)(mesh.minimum.z, v.z);
        mesh.maximum.z = (std::max)(mesh.maximum.z, v.z);
        v.joints = {31, 31, 31, 31};
        v.weights = {1, 0, 0, 0};
    }
    mesh.physicalSize = {mesh.maximum.x - mesh.minimum.x, mesh.maximum.y - mesh.minimum.y,
                         mesh.maximum.z - mesh.minimum.z};
    unsigned int id = unsigned(store.meshes.size() + store.assemblies.size() + 1);
    store.names[mesh.name] = id;
    store.assemblies.push_back(std::make_unique<ResourceMesh>(std::move(mesh)));
    return id;
}

void ResourceAssets::Bind(Model &model, const std::string &name)
{
    model.resourceId = Find(name);
    model.space = CoordinateSpace::World;
    model.count = 0;
}
void ResourceAssets::Sample(unsigned int id, const CharacterPose *pose, SkinMatrices &matrices)
{
    matrices.fill(0);
    for (size_t joint = 0; joint < 32; ++joint)
    {
        for (size_t axis = 0; axis < 4; ++axis)
        {
            matrices[joint * 16 + axis * 5] = 1;
        }
    }
    const auto &mesh = Get(id);
    if (pose && mesh.boneCount == 23 && mesh.rig && !pose->riding)
    {
        LayeredAnimation::Sample(mesh, *pose, matrices);
        return;
    }
    auto base = Select(mesh, pose);
    if (!base.clip)
    {
        return;
    }
    const size_t size = mesh.boneCount * 16;
    std::copy_n(base.clip->matrices.data() + size * base.frame, size, matrices.begin());
}
Point ResourceAssets::Muzzle(const CharacterPose &pose)
{
    auto id = Find(pose.gender == Gender::Female ? "character/Female/Explorer" : "character/Male/Explorer");
    const auto &mesh = Get(id);
    if (mesh.rig && mesh.rig->muzzle >= 0 && !pose.riding)
    {
        SkinMatrices matrices;
        Sample(id, &pose, matrices);
        AnimationMatrix skin;
        std::copy_n(matrices.begin() + mesh.rig->muzzle * 16, 16, skin.begin());
        auto world = AnimationMath::Multiply(skin, mesh.rig->bind[size_t(mesh.rig->muzzle)]);
        return {world[12], world[13], world[14]};
    }
    auto selection = Select(Get(id), &pose, true);
    return selection.clip ? selection.clip->muzzles[selection.frame] : Point{.5f, 0, .8f};
}
void ResourcePoseSampler::Sample(unsigned int id, const CharacterPose &pose, SkinMatrices &matrices)
{
    const auto &mesh = ResourceAssets::Get(id);
    if (mesh.boneCount != 23 || !mesh.rig || pose.riding)
    {
        ResourceAssets::Sample(id, &pose, matrices);
        return;
    }
    const std::array<float, 13> nextInputs = {
        pose.animationPhase,  pose.recoil,         pose.locomotionSpeed, pose.meleeSeconds,
        pose.locomotionPhase, pose.moveLeft,       pose.moveForward,     pose.aimWeight,
        pose.fireSeconds,     pose.reloadProgress, float(pose.gender),   pose.motionForwardLean,
        pose.motionSideLean};
    const unsigned int nextFlags =
        unsigned(pose.armed) | (unsigned(pose.walking) << 1) | (unsigned(pose.blended) << 2);
    if (rig != mesh.rig.get() || animations != mesh.animations.get() || flags != nextFlags ||
        inputs != nextInputs)
    {
        ResourceAssets::Sample(id, &pose, cached);
        rig = mesh.rig.get();
        animations = mesh.animations.get();
        flags = nextFlags;
        inputs = nextInputs;
    }
    matrices = cached;
}

Point ResourceAssets::SkinPoint(const ResourceVertex &vertex, const SkinMatrices &matrices)
{
    Point result = {};
    for (size_t i = 0; i < 4; ++i)
    {
        const float *m = matrices.data() + size_t(vertex.joints[i]) * 16;
        float w = vertex.weights[i];
        result.x += (m[0] * vertex.x + m[4] * vertex.y + m[8] * vertex.z + m[12]) * w;
        result.y += (m[1] * vertex.x + m[5] * vertex.y + m[9] * vertex.z + m[13]) * w;
        result.z += (m[2] * vertex.x + m[6] * vertex.y + m[10] * vertex.z + m[14]) * w;
    }
    return result;
}
} // namespace models
