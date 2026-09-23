#include "stdafx.h"
#include "Profiler.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "AssetCache.h"
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace assetcache
{
namespace
{
struct Header
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t byteCount;
    std::uint32_t checksum;
};
constexpr std::uint32_t kMagic = 0x48414C4F;
constexpr std::size_t kMaximumBytes = 64 * 1024 * 1024;

std::filesystem::path CachePath(const std::string &name)
{
    wchar_t modulePath[32768] = {};
    DWORD length = GetModuleFileNameW(nullptr, modulePath, 32768);
    if (length == 0 || length >= 32768)
    {
        return {};
    }
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_.") != std::string::npos)
    {
        return {};
    }
    return std::filesystem::path(modulePath).parent_path() / L"Cache" / name;
}

std::uint32_t Checksum(const unsigned char *bytes, std::size_t size)
{
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}
} // namespace

bool Load(const std::string &name, std::uint32_t version, std::size_t expectedBytes,
          std::vector<unsigned char> &bytes)
{
    profiling::Scope timer(profiling::Timer::DiskCacheRead);
    bytes.clear();
    if (expectedBytes == 0 || expectedBytes > kMaximumBytes)
    {
        profiling::Count(profiling::Counter::DiskCacheMisses);
        return false;
    }
    auto path = CachePath(name);
    if (path.empty())
    {
        profiling::Count(profiling::Counter::DiskCacheMisses);
        return false;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != std::streamoff(sizeof(Header) + expectedBytes))
    {
        profiling::Count(profiling::Counter::DiskCacheMisses);
        return false;
    }
    file.seekg(0);
    Header header = {};
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!file || header.magic != kMagic || header.version != version || header.byteCount != expectedBytes)
    {
        profiling::Count(profiling::Counter::DiskCacheMisses);
        return false;
    }
    bytes.resize(expectedBytes);
    file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
    if (!file || Checksum(bytes.data(), bytes.size()) != header.checksum)
    {
        bytes.clear();
        profiling::Count(profiling::Counter::DiskCacheMisses);
        return false;
    }
    std::fprintf(stdout, "캐시 불러오기: %s\n", name.c_str());
    profiling::Count(profiling::Counter::DiskCacheHits);
    return true;
}

bool Save(const std::string &name, std::uint32_t version, const void *data, std::size_t byteCount)
{
    profiling::Scope timer(profiling::Timer::DiskCacheWrite);
    if (!data || byteCount == 0 || byteCount > kMaximumBytes)
    {
        return false;
    }
    auto path = CachePath(name);
    if (path.empty())
    {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        std::fprintf(stderr, "캐시 디렉터리 생성 실패. 이번 실행에서는 메모리 모델을 사용합니다.\n");
        return false;
    }
    auto temporary = path;
    temporary += L".tmp";
    Header header = {kMagic, version, std::uint32_t(byteCount),
                     Checksum(static_cast<const unsigned char *>(data), byteCount)};
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    file.write(static_cast<const char *>(data), std::streamsize(byteCount));
    file.close();
    // Replace only after a complete write. Corrupt or interrupted caches regenerate on next load.
    if (!file ||
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::fprintf(stderr, "캐시 저장 실패: %s. 다음 실행에서 다시 생성합니다.\n", name.c_str());
        return false;
    }
    std::fprintf(stdout, "캐시 생성: %s\n", name.c_str());
    return true;
}
} // namespace assetcache
