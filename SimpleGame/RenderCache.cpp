#include "stdafx.h"
#include "RenderCache.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include "Drawing.h"
#include <vector>
#include <unordered_map>

namespace rendercache
{
namespace
{
std::unordered_map<std::string, renderqueue::MeshPtr> meshes;
struct SlotEntry
{
    std::uint64_t version = 0;
    renderqueue::MeshPtr mesh;
};
std::unordered_map<std::string, SlotEntry> slots;
} // namespace
void Draw(const std::string &key, const std::function<void()> &build)
{
    auto found = meshes.find(key);
    if (found == meshes.end())
    {
        profiling::Count(profiling::Counter::MeshCacheMisses);
        found = meshes.emplace(key, renderqueue::Capture(build)).first;
    }
    else
    {
        profiling::Count(profiling::Counter::MeshCacheHits);
    }
    renderqueue::DrawMesh(found->second);
}
void DrawSlot(const std::string &slot, std::uint64_t version, const std::function<void()> &build)
{
    auto &entry = slots[slot];
    if (!entry.mesh || entry.version != version)
    {
        profiling::Count(profiling::Counter::MeshCacheMisses);
        entry.mesh = renderqueue::Capture(build);
        entry.version = version;
    }
    else
    {
        profiling::Count(profiling::Counter::MeshCacheHits);
    }
    renderqueue::DrawMesh(entry.mesh);
}
void DrawWorld(const std::string &key, const std::function<void()> &build)
{
    // Geometry is now shared in the material renderer. Emit instances for each
    // visible terrain chunk; capturing submissions would freeze scene membership.
    build();
}
void Clear()
{
    renderqueue::Flush();
    meshes.clear();
    slots.clear();
}
void InvalidateTerrain()
{
    renderqueue::Flush();
    for (auto it = meshes.begin(); it != meshes.end();)
    {
        if (it->first.find("terrain/") == 0)
        {
            it = meshes.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
} // namespace rendercache
