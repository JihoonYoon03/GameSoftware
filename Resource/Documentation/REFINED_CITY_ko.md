# 유리창 개선·횡단보도 분리·건물 5종

2026-10-22 · Blender 원본과 GLB를 함께 갱신했습니다. 디테일을 먼저 제작한 뒤 외부 형상을 유지하며 내부 면·중복 정점·평면 분할을 정리합니다. 최종 독립 파트는 500삼각형 이하입니다.

## 이동수단과 기존 메시 교체

지상 이동수단 5종의 기존 122개 파트를 새 파이프라인으로 제작했습니다. 제조 모서리와 곡선 단면을 보강하고, 모든 지상 차량·비행체의 기본/대체 캐노피를 새 프레임으로 교체했습니다. 셔틀 승객실 옆창도 벽체와 분리했습니다. 이전의 유리 덩어리 대신 금속 프레임, 고무 실링, 얇은 개별 접합 유리를 사용합니다. 유리는 별도 메시이며 프레임과 같은 부착 본에 고정됩니다. 캐노피를 열 때 실링과 유리가 함께 움직입니다. 바이크는 핸들 지지대를 유지한 분절 윈드스크린입니다.

캐노피 프레임 ID는 유지했습니다. `vehicle_assemblies.json`과 `aircraft_assemblies.json`의 `glazing_companions`에 프레임 → 유리/실링 파트가 기록되어 있습니다. 선택한 프레임과 대응 유리를 함께 사용하십시오. `default_parts`와 `alternate_parts`, 전체 조립 GLB에는 대응 유리가 포함됩니다. `Parts` 파일은 각각 독립 메시 하나이며 공통 바인드 원점과 본 이름은 유지합니다. 유리 재질은 GLB의 표준 alpha BLEND를 사용합니다.

기존 `Hab_Wall_Window_2m`는 실제 개구부와 중앙 멀리언, 단차가 있는 창턱·실링으로 교체했습니다. `Hab_Slit_Window`와 `Interior_Terminal`도 프레임·화면·조작부를 보강했습니다. 기존 5개 건물의 조립본과 원본 장면에도 교체 부품을 반영했습니다.

## 횡단보도와 보행로

- `Export/Transit/Transit_CrosswalkPaint_2L.glb`, `4L`, `6L`: 흰색 횡단보도 페인트만 포함합니다. 도로 바닥·보행로·연석을 포함하지 않습니다. 기존 도로 위에 같은 원점으로 놓습니다.
- `Transit_Road_2L/4L/6L_Crosswalk_4m`: 호환용 도로+페인트 조립체입니다. `Surface`와 `MarkCrosswalk`는 별도 메시입니다. JSON의 `marking_module`, `surface_module`, `walkway_is_separate`로 구분합니다.
- `Road_Crossing_2m.glb`: 이전 2m 자산의 노면. 표식은 `Road_Crossing_Paint_2m.glb`로 분리했습니다. 기존 Blender 배치 예제는 둘을 함께 표시합니다.
- 폭 4m 보행로·경사로·끝막음은 계속 별도 모듈입니다. `16_Transit_Connected_Example`의 횡단보도 예제는 양쪽의 8m 직선 보행로를 2m로 줄였습니다. 페인트 파일을 놓았을 때 긴 보행로가 따라 나오지 않습니다.

## 새 건물 양식

층고는 3.6m입니다. 각 층에는 실제 층판·외벽 베이·기둥을 배치했고 유리·프레임·차양·테라스·설비는 독립 파트입니다. 높이는 기단·옥상 설비까지 포함한 GLB 재가져오기 측정값입니다.

| 조립 파일 | 층수 | 전체 높이 | 특징 |
|---|---:|---:|---|
| `Spire_Helix_12F.glb` | 12 | 46.18m | 4개 층마다 회전하는 외골격, 대각 보강대, 부상 필드 왕관 |
| `Tower_TwinBridge_16F.glb` | 16 | 60.58m | 쌍둥이 타워와 6·12층 스카이브리지 |
| `Arcology_Terrace_10F.glb` | 10 | 38.98m | 단계별 후퇴 매스, 식재 테라스, 테라코타 차양 |
| `Civic_TransitAtrium_4F.glb` | 4 | 17.14m | 양측 서비스동, 분절 유리 아치형 아트리움 |
| `Habitat_Courtyard_3F.glb` | 3 | 13.54m | 열린 중앙 정원, 중정 회랑과 주거 발코니 |

완성본은 `Export/Buildings/`, 새 교체 모듈은 `Export/Buildings/Parts/`에 있습니다. 새 건축 모듈 28종으로 5개 양식을 조립했습니다. `architecture_styles.json`에는 층수·층고·부품 변환·층 인덱스를, `assembly_recipes.json`에는 기존 건물을 포함한 전체 조립 정보를 저장했습니다. 건물 전체 합계는 500개를 초과할 수 있으며 제한은 독립 파트마다 적용합니다.

## 확인 위치

- `Source/Erma_Resource_Library.blend` → `19_Glazing_Review`: 완성 이동수단의 유리창.
- `20_Architecture_Styles`: 새 건물 5종.
- `21_Crosswalk_Separation`: 페인트·노면·보행로를 따로 전시한 장면.
- `22_Legacy_Asset_Refinements`: 이전/개선 유리창과 건축 창호 비교.
- `23_Architecture_Closeups`: 외벽·차양·식재 테라스·분절 유리 아치.
- `24_Refined_Detail_Sources`: 최적화 이전의 독립 원본.
- `Preview/24_glazing_review.png`부터 `28_legacy_refinements.png`: 위 검토 장면의 렌더.
- `Validation/refinement_pipeline_report.json`: 상세 원본/최종 면 수와 표면 보존 오차.
- `Validation/refined_assets_report.json`: 층판·유리 본·실제 창문 움직임·페인트 분리 검사.

재제작은 `build_air_city_expansion.py` → `build_transit_kit.py` → `build_refined_city_assets.py` 순서입니다. 기존 전체 팩 제작 도구는 라이브러리를 초기화하므로 부분 갱신용으로 실행하지 마십시오. 검증은 `verify_resource_pack.py`, Blender용 `verify_detail_pipeline.py`, `verify_refined_assets.py`, `verify_blender_roundtrip.py`, `verify_air_city_roundtrip.py`, 접속 검증 `verify_transit_kit.py`를 사용합니다.
