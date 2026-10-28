# 도로 우선 도시계획 모듈 납품

> 2026-10-26 갱신: 현재 납품본은 [크기별 최대 200삼각형·분할 금지 규칙](POLYGON_BUDGET_ko.md)을 적용했습니다. 아래 500 상한·이전 최대값·무손실 수치는 상세 제작 당시의 기록이며, 현재 수치는 예산 목록을 따릅니다.


2026-10-23 · 제작 명세: [도로 모듈 모델링 요청서](../ROAD_MODULE_REQUEST.md)

요청서의 P0·P1을 **조립 모듈 88개 / 독립 파트 428개 / GLB 516개**로 제작했습니다. 6차선·터널 등 P2 확장은 추가하지 않았습니다. 노면·도색·보도·연석·난간·거더·교각·배수구는 독립 파트입니다. 새 파트는 상세 원본 43,680삼각형에서 외부 표면을 유지하며 40,571삼각형으로 정리했으며, 최종 파트의 최대값은 **492삼각형**입니다.

## 파일

- **`Source/RoadPlan_Modules.blend`**: 이번 도로 모듈만 포함한 편집 프로젝트. 열면 `29_RoadPlan_Ground_Connections` 검토 장면이 표시됩니다. `RoadPlan_Library`는 공통 원점의 실제 파트입니다.
- `Source/Erma_Resource_Library.blend`: 기존 전체 자산에 도로 확장을 합친 원본. 기본 진입 장면 `16_Transit_Connected_Example`은 보존했습니다. 파트 컬렉션은 `00_Library / RESOURCE_LIBRARY / 20_RoadPlan_Modules`입니다.
- **`Export/RoadPlan/*.glb`**: 조립 모듈 88개.
- **`Export/RoadPlan/Parts/*.glb`**: 독립 단일 메시 파트 428개. 합치거나 개별 중심으로 재배치하지 않았습니다.
- **`Documentation/road_plan_modules.json`**: 미터 단위 bounds, 포트·차선·level·높이·경사·외곽·충돌 예약 공간·하부 여유·companion.
- `Documentation/road_plan_examples.json`: 위치·회전·접속 쌍, 전체 고가와 현재 월드 위치의 대응.
- `36_RoadPlan_Detail_Sources`: 편집 가능한 최적화 이전 원본.

## 요청 항목별 대응

| 범위 | 파일/접두사 | 제작 규격 |
|---|---|---|
| P0 직선 | `RoadPlan_Surface_2L/4L_L4/L8/L16` | 폭 6/12m, 길이 4/8/16m, 중앙 원점·양끝 ±L/2 포트 |
| P0 혼합 T | `RoadPlan_T_4L_2L` | ±X 4차선 두 포트, +Y 2차선 한 포트, 거리 10m·차선 합 10 |
| P0 생활 T/사거리 | `RoadPlan_T_2L`, `RoadPlan_X_2L` | 포트 거리 7m, 차선 합 6/8. 각각 하나의 연속 노면 외곽 |
| P0 폭 전환 | `RoadPlan_Transition_4L_2L` | 길이 24m, ±12m 포트, 폭 12→6m. 양끝 폭 변화율 0 |
| P0 생활 곡선 | `RoadPlan_Curve_2L_A90_R7` | 중심선 반경 7m·90도. 시작 (0,0), 종료 (7,7), 폭 6m |
| P0 지역 곡선 | `RoadPlan_Curve_2L_A15/A30/A45_R18/R36` | 각도 15/30/45도 × 반경 18/36m, 시작/끝 포트·접선·중심선 표본 |
| P0 램프 | `RoadPlan_Ramp_2L_Rise8_L110`, `Rise8p4` | 아래 높이 표 참조. 하단 원점 (0,0,0), 상단 (110,0,H) |
| P0 고가 | `RoadPlan_Deck_2L_L8/L16` | 폭 6m·level 1. 노면·하부 구조·양측 난간 분리 |
| 마감 고가 | `RoadPlan_Deck_2L_L4` | 현재 180m 평탄 구간을 16m×11+4m로 정확히 조립하는 P1 마감. 스케일을 늘리지 않음 |
| P0 횡단보도 | `RoadPlan_CrosswalkPaint_2L/4L` | 길이 4m 명목 띠·횡단 폭 6/12m, 페인트만 포함. 실제 줄 길이 3.6m·여백 0.2m |
| P0 보행로 | `RoadPlan_Walk_W2_L2/L4/L8`, `Walk_W2_EndCap_L1` | 유효 폭 2m, 표면 Z0.15m, 별도 짧은 끝막음 |
| P1 교차로 보도 | `RoadPlan_WalkCorner_T_4L_2L/T_2L/X_2L` | 도로와 같은 원점·회전. 연석 0.12m를 제외한 보행 폭 2m, 접근 경로 포트 |
| P1 곡선/전환 보도 | `RoadPlan_WalkCurve_*`, `WalkTransition_4L_2L` | 도로 외곽과 동일한 중심선·분할. 안쪽 보행 폭도 1mm 허용 오차 이내 |
| P1 보도 경사로 | `RoadPlan_AccessibleRamp_W2/W4` | 4m 길이·0.15m 높이 변화·양끝 접선 수평. 별도 점자 카세트 |
| P1 난간 | `RoadPlan_Parapet_L4/L8/L16`, `RampParapet_*`, `Barrier_EndProtection` | 상부·중간·발끝 레일, 수직 지지·기초판·체결부·반사/발광 표식. 차로를 침범하지 않음 |
| P1 구조물 | `RoadPlan_DeckStructure_*`, `RampStructure_*`, `Pier_H7p6/H8`, `Abutment_W6` | 독립 거더·크로스헤드·교각·교대. 교각 위치를 교차 중심에 고정하지 않음 |
| P1 가장자리 | `RoadPlan_Curb_L2/L4/L8`, `Drain_L2`, `EdgeCap_2L/4L` | 별도 연석·교체식 배수 그레이팅·노면 끝막음. 배수 그레이팅 상면 Z0 |
| 지면 신호 | `RoadPlan_PedestrianHolo_2L/4L` | 횡단보도 양측 바닥 홀로그램만. 기둥·보행 신호 헤드·도로 판 없음 |

