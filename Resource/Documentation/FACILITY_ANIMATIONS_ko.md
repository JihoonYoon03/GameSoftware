# CQ-11B 시설과 대면 NPC 애니메이션

2026-11-27. 카운터 대기·환영·물품 전달·설명·작별·단말 점검·길거리 경청 7개를 제작했다. HUMAN_7H 23본과 Field Medic Mixamo 22본의 기존 스킨에 맞춰 각 리그에 키를 제작했다. 공용 뱅크를 옷이나 장비마다 복제하지 않는다. [Blender 검토 원본](../Source/Facility_Talk_Animations.blend) · [HUMAN_7H GLB](../Export/AnimationBanks/HUMAN_7H_Facility.glb) · [Mixamo 22 GLB](../Export/AnimationBanks/Mixamo_22_Facility.glb)

Mixamo의 공개 미리보기에서 2026-11-27에 `Standing Greeting`의 손이 가슴 위로 올라왔다가 내려오는 인사와 `Talking`의 좌우 손짓, 팔꿈치와 몸통 동세를 확인해 신규 클립 키를 제작했다. Field Medic용 기본 대기·걷기·달리기·조준·회피 한 방향·점검 자세와 조준 걷기/달리기 8방향씩은 게임의 기존 HUMAN_7H 모션을 22본에 리타기팅했다. [Standing Greeting](https://www.mixamo.com/#/?page=1&query=standing%20greeting&type=Motion%2CMotionPack) · [Talking](https://www.mixamo.com/#/?page=1&query=talking&type=Motion%2CMotionPack)

Field Medic의 방향별 조준 클립은 기존 조준 이동 그리드의 이름과 루프 위상을 공유한다. 후진·좌우·대각선 표본을 포함하며, 360도 혼합용 샘플 위치는 기존 계약을 사용한다. [조준 이동 블렌드 그리드](aim_locomotion_blending.json)

| 클립 | 길이(초) | 루프 |
|---|---:|:---:|
| `Shop_Idle_Counter_Loop` | 3.60 | ✓ |
| `Shop_Talk_Welcome` | 1.23 | |
| `Shop_Trade_HandOver` | 1.13 | |
| `Shop_Talk_Explain_Loop` | 3.20 | ✓ |
| `Shop_Farewell_Nod` | 0.97 | |
| `Shop_Service_Inspect_Loop` | 3.20 | ✓ |
| `Resident_Talk_Listen_Loop` | 3.00 | ✓ |

원본 타임라인은 30fps이고 GLB는 60Hz로 저장했다. 루트 위치와 회전을 고정했다. 인사·전달·끄덕임·복귀 표식은 각 클립의 정규화 위상으로 제공한다. 상호작용이 중단되면 그 순간 포즈에서 대상 클립으로 0.2초 동안 블렌드한다. 결제, 보상, 방향, 거리와 카메라 제어는 게임 상태가 소유한다. [클립과 표식 계약](facility_animation_bank.json)

GLB 본 순서, 부모, 역바인드 행렬, 길이와 루프 접합을 검사했다. Field Medic 차림으로 대기·인사·전달·설명·점검 피크를 렌더링해 손의 방향과 상체 구도를 확인했다. 실제 거래창에서 상태 전환과 중단 블렌드, 모든 옷의 관통 검수는 게임 연결 후 확인한다. [인사 프리뷰](../Preview/Facility/Shop_Talk_Welcome.png) · [전달 프리뷰](../Preview/Facility/Shop_Trade_HandOver.png)
