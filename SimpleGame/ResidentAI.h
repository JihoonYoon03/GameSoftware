#pragma once
#include "Game.h"
#include <map>

namespace tutorial
{
// Shared, immutable-after-reset pedestrian graph; movement is independent of rendering/culling.
class CityResidents
{
  public:
    static CityResidents &Get();
    void Reset(GameState &world);
    void Update(GameState &world, float deltaSeconds);
    static const char *ProfessionName(ResidentProfession profession);
    static const char *ActivityName(ResidentActivity activity);
    static const char *ActivityLabel(const Resident &resident);
    static bool CanWalkSegment(const GameState &world, Vector2 from, Vector2 to);
    size_t NavigationNodeCount() const;
    std::uint64_t NavigationFingerprint() const;

  private:
    struct Link
    {
        int to, crossing;
    };
    struct Node
    {
        Vector2 position;
        int city, component = -1;
        std::vector<Link> links;
        bool sidewalk = false;
    };
    void Build(const GameState &world);
    int Nearest(const GameState &world, Vector2 position, int city, float limit) const;
    bool Plan(GameState &world, Resident &resident);
    bool FindRoute(int from, int to, Resident &resident) const;
    std::vector<Node> nodes;
    std::map<std::pair<int, int>, std::vector<int>> nodeCells;
    std::vector<std::vector<int>> workplaces;
    size_t decisionCursor = 0;
    size_t layoutSignature = 0;
};
} // namespace tutorial
