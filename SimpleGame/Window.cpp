#include "stdafx.h"
#include <windows.h>
#include <mmsystem.h>
#include "Window.h"
#include "Game.h"
#include "Dependencies/glew/include/GL/glew.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#pragma comment(lib, "winmm.lib")

namespace platform
{
namespace
{
GLFWwindow *window = nullptr;
const auto clockOrigin = std::chrono::steady_clock::now();
std::array<unsigned char, GLFW_KEY_LAST + 1> heldKeys{};
double scrollRemainder = 0;

std::pair<int, int> Cursor()
{
    double x = 0, y = 0;
    int width = 0, height = 0, pixelsWide = 0, pixelsHigh = 0;
    glfwGetCursorPos(window, &x, &y);
    glfwGetWindowSize(window, &width, &height);
    glfwGetFramebufferSize(window, &pixelsWide, &pixelsHigh);
    return FramebufferCursor(x, y, width, height, pixelsWide, pixelsHigh);
}

void Key(GLFWwindow *, int key, int, int action, int modifiers)
{
    if (key < 0 || key > GLFW_KEY_LAST || action == GLFW_REPEAT)
    {
        return;
    }
    const auto cursor = Cursor();
    if (action == GLFW_RELEASE)
    {
        // Use the character from key-down, even if Shift was released first.
        if (heldKeys[key])
        {
            tutorial::GameCallbacks::KeyUp(heldKeys[key], cursor.first, cursor.second);
            heldKeys[key] = 0;
        }
        return;
    }
    const auto character = GameKey(key, modifiers);
    if (character)
    {
        heldKeys[key] = character;
        tutorial::GameCallbacks::KeyDown(character, cursor.first, cursor.second);
    }
    else
    {
        tutorial::GameCallbacks::SpecialKeyDown(key, cursor.first, cursor.second);
    }
}

void Focus(GLFWwindow *, int focused)
{
    if (!focused)
    {
        scrollRemainder = 0;
        for (auto &key : heldKeys)
        {
            if (key)
            {
                tutorial::GameCallbacks::KeyUp(key, 0, 0);
                key = 0;
            }
        }
        tutorial::GameCallbacks::MouseEntry(GLFW_FALSE);
        auto &state = tutorial::Game::Get().GetState();
        std::fill(std::begin(state.pressedKeys), std::end(state.pressedKeys), false);
        SetCursorHidden(false);
    }
}

void Resize(GLFWwindow *, int width, int height)
{
    if (width > 0 && height > 0)
    {
        tutorial::GameCallbacks::Resize(width, height);
        const auto cursor = Cursor();
        tutorial::GameCallbacks::MouseMotion(cursor.first, cursor.second);
    }
}
} // namespace

unsigned char GameKey(int key, int modifiers)
{
    // These are gameplay controls, independent of IME composition. Shift affects
    // punctuation (not movement); there is no editable text field in this game.
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
    {
        return static_cast<unsigned char>('a' + key - GLFW_KEY_A);
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
    {
        return static_cast<unsigned char>((modifiers & GLFW_MOD_SHIFT)
                                              ? ")!@#$%^&*("[key - GLFW_KEY_0]
                                              : '0' + key - GLFW_KEY_0);
    }
    switch (key)
    {
    case GLFW_KEY_ESCAPE: return 27;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return 13;
    case GLFW_KEY_TAB: return 9;
    case GLFW_KEY_BACKSPACE: return 8;
    case GLFW_KEY_SPACE: return ' ';
    case GLFW_KEY_EQUAL: return modifiers & GLFW_MOD_SHIFT ? '+' : '=';
    case GLFW_KEY_MINUS: return modifiers & GLFW_MOD_SHIFT ? '_' : '-';
    case GLFW_KEY_LEFT_BRACKET: return modifiers & GLFW_MOD_SHIFT ? '{' : '[';
    case GLFW_KEY_RIGHT_BRACKET: return modifiers & GLFW_MOD_SHIFT ? '}' : ']';
    case GLFW_KEY_SLASH: return modifiers & GLFW_MOD_SHIFT ? '?' : '/';
    case GLFW_KEY_BACKSLASH: return modifiers & GLFW_MOD_SHIFT ? '|' : '\\';
    case GLFW_KEY_SEMICOLON: return modifiers & GLFW_MOD_SHIFT ? ':' : ';';
    case GLFW_KEY_APOSTROPHE: return modifiers & GLFW_MOD_SHIFT ? '"' : '\'';
    case GLFW_KEY_COMMA: return modifiers & GLFW_MOD_SHIFT ? '<' : ',';
    case GLFW_KEY_PERIOD: return modifiers & GLFW_MOD_SHIFT ? '>' : '.';
    case GLFW_KEY_GRAVE_ACCENT: return modifiers & GLFW_MOD_SHIFT ? '~' : '`';
    case GLFW_KEY_KP_ADD: return '+';
    case GLFW_KEY_KP_SUBTRACT: return '-';
    default: return 0;
    }
}

std::pair<int, int> FramebufferCursor(double x, double y, int windowWidth, int windowHeight,
                                    int framebufferWidth, int framebufferHeight)
{
    return {int(std::floor(x * framebufferWidth / (std::max)(1, windowWidth))),
            int(std::floor(y * framebufferHeight / (std::max)(1, windowHeight)))};
}

bool Initialize(const char *title, int width, int height, bool visible)
{
    glfwSetErrorCallback([](int code, const char *message) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, message);
    });
    if (!glfwInit())
    {
        return false;
    }
    glfwDefaultWindowHints();
    // The renderer uses matrix stacks, GL_QUADS and compatibility GLSL. Do not
    // request a core or forward-compatible context. As with GLUT, let the driver
    // expose its highest backwards-compatible version and keep renderer fallbacks.
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 0);
    glfwWindowHint(GLFW_SAMPLES, 0); // The renderer already owns the MSAA FBO.
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    // Preserve the existing CPU 60 Hz scheduler without a second vsync wait.
    glfwSwapInterval(0);
    const GLenum status = glewInit();
    if (status != GLEW_OK)
    {
        std::fprintf(stderr, "GLEW initialization failed: %s\n", glewGetErrorString(status));
        Shutdown();
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {}
    heldKeys.fill(0);
    scrollRemainder = 0;
    return true;
}