도색은 `Marking_*`, `CurveMarking_*`, `JunctionMarking_*`, `TransitionMarking_*`, `DeckMarking_*`, `RampMarking_*`로 분리했습니다. 우측통행의 중앙선·차로 구분·갓선과 진입 차로의 정지선만 사용합니다. 직진/좌회전/우회전 화살표를 기본 노면에 굽지 않았습니다.

## 높이·경사와 실제 게임 좌표

| 램프 | 수평 길이 | 상승 | 실제 메시 최대 경사 | 구조 포함 상단 하부 높이 |
|---|---:|---:|---:|---:|
| `Rise8_L110` | 110m | 8m | 8.51064% | 7.6m |
| `Rise8p4_L110` | 110m | 8.4m | 8.93617% | 8.0m |

양 끝 종단곡선은 각각 16m, 중간 경사 구간은 78m입니다. 두 종류 모두 전 구간 9% 이하이며 양 끝 접선은 수평입니다. 데크·거더 합계 깊이가 0.4m이므로 **현재 그래프의 상부 노면 8m에 구조물을 붙이면 통과 높이는 7.6m**입니다. 최소 통과 높이 8m가 필요한 배치는 노면을 8.4m로 올리는 `Rise8p4`를 사용해야 합니다.

고가 예제는 **110m 램프 + 180m 고가 + 110m 램프**로 조립했습니다. 로컬 원점에 미터 위치 (96,-200,0), Z 회전 90도, 게임 변환 0.5를 적용하면 요청서의 월드 (48,-100) → (48,-45) → (48,45) → (48,100)에 대응합니다. 권장형을 연결할 때 상부 그래프 고도도 **월드 4 → 4.2**로 갱신해야 합니다. 메시만 높이고 차량 그래프를 그대로 두면 안 됩니다.

동서 집산도로와 만나는 고가 로컬 위치는 166m입니다. 조립 예제의 교각은 그 지상 도로·양측 보도의 예약 폭 밖에 배치했습니다. 하부 통과 예제의 실제 구조 최저점은 Z8.0m이며 지상 노면은 Z0입니다. 지상/고가 포트의 level 0/1은 별개입니다.

현재 게임의 임시 +0.05m 배치 오프셋은 원본에 굽지 않았습니다. 이를 유지할 경우 지상과 고가에 동일한 기준 오프셋을 적용해 높이 차를 보존해야 합니다. JSON의 `clearance_envelope`는 램프 아래를 높이에 따라 구분하며, 보행 여유 2.2m와 차량 여유 8m의 허용 여부를 표본별로 제공합니다. 낮은 램프 아래를 보행·건축 가능 공간으로 간주하지 마십시오.

## 조립·통합 계약

