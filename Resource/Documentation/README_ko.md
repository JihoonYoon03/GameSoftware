# 에르마 · 헤일로 그래픽 리소스 팩

Blender 4.5.2 LTS에서 제작한 로우 폴리 모듈 라이브러리입니다. 게임 컨셉은 사용자가 읽기를 허용한 `GAME_DOCUMENTATION.md`의 SF 개척지·압력 선체·선행 문명 유적·발광 식생·공간 왜곡을 따랐습니다. 몹의 명칭과 외형은 아트 제안이며, 우주 밖 존재의 정체나 스토리를 확정하지 않습니다.

이 작업의 모든 산출물은 Resource 안에 있습니다. 게임 코드와 원래 기획 문서는 수정하지 않았습니다. `Tools`의 Python 파일은 Blender 제작·내보내기·검증용 도구입니다.

## 열어보기

- `Source/Erma_Resource_Library.blend`: 편집 가능한 메시·스켈레톤·스킨·재질·Actions·NLA·조립 예제·조명·카메라 원본.
- Blender 상단 Scene 메뉴의 `01_Characters`, `02_Creatures`, `03_Halo_Settlement`, `04_Interior_Kit`, `05_Environment`, `06_Animation_Review`에서 확인합니다. 체형·의복 비교는 `07_Body_Profiles`, `08_Outfit_Purposes`, `08b_Outfit_Silhouette_Check`, 차량은 `09_Vehicles`, `09b_Vehicle_Variants`, `09c_Vehicle_Exploded`입니다.
- `00_Library`의 컬렉션은 교체용 파트가 공통 원점에 겹쳐 있습니다. 필요한 파트/컬렉션만 표시하거나 Asset Browser로 사용합니다.
- `Preview`: Blender 렌더 이미지, 동작별 8프레임 시퀀스, 애니메이션 GIF와 접촉 시트.
- `Export/Characters`, `Creatures`: 조립된 스켈레탈 GLB. 각 캐릭터/몹에 15개 애니메이션 포함.
- `Export/Modules`: 교체용 신체·의복·몹 부위·건축·가구 GLB. 동적 부위에는 공통 리그와 스킨을 포함하고 동작 중복은 생략했습니다.
- `Export/Buildings`: 기존 5종과 새 5종, 총 10개 조립식 건물 GLB. 파트는 합쳐지지 않아 개별 교체가 가능합니다.
- `Export/Environment`: 지면·도로·바위·발광 식생 GLB.
- `Export/Vehicles`: 차량 5종의 A/B 조립 예제 10개와 `Parts` 하위의 교체 파트 133개.
- `Export/Aircraft`: 비행체 4종의 A/B 조립 예제 8개와 `Parts`의 교체 파트 90개. 각 타입에 17본·12개 클립.
- `Export/Scenery`: 도시·도로·야외 배경 조립체 34종과 `Parts`의 파트 137개. [확장 사용 안내](AIR_CITY_ko.md)를 참고합니다.
- `Export/Textures`: 직접 생성한 512×512 반복 PBR PNG 8세트. 각 세트는 BaseColor / OpenGL Normal / ORM입니다. ORM은 R=AO(1), G=Roughness, B=Metallic입니다.
- `Validation`: 실제 메시·GLB 검사 결과와 제작 로그.

2026-10-27 현재 통행로 확장을 포함한 전체 독립 파트는 2,293개, GLB는 2,575개입니다. 지상 차량·비행체·도시 프롭·선별한 기존 자산·새 건축을 포함한 394개 파트와 캐릭터 854개 파트, 도로계획 428개까지 총 1,676개에 디테일 원본과 보존 최적화를 적용했습니다. 현재 전체 파트의 최대값은 196삼각형입니다. [2026-10-26 크기별 예산과 분할 금지 정책](POLYGON_BUDGET_ko.md)이 이전 수치보다 우선합니다. 기본 장면은 `16_Transit_Connected_Example`입니다. 추가 장면 `10b_Aircraft_Variants`, `10c_Aircraft_Exploded`, `11_City_Frontier_Props`, `11b_Road_Modules`, `12_Airfield_Outpost`에서 조합과 배치를 확인할 수 있습니다.

후속 디자인 수정에서 로터를 분절된 부상 필드로 교체하고 차량용 신호기를 지지대 없는 공중 부유형으로 바꿨습니다. 보행자 안내는 바닥 홀로그램이며 `13_Field_Tech_Details`에서 확인할 수 있습니다. 자세한 상태별 파트·재질 규칙은 [확장 안내](AIR_CITY_ko.md)를 따릅니다.

## 제작 규격

