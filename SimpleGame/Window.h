#pragma once

// Keep GL headers under GLEW's control in every translation unit.
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "Dependencies/glfw/include/GLFW/glfw3.h"
#include <array>
#include <utility>

namespace platform
{
struct ResolutionOption
{
    int width;
    int height;
    const char *label;
};
inline constexpr std::array<ResolutionOption, 5> kResolutionOptions = {{
    {1280, 720, "1280×720 (HD)"},
    {1600, 900, "1600×900 (HD+)"},
    {1920, 1080, "1920×1080 (FHD)"},
    {2560, 1440, "2560×1440 (QHD)"},
    {3840, 2160, "3840×2160 (4K UHD)"},
}};
// Synthetic gameplay token; left/right Shift share one logical held state.
inline constexpr unsigned char SprintKey = 0x10;
// The context stays alive until Shutdown, including renderer/cache destruction.
bool Initialize(const char *title, int width = 1280, int height = 800, bool visible = true);
void InstallCallbacks();
void Run();
void Shutdown();
void RequestClose();
void SwapBuffers();
void SetCursorHidden(bool hidden);
bool ApplyDisplayMode(int width, int height, bool fullscreen);
bool IsFullscreen();
int ElapsedMilliseconds();
GLFWwindow *Window();

// Pure conversion helpers shared with the platform regression checks.
unsigned char GameKey(int key, int modifiers);
std::pair<int, int> FramebufferCursor(double x, double y, int windowWidth, int windowHeight,
                                      int framebufferWidth, int framebufferHeight);
} // namespace platform
