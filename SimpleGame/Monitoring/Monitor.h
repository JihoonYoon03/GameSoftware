#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
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
    // Capture on the game thread. The returned job reads immutable resource geometry only;
    // it must finish before ResourceAssets is destroyed (GameMonitor::Shutdown joins it).
    static std::function<std::string()> Capture(std::shared_ptr<std::atomic<bool>> cancel = {});
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
    ~GameMonitor();

  private:
    bool enabled = false;
    std::uint64_t sequence = 0, lastRequestCount = 0;
    std::string sessionId;
    std::chrono::steady_clock::time_point nextPublish;
    bool catalogRequested = false;
    std::shared_ptr<std::atomic<bool>> catalogCancel;
    std::future<std::string> catalogBuild;
    void UpdateCatalog();
};
} // namespace monitoring
