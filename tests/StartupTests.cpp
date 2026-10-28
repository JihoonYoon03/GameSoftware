#include "stdafx.h"
#include "Game.h"
#include "ModelLibrary.h"
#include "Overworld.h"
#include "TransitLayout.h"
#include "ResidentAI.h"
#include "Profiler.h"
#include "Civic.h"
#include <chrono>
#include <iostream>
#include <windows.h>
#include <psapi.h>

int main(int argc, char **argv)
{
    const bool profile = argc > 1 && std::string(argv[1]) == "--profile";
    if (profile)
    {
        profiling::BeginFrame("startup");
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    models::ModelLibrary::Get();
    const auto assets = Clock::now();
    tutorial::RoadNetwork::Get();
    const auto roads = Clock::now();
    tutorial::TransitLayout::Get();
    const auto modules = Clock::now();
    tutorial::Game::Get().Reset();
    const auto world = Clock::now();
    PROCESS_MEMORY_COUNTERS_EX memory = {};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                         sizeof(memory));
    const auto ms = [](auto from, auto to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    std::cout << "{\"asset_load_ms\":" << ms(start, assets) << ",\"road_graph_ms\":" << ms(assets, roads)
              << ",\"road_mesh_ms\":" << ms(roads, modules) << ",\"population_ms\":" << ms(modules, world)
              << ",\"world_prepare_ms\":" << ms(assets, world)
              << ",\"total_cpu_startup_ms\":" << ms(start, world)
              << ",\"private_bytes\":" << memory.PrivateUsage
              << ",\"model_binding_bytes\":" << sizeof(models::Model) << "}\n";
    std::cout << "Navigation nodes: " << tutorial::CityResidents::Get().NavigationNodeCount() << "\n";
    std::cout << "Navigation fingerprint: " << tutorial::CityResidents::Get().NavigationFingerprint() << "\n";
    if (argc > 1 && std::string(argv[1]) == "--collision")
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const auto &city : tutorial::Overworld::Cities())
        {
            for (int y = -64; y <= 64; ++y)
            {
                for (int x = -64; x <= 64; ++x)
                {
                    for (float radius : {.18f, .3f, .9f})
                    {
                        hash =
                            (hash ^ std::uint64_t(tutorial::CivicJustice::Solid(
                                        {city.center.x + x * .75f + .137f, city.center.y + y * .75f + .137f},
                                        radius))) *
                            1099511628211ull;
                    }
                }
            }
        }
        std::cout << "Collision fingerprint: " << hash << "\n";
    }
    if (profile)
    {
        profiling::EndFrame();
        profiling::Shutdown();
    }
}
