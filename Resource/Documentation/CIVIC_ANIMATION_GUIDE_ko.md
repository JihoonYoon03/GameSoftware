# 도시 충돌·치안 대응 애니메이션 납품

> 2026-10-27 갱신: 편집 원본과 GLB는 [실루엣 우선·최대 200삼각형 정책](POLYGON_BUDGET_ko.md)을 적용했습니다. 인체는 기존 43개를 유지·개선하고 조준 이동 16개를 더한 59클립입니다. [상체·방향별 이동 안내](AIM_LOCOMOTION_ko.md)를 함께 확인합니다.




`CIVIC_ANIMATION_REQUEST.md`의 인체 P0/P1/P2와 차량 반응을 제작했다. HUMAN_7H는 기존 43개를 유지·개선하고 조준 걷기·달리기 16개를 더한 59개 클립이다. 차량 5종에는 총 21개 액션을 추가했다. 기존 메시·스킨·재질·23본·바인드·부품 원점은 변경하지 않았다.

`Source/Civic_Animations.blend`를 열면 실제 스킨이 연결된 편집/재생 장면이 보인다. 원본 전체는 `Source/Erma_Resource_Library.blend`에도 들어 있다. 기존 캐릭터 10종과 차량 5종의 기본/교체 조립체 GLB를 각각 갱신했다. 파트 GLB와 다른 확장은 유지한다.

## 조준 상태의 방향별 이동

`Aim_Walk_*` 8개와 `Aim_Run_*` 8개는 몸의 조준 방향을 유지하는 전진·후진·좌우·대각선 루프이다. 발 디딤 위상을 공유하며 [별도 연결 안내](AIM_LOCOMOTION_ko.md)와 `aim_locomotion_blending.json`의 속도 표본·접지 보정식을 따른다.

## 클립과 연결

| 동작 | 길이 | 종료 처리 |
|---|---:|---|
| Hit_Front / Back / Left / Right | 0.30초 | Aim_Base 복귀. 하체 보존 상체 마스크 |
| Knockdown_Front / Back / Left / Right | 0.80초 | 같은 방향 Downed로 연결 |
| Downed_Loop, Downed_Back / Left / Right_Loop | 2.00초 | 호흡 루프. 앞 방향은 기본 Downed_Loop 사용 |
| GetUp_Front / Back / Left / Right | 1.40초 | 완전 기립 이벤트 뒤 Idle |
| Death_Front / Back | 1.00초 | 마지막 프레임 유지. 자동 기립 없음 |
| Death_Front_Pose / Death_Back_Pose | 정지 포즈 | 사망 클립 마지막 자세와 동일 |
| Flee_Start / Flee_Stop | 각각 0.20초 | 시작→Flee_Run 위상0, 종료→Idle |
| Flee_Run | 0.667초 | 반복. 이동 방향으로 시선 유지 |
| Security_StunFire | 0.40초 | Aim_Base 복귀. 0.032초 발사 이벤트 |
| Medical_Stabilized / Medical_GetUp | 1.00 / 1.40초 | 의료 안정화→기립. 병상 장면은 제작하지 않음 |
| Ride_Base / Rider_Brace | 기준 포즈 / 0.50초 | 바이크 접점을 공유하는 탑승 기준과 버티기 |
| Vehicle_Impact_Front / Rear / Side | 0.50초 | 고정 차체의 부상 장치 반동 |
| Vehicle_Disabled | 0.80초 | 동력 차단 이벤트와 최종 잔해 자세 유지 |
| Bike_Impact | 0.50초 | Rider_Brace와 같은 시계로 재생 |

앞뒤/좌우 넘어짐마다 종단 자세가 다르므로 `civic_animation_contract.json`의 방향별 표를 따른다. 무조건 기본 Downed_Loop로 보내면 몸이 갑자기 돌아간다. 각 Downed의 위상0과 해당 GetUp 첫 프레임은 일치한다. 사망 클립과 정지 포즈도 연결된다.

