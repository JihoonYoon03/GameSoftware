cmake_minimum_required(VERSION 3.21)
project(GSE LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The game currently requires Windows (GDI fonts, WIC images, WinSock and saves). GLFW itself is portable; these game services still need platform backends.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
add_subdirectory(SimpleGame/Dependencies/glfw EXCLUDE_FROM_ALL)
add_library(glew STATIC SimpleGame/Dependencies/glew/src/glew.c)
target_include_directories(glew PUBLIC SimpleGame/Dependencies/glew/include)
target_compile_definitions(glew PUBLIC GLEW_STATIC GLEW_NO_GLU)
target_link_libraries(glew PUBLIC opengl32)

include(cmake/GameSources.cmake)
add_library(GameCore STATIC ${GAME_SOURCES})
target_include_directories(GameCore PUBLIC SimpleGame)
target_compile_definitions(GameCore PUBLIC GLFW_INCLUDE_NONE NOMINMAX)
target_link_libraries(GameCore PUBLIC glfw glew opengl32 user32 gdi32 shell32
    windowscodecs ole32 psapi ws2_32 winmm)
if(MSVC)
    target_compile_options(glfw PRIVATE /utf-8)
    target_compile_options(glew PRIVATE /utf-8)
    target_compile_options(GameCore PUBLIC /utf-8 /we4819 /we4828)
    target_compile_options(GameCore PRIVATE /W3)
endif()
add_executable(SimpleGame SimpleGame/SimpleGame.cpp)
target_link_libraries(SimpleGame PRIVATE GameCore)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT SimpleGame)

# The existing preparer discovers Node, validates inputs and reuses current packs.
# No package manager or network request is used to build the graphics libraries.
set(NodeExecutable "node" CACHE STRING "Node.js 18+ executable used for resource preparation")
add_custom_target(GameResources
    COMMAND powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${PROJECT_SOURCE_DIR}/tools/prepare-resources.ps1" -NodeExecutable "${NodeExecutable}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
add_dependencies(GameCore GameResources)
# Stage before every game build, even when only assets changed and no link runs.
add_custom_target(GameRuntime
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SimpleGame>/Resource/Runtime"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/Resource/Runtime/game-assets.bin"
        "${PROJECT_SOURCE_DIR}/Resource/Runtime/manifest.json"
        "$<TARGET_FILE_DIR:SimpleGame>/Resource/Runtime/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/Resource/UI/Export" "$<TARGET_FILE_DIR:SimpleGame>/Resource/UI/Export"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/Resource/UI/ui_manifest.json" "$<TARGET_FILE_DIR:SimpleGame>/Resource/UI/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/monitor" "$<TARGET_FILE_DIR:SimpleGame>/Monitor"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:SimpleGame>/licenses"
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:SimpleGame>/freeglut.dll" "$<TARGET_FILE_DIR:SimpleGame>/glew32.dll"
    VERBATIM)
add_dependencies(GameRuntime GameResources)
add_dependencies(SimpleGame GameRuntime)

include(CTest)
if(BUILD_TESTING)
    # The existing console and monitor checks write diagnostics at this location.
    file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/x64/RevisionChecks")
    foreach(name Gameplay Movement Civic MapHud SceneGraph RoadPlan ConsoleOutput World
                 WorldGeneration WorldStreaming ResourceAssets ResourceLoading RenderQueueSmoke
                 MonitorServer Startup GlfwPlatform Rendering)
        add_executable(${name}Tests tests/${name}Tests.cpp)
        target_link_libraries(${name}Tests PRIVATE GameCore)
        if(MSVC)
            target_compile_options(${name}Tests PRIVATE /UNDEBUG)
        endif()
        if(NOT name STREQUAL "Rendering")
            add_test(NAME ${name} COMMAND ${name}Tests)
            set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                ENVIRONMENT "LOCALAPPDATA=${PROJECT_BINARY_DIR}/TestUserData/${name}"
                TIMEOUT 600)
        endif()
    endforeach()
    foreach(backend WGL GLFW)
        if(backend STREQUAL "GLFW")
            set(backend_arg --glfw)
        else()
            set(backend_arg "")
        endif()
        add_test(NAME Rendering${backend} COMMAND RenderingTests
            "${PROJECT_BINARY_DIR}/renders-${backend}" ${backend_arg})
        set_tests_properties(Rendering${backend} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            ENVIRONMENT "LOCALAPPDATA=${PROJECT_BINARY_DIR}/TestUserData/Rendering${backend}"
            TIMEOUT 600)
    endforeach()
endif()
