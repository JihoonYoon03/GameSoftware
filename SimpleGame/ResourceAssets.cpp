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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace models
{
namespace
{
static_assert(sizeof(PackedResourceVertex) == 36, "PackedResourceVertex layout must be 36 bytes");
static_assert(std::is_trivially_copyable<PackedResourceVertex>::value, "PackedResourceVertex must be POD");
static_assert(sizeof(Point) == 3 * sizeof(float), "Resource point layout changed");

std::uint16_t FloatToHalf(float value)
{
    if (!std::isfinite(value))
    {
        return 0;
    }
    value = std::clamp(value, -65504.f, 65504.f);
    std::uint32_t x = 0;
    std::memcpy(&x, &value, sizeof(x));
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    const int exp = int((x >> 23) & 0xffu) - 127 + 15;
    std::uint32_t mant = x & 0x007fffffu;
    if (exp <= 0)
    {
        if (exp < -10)
        {
            return std::uint16_t(sign);
        }
        mant = (mant | 0x00800000u) >> (1 - exp);
        return std::uint16_t(sign | ((mant + 0x00001000u) >> 13));
    }
    if (exp >= 31)
    {
        return std::uint16_t(sign | 0x7bffu);
    }
    return std::uint16_t(sign | (std::uint32_t(exp) << 10) | ((mant + 0x00001000u) >> 13));
}
float HalfToFloat(std::uint16_t h)
{
    const std::uint32_t sign = std::uint32_t(h & 0x8000u) << 16;
    const std::uint32_t exp = (h >> 10) & 0x1fu;
    std::uint32_t mant = h & 0x03ffu;
    std::uint32_t bits = 0;
    if (exp == 0)
    {
        if (mant == 0)
        {
            bits = sign;
        }
        else
        {
            int e = -14;
            while ((mant & 0x0400u) == 0)
            {
                mant <<= 1;
                --e;
            }
            mant &= 0x03ffu;
            bits = sign | (std::uint32_t(e + 127) << 23) | (mant << 13);
        }
    }
    else if (exp == 31)
    {
        bits = sign | 0x7f800000u | (mant << 13);
    }
    else
    {
        bits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float out = 0;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}
std::array<std::int16_t, 2> EncodeOctNormal(float nx, float ny, float nz)
{
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    float x = len > 1.e-10f ? nx / len : 0.f;
    float y = len > 1.e-10f ? ny / len : 0.f;
    float z = len > 1.e-10f ? nz / len : 1.f;
    const float invL1 = 1.f / (std::fabs(x) + std::fabs(y) + std::fabs(z));
    x *= invL1;
    y *= invL1;
    if (z < 0.f)
    {
        const float ox = (1.f - std::fabs(y)) * (x >= 0.f ? 1.f : -1.f);
        const float oy = (1.f - std::fabs(x)) * (y >= 0.f ? 1.f : -1.f);
        x = ox;
        y = oy;
    }
    return {std::int16_t(std::clamp(int(std::lround(x * 32767.f)), -32767, 32767)),
            std::int16_t(std::clamp(int(std::lround(y * 32767.f)), -32767, 32767))};
}
Point DecodeOctNormal(std::int16_t ix, std::int16_t iy)
{
    float x = (std::max)(-1.f, float(ix) / 32767.f);
    float y = (std::max)(-1.f, float(iy) / 32767.f);
    float z = 1.f - std::fabs(x) - std::fabs(y);
    if (z < 0.f)
    {
        const float ox = (1.f - std::fabs(y)) * (x >= 0.f ? 1.f : -1.f);
        const float oy = (1.f - std::fabs(x)) * (y >= 0.f ? 1.f : -1.f);
        x = ox;
        y = oy;
    }
    const float len = std::sqrt(x * x + y * y + z * z);
    return len > 1.e-8f ? Point{x / len, y / len, z / len} : Point{0, 0, 1};
}
struct PackedVertexKey
{
    PackedResourceVertex v;
    bool operator==(const PackedVertexKey &o) const noexcept
    {
        return std::memcmp(&v, &o.v, sizeof(v)) == 0;
    }
};
struct PackedVertexKeyHash
{
    size_t operator()(const PackedVertexKey &k) const noexcept
    {
        return std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char *>(&k.v), sizeof(k.v)));
    }
};
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
        if (magic != 0x41524d45 || version != 4)
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
            unsigned bones = reader.Count(kMaxSkinBones - 1), clipCount = reader.Count(256);
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
                if (rig->names.back() == "hand.R")
                {
                    rig->handR = int(j);
                }
            }
            LayeredAnimation::ClassifyRig(*rig);
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
                LayeredAnimation::BindDodgeClip(*rig, name, clipIndex);
                LayeredAnimation::BindSupportClip(*rig, name, clipIndex);
                LayeredAnimation::BindTransitionClip(*rig, name, clipIndex);
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
            if (bones == 23 || rig->preset == RigPreset::Mixamo)
            {
                LayeredAnimation::PrepareGraph(*clips, *rig);
            }
            banks.push_back({bones, std::move(clips), std::move(rig)});
        }
        meshes.reserve(count);
        size_t lod0Triangles = 0;
        size_t uniqueVerticesTotal = 0;
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
            const auto uniqueVertexCount = reader.Count(2000000);
            const auto totalIndexCount = reader.Count(6000000);
            const auto indexBytes = reader.Count(4);
            mesh.boneCount = reader.Count(kMaxSkinBones - 1);
            const auto bank = reader.Count(bankCount);
            if (uniqueVertexCount == 0 || totalIndexCount == 0 || totalIndexCount % 3 != 0 ||
                (indexBytes != 2 && indexBytes != 4) ||
                (indexBytes == 2 && uniqueVertexCount > 65535))
            {
                throw std::runtime_error("Invalid v4 indexed mesh header");
            }
            mesh.indexBytes = std::uint8_t(indexBytes);
            std::array<std::uint32_t, 4> offsets{}, counts{};
            reader.Read(offsets.data(), offsets.size());
            reader.Read(counts.data(), counts.size());
            for (size_t k = 0; k < 4; ++k)
            {
                if (counts[k] == 0 || counts[k] % 3 != 0 ||
                    size_t(offsets[k]) + size_t(counts[k]) > totalIndexCount)
                {
                    throw std::runtime_error("Invalid v4 LOD submesh range");
                }
                mesh.lods[k] = {offsets[k], counts[k]};
            }
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
            mesh.packedVertices.resize(uniqueVertexCount);
            reader.Read(mesh.packedVertices.data(), uniqueVertexCount);
            for (const auto &vertex : mesh.packedVertices)
            {
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
                {
                    throw std::runtime_error("Nonfinite resource vertex");
                }
                unsigned sum = 0;
                for (size_t j = 0; j < 4; ++j)
                {
                    const unsigned joint = vertex.joints[j];
                    if (joint >= kMaxSkinBones || (joint >= mesh.boneCount && joint != 31 && joint != kMaxSkinBones - 1))
                    {
                        throw std::runtime_error("Invalid resource skin binding");
                    }
                    sum += vertex.weights[j];
                }
                if (sum != 255)
                {
                    throw std::runtime_error("Unnormalized resource skin");
                }
            }
            if (indexBytes == 2)
            {
                mesh.indices16.resize(totalIndexCount);
                reader.Read(mesh.indices16.data(), totalIndexCount);
                for (std::uint16_t idx : mesh.indices16)
                {
                    if (idx >= uniqueVertexCount)
                    {
                        throw std::runtime_error("Resource index out of range");
                    }
                }
            }
            else
            {
                mesh.indices32.resize(totalIndexCount);
                reader.Read(mesh.indices32.data(), totalIndexCount);
                for (std::uint32_t idx : mesh.indices32)
                {
                    if (idx >= uniqueVertexCount)
                    {
                        throw std::runtime_error("Resource index out of range");
                    }
                }
            }
            lod0Triangles += mesh.lods[0].indexCount / 3;
            uniqueVerticesTotal += uniqueVertexCount;
        }
        if (reader.remaining)
        {
            throw std::runtime_error("Unexpected trailing resource pack data");
        }
        profiling::Count(profiling::Counter::DiskCacheHits);
        std::printf("{\"event\":\"resource_pack_loaded\",\"schema\":\"v4\",\"asset_count\":%zu,"
                    "\"triangles\":%zu,\"unique_vertices\":%zu,\"skin_palette_limit\":%u,"
                    "\"animation_clips\":%zu,\"pack_path\":\"%s\"}\n",
                    meshes.size(), lod0Triangles, uniqueVerticesTotal, kMaxSkinBones, animationClips,
                    packPath.generic_u8string().c_str());
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
    const auto id = TryFind(name);
    if (!id)
    {
        throw std::runtime_error("Resource mapping missing: " + name);
    }
    return id;
}
unsigned int ResourceAssets::TryFind(const std::string &name)
{
    const auto &names = Assets().names;
    auto found = names.find(name);
    return found == names.end() ? 0u : found->second;
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
size_t ResourceAssets::PackMeshCount()
{
    return Assets().meshes.size();
}
PackedResourceVertex ResourceAssets::PackVertex(const ResourceVertex &vertex)
{
    PackedResourceVertex p{};
    p.x = vertex.x;
    p.y = vertex.y;
    p.z = vertex.z;
    const auto oct = EncodeOctNormal(vertex.nx, vertex.ny, vertex.nz);
    p.nx = oct[0];
    p.ny = oct[1];
    p.u = FloatToHalf(vertex.u);
    p.v = FloatToHalf(vertex.v);
    auto u8Norm = [](float c) { return std::uint8_t(std::clamp(int(std::lround(c * 255.f)), 0, 255)); };
    p.r = u8Norm(vertex.r);
    p.g = u8Norm(vertex.g);
    p.b = u8Norm(vertex.b);
    p.a = u8Norm(vertex.a);
    p.palette = std::uint8_t(std::clamp(int(std::lround(vertex.palette)), 0, 3));
    p.visibility = std::uint8_t(std::clamp(int(std::lround(vertex.visibility)), 0, 4));
    p.surface = std::uint8_t(std::clamp(int(std::lround(vertex.surface)), 0, 15));
    p.flags = vertex.hasUv ? 1u : 0u;
    std::array<float, 4> raw{};
    std::array<int, 4> q{};
    int sum = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        p.joints[i] = std::uint8_t(std::clamp(int(std::lround(vertex.joints[i])), 0, int(kMaxSkinBones - 1)));
        raw[i] = (std::max)(0.f, vertex.weights[i]) * 255.f;
        q[i] = int(std::floor(raw[i]));
        sum += q[i];
    }
    int rem = 255 - sum;
    std::array<size_t, 4> order = {0, 1, 2, 3};
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return (raw[a] - q[a]) > (raw[b] - q[b]); });
    for (size_t i = 0; i < 4 && rem > 0; ++i)
    {
        if (q[order[i]] < 255)
        {
            ++q[order[i]];
            --rem;
        }
    }
    for (int i = 3; i >= 0 && rem < 0; --i)
    {
        if (q[order[size_t(i)]] > 0)
        {
            --q[order[size_t(i)]];
            ++rem;
        }
    }
    for (size_t i = 0; i < 4; ++i)
    {
        p.weights[i] = std::uint8_t(q[i]);
    }
    return p;
}
ResourceVertex ResourceAssets::UnpackVertex(const PackedResourceVertex &p)
{
    const auto n = DecodeOctNormal(p.nx, p.ny);
    ResourceVertex v{};
    v.x = p.x;
    v.y = p.y;
    v.z = p.z;
    v.nx = n.x;
    v.ny = n.y;
    v.nz = n.z;
    v.r = float(p.r) / 255.f;
    v.g = float(p.g) / 255.f;
    v.b = float(p.b) / 255.f;
    v.a = float(p.a) / 255.f;
    v.palette = float(p.palette);
    v.visibility = float(p.visibility);
    v.surface = float(p.surface);
    for (size_t i = 0; i < 4; ++i)
    {
        v.joints[i] = float(p.joints[i]);
        v.weights[i] = float(p.weights[i]) / 255.f;
    }
    v.u = HalfToFloat(p.u);
    v.v = HalfToFloat(p.v);
    v.hasUv = (p.flags & 1u) != 0;
    return v;
}
ResourceVertex ResourceAssets::TriangleVertex(const ResourceMesh &mesh, size_t lodVertexIndex, LodLevel lod)
{
    if (!mesh.packedVertices.empty())
    {
        const auto &range = mesh.lods[size_t(lod)];
        return UnpackVertex(mesh.packedVertices[mesh.IndexAt(size_t(range.firstIndex) + lodVertexIndex)]);
    }
    return mesh.vertices[lodVertexIndex];
}
std::vector<ResourceVertex> ResourceAssets::UnpackLod0Triangles(const ResourceMesh &mesh)
{
    if (mesh.packedVertices.empty())
    {
        return mesh.vertices;
    }
    const auto &range = mesh.lods[0];
    std::vector<ResourceVertex> result;
    result.reserve(range.indexCount);
    for (size_t i = 0; i < range.indexCount; ++i)
    {
        result.push_back(UnpackVertex(mesh.packedVertices[mesh.IndexAt(size_t(range.firstIndex) + i)]));
    }
    return result;
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
    mesh.rig.reset();
    if (!mesh.vertices.empty())
    {
        mesh.packedVertices.clear();
        mesh.indices16.clear();
        mesh.indices32.clear();
        std::unordered_map<PackedVertexKey, std::uint32_t, PackedVertexKeyHash> dedup;
        dedup.reserve(mesh.vertices.size());
        std::vector<std::uint32_t> indices;
        indices.reserve(mesh.vertices.size());
        for (auto &v : mesh.vertices)
        {
            v.joints = {31, 31, 31, 31};
            v.weights = {1, 0, 0, 0};
            const auto packed = PackVertex(v);
            auto it = dedup.find({packed});
            if (it == dedup.end())
            {
                const auto idx = std::uint32_t(mesh.packedVertices.size());
                dedup.emplace(PackedVertexKey{packed}, idx);
                mesh.packedVertices.push_back(packed);
                indices.push_back(idx);
            }
            else
            {
                indices.push_back(it->second);
            }
        }
        if (mesh.packedVertices.size() <= 65535)
        {
            mesh.indexBytes = 2;
            mesh.indices16.resize(indices.size());
            for (size_t i = 0; i < indices.size(); ++i)
            {
                mesh.indices16[i] = std::uint16_t(indices[i]);
            }
        }
        else
        {
            mesh.indexBytes = 4;
            mesh.indices32 = std::move(indices);
        }
        const SubmeshRange full{0, std::uint32_t(mesh.vertices.size())};
        mesh.lods = {full, full, full, full};
        mesh.vertices.clear();
        mesh.vertices.shrink_to_fit();
    }
    mesh.minimum = {1.e9f, 1.e9f, 1.e9f};
    mesh.maximum = {-1.e9f, -1.e9f, -1.e9f};
    for (auto &v : mesh.packedVertices)
    {
        mesh.minimum.x = (std::min)(mesh.minimum.x, v.x);
        mesh.maximum.x = (std::max)(mesh.maximum.x, v.x);
        mesh.minimum.y = (std::min)(mesh.minimum.y, v.y);
        mesh.maximum.y = (std::max)(mesh.maximum.y, v.y);
        mesh.minimum.z = (std::min)(mesh.minimum.z, v.z);
        mesh.maximum.z = (std::max)(mesh.maximum.z, v.z);
        v.joints = {31, 31, 31, 31};
        v.weights = {255, 0, 0, 0};
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
bool ResourceAssets::BindOptional(Model &model, const std::string &name)
{
    const auto id = TryFind(name);
    if (!id)
    {
        model.resourceId = 0;
        return false;
    }
    model.resourceId = id;
    model.space = CoordinateSpace::World;
    model.count = 0;
    return true;
}
void ResourceAssets::Sample(unsigned int id, const CharacterPose *pose, SkinMatrices &matrices)
{
    matrices.fill(0);
    for (size_t joint = 0; joint < kMaxSkinBones; ++joint)
    {
        for (size_t axis = 0; axis < 4; ++axis)
        {
            matrices[joint * 16 + axis * 5] = 1;
        }
    }
    const auto &mesh = Get(id);
    const bool layeredRig = mesh.rig && (mesh.boneCount == 23 || mesh.rig->preset == RigPreset::Mixamo) &&
                            mesh.rig->idle >= 0 && mesh.rig->walk >= 0 && mesh.rig->run >= 0;
    if (pose && layeredRig && !pose->riding)
    {
        LayeredAnimation::Sample(mesh, *pose, matrices);
        return;
    }
    auto base = Select(mesh, pose);
    if (!base.clip)
    {
        return;
    }
    const size_t size = (std::min)(size_t(mesh.boneCount), size_t(kMaxSkinBones)) * 16;
    std::copy_n(base.clip->matrices.data() + size * base.frame, size, matrices.begin());
}
Point ResourceAssets::Muzzle(const CharacterPose &pose)
{
    auto id = Find(pose.gender == Gender::Female ? "character/Female/Explorer" : "character/Male/Explorer");
    const auto &mesh = Get(id);
    if (mesh.rig && mesh.rig->muzzle >= 0 && !pose.riding)
    {
        return Socket(id, pose, RigSocket::Muzzle);
    }
    auto selection = Select(Get(id), &pose, true);
    return selection.clip ? selection.clip->muzzles[selection.frame] : Point{.5f, 0, .8f};
}
Point ResourceAssets::Socket(unsigned int id, const CharacterPose &pose, RigSocket socket)
{
    const auto &mesh = Get(id);
    if (mesh.rig && !pose.riding)
    {
        const int bone = mesh.rig->SocketBone(socket);
        if (bone >= 0 && size_t(bone) < mesh.rig->bind.size() && size_t(bone) < kMaxSkinBones)
        {
            SkinMatrices matrices;
            Sample(id, &pose, matrices);
            AnimationMatrix skin;
            std::copy_n(matrices.begin() + size_t(bone) * 16, 16, skin.begin());
            auto world = AnimationMath::Multiply(skin, mesh.rig->bind[size_t(bone)]);
            return {world[12], world[13], world[14]};
        }
    }
    switch (socket)
    {
    case RigSocket::Head:
        return {0.f, 0.f, 1.15f};
    case RigSocket::Chest:
        return {.08f, 0.f, .92f};
    case RigSocket::Back:
        return {-.14f, 0.f, .90f};
    case RigSocket::Pelvis:
        return {0.f, 0.f, .64f};
    case RigSocket::HandL:
        return {.22f, .18f, .72f};
    case RigSocket::Muzzle:
        return {.5f, 0.f, .8f};
    case RigSocket::HandR:
    default:
        return {.22f, -.18f, .72f};
    }
}
Point ResourceAssets::HandR(unsigned int id, const CharacterPose &pose)
{
    return Socket(id, pose, RigSocket::HandR);
}
void ResourcePoseSampler::Sample(unsigned int id, const CharacterPose &pose, SkinMatrices &matrices)
{
    const auto &mesh = ResourceAssets::Get(id);
    const bool layeredRig = mesh.rig && (mesh.boneCount == 23 || mesh.rig->preset == RigPreset::Mixamo) &&
                            mesh.rig->idle >= 0 && mesh.rig->walk >= 0 && mesh.rig->run >= 0;
    if (!layeredRig || pose.riding)
    {
        ResourceAssets::Sample(id, &pose, matrices);
        return;
    }
    const std::array<float, 25> nextInputs = {pose.animationPhase,
                                              pose.recoil,
                                              pose.locomotionSpeed,
                                              pose.meleeSeconds,
                                              pose.locomotionPhase,
                                              pose.moveLeft,
                                              pose.moveForward,
                                              pose.aimWeight,
                                              pose.fireSeconds,
                                              pose.reloadProgress,
                                              float(pose.gender),
                                              pose.motionForwardLean,
                                              pose.motionSideLean,
                                              float(pose.dodgeSample),
                                              pose.dodgeTime,
                                              pose.dodgeWeight,
                                              float(pose.dodgeEntrySample),
                                              pose.dodgeEntryTime,
                                              pose.dodgeEntryWeight,
                                              float(pose.supportClip),
                                              pose.supportPhase,
                                              pose.supportWeight,
                                              float(pose.transitionClip),
                                              pose.transitionPhase,
                                              pose.transitionWeight};
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
Point ResourceAssets::SkinPoint(const PackedResourceVertex &vertex, const SkinMatrices &matrices)
{
    Point result = {};
    for (size_t i = 0; i < 4; ++i)
    {
        const float *m = matrices.data() + size_t(vertex.joints[i]) * 16;
        const float w = float(vertex.weights[i]) / 255.f;
        result.x += (m[0] * vertex.x + m[4] * vertex.y + m[8] * vertex.z + m[12]) * w;
        result.y += (m[1] * vertex.x + m[5] * vertex.y + m[9] * vertex.z + m[13]) * w;
        result.z += (m[2] * vertex.x + m[6] * vertex.y + m[10] * vertex.z + m[14]) * w;
    }
    return result;
}
} // namespace models
