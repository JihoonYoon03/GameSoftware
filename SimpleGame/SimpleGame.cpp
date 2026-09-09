/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)

This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.
*/

#include "stdafx.h"
#include "Tutorial.h"
#include "TutorialGraphics.h"
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include <windows.h>

int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1280, 800);
    glutCreateWindow("헤일로 / 관문 인접 주거 구역 07");
    SetWindowTextW(GetActiveWindow(), L"헤일로 / 관문 인접 주거 구역 07");
    glewInit();
    tutorial::InitializeGraphics();
    glutCloseFunc(tutorial::ShutdownGraphics);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glutIgnoreKeyRepeat(1);
    tutorial::Reset();
    glutDisplayFunc(tutorial::Draw);
    glutReshapeFunc(tutorial::Resize);
    glutKeyboardFunc(tutorial::KeyDown);
    glutKeyboardUpFunc(tutorial::KeyUp);
    glutTimerFunc(16, tutorial::Tick, 0);
    glutMainLoop();
    return 0;
}
