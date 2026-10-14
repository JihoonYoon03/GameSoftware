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
