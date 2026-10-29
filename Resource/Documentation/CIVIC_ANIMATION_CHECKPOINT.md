# 애니메이션 작업 완료 기록

> 2026-10-29: 기존 63개를 보존하고 [가감속·스프린트 19개](ACCELERATION_ko.md)를 추가해 현재 인체는 총 82클립입니다. 아래는 이전 납품 당시의 기록이며 기존 계약은 유지합니다.


## 2026-10-28 권총 상체 레이어 추가

최신 `ANIMATION_BLEND_REQUEST.md`의 운반 대기·걷기·달리기와 1.4초 비반복 재장전을 제작했다. 기존 59클립을 보존해 총 63개이며 캐릭터 GLB 10개에 추가했다. 두 Blender 파일에는 검토 장면 43을 더해 37~43의 7개 장면이 있다. 기본 장면은 유지한다.

Mixamo 공개 미리보기 3종을 실제 확인해 기존 23본에 키를 직접 작성했다. 기본 참고 원칙을 `Resource/AGENTS.md`에 기록했다. 64개 체형·의복 조합의 무기 간섭과 조준 전환, 새 4클립의 실제 GLB 재가져오기, 이전 59클립·메시·스킨·바인드 보존 검사를 통과했다. [제작·연결 안내](PISTOL_LAYERS_ko.md)와 `Validation/pistol_layer_delivery_summary.json`이 이번 납품 기록이다. 아래는 이전 납품 시점의 기록이다.

## 2026-10-27 추가 갱신

기존 43개 상체를 개선하고 조준 이동 16개를 더한 59클립을 납품한다. 하체 보행 키와 23본을 유지한다. 검토 장면은 37~42의 6개이며 최신 규격은 [상체·방향별 이동 안내](AIM_LOCOMOTION_ko.md)와 [블렌딩 계약](aim_locomotion_blending.json)을 따른다. 추진장치는 실루엣 우선 정책으로 원래 곡선을 복원했다.

## 이전 2026-10-26 제작 기록

2026-10-23 사용자의 요청으로 중단했던 작업을 2026-10-26 재개하여 마무리했다. 원본·GLB·검증·최신 미리보기를 저장했다. 게임 상태 연결은 별도 통합 단계다.

## 저장된 결과

- `Source/Erma_Resource_Library.blend`: 기존 자산과 기본 16_Transit_Connected_Example 장면을 유지한 전체 원본. 새 인체 28개를 포함한 43개 클립, 차량 반응 액션 21개와 검토 장면 37~41이 저장되어 있다.
- `Source/Civic_Animations.blend`: 바로 열어 재생하는 별도 프로젝트. 5개 검토 장면, 기존/개선 보행 비교, 6체형, 바이크 탑승 접점과 차량 5종을 포함한다.
- 캐릭터 GLB 10개와 차량 기본/교체 GLB 10개 저장 완료. 인체 9개에는 `share_human_animation_bank.py`로 Player_Explorer의 동일 23본 애니메이션을 복사했고, 목적지 메시·스킨·바인드·재질·장비 오프셋 노드는 보존했다.
- 최근 수정: 기립 시 리아의 머리카락/등 장비 간섭과 Guard 헬멧/권총 간섭을 수정했다. 기립 초반에 무장 손을 머리에서 떨어뜨리는 guard 자세가 있다. 왼손 바닥 지지는 약 6.6mm 오차 이내다.
- `civic_animation_contract.json`, 클립/블렌딩 JSON, 총구 60Hz 궤적과 `CIVIC_ANIMATION_GUIDE_ko.md`를 저장했다. 게임 코드·Resource/Runtime은 이 작업에서 수정하지 않았다.
- `Preview/Civic`: 개별 클립 원본 PNG, 새 기립/의료 회복과 무기를 숨긴 Rider_Brace PNG, Before_After와 Bike_Contact 13프레임, 체형/차량 검토 PNG가 저장되어 있다. 최신 PNG로 GIF 28개와 접촉 시트 28개를 다시 합성했으며 `delivery_overview.jpg`에 주요 결과를 모았다.

## 통과한 검사

- `Validation/civic_contact_report.json`: 보행 접지, 양손 그립, 상체 반응의 하체 보존, 바이크 5개 접점, 기립 손 접촉 통과.
- `Validation/civic_source_equipment.json`: 실제 캐릭터 10종 + 6체형×9용도 조합 54개, 총 64개 조립체의 17개 클립×9위상 장비/머리 간섭 통과. 최종 로그는 `civic_equipment_delivery.log`.
- `Validation/export_report.json`: 전체 GLB 2,575개, 최대 492삼각형, 클립 개수/스킨/UV 검사 통과. 최종 로그는 `civic_global_delivery.log`.
- `Validation/civic_animation_authoring.json`: 원본 메시·스킨 해시와 본 바인드 보존, 중간 프레임 바닥 보정 기록.
- 중단 처리 시 확인한 최종 로그에서 `CIVIC_ROUNDTRIP_COMPLETE 10 10`과 정상 종료를 확인했다. `Validation/civic_animation_roundtrip.json`의 최종 캐릭터 10종 + 차량 조립체 10개 재가져오기 검사도 완료했다. 관련 실행 프로세스는 종료되었다.

## 2026-10-26 재개 후 완료한 작업

1. 최종 10+10 GLB 재가져오기, 64개 조립체의 장비 간섭, 전체 2,575개 GLB 검증 통과 결과를 확인했다. 모델 변경 없이 이미 완료한 전체 검사를 반복하지 않았다.
2. `verify_civic_projects.py`로 원본 두 파일을 직접 열었다. 메인은 16번, 별도 프로젝트는 37번 장면으로 열리고, 양쪽의 5개 검토 장면에서 카메라·스킨·43개 인체 액션과 실제 변형을 확인했다. 결과는 `Validation/civic_project_report.json`이다.
3. 최신 PNG로 GIF/접촉 시트를 모두 갱신했다. 저장 파일을 임시 파일에 쓴 뒤 교체하도록 개선하고, 비교 장면의 화면 비율을 보존했다. 한국어 요약 이미지와 기립/탑승/비교 장면을 시각 검수했다.
4. 납품 원본은 `Source/Civic_Animations.blend`, 요약은 `Preview/Civic/delivery_overview.jpg`, 보행 비교는 `Preview/Civic/Before_After.gif`다. 새 클립의 게임 등록·상태 연결·이벤트 소비는 별도 통합 단계이며 이 원본 납품에서 구현하지 않았다.

## 제작 시 주의

- `build_resource_pack.py` 전체를 실행하지 않는다. 부분 제작 도구만 사용한다.
- 편집은 30fps 반 프레임 키, GLB는 NLA strip scale=2 / export fps=60으로 내보내고 원본 scale/fps를 복원한다.
- 새 기준은 Ride_Base다. 기존 게임 합성 Ride를 덮어쓰거나 같은 이름을 추가하지 않는다.
- 공유 애니메이션 복사 도구는 은행 해시가 같으면 다시 덧붙이지 않는다. 새 소스일 때만 다시 복사한다.
- 다른 작업에서 수정 중인 SimpleGame 코드와 사용자 요청서 이동/커밋을 보존한다. 이 작업이 만든 임시 Draft .blend와 검사 보조 스크립트 2개는 이미 정리했다.