- **디테일 원본 → 내부 면·중복·평면 정리 → 전후 비교 → 크기별 상한(최대 200삼각형) 검증** 순서로 제작합니다. 최소 폴리곤으로 형체만 유지하는 방식이 아닙니다. `17_Detail_Sources`에 원본, `18_Detail_Comparison`에 비교 예제가 있으며 [파이프라인 안내](MODELING_PIPELINE_ko.md)에 적용 범위와 검사 규칙을 적었습니다.
- 모든 **독립 메시 파트**는 삼각형으로 변환한 뒤에도 최대 200개이며 작은 파트는 더 낮게 제한합니다. 한도를 맞추려고 파트를 나누지 않으며 필요한 경우 장식을 일부 줄입니다. 완성 조립체의 합계에 적용하는 규격은 아닙니다.
- 실루엣·곡면·패널·창틀·실링·체결부를 먼저 제작하고 외부 디테일을 보존하며 정리했습니다. 베이스 컬러, 금속, 고무, 직물, 피부, 키틴, 암석, 선행 문명 물질과 발광 재질을 구분했습니다.
- 인체 맨머리 높이는 1.8 m, 머리 높이는 1.8/7 m입니다. 헤어·모자·안테나는 별도 높이입니다.
- Blender 단위는 미터, +Z 위, -Y 전방입니다. GLB의 표준 축 변환은 (x,y,z) → (x,z,-y)이며 +Y 위, +Z 전방입니다.
- 기획 문서의 1월드 단위=2m에 맞춰 가져올 때는 **미터 좌표에 0.5를 곱합니다.** GLB 축 변환과 단위 변환은 별개입니다.
- 기존 건축 기본 격자는 수평 2m, 새 외벽 베이는 3m, 주거 층고는 3.6m입니다. 압력문 잎 높이는 2.15m입니다.
- 건물 본체 높이: Habitat 7.2m / TerracedPods 10.8m / Workshop 5.4m / RibDome 6.5m / Observatory 11.5m. 태양광·센서·안테나는 위에 추가됩니다.
- 파트에는 UV, 법선, 재질과 식별용 extras를 포함했습니다. 콜리전·LOD·게임 런타임용 로더는 이번 작업 범위에 포함하지 않았습니다.

## 인체 레고 조합

공통 `HUMAN_7H`는 23개 본을 사용합니다. 현재 권장 체형은 **Male/Female × Lean/Normal/Heavy의 6종**입니다. Standard / Slender / Broad는 기존 정상 체형의 기준·호환 자료로 보존했습니다. 모든 체형의 관절 위치와 본 이름·바인드 포즈는 같습니다. 좌우 L/R은 캐릭터 기준입니다. 상세 형상과 부착 규격은 `CHARACTER_DESIGN_ko.md`를 따릅니다.

1. 체형 한 종류를 고릅니다. 맨몸을 쓰면 해당 체형의 Pelvis / Abdomen / Chest / UpperArm / Forearm / Thigh / Shin을 고릅니다.
2. 공통 Neck / Hand_L,R / Boot_L,R을 붙입니다.
3. Head는 Oval / Angular / Round / Narrow 중 하나, Face는 Neutral / Alert / Stern 중 하나를 고릅니다.
4. 헤어는 Buzz / SidePart / Curls / Bob / Ponytail / Bun 중 하나를 고릅니다.
5. 의복은 Explorer / Engineer / Guard / Medic / Researcher / Miner / HeavyCombat / Pilot / Civilian의 9종 중 하나를 고르고 **체형 이름이 같은** Torso / Sleeve / Cuff / Trouser / Greave를 좌우로 선택합니다. 새 의복은 6종의 새 체형 모두에 맞췄습니다.
6. 의복 착용 시 겹치는 맨몸 부위 Abdomen / Chest / UpperArm / Forearm / Thigh / Shin은 숨깁니다. Pelvis·Neck·Hands·Boots는 유지합니다. 이 규칙은 관통과 중복 면을 줄입니다.
7. 머리·목·손의 Skin 재질 슬롯을 같은 피부 재질로 맞춥니다. 얼굴 Nose와 귀도 함께 변경합니다. 헤어·의복 색은 별도 재질 슬롯에서 바꿀 수 있습니다.
8. 안전모/의료모에는 `Hair_HatClearance_*`를 사용합니다. Guard 밀폐 헬멧에는 헤어를 숨깁니다.

파트는 부위 중심이 아니라 **공통 캐릭터 원점(양발 사이 지면)** 에 바인딩되어 있습니다. 파트를 위치로 수동 이동하지 말고 스킨의 본 매핑을 공유하십시오. 여러 GLB를 각각 로드하면 리그 객체도 여러 개 생성되므로 실제 조합에서는 공통 바인드 포즈와 본 이름으로 하나의 리그에 재바인딩해야 합니다. 예시 조합은 `assembly_recipes.json`, 본 정보는 `skeletons.json`에 있습니다.

리아는 Female_Normal·정비복·Ponytail, 카인은 Male_Normal·경비복·짧은 머리, 마라는 Female_Normal·의무복·Bun으로 구성했습니다. 마라의 의무복은 이 리소스 팩의 디자인 제안입니다. 기존 문서는 마라의 확정 의복을 지정하지 않습니다. 주인공·연구원·채굴자·밀폐 경비원·중장갑 전투원·조종사·일상복 주민까지 10개 조립 예제를 포함했습니다. 액세서리 34개는 의복과 분리되어 있으며 `accessory_sockets.json`과 레시피의 `offsets`로 부착합니다.

## 몹 조합

