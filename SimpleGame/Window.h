#pragma once

// Keep GL headers under GLEW's control in every translation unit.
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "Dependencies/glfw/include/GLFW/glfw3.h"
#include <utility>

namespace platform
{
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
int ElapsedMilliseconds();
GLFWwindow *Window();

// Pure conversion helpers shared with the platform regression checks.
unsigned char GameKey(int key, int modifiers);
std::pair<int, int> FramebufferCursor(double x, double y, int windowWidth, int windowHeight,
                                      int framebufferWidth, int framebufferHeight);
} // namespace platform
