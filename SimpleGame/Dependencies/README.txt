Graphics dependencies (built from source, no graphics DLL distribution)

GLFW 3.4: glfw/ contains the upstream src, include, CMake and deps directories,
CMakeLists.txt, README.md and LICENSE.md. Source tag:
https://github.com/glfw/glfw/tree/3.4
Archive: https://codeload.github.com/glfw/glfw/zip/refs/tags/3.4
Archive SHA256: a133ddc3d3c66143eba9035621db8e0bcf34dba1ee9514a9e23e96afd39fd57a

GLEW 2.1.0: glew/ contains the generated upstream glew.c, include/GL headers
and LICENSE.txt, from the same version as the former import libraries.
https://github.com/nigels-com/glew/releases/tag/glew-2.1.0
Archive: https://github.com/nigels-com/glew/releases/download/glew-2.1.0/glew-2.1.0.zip
Archive SHA256: 2700383d4de2455f06114fbaf872684f15529d4bdc5cdea69b5fb0e9aa7763f1

The upstream source files are unmodified. Examples, tests and documentation
not required for the game build are omitted. No FetchContent, submodule,
package restore, network connection or external graphics SDK is needed.

Visual Studio: GraphicsDependencies.vcxproj builds GameGraphics.lib for the
selected configuration and architecture. The game has a ProjectReference.
CMake: the upstream GLFW CMake target and the generated GLEW C source build
static libraries. GLEW_STATIC and GLFW_INCLUDE_NONE propagate to consumers.
Both build paths use a static MSVC runtime (/MT, /MTd for Debug).
FreeGLUT, glew32.dll and precompiled platform-specific libraries are removed.
Windows system OpenGL, windowing, font, image, socket and timer APIs are linked
through the Windows SDK. A working OpenGL driver is still required.

Licenses: glfw/LICENSE.md, glew/LICENSE.txt and ../../licenses/*.txt.
The game output includes licenses/ for redistribution.
See ../../README.md and ../../docs/GLFW_MIGRATION.md for build and validation.
