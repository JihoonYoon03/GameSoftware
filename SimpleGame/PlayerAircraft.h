#pragma once
#include "World.h"
#include <optional>

namespace world
{
class SurfaceSession;
}
namespace tutorial
{
class Game;
// Persistent ownership belongs to the planet checkpoint. Inputs and velocities are transient.
struct PlayerAircraft
{
    static constexpr int MaxHealth = 160;
    static constexpr double MaxHeightMeters = 120;
    std::optional<world::SurfacePoint> position;
    world::Vector3d heading{};
    int health = MaxHealth;
    float wreckSeconds = 0;
    bool grounded = true;
    bool mounted = false, landingRequested = false;
    world::Vector3d velocity{}; // Metres/second in the current floating origin.
    float bank = 0, fallSpeed = 0, spawnRetry = 0;

    void Stop();
    void SetYaw(float yaw, const world::TangentFrame &origin);
    float Yaw(const world::TangentFrame &frame) const;
    bool Contains(const world::SurfaceSession &session, const world::SurfacePoint &point,
                  double padding = 0) const;
    static double RadiusMeters();
    static double HeightMeters();
};
class PlayerAircraftController
{
  public:
    static void Update(Game &game, float dt);
    static void Drive(Game &game, float dt);
    static void Interact(Game &game);
    static bool Nearby(const Game &game);

  private:
    static bool Occupied(const Game &game, const world::SurfacePoint &point);
    static std::optional<double> LandingHeight(const Game &game, const world::SurfacePoint &point);
    static bool Exit(Game &game);
};
} // namespace tutorial
