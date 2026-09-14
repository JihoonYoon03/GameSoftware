#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace assetcache
{
// Cache paths are relative to the executable, not Visual Studio's working directory.
bool Load(const std::string &name, std::uint32_t version, std::size_t expectedBytes,
          std::vector<unsigned char> &bytes);
bool Save(const std::string &name, std::uint32_t version, const void *data, std::size_t byteCount);
} // namespace assetcache
