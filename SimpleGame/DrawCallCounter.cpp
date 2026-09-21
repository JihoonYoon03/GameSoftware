#include "stdafx.h"
#include "DrawCallCounter.h"
#include "Dependencies/glew.h"
#include <cstdint>
#include <cstdio>

namespace renderdebug
{
namespace
{
// Rendering and frame callbacks run on the same GLUT thread.
std::uint64_t frameNumber = 0;
std::uint64_t drawCallCount = 0;
bool frameActive = false;
} // namespace

void BeginFrame()
{
    ++frameNumber;
    drawCallCount = 0;
    frameActive = true;
}

void BeginPrimitive(unsigned int mode)
{
    if (frameActive)
    {
        ++drawCallCount;
    }
    glBegin(mode);
}

void EndFrame()
{
    if (!frameActive)
    {
        return;
    }
    frameActive = false;
    std::printf("[Frame %llu] Draw calls: %llu\n", static_cast<unsigned long long>(frameNumber),
                static_cast<unsigned long long>(drawCallCount));
    std::fflush(stdout);
}
} // namespace renderdebug
