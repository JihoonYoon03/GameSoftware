# 에르마 · 헤일로 Blender 리소스

세계관에 맞춘 조립식 로우 폴리 그래픽 팩입니다. 모든 작업 파일은 이 Resource 폴더 안에 있습니다.

- **1,031개 독립 메시 파트**: 인체·의복·액세서리 785 / 몹 63 / 건축·환경 61 / 차량 122. 기존 인체·의복 호환 파트 199개를 포함합니다.
- **파트당 최대 424삼각형**: 삼각형 변환 후에도 요청한 500개 이하
- **체형 6종**, 목적별 의복 **9종 × 6체형**, 독립 액세서리 **34개**
- **인물 조립 예제 10개**, 몹 3계열·머리 교체형 포함 **6개**, 건물 예제 **5개**, 차량 5종의 A/B 예제 **10개**
- 공통 인체와 3개 몹 리그에 **각 15개 스켈레탈 클립**, 압력문에 개폐 2개, 차량에 타입별 10~11개
- 512×512 반복 PBR 재질 **8세트·PNG 24개**
- **GLB 1,063개**, Blender 편집 원본, 조립·본·클립·블렌딩 JSON, 검증 결과와 렌더 미리보기

시작 파일은 [Blender 원본](Source/Erma_Resource_Library.blend)입니다. 기본 Scene은 인물 도감이며, Scene 메뉴에서 몹·주거 구역·실내·자연·애니메이션 리뷰로 이동할 수 있습니다.

[상세 사용·조립 안내](Documentation/README_ko.md) · [애니메이션 조합 안내](Documentation/ANIMATION_GUIDE_ko.md) · [조합 데이터](Documentation/assembly_recipes.json) · [애니메이션 블렌딩 데이터](Documentation/animation_blending.json)

[새 체형·의복·액세서리 설계](Documentation/CHARACTER_DESIGN_ko.md) · [차량 조립 안내](Documentation/VEHICLES_ko.md)

![리소스 도감](Preview/00_asset_overview.jpg)

제작 당시에는 게임 코드 연결을 수행하지 않았습니다. `Tools`는 Blender 제작·검증용 원본 도구입니다.

## 게임 연결 — 2026-10-21

게임은 `Runtime/game-assets.bin`의 변환 캐시를 읽습니다. 원본 GLB와 Blender 파일은 유지합니다.
변환 도구는 저장소 루트의 `tools/pack-resources.mjs`이며 외부 npm 패키지 없이 Node.js 18 이상에서 실행합니다.

```text
node tools/pack-resources.mjs
node tools/pack-resources.mjs --check
node tools/check-resource-pack.mjs
```

첫 명령은 런타임 캐시와 원본 SHA-256·모델 매핑을 담은 `Runtime/manifest.json`을 생성합니다.
두 번째는 캐시·원본 해시가 일치하는지 검사하며 파일을 변경하지 않습니다. 게임 빌드/실행은 아닙니다.
세 번째는 바이너리를 독립적으로 읽어 해시·정점·법선·가중치·소켓·지면·클립 경계를 검사합니다.
Visual Studio 프로젝트는 빌드 후 두 파일을 실행 파일 옆 `Resource/Runtime`에 복사합니다.
게임 실행에 Node.js·Blender·GLB 파서가 필요한 것은 아닙니다.

인물·차량·몹의 스킨은 4개 가중치와 최대 32개 본으로 출력하고, 선택한 GLB 클립을 30Hz 팔레트로 캐싱합니다.
인체는 23본을 유지하며 이동·상체 조준을 결합합니다. 바이크 자세는 Jump_Air 하체와 Aim_Base 상체를
조합한 런타임용 Ride 포즈입니다. 리소스의 별도 탑승 클립을 재생한다고 주장하지 않습니다.
BaseColor는 UV 위치에서 정점색으로 샘플링하며, 표면 종류와 독립 팔레트를 기존 조명 경로에 연결합니다.
Normal/ORM 텍스처의 픽셀 단위 PBR 샘플링과 모든 GLB 확장 기능을 지원하는 범용 glTF 뷰어는 아닙니다.
지면은 상향 면을 추출해 높이 0에 놓고, 건물은 원래 수평 비율·실측 높이에 맞춥니다.
게임의 빌드·실제 화면·장시간 플레이 검증은 사용자 확인 대기입니다.
