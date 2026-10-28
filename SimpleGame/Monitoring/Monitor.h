#pragma once
#include <chrono>
#include <cstdint>
#include <string>
namespace monitoring
{

// ============================================================================
// WorldSnapshot — game-thread serialization
// ============================================================================
class WorldSnapshot
{
  public:
    static std::string Build(std::uint64_t sequence, const std::string &sessionId);
};

// ============================================================================
// ModelCatalog — model inspection data
// ============================================================================
class ModelCatalog
{
  public:
    static std::string Build();
};

// ============================================================================
// GameMonitor — server lifetime and publish schedule
// ============================================================================
class GameMonitor
{
  public:
    static GameMonitor &Get();
    void Initialize();
    void Update();
    void Shutdown();

  private:
    bool enabled = false;
    std::uint64_t sequence = 0, lastRequestCount = 0;
    std::string sessionId;
    std::chrono::steady_clock::time_point nextPublish;
};
} // namespace monitoring
