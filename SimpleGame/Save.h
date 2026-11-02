#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace tutorial
{
// ============================================================================
// SaveSystem — per-world campaign records; prepare/validate before replacing the live session
// ============================================================================
class SaveSystem
{
  public:
    static std::filesystem::path DefaultPath();
    static const char *Restriction();
    static bool Save(const std::filesystem::path &path = DefaultPath());
    static bool Load(const std::filesystem::path &path = DefaultPath());
    static bool CheckFile(const std::filesystem::path &path);
    static void Update();
    static void Cancel();
    static bool Loading();
    static bool Available();
    static void Refresh();
    static const std::string &Message();
    static bool SwitchPlanet(std::string_view profileId);
    static void ResetCampaign();
};
} // namespace tutorial
