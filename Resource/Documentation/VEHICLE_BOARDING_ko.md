# CQ-06B/C 승하차 모델과 모션

2026-11-27. 쿠페 좌우 승차·하차와 운전 자세 5클립, Courier 왼쪽 승차·하차와 조종석 대기·상승·좌우 선회·착륙 7클립을 HUMAN_7H 공용 뱅크로 제공한다. 두 차량 모두 움직이는 캐노피가 출입구를 연다. 쿠페는 양쪽 발판으로 들어올 수 있고, Courier는 날개 앞쪽 왼편으로 들어온다. [Blender 원본](../Source/Vehicle_Boarding.blend) · [공용 GLB 뱅크](../Export/AnimationBanks/HUMAN_7H_Boarding.glb)

기존 차체의 조종석을 파내고 시트·발밑 공간·조종대를 조정했다. 손잡이와 발판을 기존 파츠에 합쳤다. 조립 파츠 수와 기존 차량 본·캐노피 축을 유지했다. 차량의 원점과 축을 공유하는 승하차용 조립체이며, 리소스 팩에서 `boarding/Coupe`, `boarding/Courier`로 선택할 수 있다. [쿠페 GLB](../Export/CQ06/Coupe_Boarding.glb) · [Courier GLB](../Export/CQ06/Courier_Boarding.glb)

2026-11-27에 Mixamo 공개 미리보기의 `Stand To Sit` 중간 프레임에서 무릎을 굽히면서 몸통을 앞으로 기울이는 동세와 `Sitting Idle`의 하체 지지와 앉은 호흡을 확인했다. 게임 원본 리그에 조종석 치수와 접점에 맞는 동작을 직접 제작했다. [Stand To Sit](https://www.mixamo.com/#/?page=1&query=stand%20to%20sit&type=Motion%2CMotionPack) · [Sitting Idle](https://www.mixamo.com/#/?page=1&query=sitting&type=Motion%2CMotionPack)

접점은 차량 로컬 미터 좌표(+X 전방, +Z 상방)이며 승하차 골반 궤적은 화면 연출에 사용한다. 인체 루트는 고정한다. 캐노피가 완전히 열린 뒤 사람 동작을 진행하고, 닫기 전에 발과 손이 출입구를 벗어났는지 상태를 확인한다. 좌석, 발판, 조종대와 시각 이벤트 위상은 계약 파일에 기록했다. 출구 안전 판정·승객 배치·기체 이동은 게임에서 처리한다. [좌표와 클립 계약](vehicle_boarding_bank.json)

본과 바인드, 차량 메커니즘 클립 보존, 인체 루트 고정, 루프 접합, 발판 접점과 조종석 바닥 깊이를 검사했다. 정상 체형의 기존 Explorer 복장으로 두 차량의 승차와 착석을 렌더링했다. 쿠페 좌석은 닫힌 캐노피에서 머리 여유를 확보하도록 11.4cm 낮췄다. 양쪽 차량에서 착석 루프 5프레임의 머리·헤어·목과 닫힌 캐노피 충돌을 검사했다. 실제 게임 및 다른 체형·복장 조합의 관통 검수는 상태 연결 후 진행한다. [쿠페 승차 프리뷰](../Preview/Boarding/Coupe_Enter_Left.png) · [Courier 착석 프리뷰](../Preview/Boarding/Courier_Pilot_Idle_Loop.png)
