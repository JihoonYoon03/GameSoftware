#include "stdafx.h"
#include "RenderCache.h"
#include "RenderQueue.h"
#include "Profiler.h"
#include "TutorialDrawing.h"
#include "LevelOne.h"
#include <vector>
#include <unordered_map>

namespace rendercache
{
namespace
{
std::unordered_map<std::string, renderqueue::MeshPtr> meshes;
GLuint minimapTexture = 0;
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
    auto origin = tutorial::WorldToScreen(0, 0);
    renderqueue::PushMatrix();
    renderqueue::Translate(origin.x, origin.y, 0);
    Draw(key, [&]() {
        renderqueue::Translate(-origin.x, -origin.y, 0);
        build();
    });
    renderqueue::PopMatrix();
}
void Clear()
{
    renderqueue::Flush();
    meshes.clear();
    if (minimapTexture)
    {
        glDeleteTextures(1, &minimapTexture);
        minimapTexture = 0;
    }
}
void InvalidateTerrain()
{
    renderqueue::Flush();
    if (minimapTexture)
    {
        glDeleteTextures(1, &minimapTexture);
        minimapTexture = 0;
    }
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
void DrawMinimapTerrain(float left, float top)
{
    constexpr int cellSize = 5, size = levelone::kMapSize * cellSize;
    if (!minimapTexture)
    {
        profiling::Scope timer(profiling::Timer::TextureUpload);
        profiling::Count(profiling::Counter::MinimapRebuilds);
        std::vector<unsigned char> pixels(size * size * 4, 0);
        for (int y = 0; y < levelone::kMapSize; ++y)
        {
            for (int x = 0; x < levelone::kMapSize; ++x)
            {
                if (!levelone::IsWalkable(x, y))
                {
                    continue;
                }
                for (int py = 0; py < 4; ++py)
                {
                    for (int px = 0; px < 4; ++px)
                    {
                        size_t offset = ((y * cellSize + py) * size + x * cellSize + px) * 4;
                        for (int channel = 0; channel < 4; ++channel)
                        {
                            pixels[offset + channel] = 255;
                        }
                    }
                }
            }
        }
        glGenTextures(1, &minimapTexture);
        glBindTexture(GL_TEXTURE_2D, minimapTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        profiling::Count(profiling::Counter::TextureUploadBytes, pixels.size());
    }
    renderqueue::Color(.21f, .34f, .37f, 1);
    renderqueue::Texture(minimapTexture);
    renderqueue::Begin(GL_QUADS);
    renderqueue::TexCoord(0, 0);
    renderqueue::Vertex(left, top);
    renderqueue::TexCoord(1, 0);
    renderqueue::Vertex(left + size, top);
    renderqueue::TexCoord(1, 1);
    renderqueue::Vertex(left + size, top + size);
    renderqueue::TexCoord(0, 1);
    renderqueue::Vertex(left, top + size);
    renderqueue::End();
    renderqueue::Texture(0);
}
} // namespace rendercache
