#pragma once
#include "ModelLibrary.h"
#include "Animation.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace models
{
// Unpacked CPU vertex representation (used for skinning queries, procedural assembly & fallback rendering).
struct ResourceVertex
{
    float x, y, z, nx, ny, nz, r, g, b, a, palette, visibility, surface;
    std::array<float, 4> joints;
    std::array<float, 4> weights;
    float u = 0, v = 0;
    bool hasUv = false;
};
// Compact 36-byte GPU/disk vertex layout shared with tools/pack-resources.mjs (v4).
struct PackedResourceVertex
{
    float x, y, z;
    std::int16_t nx, ny;
    std::uint16_t u, v;
    std::uint8_t r, g, b, a;
    std::uint8_t palette, visibility, surface, flags;
    std::array<std::uint8_t, 4> joints;
    std::array<std::uint8_t, 4> weights;
};
enum class LodLevel : std::uint8_t
{
    Lod0 = 0,
    Lod1 = 1,
    Lod2 = 2,
    ShadowProxy = 3,
    Count = 4
};
struct SubmeshRange
{
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};
struct ResourceClip
{
    std::string name;
    unsigned int frames = 0;
    float duration = 0;
    std::vector<float> matrices;
    std::vector<Point> muzzles;
    std::vector<BonePose> localFrames;
};
struct ResourceMesh
{
    std::string name, source;
    unsigned int boneCount = 0;
    Point minimum, maximum, physicalSize;
    std::array<Point, 6> sockets;
    std::vector<PackedResourceVertex> packedVertices;
    std::vector<std::uint16_t> indices16;
    std::vector<std::uint32_t> indices32;
    std::uint8_t indexBytes = 4;
    std::array<SubmeshRange, size_t(LodLevel::Count)> lods = {};
    // Optional transient unpacked vertex list used only while building static assemblies before RegisterStatic.
    std::vector<ResourceVertex> vertices;
    std::shared_ptr<const std::vector<ResourceClip>> animations;
    std::shared_ptr<const AnimationRig> rig;
    const std::vector<ResourceClip> &Clips() const
    {
        static const std::vector<ResourceClip> empty;
        return animations ? *animations : empty;
    }
    bool HasGeometry() const
    {
        return (!packedVertices.empty() && lods[0].indexCount > 0) || !vertices.empty();
    }
    size_t LodIndexCount(LodLevel lod = LodLevel::Lod0) const
    {
        if (!packedVertices.empty())
        {
            return lods[size_t(lod)].indexCount;
        }
        return vertices.size();
    }
    size_t TriangleCount(LodLevel lod = LodLevel::Lod0) const
    {
        return LodIndexCount(lod) / 3;
    }
    std::uint32_t IndexAt(size_t indexPos) const
    {
        return indexBytes == 2 ? std::uint32_t(indices16[indexPos]) : indices32[indexPos];
    }
};

// CQ-11 Mixamo 3-tier modular character descriptor (stable Model* pointers from ModelLibrary).
struct ModularAttachment
{
    const Model *model = nullptr;
    RigSocket socket = RigSocket::Head;
    bool skinned = false;
    Point localOffset = {};
    Transform3D localRotation = {};
    Point localScale = {1, 1, 1};
    Palette palette = {};
};
struct ModularCharacter
{
    const Model *body = nullptr;
    std::vector<const Model *> outfits;
    std::vector<Palette> outfitPalettes;
    std::vector<ModularAttachment> attachments;
    // Lower 3 bits map to shader visibility slots: bit 0 -> slot 2 (Body_Upper), bit 1 -> slot 3 (Body_Lower),
    // bit 2 -> slot 4 (Body_Head).
    std::uint8_t bodyVisibilityMask = 0;
};

class ResourceAssets
{
  public:
    static unsigned int Find(const std::string &name);
    static unsigned int TryFind(const std::string &name);
    static const ResourceMesh &Get(unsigned int id);
    static size_t PackMeshCount();
    static void Bind(Model &model, const std::string &name);
    static bool BindOptional(Model &model, const std::string &name);
    // Immutable runtime assembly of imported transit surfaces; source pack remains unchanged.
    static unsigned int RegisterStatic(ResourceMesh mesh);
    static PackedResourceVertex PackVertex(const ResourceVertex &vertex);
    static ResourceVertex UnpackVertex(const PackedResourceVertex &packed);
    static ResourceVertex TriangleVertex(const ResourceMesh &mesh, size_t lodVertexIndex,
                                         LodLevel lod = LodLevel::Lod0);
    static std::vector<ResourceVertex> UnpackLod0Triangles(const ResourceMesh &mesh);
    static void Sample(unsigned int id, const CharacterPose *pose, SkinMatrices &matrices);
    static Point Muzzle(const CharacterPose &pose);
    static Point HandR(unsigned int id, const CharacterPose &pose);
    static Point Socket(unsigned int id, const CharacterPose &pose, RigSocket socket);
    static Point SkinPoint(const ResourceVertex &vertex, const SkinMatrices &matrices);
    static Point SkinPoint(const PackedResourceVertex &vertex, const SkinMatrices &matrices);
};

// One-entry, owner-local cache for adjacent body/hair submissions sharing an immutable bank.
// Gameplay sampling stays stateless. No quantization, cross-thread state or lazy clip preparation.
class ResourcePoseSampler
{
  public:
    void Sample(unsigned int id, const CharacterPose &pose, SkinMatrices &matrices);

  private:
    const AnimationRig *rig = nullptr;
    const std::vector<ResourceClip> *animations = nullptr;
    std::array<float, 31> inputs = {};
    unsigned int flags = 0;
    SkinMatrices cached = {};
};
} // namespace models
