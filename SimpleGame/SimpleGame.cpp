/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)

This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.
*/

#include "stdafx.h"
#include "Game.h"
#include "Renderer.h"
#include "Monitoring/Monitor.h"
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include <windows.h>
#include <cstdio>
#include <exception>

int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(1280, 800);
    glutCreateWindow("헤일로 / 관문 인접 주거 구역 07");
    SetWindowTextW(GetActiveWindow(), L"헤일로 / 관문 인접 주거 구역 07");
    glewInit();
    try
    {
        tutorial::GameRenderer::Get().Initialize();
        tutorial::Game::Get().Reset();
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "게임 리소스 초기화 실패: %s\nResource/Runtime 캐시와 배포 경로를 확인하세요.\n",
                     error.what());
        tutorial::GameRenderer::Get().Shutdown();
        glutDestroyWindow(glutGetWindow());
        return 1;
    }
    glutCloseFunc(tutorial::GameCallbacks::Shutdown);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glutIgnoreKeyRepeat(1);
    monitoring::GameMonitor::Get().Initialize();
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
