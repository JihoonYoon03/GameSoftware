# 권총 운반·재장전 상체 레이어 — 2026-10-28

[추가 제작 요청서](../ANIMATION_BLEND_REQUEST.md)의 P1 운반 3종과 P2 재장전을 제작했습니다. 기존 59개에 4개를 더해 **HUMAN_7H 총 63클립**입니다. 기존 캐릭터 GLB 10개와 Blender 원본에 추가하며 모션별 캐릭터·스켈레톤 파일은 만들지 않습니다.

| 클립 | 길이 | 반복 | 기준 하체 |
|---|---:|---|---|
| `Pistol_Carry_Idle` | 2초 | 반복 | 기존 Idle |
| `Pistol_Carry_Walk` | 1초 | 반복 | 기존 Walk |
| `Pistol_Carry_Run` | 2/3초 | 반복 | 기존 Run |
| `Reload_Pistol` | 1.4초 | 비반복 | 기존 Idle 위상 0, 실제 사용 시 이동 하체 유지 |

운반은 오른손으로 권총을 지지하며 총구를 아래·전방 아래로 둡니다. 오른팔은 가슴의 움직임을 따라가고 왼팔은 기존 이동의 자연스러운 흔들림을 유지합니다. 재장전은 권총을 몸 앞에 올린 뒤 왼손으로 탄창부·슬라이드 순서의 조작을 하고 원래 운반 자세로 복귀합니다. 두꺼운 여성 전투복의 가슴 외장을 피하도록 무기를 먼저 앞으로 빼는 경로를 사용했습니다.

## 상체와 이동 연결

기존 `human_upper_body` 마스크를 사용합니다. spine/chest/neck/head, clavicle/arm/forearm/hand와 weapon_socket/back_socket/muzzle이 대상입니다. root·pelvis·thigh/shin/foot는 마스크에서 제외합니다. 파일에는 미리보기용 기준 하체 키가 함께 있지만 게임에서는 기존 이동 레이어가 하체를 담당합니다.

걷기·달리기는 기존 왼발 접지 위상 0, 오른발 접지 0.5와 동기화합니다. 방향·속도·조준 상태를 바꿀 때 위상을 초기화하지 않습니다. 운반↔조준은 0.1~0.2초, 기본 0.15초 블렌드를 권장합니다. 요청서의 기존 8방향 조준 이동·후진 클립을 그대로 사용합니다.

`Reload_Pistol`의 시작과 끝은 `Pistol_Carry_Idle:0`과 일치합니다. 다른 운반/조준 상태에서 재장전에 들어가거나 돌아갈 때는 같은 상체 마스크로 0.15초 정도 섞습니다.

## 재장전 이벤트

| 시간 | 시각 이벤트 |
|---:|---|
| 0.420초 | `reload_magazine_release_visual` |
| 0.938초 | `reload_magazine_seat_visual` |
| 1.120초 | `reload_slide_release_visual` |
| 1.400초 | `reload_pose_recovered` |

이벤트는 손 동작·시각 연출의 표식입니다. 탄창 소모와 탄약 충전은 게임 코드가 결정하며 이 이벤트가 탄약을 지급하지 않습니다. 현재 권총은 기존 단일 스킨 파트를 그대로 사용하므로, 별도 탈착 탄창 모델 없이 손동작과 시각 이벤트를 제공합니다. 게임 Runtime과 탄약 규칙은 이 제작 작업에서 변경하지 않습니다.

## 기본 레퍼런스

앞으로 애니메이션의 기본 레퍼런스는 **Mixamo의 실제 동작 미리보기**입니다. 작업 지침에도 반영했습니다. 이번에는 2026-10-28에 다음 공개 3D 미리보기를 확인했습니다.

- [Pistol Run — Running With A Pistol](https://www.mixamo.com/#/?page=1&query=pistol&type=Motion%2CMotionPack): 무기 팔의 흔들림 억제와 팔꿈치 굽힘을 참고하고 한 손의 낮춘 운반으로 조정했습니다.
- [Pistol Aim — Pistol Aim To Holster Idle](https://www.mixamo.com/#/?page=1&query=pistol&type=Motion%2CMotionPack): 권총을 내리는 팔 경로와 조준 해제 자세를 참고했습니다.
- [Reloading — Gun Reload While Walking](https://www.mixamo.com/#/?page=1&query=reload&type=Motion%2CMotionPack): 오른손 지지와 왼손 조작의 시간차를 참고해 요청된 1.4초 동작으로 구성했습니다.

기존 리그에 직접 키를 작성한 결과입니다. Mixamo 동작 파일을 가져와 자동 리타기팅했다고 주장하지 않습니다.

## 저장 위치와 검사

- `Source/Civic_Animations.blend`의 `43_Pistol_Carry_Reload_Review`: 플레이어와 중장비 캐릭터의 재장전 비교. 액션 편집기에서 운반 3종도 선택할 수 있습니다. 기본 장면 37과 기존 검토 장면은 유지합니다.
- [권총 운반 걷기](../Preview/Civic/Pistol_Carry_Walk.gif), [권총 운반 달리기](../Preview/Civic/Pistol_Carry_Run.gif), [재장전](../Preview/Civic/Reload_Pistol.gif).
- [레이어·마스크·이벤트 계약](pistol_layer_contract.json), `animation_clips.json`, `animation_blending.json`.
- `Validation/pistol_layer_authoring.json`: 기존 59개 액션 보존, 기준 하체 일치.
- `Validation/pistol_layer_validation.json`: 64개 체형·의복 조합, 무기/머리·의복·가방 간섭과 운반↔조준 혼합, 재장전 경계 검사.
- `Validation/pistol_glb_preservation.json`: 실제 GLB 10개의 기존 애니메이션·메시·스킨·바인드·부착점·재질 보존.
- `Validation/pistol_roundtrip_report.json`: 새 4클립의 실제 GLB 재가져오기, 길이·루프·바닥·루트와 원본 리그 구동 비교.

빠른 이동 표본 확대는 요청서에서 향후 속도 조정과 함께 검토하도록 구분한 항목입니다. 이번 운반 걷기·달리기는 기존 1.08m/s·2.46m/s 기준과 같은 주기입니다. 스프린트나 더 빠른 조준 이동을 임의로 추가하지 않았습니다.
