# CQ-05B 짧은 회피 스텝

2026-11-09. 전진·후진·좌우·대각선 8방향에 왼발과 오른발로 밀어내는 변형을 만들어 `Dodge_<Direction>_L/R` 16개를 납품했습니다. 기존 캐릭터 10종의 조립 GLB와 Blender 원본 두 파일에 들어 있습니다. 편집 원본과 GLB에서 HUMAN_7H는 23본과 98개 클립을 사용합니다.

각 클립은 **0.40초 단발**입니다. 0.22초 회피 이동에 맞춰 한 발로 밀고 반대 발로 착지합니다. 남은 약 0.18초에는 낮은 골반을 유지하며 체중을 다음 스텝으로 넘깁니다. 전진은 `Run`, 나머지 방향은 대응 `Aim_Run_*`의 하체 접지 위상에 맞췄습니다. 양 끝의 하체 키를 기준 이동과 일치시켰습니다.

| 시각 표식 | 시간 | 용도 |
|---|---:|---|
| `dodge_push_plant` | 0초 | 밀어내는 발 접지 |
| `dodge_push_off` | 0.033초 | 지지 발 이탈 |
| `dodge_land_plant` | 0.183초 | 반대 발 착지 |
| `dodge_move_window_end` | 0.220초 | 기준 이동 구간 끝 |
| `locomotion_handoff` | 0.400초 | 다음 이동의 접지 위상으로 연결 |

배우의 이동·방향·충돌·중단·재사용 대기는 게임이 결정합니다. 클립은 제자리 재생용이며 루트 이동과 yaw가 0입니다. 15m/s × 0.22초의 이동은 접지와 검토 장면을 맞추는 참조값입니다. 표식은 시각 동기화용입니다.

입력 방향은 배우 전방 기준으로 표본을 고르고, 발은 현재 위상에 가까운 왼발 0 또는 오른발 0.5를 선택합니다. 방향과 발이 정해지면 현재 포즈에서 바로 섞습니다. 무장 운반·조준·재장전 중에는 새 클립의 `human_lower_body` 위에 기존 상체를 얹습니다. 정지 상태에서 진입하거나 충돌·피격·탑승으로 도중에 끝나면 그때의 포즈에서 상태에 맞는 동작으로 섞습니다. 정확한 위상·축·이벤트는 [블렌딩 계약](dodge_blending.json)에 있습니다.

Blender의 `45_Dodge_Direction_Review`에서 비무장 전진, 운반 대각선, 조준 후진·옆 회피를 확인할 수 있습니다. `46_Dodge_Interrupt_Review`에는 달리기 복귀와 0.10초에 회피를 끊는 벽 접촉·메뉴 일시정지·피격·탑승 자세 예제가 있습니다. 이 장면은 전환 검토용이며 게임에서 동작을 선택하는 연결은 별도 단계입니다.

- GIF는 동작을 읽기 쉽도록 약 0.5배속으로 재생합니다. [방향별 회피](../Preview/Dodge/directions.gif) · [중단과 복귀](../Preview/Dodge/interrupts.gif) · [Blender 검토 원본](../Source/Civic_Animations.blend)
- `Validation/dodge_authoring.json`의 발 고정·바닥 검사, `dodge_equipment.json`의 64개 조립 조합 검사, `dodge_glb_preservation.json`의 기존 82클립·스킨 보존 검사, `dodge_roundtrip_report.json`의 GLB 재가져오기 검사

2026-11-09에 믹사모의 **Standing Dodge Forward**, **Standing Dodge Backward**, **Dodging Right** 공개 3D 미리보기를 확인했습니다. 전진에서 무릎을 굽혀 체중을 받는 자세, 후진에서 짧게 발을 빼는 자세, 옆 회피에서 골반과 몸통이 시차를 두고 기울어지는 모습을 참고해 기존 리그에 직접 키를 제작했습니다. [믹사모 회피 미리보기](https://www.mixamo.com/#/?page=1&query=dodge&type=Motion%2CMotionPack)
