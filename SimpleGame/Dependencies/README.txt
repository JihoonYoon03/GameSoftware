그래픽 라이브러리 구성

Win32/: FreeGLUT 3.0.0 및 GLEW 2.1.0의 32비트 import library/DLL.
x64/: 기존에 사용자가 교체한 FreeGLUT import library와 동일한
      로컬 freeglut-3.8.0 x64-Release 빌드의 DLL, GLEW 2.1.0 64비트 파일.

GLEW는 로컬 다운로드 패키지의 헤더가 프로젝트 헤더와 동일한지 확인했다.
프로젝트는 Dependencies/$(Platform)을 우선 검색한다.
빌드 성공 후 동일 플랫폼의 freeglut.dll 및 glew32.dll을 출력 폴더로 복사한다.
루트에 있던 라이브러리는 보존하지만 플랫폼별 파일을 우선 사용한다.
라이브러리 갱신 시 같은 패키지/빌드의 LIB와 DLL을 함께 교체한다.
라이선스는 GLEW-LICENSE.txt 및 각 플랫폼 폴더의 FREEGLUT-LICENSE.txt 참조.
