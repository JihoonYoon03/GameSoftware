#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace rendercache
{
void Draw(const std::string &key, const std::function<void()> &build);
void DrawSlot(const std::string &slot, std::uint64_t version, const std::function<void()> &build);
void DrawWorld(const std::string &key, const std::function<void()> &build);
void Clear();
void InvalidateTerrain();
} // namespace rendercache
