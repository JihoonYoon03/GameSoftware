# Resource 모델링·내보내기 지침

사용자가 정정한 제작 원칙: **디테일을 먼저 제작한 뒤 불필요한 폴리곤을 정리한다. 납품 파트당 최대 200삼각형이며, 작을수록 디테일을 유지하는 선에서 상한을 낮춘다. 한도를 맞추려고 파트를 쪼개거나 GLB 파일을 늘리지 않는다. 불가피하면 작은 장식 디테일을 일부 줄인다.**

현재 기준은 `Documentation/POLYGON_BUDGET_ko.md`, 파트별 실제 상한은 `Documentation/part_polygon_budgets.json`이다. 단일 원래 파트의 모든 재질 프리미티브를 합산한다. 보편적인 파트당 300폴리곤 업계 표준을 가정하지 않는다.

## 제작 순서

1. 실제 크기·용도·조립 단위를 정하고, 실루엣뿐 아니라 패널 분할·곡면·모서리·체결부·흡배기구·힌지·센서 등 필요한 디테일을 먼저 제작한다. 원본 단계에는 납품 상한을 강제하지 않는다.
2. 편집 가능한 디테일 원본을 최적화본과 별도로 보관한다. 최적화로 디테일을 잃었는지 항상 원본과 비교할 수 있어야 한다.
3. 같은 고정 부품에 완전히 가려진 내부 면, 중복 정점, 불필요한 평면 분할부터 정리한다. 곡률·재질 경계·법선·UV·스킨·가동부·접속면을 보존한다. 움직여서 드러날 수 있는 면을 삭제하지 않는다.
4. 숫자에 맞추는 전역 Decimate나 임의 비율 감축을 기본으로 사용하지 않는다. 같은 파트 안에서 곡률·재질·관절·접속을 보호하며 면 배치를 정리하고 전후를 비교한다.
5. 최종 파트가 크기별 상한(최대 200삼각형)을 넘으면 같은 파트의 토폴로지를 정리한다. 그래도 초과하면 나사·봉제선·작은 홈·트림을 먼저 통째로 제거하고, 외곽 곡면과 실루엣을 보호한다. 특히 추진장치의 원래 곡선 분할을 보존한다. 한도를 맞추기 위한 파트 분할·companion 생성·추가 모델 파일은 금지한다. 상한을 채우기 위한 면도 추가하지 않는다.
6. 원본/최적화본 수치와 보존 검사 결과, 실제 GLB 검사·재가져오기·렌더 미리보기를 남긴다.

## 도구·저장

