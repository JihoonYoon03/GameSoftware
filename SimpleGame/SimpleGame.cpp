/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)

This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.
*/

#include "stdafx.h"
#include "Game.h"
#include "Hud.h"
#include "Renderer.h"
#include "Profiler.h"
#include "Monitoring/Monitor.h"
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include <windows.h>
#include <cstdio>
#include <exception>
#include <chrono>

int main(int argc, char **argv)
{
    profiling::InitializeConsoleOutput();
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const auto milliseconds = [](auto from, auto to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(1280, 800);
    glutCreateWindow("헤일로 / 관문 인접 주거 구역 07");
    SetWindowTextW(GetActiveWindow(), L"헤일로 / 관문 인접 주거 구역 07");
    glewInit();
    const auto contextReady = Clock::now();
    double renderResourcesMs = 0, worldMs = 0;
    try
    {
        tutorial::GameRenderer::Get().Initialize();
        const auto resourcesReady = Clock::now();
        renderResourcesMs = milliseconds(contextReady, resourcesReady);
        tutorial::Game::Get().Reset();
        tutorial::Hud::OpenMenu(tutorial::UiScreen::Title);
        worldMs = milliseconds(resourcesReady, Clock::now());
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr,
                     "게임 리소스 초기화 실패: %s\nResource/Runtime 캐시 및 Resource/UI/Export 배포 경로를 "
                     "확인하세요.\n",
                     error.what());
        tutorial::GameRenderer::Get().Shutdown();
        glutDestroyWindow(glutGetWindow());
        return 1;
    }
    glutCloseFunc(tutorial::GameCallbacks::Shutdown);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glutIgnoreKeyRepeat(1);
    const auto monitorStarted = Clock::now();
    monitoring::GameMonitor::Get().Initialize();
    const auto ready = Clock::now();
    std::printf("{\"event\":\"startup_ready\",\"window_context_ms\":%.3f,\"render_resources_ms\":%.3f,"
                "\"world_ms\":%.3f,\"monitor_ms\":%.3f,\"total_ms\":%.3f}\n",
                milliseconds(started, contextReady), renderResourcesMs, worldMs,
                milliseconds(monitorStarted, ready), milliseconds(started, ready));
    glutDisplayFunc(tutorial::GameCallbacks::Draw);
    glutReshapeFunc(tutorial::GameCallbacks::Resize);
    glutKeyboardFunc(tutorial::GameCallbacks::KeyDown);
    glutKeyboardUpFunc(tutorial::GameCallbacks::KeyUp);
    glutSpecialFunc(tutorial::GameCallbacks::SpecialKeyDown);
    glutPassiveMotionFunc(tutorial::GameCallbacks::MouseMotion);
    glutMotionFunc(tutorial::GameCallbacks::MouseMotion);
    glutMouseFunc(tutorial::GameCallbacks::MouseButton);
    glutEntryFunc(tutorial::GameCallbacks::MouseEntry);
    glutTimerFunc(16, tutorial::GameCallbacks::Tick, 0);
    glutMainLoop();
    monitoring::GameMonitor::Get().Shutdown();
    return 0;
}
