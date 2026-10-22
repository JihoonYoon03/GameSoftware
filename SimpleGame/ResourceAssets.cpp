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
        if (!file || file.tellg() < 12 || file.tellg() > 128 * 1024 * 1024)
        {
            throw std::runtime_error("Resource pack missing/invalid: run tools/pack-resources.mjs");
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
    std::map<std::string, unsigned int> names;
    Store()
    {
        profiling::Scope timer(profiling::Timer::DiskCacheRead);
        Reader reader(FindPack());
        unsigned int magic, version;
        reader.Read(&magic);
        reader.Read(&version);
        if (magic != 0x41524d45 || version != 1)
        {
            throw std::runtime_error("Unsupported resource pack version");
        }
        const auto count = reader.Count(512);
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
            auto clipCount = reader.Count(32);
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
            for (unsigned int clipIndex = 0; clipIndex < clipCount; ++clipIndex)
            {
                ResourceClip clip;
                clip.name = reader.String();
                clip.frames = reader.Count(10000);
                reader.Read(&clip.duration);
                if (clip.frames < 2 || !std::isfinite(clip.duration) || clip.duration <= 0 || !mesh.boneCount)
                {
                    throw std::runtime_error("Invalid resource animation");
                }
                clip.matrices.resize(size_t(clip.frames) * mesh.boneCount * 16);
                reader.Read(clip.matrices.data(), clip.matrices.size());
                clip.muzzles.resize(clip.frames);
                reader.Read(clip.muzzles.data(), clip.muzzles.size());
                if (std::any_of(clip.matrices.begin(), clip.matrices.end(),
                                [](float v) { return !std::isfinite(v); }))
                {
                    throw std::runtime_error("Invalid resource skin palette");
                }
                mesh.clips.push_back(std::move(clip));
            }
            vertices += vertexCount;
        }
        if (reader.remaining)
        {
            throw std::runtime_error("Unexpected trailing resource pack data");
        }
        profiling::Count(profiling::Counter::DiskCacheHits);
        std::printf("{\"event\":\"resource_pack_loaded\",\"asset_count\":%zu,\"triangles\":%zu,\"skin_"
                    "palette_limit\":32}\n",
                    meshes.size(), vertices / 3);
    }
};
const Store &Assets()
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
        else if (upper && pose->armed)
        {
            name = pose->recoil > 0 ? "Attack_Fire" : "Aim_Base";
            time = pose->recoil > 0 ? (1 - std::clamp(pose->recoil, 0.f, 1.f)) * .4f : 0;
        }
        else if (pose->walking)
        {
            name = "Run";
            const bool creature = mesh.name.find("creature/") == 0;
            const float referenceSpeed =
                creature ? (mesh.name.find("RelaySentinel") != std::string::npos ? 2.1f : 1.98f) : 2.46f;
            time *= pose->locomotionSpeed * WorldScale::metersPerUnit / referenceSpeed;
        }
    }
    for (const auto &clip : mesh.clips)
    {
        if (clip.name == name)
        {
            const bool looping =
                name == "Run" || name == "Walk" || name == "Idle" || name == "Cruise" || name == "Hover_Idle";
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
    if (!id || id > Assets().meshes.size())
    {
        throw std::out_of_range("Invalid resource model id");
    }
    return Assets().meshes[id - 1];
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
    auto base = Select(mesh, pose);
    if (!base.clip)
    {
        return;
    }
    const size_t size = mesh.boneCount * 16;
    std::copy_n(base.clip->matrices.data() + size * base.frame, size, matrices.begin());
    // HUMAN_7H skin order: root, pelvis, spine/chest/arms/sockets, then six leg bones.
    if (pose && pose->armed && !pose->riding && mesh.boneCount == 23)
    {
        auto upper = Select(mesh, pose, true);
        if (upper.clip)
        {
            const float *source = upper.clip->matrices.data() + size * upper.frame;
            // Rebase the upper-body override onto the moving pelvis instead of detaching the waist.
            std::array<float, 16> inversePelvis = {}, correction = {};
            inversePelvis[15] = 1;
            for (size_t c = 0; c < 3; ++c)
            {
                for (size_t r = 0; r < 3; ++r)
                {
                    inversePelvis[c * 4 + r] = source[16 + r * 4 + c];
                }
            }
            for (size_t r = 0; r < 3; ++r)
            {
                inversePelvis[12 + r] = -(inversePelvis[r] * source[28] + inversePelvis[4 + r] * source[29] +
                                          inversePelvis[8 + r] * source[30]);
            }
            auto multiply = [](const float *a, const float *b, float *out) {
                for (size_t c = 0; c < 4; ++c)
                {
                    for (size_t r = 0; r < 4; ++r)
                    {
                        out[c * 4 + r] = 0;
                        for (size_t k = 0; k < 4; ++k)
                        {
                            out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
                        }
                    }
                }
            };
            multiply(matrices.data() + 16, inversePelvis.data(), correction.data());
            for (size_t joint = 2; joint < 17; ++joint)
            {
                multiply(correction.data(), source + joint * 16, matrices.data() + joint * 16);
            }
        }
    }
}
Point ResourceAssets::Muzzle(const CharacterPose &pose)
{
    auto id = Find("character/Male/Explorer");
    auto selection = Select(Get(id), &pose, true);
    return selection.clip ? selection.clip->muzzles[selection.frame] : Point{.5f, 0, .8f};
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
