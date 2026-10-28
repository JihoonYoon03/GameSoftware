#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace monitoring::transport
{
// Only accepts loopback connections and serves the fixed dashboard/API routes.
bool Start(const std::filesystem::path &assetDirectory, unsigned short port);
void Publish(std::string snapshot);
void PublishModels(std::string catalog);
void Stop();
std::uint64_t ReadRequestCount();
} // namespace monitoring::transport
