# CQ-04B 도시 모듈과 Field Medic 모델 납품

2026-11-27 · [리소스 제작 요청서](../RENDER_RESOURCE_REQUEST.md)의 7.3절과 7.4절 기준

## CQ-04B 도시 모듈

[Blender 원본](../Source/CQ04B_City_Kit.blend) · [미리보기](../Preview/CQ04B_City_Kit.png) · [모듈·접속 규격](cq04b_city_kit_v4.json)

- 직선·테이퍼·순환·시외 도로, 십자·T자·회전교차로, 고가·램프·지하차도·교량, 신호·표지·거리 소품을 `Resource/Export/CQ04B`에 저장했다. 7.4절의 기본 모듈 32종과 우회전 베이·정류장 베이·시외 가드레일 변형 3종에 각각 LOD0·LOD1·LOD2·그림자 프록시가 있다.
- Blender 좌표는 실측 미터이며 +X 진행, +Y 좌측, +Z가 위다. GLB는 Y-up으로 내보냈다. 도로 접속 높이에는 런타임 노면 오프셋을 굽지 않았다. 모듈별 진입 폭과 높이, 신호 발광면 이름과 소켓, 컷어웨이 대상 메시를 조립 목록에 기록했다.
- 고가 데크와 교각을 분리했고 지하차도 덮개는 `roof_slab`으로 내보냈다. 100m 안에서 8.4m를 오르며 최대 경사도 8.4%를 지키기 위해 고가 램프는 본체에 일정 경사를 적용했다. 개착 지하 램프는 양끝을 완화하고 최대 경사를 약 7.34%로 맞췄다.
- 새 도시 목록은 제작 납품이다. 절차 도로의 메시 슬롯·신호 상태별 발광 제어·컷어웨이 페이드는 게임 연결 작업에 해당한다. 보행 신호 기둥은 최신 7.4절 납품 모델이다. 현재 게임에서 쓰는 바닥 홀로그램의 배치에는 게임 쪽 설정이 적용된다.

## Field Medic

[Blender 조립 원본](../Source/Characters/Field_Medic_Mixamo_Assembly.blend) · [기본 자세](../Preview/HumanV4/Field_Medic_Mixamo_Review.png) · [관절 굽힘 검토](../Preview/HumanV4/Field_Medic_Mixamo_Flex.png) · [조립 목록](mixamo_characters.json) · [수치 검수](../Validation/field_medic_mixamo_model_delivery.json)

- 머리·목과 손, 오른쪽 전완 보호대, 오른쪽 의료 파우치, 구조 팩, 가슴 카트리지 랙을 추가했다. 기존 코트·바지·부츠·머리카락·어깨 및 무릎 보호대는 표면 외형과 텍스처를 유지하며 면을 정리했다. 왼쪽 진단 커프는 같은 착용 위치를 기준으로 새 규격에 맞춰 모델링했다.
- 노출 피부는 머리·목·손만 내보냈다. 스킨 메시 8개는 공용 Mixamo 22본의 본 순서·부모 관계·역바인드 행렬을 공유한다. 강체 장비 5개는 소켓 원점의 정적 GLB이며 조립 목록에 본 이름과 glTF 본 로컬 행렬을 기록했다.
- LOD0 10,805개, LOD1 5,335개, LOD2 2,505개, 그림자 프록시 1,658개의 삼각형을 확인했다. 관절을 굽힌 검토 자세에서 강체 소켓 추종을 확인했다. 공용 Mixamo 클립과 게임 안에서의 조립·관통 검수는 별도 연결 작업이다.
- Field Medic의 패커 입력은 `HumanV4/FieldMedicMixamo`로 연결된다. 기존 패커는 등록된 LOD0를 바탕으로 런타임 LOD를 생성한다. 조립 목록의 `lod_sources`에 명시한 별도 제작 LOD는 후속 연동 대상이다. 기존 HUMAN_7H 캐릭터는 7.3절의 하위 호환 대상이다.
