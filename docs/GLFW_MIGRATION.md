# FreeGLUT to GLFW migration

## Project structure

- `SimpleGame.cpp`: startup, renderer/world initialization, monitoring and shutdown.
- `Window.h/.cpp`: GLFW window, compatible OpenGL context, GLEW initialization,
  event callbacks, frame scheduling, cursor and buffer presentation.
- `Game.cpp`, `Hud.cpp`: existing game/input behavior; use GLFW key/button values.
- `Renderer`, `RenderQueue`, `RenderAssets`, `Drawing`: existing rendering,
  resource/font caches and explicit `LabelFont` sizes.
- `SimpleGame/Dependencies`: vendored GLFW 3.4 and GLEW 2.1.0 source, licenses and
  the native Visual Studio static-library project.
- `CMakeLists.txt`, `CMakePresets.json`, `cmake/GameSources.cmake`: alternative
  source build, runtime asset staging and CTest registration.
- `Resource`, `tools`: existing GLB/JSON/UI assets and Node resource preparation.
- `monitor`, `SimpleGame/Monitoring`: existing local monitoring dashboard/server.
- `tests`: simulation, resources, input, font, rendering and performance checks.

## Preserved behavior

The renderer still uses compatibility OpenGL (matrix stacks, GL_QUADS and its
existing shader/FBO fallbacks). GLFW does not request a core or forward-compatible
context. The default framebuffer remains RGBA8 with 24-bit depth; antialiasing
belongs to the existing renderer MSAA FBO. Geometry, shaders, resource packs and
simulation rules are unchanged by this migration.

The existing 60 Hz `FrameSchedule` controls update/render deadlines. Input events
may wake an event wait but cannot accelerate simulation. The Windows wait has a
minimum 1 ms timeout to avoid spinning on truncated sub-millisecond timeouts.
Timer resolution is requested only during the run loop and restored on exit.
Vsync is explicitly disabled to avoid a second frame limiter.

GLFW key/button callbacks replace GLUT callbacks; key repeat remains suppressed.
Key-up retains the character generated on key-down, including Shift punctuation.
Losing focus releases held movement/aim/fire controls. Cursor positions are
converted from window coordinates to framebuffer pixels for DPI scaling.
Scroll callbacks accumulate fractional trackpad input into existing map zoom
steps. Framebuffer resize updates the existing letterbox/viewport mapping.

The old GLUT font pointers only selected the game's existing 15/19 pixel GDI
font path. `LabelFont::Normal/Large` now selects those same sizes. UTF-8 decoding,
Korean GDI glyph rasterization, text measurement and texture caching are retained.
Renderer shutdown runs with the GL context alive, then GLFW destroys the window.

## Dependencies and distribution

Both the solution's GameGraphics project and CMake build GLFW/GLEW from source
with the same static MSVC runtime as the game. Old FreeGLUT/GLEW import libraries,
headers and redistributable DLLs are removed. Builds clear those two obsolete
DLLs from the game output. The upstream graphics licenses are staged alongside
the game. See `SimpleGame/Dependencies/README.txt` for versions and archive hashes.

No platform-independent font/image/save/network implementation was added; this
remains a Windows game. See the root README for prerequisites and build commands.

## Validation

Checked on Windows on 2026-11-04:

| Build | Configuration | Result |
|---|---|---|
| Visual Studio 2022, v143 | x64 Release, x64 Debug, x86 Release | Passed |
| Visual Studio 2026, installed default v145 | x64 Release | Passed |
| CMake with Visual Studio 2022 | x64 Release and x86 Release | Passed |

Both native projects explicitly select `DefaultPlatformToolset` after the C++
default properties load; omitting the setting otherwise selects legacy v100.
They prefer x64 host tools, which resolved the local MSPDB140.DLL link failure.
MSVC toolset/host architecture can still be overridden from the command line.

The full CTest run passed 14 of 18 tests. Gameplay fails at the FrontierSite
interaction assertion (GameplayTests.cpp:632), Movement at the movement-speed
assertion (MovementTests.cpp:99), and WorldGeneration at the road terrain grade
budget check after 110976 checks. All three failures reproduce with the original
FreeGLUT code in the separate baseline build. MonitorServer fails with WinSock
10013 when connecting to the local server in the restricted execution environment.
These checks are not disabled or marked as expected successes.

Input/focus/DPI/Korean metrics/shutdown, map, console output, render queue,
resources, startup and both real-driver rendering context checks pass. The
rendering test uses GLFW client sizes and explicit hidden-window size limits
for its 1920x1080 and 2560x1440 captures; outer-window sizes/default desktop
tracking limits would otherwise truncate those captures.

The production Release game was also opened: title/start mouse input, Esc pause,
maximizing and clicking the resized menu, Korean options text, and window-close
shutdown were verified. Import inspection of Debug and Release executables
shows Windows system DLLs only, with no FreeGLUT/GLEW/GLFW or MSVC runtime DLL.

Performance measurements use the existing real-driver hidden-context benchmark.
They measure simulation/render submission plus `glFinish`; they do not exercise
the production event wait, swap interval or interactive input latency. A short
window smoke check does not establish long-duration frame pacing or performance
on other drivers/hardware.

The final comparison ran the original FreeGLUT build and the GLFW build in
before/after/after/before order, without concurrent builds or graphics tests.
Each run uses 14 scenes at 1280x800 on Intel UHD Graphics, 24 warm-up frames and
120 measured frames per scene (6720 measured frames across the four runs).

| Mean of the 14 scene averages | Original FreeGLUT build | GLFW build |
|---|---:|---:|
| Two-run mean | 9.957 ms | 9.808 ms |

The aggregate measured time decreased 1.49%. Draw-call and triangle counts match
in every scene. Individual scene timings vary: the largest two-run increase was
the bike scene (+8.70%, 6.941 to 7.545 ms); mine entry increased 3.73%. This short
sample does not prove a zero regression for every scene or machine. Raw runs and
the per-scene comparison are in the ignored local `build/verification` directory.
The interactive game smoke log contained 68 full measurement windows with a
median 60.0 FPS and median per-window p95 interval of 18.06 ms; these include menu
and paused states and are not a sustained gameplay benchmark.