- 재사용 제작 모듈: `Tools/detail_mesh_pipeline.py`. `build_air_city_expansion.py`, `build_refined_city_assets.py`, `build_character_detail_expansion.py`에 적용되어 있다. 지상 차량 5종·비행체·도시 프롭·선별한 기존 건축/가구·새 건축 5종과 인체 관련 기존 785개 파트를 갱신했다. 생물·통행로 전체를 다시 제작한 상태는 아니다.
- 상세 원칙·현재 적용 범위: `Documentation/MODELING_PIPELINE_ko.md`.
- Blender의 `17_Detail_Sources`에 개별 원본, `18_Detail_Comparison`에 이전/원본/최적화 비교가 있다.
- 후속 갱신의 원본은 `24_Refined_Detail_Sources`, 유리창 비교는 `22_Legacy_Asset_Refinements`, 건물 5종은 `20_Architecture_Styles`에 있다. 유리 패널+실링은 프레임의 별도 companion이며 같은 본으로 조립한다. 각 `Parts` GLB는 독립 메시 하나를 유지한다.
- 캐릭터 상세 원본은 `25_Character_Detail_Sources`, 비교·확대·머리 검토는 `26_Character_Detail_Comparison`~`28_Character_Head_Study`에 있다. 인체 관련 854개 파트는 기존 785개 교체 + 흉부 외장/하네스 69개이다. `character_detail_assemblies.json`에 따라 Torso와 대응 OuterAssembly를 함께 조립한다. 23본·15클립·피부색·장비 오프셋을 유지하고 새 스킨을 원본과 대조한다.
- 횡단보도 페인트 `Transit_CrosswalkPaint_2L/4L/6L`과 `Road_Crossing_Paint_2m`는 노면·보행로를 포함하지 않는다. 보행로는 별도 모듈을 연결한다.
- 새 도시계획 도로는 `ROAD_MODULE_REQUEST.md`를 따른 `road_plan_v1` 확장이다. `Tools/build_road_plan.py`와 공통 상세 파이프라인을 사용한다. 2/4차선·T4+4+2·T2·X2·R7/18/36 곡선·110m 램프·고가·별도 보도/구조물의 P0/P1 88개 모듈, 428개 독립 파트이다. `Documentation/ROAD_PLAN_ko.md`, `road_plan_modules.json`, `road_plan_examples.json`이 현재 계약이다. `Export/RoadPlan`과 독립 `Source/RoadPlan_Modules.blend`로 납품한다. 형상은 `asset_id`로 식별한다.
- 도로계획 확장은 노면 8m 상승형(구조 포함 하부 7.6m)과 권장 8.4m 상승형(하부 8m)을 함께 제공한다. 두 램프 최대 경사는 9% 이내이다. 8.4m형은 게임 그래프 고도 4.2월드 단위와 함께 통합해야 하며 모델링 도구가 게임 코드/Runtime을 수정하지 않는다. 180m 고가는 16m×11+4m 마감으로 조립한다. 지상/고가 level은 분리하고 교각은 도로·보도 예약 공간 밖에 둔다.
- RoadPlan 독립 파일은 append 전용 데이터 라이브러리가 아니라 바로 열리는 일반 프로젝트로 저장한다. `road_plan_source.write_project`의 인메모리 정리는 마스터 저장 후 독립 도로 프로젝트에만 저장한다. 메인 원본의 기존 자산과 기본 장면은 보존한다.
- 재제작 순서: 공통 원본 → air/city → transit → refined city → character detail. 앞 단계 제작 도구는 뒤 단계의 조립체를 자동 갱신하지 않으므로 뒤 단계를 다시 실행하고 검증한다.
- 기존 라이브러리를 열어 해당 제작 도구가 소유한 데이터만 갱신한다. 다른 확장·본·재질·조립 데이터·기본 진입 장면을 보존한다.
- 도시 반응 애니메이션은 `Tools/civic_motion.py`, `build_civic_animations.py`가 소유한다. 인체는 기존 43개에 조준 이동 16개를 더한 59개 클립이며, 차량 반응 액션 21개를 유지한다. 방향별 이동은 `Documentation/aim_locomotion_blending.json`을 따르며 몸의 조준 방향을 고정하고 위상을 공유한다. `Documentation/civic_animation_contract.json`과 `CIVIC_ANIMATION_GUIDE_ko.md`를 따른다. 30fps 편집 타임라인에 반 프레임 키를 두고 GLB는 60Hz로 샘플링한다. 원래 본/바인드/메시/스킨/장비 오프셋과 메인 진입 장면은 보존한다. `Source/Civic_Animations.blend`는 직접 열어 재생하는 별도 검토 프로젝트다. 게임이 합성하는 Ride와 이름이 겹치지 않도록 새 기준은 Ride_Base이며 자동 연결하지 않는다. 새 반응의 루트 이동·yaw, 차량 차체/좌석 이동을 만들지 않는다.
- 파트 공통 원점·미터 단위·축·본 이름과 통행로 접속 좌표를 지킨다. 원점 재중앙화와 임의 비율 변경을 하지 않는다.

## 확정된 아트 방향

- SF 개척지 스타일. 비행체는 분절된 부상 필드와 발광 코어를 사용하며, 현대적인 노출 로터·프로펠러를 다시 넣지 않는다.
- 차량 신호기는 지지대 없이 공중에 뜬다. 보행자 신호는 도로 표면의 바닥 홀로그램으로 표현한다. 보행자 신호 헤드·기둥을 추가하지 않는다.
- 사용자 요청 범위에 맞춰 제작하고, 다른 작업에서 변경 중인 게임 코드·Runtime 캐시를 모델링 작업에 함께 수정하지 않는다.