| 종류 | 보행 | 디자인·교체 파트 |
|---|---|---|
| RiftGrazer | 4족, 대각선 페어 | 비대칭 인상의 뿔·다중 눈·노출된 턱·등뼈·꼬리·키틴 갑각. Antler/Crest 머리 교체 |
| VeilWalker | 6족, 교대 삼각 지지 | 갈라진 흑요석 외피 사이에 부유한 발광 핵·떠 있는 파편·가느다란 다리. Crown/Halo 머리 교체 |
| RelaySentinel | 2족 | 탐지 헤드·금속 관절·배터리·코일 포신·중량 발판. Scanner/Armored 머리 교체 |

각 계열 내부에서 머리와 사지·외장·무장을 교체할 수 있습니다. **서로 다른 족수/계열의 본 구조는 다르므로** 인체 또는 다른 계열의 다리를 그대로 붙이지 않습니다. 4족 사격은 포자 방출, 6족은 위상 방출, 기계는 코일 포격 동작이며 실제 투사체/이펙트는 게임 작업에서 연결합니다.

## 애니메이션 재생과 조합

각 동적 리그에 Reference_A / Turn_Base / Idle / Walk / Run / Turn_Left_90 / Turn_Right_90 / Attack_Melee / Aim_Base / Attack_Fire / Jump_Start / Jump_Air / Jump_Land / Jump_Full / Walk_To_Run을 제작했습니다. 본별 키프레임이 들어 있는 실제 스켈레탈 애니메이션입니다. 회전은 쿼터니언, 샘플링은 30fps입니다.

원본 `00_Library`는 파트 확인을 위해 NLA 트랙을 기본 음소거했습니다. 리그를 선택하고 Action Editor에서 `리그명__Walk` 같은 Action 하나를 선택해 재생합니다. 모든 NLA 트랙을 동시에 켜면 같은 시간대에 겹칩니다. GLB는 분리된 이름의 클립으로 내보냈습니다.

상태 전환 시간·좌우 접지 위상·상체 본 마스크·점프 연결 방식은 **ANIMATION_GUIDE_ko.md**와 **animation_blending.json**에 있습니다. `PressureDoor_Animated.glb`에는 1초 슬라이드 열기/닫기 2개 클립이 들어 있습니다. 고정 건물·지면에 보행/공격을 넣지는 않습니다.

## 검증과 사용 범위

차량의 조합·좌표·스켈레톤·개폐/조향 클립은 `VEHICLES_ko.md`와 `vehicle_assemblies.json`을 참고합니다. 차량은 +X 전방이며 인체/몹의 -Y 전방과 다릅니다.

`source_report.json`은 부위별 삼각형 수·유효 좌표·퇴화 삼각형·스킨 가중치를, `export_report.json`은 실제 GLB의 파일 구조·부위별 삼각형 수·UV/법선·스킨·클립 이름과 샘플을 검사합니다. `roundtrip_report.json`은 Blender로 재가져온 대표 GLB의 실제 본 변형을 확인합니다. 렌더는 완성 예제의 형태와 재질을 확인하기 위한 이미지입니다.

애니메이션은 이 라이브러리의 공통 본 비율에 맞춘 제작본이며, 실제 게임의 속도·무기 조준·발 접지 보정·피격 타이밍은 통합 단계에서 조율해야 합니다. 게임 코드 연결이나 현재 커스텀 OpenGL 렌더러의 GLB 로딩은 수행하지 않았습니다.

제작 재실행: Blender를 background 모드로 실행하고 `Tools/build_resource_pack.py`를 지정합니다. 같은 산출물 이름을 갱신하므로 수작업 편집본은 별도 이름으로 보관하십시오. 검사만 할 때는 `python Tools/verify_resource_pack.py`를 실행합니다. 모든 제작 파일과 텍스처는 이 팩에서 생성했으며 외부 게임 자산은 사용하지 않았습니다.

후속 교체: [유리창·횡단보도·건물 5종 안내](REFINED_CITY_ko.md). `19_Glazing_Review`에서 이동수단 창호, `20_Architecture_Styles`에서 12·16·10·4·3층 건물을 확인합니다. 원본은 `24_Refined_Detail_Sources`에 보관했습니다. 횡단보도 페인트는 노면·보행로와 독립된 파일입니다.

캐릭터의 기존 785개 파트도 새 메시로 교체했습니다. 의복 외장 69개를 별도로 조립하며 `character_detail_assemblies.json`의 Torso/OuterAssembly 대응을 따릅니다. [캐릭터 디테일 개선 안내](CHARACTER_DETAIL_ko.md) · [이전/개선 비교](../Preview/29_character_detail_comparison.png).

[새 도로계획 모듈 안내](ROAD_PLAN_ko.md): 독립 원본 `Source/RoadPlan_Modules.blend`, 납품 `Export/RoadPlan`, 포트/충돌 예약/높이 JSON `road_plan_modules.json`. 기존 임시 도로와 연결할 게임 측 절차는 별도이며 노면 8.4m 권장형은 상부 그래프도 월드 고도 4.2로 맞춰야 합니다.
