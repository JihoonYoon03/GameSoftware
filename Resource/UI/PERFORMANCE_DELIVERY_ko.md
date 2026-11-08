# UI 성능 리소스 납품 — 2026-11-09

`PERFORMANCE_RESOURCE_REQUEST.md`의 P0/P1에 대응하는 리소스입니다. 원본은 보존했고 게임 로더·배포·Runtime 팩은 변경하지 않았습니다. 로딩 시간이나 프레임 성능 향상은 측정하지 않았습니다.

## 통합 아틀라스

`Export/Atlases/ui_complete.png`와 `ui_complete.json`은 2048×2048 RGBA8입니다. 기존 127개 ID·PNG 해시·크기·RGBA 픽셀을 유지하며 새 활동 부품을 포함한 총 145개 스프라이트를 담았습니다. 기존 개별 PNG와 아이콘 전용 아틀라스도 남겼습니다.

JSON의 `sprites` 키는 `Icons/<stem>`, `HUDIcons/<stem>`, `Components/<stem>`입니다. 각 영역은 좌상단 원점·아래로 증가하는 행 방향의 정수 `x/y/width/height`이며 여백을 제외합니다. `source_sha256`은 개별 PNG, `rgba_sha256`은 디코딩한 RGBA, `atlas.sha256`은 전체 PNG 해시입니다. 알파는 straight, 색 공간은 sRGB입니다.

투명 여백은 각 면 4px이며 픽셀 리사이즈 없이 복사합니다. 반 픽셀 안쪽 UV를 제공합니다. 9분할 패널의 inset 12px와 의도된 내부 여백을 유지해야 합니다. **고정 크기 활동 테두리는 행마다 맞는 크기로 쓰며 작은 페이지 점에 9분할을 적용하지 않습니다.** 필터는 linear, wrap은 clamp-to-edge, 밉맵은 비활성 기준입니다. 밉맵을 추가하면 별도 여백/LOD 검수가 필요합니다.

`Validation/20261109/atlas_report.json`에 기존 127개 누락·중복·원본 해시와 전체 145개 crop의 픽셀 일치, 투명 여백, 격리 이미지 대비 선형 필터 결과를 기록했습니다. `Source/performance_baseline_20261109.json`은 수정 전 원본 기준입니다. 생성은 `python Resource/UI/Tools/build_complete_atlas.py`입니다.

현재 게임 로더는 이 JSON을 읽도록 변경하지 않았습니다. 통합 담당자는 스키마/버전/해시/크기/영역/중복·누락 검사와 배포 항목을 연결해야 합니다. 실패를 성공으로 처리하지 않으며 원본 폴백 여부는 통합 시 결정합니다.

## 정적 배경 레이어 대안

`Export/Backdrop/backdrop_manifest.json`은 1280×800과 2560×1600의 별·관문 링·원경 건물·레일을 지정합니다. 원경은 smooth/cartoon 두 종류라 전체 PNG는 10개입니다. 별/관문/레일의 편집 SVG는 `Source/Backdrop`에 있습니다. 원경의 원본은 기존 건물 모델과 변경하지 않은 Runtime 팩이며 manifest에 입력 해시와 모델명을 기록했습니다.

별 65개와 관문 8겹×97표본, 원경 23개는 `Drawing::DrawBackdrop`의 위치/스케일을 따릅니다. 원경은 기존 Habitat/Terraced/Workshop/Dome 삼각형·팔레트·직교 투영을 숨겨진 WGL 문맥에서 오프라인 캡처했습니다. 별/관문/레일은 SVG로 래스터화해 GPU별 선 굵기 제한 없이 2배 크기로 확대합니다. straight-alpha 변환 후 투명 영역과 출력 크기를 검사했습니다.

회전 관문 표식 12개, 차량 4대, 완료 효과, `N - 0 / 관문망` 등 텍스트는 굽지 않았습니다. 순서는 manifest의 `composition`을 따릅니다. 정적 레이어를 전부 한 장으로 먼저 합치면 동적 요소의 앞뒤 순서가 달라질 수 있습니다. 모델 그림자·조명·깊이·MSAA/후처리 정책은 게임 통합 시 확인해야 합니다.

[비교 이미지](Validation/20261109/backdrop_comparison_r2.png)는 smooth/cartoon, 원본/절반/1/4 렌더 크기, 예시 채도/대비 후처리를 비교합니다. **오프라인 대안의 비교이며 실제 게임 백엔드·기존 후처리와의 픽셀 일치 검증이 아닙니다.** native 비교를 완료했다고 표시하지 않았습니다. 실제 통합 시 기존 DrawBackdrop 캡처와 나란히 비교하고 필요한 차이를 해결해야 합니다.

생성은 `python Resource/UI/Tools/build_backdrop.py`입니다. 현재 Runtime 팩을 읽고 WGL·Pillow·Node/Sharp를 사용하며 팩을 재생성하거나 수정하지 않습니다. UI와 별도로 월드 렌더 경로에 합성하고, UI 글자를 월드의 저해상도 후처리에 함께 넣지 않습니다.

## 후속 런타임 연결 — 2026-11-09

사용자 적용 지시로 사전 제작 아틀라스 JSON/PNG와 정적 배경을 소스에 연결했다. Windows SHA-256으로 PNG와 crop RGBA를 확인하며 개별 UI PNG를 런타임에 다시 패킹하지 않는다. 배경 10개 PNG 해시/메타데이터는 초기화에서 확인하고 선택한 1x/2x·smooth/cartoon 이미지는 처음 사용할 때 디코딩/업로드 후 캐시한다. GPU 텍스처는 기존 컨텍스트 생존 중 해제한다. Export JSON도 게임 옆에 배포한다. 원본 제작 리소스와 모델 Runtime 팩은 유지한다.

오프라인 이미지 대조는 통과했으며 실제 게임의 합성/깊이·MSAA·후처리와 성능/로딩 시간 비교는 실행하지 않았다. 성능 향상 수치를 새로 주장하지 않는다. [게임 통합 기록](INTEGRATION_ko.md#후속-게임-통합--2026-11-09)을 따른다.
