# 출발 · 제동 · 스프린트 애니메이션

2026-10-29. 최신 `ANIMATION_BLEND_REQUEST.md`와 실제 63클립을 대조해 가감속 전용 **19클립**을 추가했습니다. 기존 후진·스트레이프·권총 운반·재장전·Walk_To_Run은 다시 제작하지 않았습니다. 인체는 기존 63개를 포함해 **총 82클립**이며, HUMAN_7H 23본과 기존 메시·스킨·바인드·장비 오프셋을 유지합니다.

## 새 클립

이름의 L/R은 연결 기준인 왼발/오른발 접지 선택지입니다. 출발은 해당 발 접지 위상으로 끝나고, 정지·속도 전환은 해당 접지 위상에서 시작합니다. 같은 모션을 단순 역재생하거나 전체 몸을 좌우 반전한 결과가 아닙니다.

| 클립 | 수 | 길이 | 연결 |
|---|---:|---:|---|
| Walk_Start_L/R | 2 | 0.60초 | Idle → Walk |
| Walk_Stop_L/R | 2 | 0.80초 | Walk → Idle |
| Run_Start_L/R | 2 | 0.60초 | Idle → Run |
| Run_Stop_L/R | 2 | 0.80초 | Run → Idle |
| Sprint_Start_L/R | 2 | 0.60초 | Idle → Sprint |
| Sprint_Stop_L/R | 2 | 1.00초 | Sprint → Idle |
| Run_To_Walk_L/R | 2 | 0.60초 | Run → Walk |
| Run_To_Sprint_L/R | 2 | 0.50초 | Run → Sprint |
| Sprint_To_Run_L/R | 2 | 0.60초 | Sprint → Run |
| Sprint | 1 | 0.50초 반복 | 전진 스프린트 |

출발에는 몸의 전경사와 무게중심 낮추기, 정지에는 제동 자세와 마무리 발 회복을 넣었습니다. 속도가 바뀌는 동안 보행 위상을 연속적으로 진행합니다. 새 스프린트는 기존 Run을 빨리 재생한 모션이 아니라 접지 비율 32%, 다리 회복 높이와 팔 동작을 별도로 만든 루프입니다.

## 속도·위상·블렌딩

제작 기준 속도는 Walk **1.08m/s**, Run **2.46m/s**, Sprint **4.2m/s**입니다. 게임의 1월드 단위=2m를 적용하면 각각 **0.54 / 1.23 / 2.10월드 단위/초**입니다. Sprint 속도는 이번 모션의 기준값이며 게임의 최대 속도를 변경한 뜻이 아닙니다.

`acceleration_blending.json`의 각 클립에 진입·종료 기준 클립, 정확한 정규화 위상, 길이·반복·블렌드·지원 발·이벤트를 기록했습니다. `acceleration_trajectories.json`에는 60Hz 속도·위상·참고 이동거리 곡선, `acceleration_foot_contacts.json`에는 실제 저장된 키의 발바닥 높이와 접지 힌트를 제공합니다.

1. 현재 보행 위상에서 0과 0.5 중 가까운 접지를 기준으로 L/R을 선택합니다. 입력·충돌 반응을 접지 시점까지 늦추지 않습니다. 접지 사이에 즉시 바꿔야 하면 현재 포즈에서 약 **0.08초** 진입 블렌드를 사용합니다.
2. 가감속 클립은 비반복으로 재생합니다. 종료 이벤트의 `target_clip`과 `target_normalized_phase`로 다음 루프에 이어 붙입니다. 종료 때 다음 루프의 위상을 0으로 되돌리지 않습니다.
3. 기본 복귀 블렌드는 **0.10초**입니다. 중간에 다시 가속/감속을 지시하면 현재 평가 포즈·속도에서 새 전환을 시작합니다. 첫 키로 순간 이동하지 않습니다.
4. 원래 `Walk_To_Run`은 그대로 사용합니다. 기존 조준 2D 블렌드의 후진·좌우·대각선 위상도 그대로 유지합니다.

편집 타임라인은 **30fps의 반 프레임 키**, GLB는 **60Hz**입니다. 모든 새 클립의 루트 위치·Yaw는 0입니다. 실제 배우 이동·가속도·제동거리·충돌은 게임이 소유합니다. 제공한 속도/거리 곡선은 동기화 참고이며 캐릭터를 강제로 이동시키는 루트 모션이 아닙니다. 게임의 가속도와 크게 다르게 재생하면 발 미끄러짐이 생길 수 있으므로 재생률과 이동을 함께 맞추고 기존의 시각 접지 보정을 적용하십시오.

