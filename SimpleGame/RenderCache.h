#pragma once
#include <functional>
#include <string>

namespace rendercache
{
void Draw(const std::string &key, const std::function<void()> &build);
void DrawWorld(const std::string &key, const std::function<void()> &build);
void Clear();
void InvalidateTerrain();
void DrawMinimapTerrain(float left, float top);
} // namespace rendercache