1. 단위는 미터, Blender +Z 위·+X 진행입니다. GLB는 (x,y,z)→(x,z,-y)입니다. 메시·포트·예약 공간을 모두 0.5배 해야 게임 단위에 맞습니다.
2. 포트의 kind·width·lanes·level을 맞추고, 변환한 position을 일치시키며 outward는 반대로 놓습니다. 같은 XY라도 높이/level이 다른 포트를 합치지 않습니다.
3. 각 조립 GLB는 해당 기능만 담습니다. `companions`의 모듈을 지정한 로컬 위치·회전으로 함께 배치하면 도색·보도·연석·하부 구조·난간이 연결됩니다. companion 배치에는 음수 스케일과 길이 보정이 없습니다.
4. 교차로 접근 보행 포트는 외부의 4m 횡단 띠와 보도 경사로로 이어지는 경로입니다. 중앙 회전 영역 안에 횡단보도·화살표를 굽지 않았습니다. 접근성 예제는 페인트·노면·W2/W4 경사로·상부 보행로를 독립 배치하고 실제 포트로 연결합니다.
5. `collision_envelope`의 bounds는 예약·광역 검사에 쓰는 보수적 공간입니다. 경사로 전체 AABB를 평평한 최종 주행 충돌로 사용하지 말고 실제 노면/높이 표본을 연결하십시오. 도색과 신호는 collider가 없습니다.
6. 바이저처럼 색에 상태 로직을 넣지 않습니다. 지면 홀로그램의 `signal_binding`은 외부 보행 신호 상태를 요구합니다. 기존 공중 부유 차량 신호기는 기존 자산을 계속 재사용할 수 있습니다.
7. 식별은 노드 extras의 **`asset_id`**를 사용합니다. Blender 표시 이름은 63자 제한으로 축약될 수 있지만 GLB·카탈로그의 전체 ID는 보존됩니다.
8. 이 작업에서는 **게임 코드와 `Resource/Runtime`을 수정하지 않았습니다.** 게임 측에서 모델 등록·캐시 변환·임시 도로 교체·24m 전환 포트와 8.4m 고도 조정·충돌·층별 가림을 연결하는 단계가 남습니다.

공유 재질은 `Road_Asphalt`, `Road_Edge`, `Walk_Paving`, `Marking_White`, `Marking_Center`, `Barrier_Metal`, `Structure_Concrete`, `Signal_Emission`의 8종입니다. 형상·UV·노멀·탄젠트를 포함하며, 유효한 MikkTSpace 데이터를 유지하고 드문 0벡터 탄젠트는 실제 삼각형 UV 미분으로 복원한 뒤 다시 검사했습니다. 이 과정에서 위치·인덱스·UV·재질을 바꾸지 않습니다.

## 검증과 미리보기

- `road_plan_export_report.json`: 조립 88개·단일 파트 428개, 실제 노면의 포트 113개, 예제 접속 22쌍. 위치 오차 최대 **0.00000094m**, 실제 경계 접선 오차 최대 **0.0800°**. 퇴화 면·비정상 법선/탄젠트·차도 위 보도·중복 교차 노면·교각 침범 검사 통과.
- `road_plan_roundtrip_report.json`: 조립체 88개를 Blender로 재가져와 축·크기·bounds를 대조했습니다. 상세 원본/최종 428쌍과 기존 1,865개 자산의 형상 보존 해시도 확인했습니다.
- `road_plan_pipeline_report.json`: 상세 원본/최종 면 수·표면 오차·정리 방식. 숫자 비율에 따른 Decimate는 사용하지 않았습니다.
- `export_report.json`: 기존 자산을 포함한 **GLB 2,575개** 검사 통과.

| Blender 장면 | 렌더 |
|---|---|
| `29_RoadPlan_Ground_Connections` | `32_road_plan_ground.png` — 2/4차선 접속·혼합 T·생활 T/X·전환·좌우 곡선 |
| `30_RoadPlan_Curves_Transition` | `33_road_plan_curves.png` — R7/18/36 곡선과 보도 |
| `31_RoadPlan_Accessibility` | `34_road_plan_accessibility.png` — 페인트·도로·보도 경사로 분리 |
| `32_RoadPlan_Overpass_Assembly` | `35_road_plan_overpass.png` — 램프/180m 고가/램프 전체 |
| `33_RoadPlan_GradeSeparated_Crossing` | `36_road_plan_grade_separation.png` — 지상과 고가 독립 교차 |
| `34_RoadPlan_Structure_Details` | `37_road_plan_structure_details.png` — 난간·거더·교각·배수·마감 |
| `35_RoadPlan_Side_Elevation` | `38_road_plan_ramp_elevation.png` — 실제 비율의 옆면·길이·높이 |

제작 도구는 `Tools/build_road_plan.py`입니다. 기존 원본을 열어 소유 확장만 갱신합니다. `-- --geometry-check`로 파일 납품 전 상한을 확인할 수 있습니다. 검증은 `verify_road_plan.py`, Blender용 `verify_road_plan_roundtrip.py`, 공통 `verify_resource_pack.py`를 사용합니다. 저장본 렌더만 갱신하려면 `render_road_plan.py`를 사용합니다.