## 무장·조준 상태

비무장일 때는 새 전신 모션을 사용하고 무기 메시를 숨깁니다. 기존 Idle/Walk/Run의 자유 팔 동작을 보존했으므로 총기를 부착한 채 전신 기본 모션만 재생하지 않습니다.

무장 이동은 새 모션의 **하체 마스크**와 이미 납품된 **Pistol_Carry_Idle/Walk/Run 상체**를 조합합니다. 가감속 중의 속도로 운반 상체를 섞고, 동일 보행 위상으로 평가합니다. Sprint에서는 운반 표본을 Carry_Run에 고정하되 위상은 Sprint와 맞춥니다. 조준·사격·재장전은 기존 상체 마스크를 계속 사용하며 탄약 처리는 게임 코드만 담당합니다.

조준 상태의 8방향 하체를 전진 출발 클립의 회전으로 대체하지 않습니다. 조준 이동은 기존 `aim_locomotion_blending.json` 계약을 유지합니다. 이번 추가는 전진 일반 이동·무장 운반의 출발/제동과 스프린트이며 **더 빠른 8방향 조준 표본은 추가하지 않았습니다**.

## 원본·검토·내보내기

- `Source/Erma_Resource_Library.blend`: 기존 자산과 기본 진입 장면을 보존한 전체 원본.
- `Source/Civic_Animations.blend`: 바로 열어 재생하는 검토 프로젝트. 기존 진입 장면·검토 장면을 보존하고 **44_Acceleration_Review**를 추가했습니다.
- Action Editor에서 `HUMAN_7H__` 접두사의 새 클립을 선택합니다. 동시에 모든 NLA 트랙을 켜지 않습니다.
- `Export/Characters`의 기존 캐릭터 **10개 GLB에 새 애니메이션 스트림만 추가**했습니다. 클립별 캐릭터 복제 파일은 만들지 않았습니다.
- `Preview/Acceleration`: 실제 스킨 렌더, 19개 GIF와 접촉 시트. 애니메이션 자체는 제자리 재생이며 게임 이동 시연 영상이 아닙니다.

## 레퍼런스와 검사

Mixamo의 공개 `Fast Run / Running Fast`, `Run To Stop / Fast Stop From Full Run` 미리보기를 2026-10-29에 확인했습니다. `Female Stop And Start Walking`은 움직이는 카드 미리보기에서 출발 전후 체중 이동을 확인했습니다. 전경사·굽힌 팔꿈치·다리 회복·제동 발 디딤·마무리 걸음을 참고하여 기존 HUMAN_7H에 직접 키를 제작했습니다. 파일 다운로드나 자동 리타기팅은 하지 않았습니다.

[Mixamo 달리기](https://www.mixamo.com/#/?page=1&query=run&type=Motion%2CMotionPack) · [Mixamo 출발·정지](https://www.mixamo.com/#/?page=1&query=stop&type=Motion%2CMotionPack)

- `Validation/acceleration_authoring.json`: 기존 액션과 메시·스킨 해시, 바인드 보존, 새 모션의 끝점과 실제 fitted skin 바닥 검사.
- `Validation/acceleration_motion_report.json`: 저장 키 사이 120Hz 평가, 바닥·루트·회전 연속성·루프·정확한 위상 연결 검사.
- `Validation/acceleration_equipment.json`: 6체형×9용도 및 실제 캐릭터 10개, 총 64조합의 실제 무장 상체 합성·조준 전환에서 무기 간섭 검사.
- `Validation/acceleration_glb_preservation.json`: 캐릭터 10개 GLB의 기존 63애니메이션, 메시·스킨·노드·재질·기존 바이너리를 그대로 유지했는지 확인.
- `Validation/acceleration_roundtrip_report.json`: 실제 GLB 재가져오기, 82클립·23본·길이·루프·실제 변형 검사.

게임 코드와 `Resource/Runtime`은 수정하지 않았습니다. 새 클립 등록, 상태 선택, 입력/속도/접지 동기화와 캐시 갱신은 게임 개발 작업에서 연결해야 합니다.