인체 전방은 -Y, 좌측은 +X, 상방은 +Z다. 충격 속도는 **몸이 밀리는 방향**으로 해석한다. 캐릭터 로컬로 변환한 속도에서 +Y가 우세하면 Front, -Y면 Back, -X면 Left, +X면 Right다. 차량의 전방은 기존 +X다. 게임 1월드 단위는 2m다.

## 자연스러운 동작을 위한 수정

기존 대기는 팔을 아래로 편하게 내리고 호흡과 작은 체중 이동을 더했다. 걷기는 발별 60% 접지/40% 회수와 양발 지지를 사용한다. 발뒤꿈치 하중 수용 뒤 발끝으로 밀어내며, 골반과 흉곽이 서로 균형을 잡는다. 뛰기는 발별 40% 접지와 짧은 양발 체공을 사용한다. 기존 속도 기준 1.08 / 2.46m/s, 왼발 위상0 / 오른발 위상0.5는 유지했다.

회전은 발을 번갈아 옮기고, 지지 중인 발의 월드 위치를 유지한다. 근접 타격에는 준비·몸통 회전·추적·회복을 넣었다. 사격은 두 손이 같은 무기 변환을 따르도록 했고, 점프는 준비 압축과 착지 흡수를 나눴다. 원래의 클립 이름과 Jump_Start→Air→Land, Walk_To_Run 연결을 유지한다.

넘어짐은 충격 방향의 버티기→다리 접힘→옆 지지→정착 순서다. 기립은 옆 자세에서 팔과 무릎으로 체중을 받은 뒤 한 발을 세워 일어난다. 무장 손을 땅에 짚지 않고 반대 손으로 지지한다. 독립 부품의 실제 스킨 정점과 장비 오프셋을 사용해 부츠·정강이 보호대의 바닥 접촉을 보정했다. 빠른 반응은 30fps 타임라인의 반 프레임에도 키가 있으며 GLB는 60Hz로 구웠다. 재생 길이는 변하지 않는다.