void InstallCallbacks()
{
    glfwSetKeyCallback(window, Key);
    glfwSetWindowFocusCallback(window, Focus);
    glfwSetFramebufferSizeCallback(window, Resize);
    glfwSetCursorPosCallback(window, [](GLFWwindow *, double, double) {
        const auto cursor = Cursor();
        tutorial::GameCallbacks::MouseMotion(cursor.first, cursor.second);
    });
    glfwSetCursorEnterCallback(window, [](GLFWwindow *, int entered) {
        tutorial::GameCallbacks::MouseEntry(entered);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow *, int button, int action, int) {
        const auto cursor = Cursor();
        tutorial::GameCallbacks::MouseButton(button, action, cursor.first, cursor.second);
    });
    glfwSetScrollCallback(window, [](GLFWwindow *, double, double yOffset) {
        // Preserve one zoom step per wheel notch; retain fractional trackpad input.
        scrollRemainder += (std::max)(-8.0, (std::min)(8.0, yOffset));
        const int steps = int(scrollRemainder);
        if (steps)
        {
            scrollRemainder -= steps;
            const auto cursor = Cursor();
            tutorial::Game::Get().MouseWheel(steps, cursor.first, cursor.second);
        }
    });
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    Resize(window, width, height);
}

void Run()
{
    InstallCallbacks();
    // GLFW's Win32 event wait uses millisecond OS timeouts. Match GLUT's timer
    // precision and restore it on exit; otherwise a 16 ms timeout may take 31 ms.
    struct TimerResolution
    {
        bool active = timeBeginPeriod(1) == TIMERR_NOERROR;
        ~TimerResolution() { if (active) { timeEndPeriod(1); } }
    } timerResolution;
    double nextFrame = glfwGetTime() + .016;
    while (!glfwWindowShouldClose(window))
    {
        // Input can wake the wait early; it must not speed up simulation/rendering.
        const double remaining = nextFrame - glfwGetTime();
        if (remaining > 0)
        {
            // The Win32 backend truncates to DWORD milliseconds. A sub-ms wait
            // would become a zero-timeout poll and spin until the deadline.
            glfwWaitEventsTimeout((std::max)(remaining, .001));
            continue;
        }
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) { break; }
        const double started = glfwGetTime() * 1000;
        tutorial::GameCallbacks::Tick(0);
        if (!glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        {
            tutorial::GameCallbacks::Draw();
        }
        const double finished = glfwGetTime() * 1000;
        nextFrame = glfwGetTime() + tutorial::Game::Get().NextFrameDelay(started, finished) / 1000.0;
    }
}

void Shutdown()
{
    if (window)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}
void RequestClose() { if (window) { glfwSetWindowShouldClose(window, GLFW_TRUE); } }
void SwapBuffers() { if (window) { glfwSwapBuffers(window); } }
void SetCursorHidden(bool hidden)
{
    if (window)
    {
        const int mode = hidden && glfwGetWindowAttrib(window, GLFW_FOCUSED) ? GLFW_CURSOR_HIDDEN
                                                                          : GLFW_CURSOR_NORMAL;
        if (glfwGetInputMode(window, GLFW_CURSOR) != mode) { glfwSetInputMode(window, GLFW_CURSOR, mode); }
    }
}
int ElapsedMilliseconds()
{
    // Available to CPU-only simulation/tests before GLFW initialization, too.
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - clockOrigin).count());
}
GLFWwindow *Window() { return window; }
} // namespace platform
