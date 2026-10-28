#pragma once
#include "ModelLibrary.h"
#include "Animation.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace models
{
// Binary layout shared with tools/pack-resources.mjs. Geometry stays immutable after loading.
struct ResourceVertex
{
    float x, y, z, nx, ny, nz, r, g, b, a, palette, visibility, surface;
    std::array<float, 4> joints;
    std::array<float, 4> weights;
};
using SkinMatrices = std::array<float, 32 * 16>;
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
    std::vector<ResourceVertex> vertices;
    std::shared_ptr<const std::vector<ResourceClip>> animations;
    std::shared_ptr<const AnimationRig> rig;
    const std::vector<ResourceClip> &Clips() const
    {
        static const std::vector<ResourceClip> empty;
        return animations ? *animations : empty;
    }
};

class ResourceAssets
{
  public:
    static unsigned int Find(const std::string &name);
    static const ResourceMesh &Get(unsigned int id);
    static void Bind(Model &model, const std::string &name);
    // Immutable runtime assembly of imported transit surfaces; source pack remains unchanged.
    static unsigned int RegisterStatic(ResourceMesh mesh);
    static void Sample(unsigned int id, const CharacterPose *pose, SkinMatrices &matrices);
    static Point Muzzle(const CharacterPose &pose);
    static Point SkinPoint(const ResourceVertex &vertex, const SkinMatrices &matrices);
};
} // namespace models