동작 원칙은 [OUHSC 보행 주기 자료](https://www.ouhsc.edu/bserdac/dthompso/web/gait/terms.htm), [하중 수용·지지 구간 자료](https://www.ouhsc.edu/bserdac/dthompso/web/gait/knmatics/stance.htm), [Guy’s and St Thomas’의 바닥에서 일어나는 순서](https://www.guysandstthomas.nhs.uk/health-information/falls/what-do-if-you-fall)를 참고했다. 이를 게임 리그와 짧은 반응 시간에 맞춰 직접 키/IK로 제작했으며, 모션 캡처 데이터를 가져온 작업은 아니다.

## 물리·사격·게임 상태와 분리

새 반응은 root 위치와 yaw를 움직이지 않는다. 캐릭터 골반의 국소적인 무너짐만 포함한다. Actor의 비행·미끄러짐·분리·충격 감쇠는 기존 BodyCollision이 처리한다. 애니메이션에 같은 이동을 다시 더하지 않는다. 차량의 root/chassis/좌석 소켓도 고정했고, 차체 충돌 치수와 장비 접속 위치는 유지했다.

Hit와 Security_StunFire는 `animation_blending.json`의 상체 마스크를 사용한다. 기준은 Aim_Base이며 masked override다. 가산 레이어로 사용할 때는 먼저 Aim_Base 대비 본별 차이를 구한다. 하체는 보행/달리기/조준 기반 상태에서 유지한다. 총구는 기존 `muzzle` 본이며 `civic_muzzle_tracks.json`에 실제 60Hz 위치·회전을 제공한다.

GetUp의 `fully_upright`는 마지막 1.4초 시점이다. 게임의 반응 시간이 먼저 끝나도 실제 기립이 끝날 때까지 입력을 잠근다. 일반 시민/보안대 사망은 끝에서 정지하며, 리아·마라·카인은 의료 안정화 경로를 사용한다. 이벤트는 표시 시점 정보이므로 피해나 범죄를 다시 적용하지 않는다.

Vehicle_Disabled의 시작 이벤트로 발광을 0.12배로 줄이고 기존 깊이 검사 2D 스파크를 연결한다. GLB가 이벤트나 발광 변경을 자동 실행하지 않는다. 3D 불꽃이나 프로펠러는 추가하지 않았다. 잔해 정리는 마지막 `tow_cleanup_eligible_when_offscreen` 이벤트를 소비해 게임이 결정한다. 페이드 중에도 충돌체를 유지하고 실제 제거 시점에 해제한다. 견인 이동 루트 모션은 없다.

## 바이크 접점

`40_Bike_Rider_Contact_Review`는 실제 바이크와 탑승자를 함께 보여준다. 인체를 Z축 +90° 돌리고 Z=-0.076m 옮겨 바이크 +X 전방에 맞춘다. 바, 발판, 좌석과 기준점 좌표는 `civic_animation_contract.json`에 있다. Rider_Brace의 골반·양손·양발 목표점은 반응 중 고정되며, 부상 장치만 흔들린다. 양손으로 핸들을 잡으므로 `riding_weapon_visibility` 이벤트로 손의 권총을 숨긴다.

기존 Hover_Idle/Cruise의 차체 부상 움직임과 섞을 때는 탑승자 전체를 실제 chassis의 애니메이션 접속 행렬에 연결한다. 탑승자 루트에 별도의 부상 흔들림을 더하지 않는다.

기존 게임의 `Ride`는 게임 패커가 Jump_Air와 Aim_Base에서 합성하는 별도 클립이다. 이 작업에서는 게임 패커와 Runtime을 수정하지 않았고 이름이 겹치는 Ride도 내보내지 않았다. 새 접점으로 Rider_Brace를 연결할 때만 **Ride_Base를 명시적으로 사용**하고 탑승 변환/장비 숨김을 함께 연결한다. 기존 Ride를 자동으로 대체하는 납품은 아니다.

## 검수와 미리보기

[주요 결과 요약](../Preview/Civic/delivery_overview.jpg) · [보행 개선 전/후](../Preview/Civic/Before_After.gif) · [기립 동작](../Preview/Civic/GetUp_Back.gif) · [바이크 접점](../Preview/Civic/Bike_Contact.gif)

2026-10-27 렌더에 상체 개선과 16개 방향별 이동을 반영했다. 두 Blender 원본에서 59개 인체 액션과 6개 검토 장면의 실제 스킨 재생을 확인한다(`civic_project_report.json`).

- `37_Civic_Animation_Review`: 개별 인체 모션을 Action Editor에서 선택해 재생.
- `38_Motion_Before_After`: 보존한 기존 Walk와 개선 Walk 비교.
- `39_Civic_Body_Profile_Review`: 남녀 Lean/Normal/Heavy와 두꺼운 전투복·장비.
- `40_Bike_Rider_Contact_Review`: 바이크와 탑승자 동시 반응.
- `41_Vehicle_Impact_Review`: 차량 5종 반동.
- `42_Aim_Directional_Review`: 조준 방향을 유지하는 8방향 걷기 비교. `Aim_Run_*`으로 달리기 비교 가능.

`Validation/civic_animation_authoring.json`은 메시·스킨 해시와 바인드 보존, 실제 부품 정점의 바닥 보정을 기록한다. `civic_animation_roundtrip.json`은 캐릭터 10종·차량 기본/교체 조립체 10개를 다시 가져와 클립 길이·스킨 변화·바닥·루프·접속 경계·장비/머리 간섭·고정 차체를 검사한 결과다. `civic_contact_report.json`은 접지·그립·탑승 접점 검사다. `Preview/Civic`에는 실제 Blender 렌더, 접촉 시트, GIF가 있다.

게임의 신규 클립 등록·선택, 이벤트 소비, 입력 잠금, 병원 페이드, 중복 판정 방지와 10명 보안대/100명 주민 갱신 비용 검증은 게임 통합 단계다. 원본/GLB 제작 작업에서 게임 코드와 Resource/Runtime 캐시는 수정하지 않았다.
