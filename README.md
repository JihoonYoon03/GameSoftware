# GSE / SimpleGame

Windows C++17 game using OpenGL, GLFW 3.4 and GLEW 2.1.0. Graphics dependency
sources and licenses are included under `SimpleGame/Dependencies`; both build
paths compile them statically. No FreeGLUT, GLFW or GLEW DLL installation,
package restore, or dependency download is required.

## Build

Install Visual Studio 2022 or newer with **Desktop development with C++** and a
Windows 10/11 SDK. Resource preparation also uses **Node.js 18+**; install it on
PATH, or set `NodeExecutable` to its full path. The resource preparer validates
the checked-in GLB/JSON files and regenerates the runtime pack when necessary.
Blender is not needed for normal game builds.

Open `SimpleGame.sln`, choose Debug or Release and x64 or x86, and build/run
SimpleGame. `GameGraphics` builds the graphics sources automatically. The
solution selects the installed Visual Studio default C++ toolset.

Alternatively, with CMake 3.21+ on PATH, run from the repository root:

```powershell
cmake --preset windows-x64
cmake --build --preset release-x64 --parallel
ctest --preset release-x64
./build/cmake-x64/Release/SimpleGame.exe
```

Use `debug-x64`, `release-x86` or `debug-x86` for the other build presets.
For an explicit VS 2022 generator or a custom Node installation:

```powershell
cmake -S . -B build/vs2022-x64 -G "Visual Studio 17 2022" -A x64 -DNodeExecutable="C:/path/to/node.exe"
cmake --build build/vs2022-x64 --config Release --parallel
ctest --test-dir build/vs2022-x64 -C Release --output-on-failure
```

The executable's output folder includes `Resource/Runtime`, `Resource/UI`,
`Monitor` and `licenses`. Copy that folder together for deployment. The MSVC
runtime is static (`/MT`, `/MTd`); the Windows system libraries and a working
OpenGL driver are required. Existing saves remain in their original location.

The game still requires Windows for GDI fonts, WIC images, saves and WinSock.
The GLFW conversion does not provide Linux or macOS backends for those services.

See [migration and validation](docs/GLFW_MIGRATION.md) for behavior, test results
and the remaining validation limits. General game documentation is in
[GAME_DOCUMENTATION.md](GAME_DOCUMENTATION.md).
