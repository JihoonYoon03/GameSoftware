# 도면 기반 인간 모델과 텍스처

2026-11-24 · 제작 요청서 7.3절에 맞춘 3계층 v4 캐릭터 제작본

## 모델

첨부된 회전 GIF의 왼쪽 남성과 가운데 여성을 기준으로 남녀 마스터 바디를 각각 하나씩 제작했다. 팔·몸통·다리의 경계 정점을 용접하고 목·손목·발목까지 이어지는 표면을 재구성했다. 근거리에서는 팔꿈치·무릎 주변 곡면, 손가락·발등, 얼굴에 면을 더 배분했다. 공통 23본 리그와 제작 기준 키 1.80m를 적용했다.

남녀 각각의 큰 체형을 길이와 폭의 기준으로 사용했다. 보통·마른 체형은 같은 관절 높이를 사용하며, 어깨·흉곽·허리·골반과 팔다리 단면을 줄였다. 양쪽 허벅지·무릎·발 안쪽 간격도 함께 줄였다. 여성형은 어깨 외곽을 더 좁히고 위팔 시작 위치를 몸통 쪽으로 옮겼다. 마른 체형과 큰 체형은 각 마스터의 공통 토폴로지를 사용하는 형태 변형이다.

| 계층 | 조립 및 검수 기준 |
|---|---|
| 마스터 인체 | 남녀 각 1개 스킨 메시와 체형 변형 2개. `_BODY_REGION`의 0은 머리·목·손, 1은 상체, 2는 하체다. 의복을 입으면 1·2를 가리고, 밀폐 헬멧과 장갑의 가림은 조립 목록에 기록한다. |
| 의복 | 세트당 전신 의복 1개와 필요한 경우 코트 레이어 1개. Blender Data Transfer의 Nearest Face Interpolated 방식으로 알맞은 체형의 웨이트를 전사했다. 코트는 0.35 강도로 1회 평활화했다. 어깨·무릎의 곡면과 옷 주름·부츠의 접힘 부피를 유지했다. |
| 추가 파츠 | 헤어는 마스터와 같은 스킨을 사용한다. 헬멧·배낭·견갑·파우치·경질 보호판은 소켓 원점의 정적 메시다. 경질 장비가 허벅지와 정강이처럼 서로 다른 본을 따라야 할 때는 실제 움직이는 조립 단위에 따라 연결한다. |

강체 소켓은 Blender에서 +X 전방·+Z 위, GLB에서 +X 전방·+Y 위를 사용한다. 조립 목록의 `joint_local_matrix_gltf`는 지정 본 공간에서의 위치와 회전이며 열 우선 행렬이다. 캐릭터마다 한 스킨을 공유할 수 있게 기존 역바인드와 본 순서를 유지했다. 기존 애니메이션은 조립 목록의 공용 참조를 사용한다.

탐험가·경비·의무병·조종사·항만 주민의 의상과 장비는 도면의 콘셉트와 정투영 디자인을 참고했다. 도면의 로우폴리 모델과 GIF 오른쪽 인물은 참고 범위에서 제외했다.

| 캐릭터 | 도면에서 반영한 구조 |
|---|---|
| 탐험가 | 쌍통 생명유지 팩, 돌출형 흉부 콘솔, 오른손목 스캐너, 아이보리·주황 외장 |
| 경비 | 새 둥근 흉갑과 좁은 모따기, 겹치는 복부판 2장, 어깨 보호판·전완·다리 장갑·등 팩. 가슴 명찰, 줄무늬, 얕은 홈은 보호판 UV에 그렸다. |
| 의무병 | 카트리지 3개를 단 전면 랙, 사선 표식이 있는 구조 팩, 허리 파우치와 좌우 코트 자락 |
| 조종사 | 주황 띠와 측면 통신부가 달린 반투명 헬멧, 소형 비행 지원 팩, 허리·허벅지 하네스 |
| 항만 주민 | 황토색 지퍼 재킷, 짙은 소매 패널, 통근 팩, 한쪽 허리 파우치와 카고 바지 |
| 남성 탐험가 | 남성 보통 체형에 맞춘 외장, 쌍통 생명유지 팩, 독립 항법 콘솔, 왼손목 스캐너 |
| 리아 | 비대칭 작업 재킷과 앞치마, 보호 고글, 산업용 전력 팩, 작업등과 공구 롤 |
| 현장 연구원 | 청록색 패널이 들어간 긴 양갈래 코트, 시료 3개를 담는 등 랙, 보라색 스캐너와 바이저 |
| 광부 | 황토색 안전모와 전면 램프, 이중 필터 마스크, 산업용 전력 팩과 어깨 장갑 |
| 중장 외골격 운용자 | 밀폐형 헬멧, 겹친 어깨판, 허벅지·정강이 보조 링크와 등 전력 팩 |
| 헬멧형 경비 | 새 경비 복장과 장비를 공유하며 곡면 외피와 일체형 전면부를 갖춘 새 밀폐 헬멧을 착용한다. |

[Blender 원본](../Source/Human_Drawing_Only_v4.blend)에는 편집용 고밀도 원본, 체형 변형, 의복·소켓 레이어와 검토 장면이 있다. [조립용 GLB](../Export/HumanSheet/Human_Drawing_Only_Parts.glb) 하나에 공통 리그와 LOD0~2, 그림자 프록시를 담았다. [조립 목록](human_sheet_v4.json)에서 기존 17개 조립체와 각 계층의 메시·재질·경계·삼각형 수를 확인할 수 있다. R4의 게임 내 스킨 공유·부착·가림 경로는 연동 검증 전이다.

공용 장비 도면의 헬멧, 전술·중장 어깨판, 우측 유틸리티 파우치, 좌측 진단 커프·의료 파우치, 좌우 작업등, 좌측 공구 롤 총 9개를 조립 목록에 매핑했다. 광부 마스크는 착용 상태에서 입과 턱을 가리므로 머리 본을 따라 움직이게 설정했다.

[직업별 복장](../Preview/HumanSheet/HumanSheet_Front.png) · [6개 체형](../Preview/HumanSheet/HumanSheet_Bodies.png) · [얼굴·헤어 LOD 확대](../Preview/HumanSheet/HumanSheet_Face_Hair.png) · [전신 LOD 비교](../Preview/HumanSheet/HumanSheet_LODs.png) · [상세 원본과 납품본 비교](../Preview/HumanSheet/HumanSheet_Original_vs_LOD0.png)

[추가 복장](../Preview/HumanSheet/HumanSheet_New_Outfits.png) · [대기·걷기·뛰기](../Preview/HumanSheet/HumanSheet_Quarter_Stances.png) · [조준·회피](../Preview/HumanSheet/HumanSheet_Quarter_Actions.png) · [지원 동작](../Preview/HumanSheet/HumanSheet_Quarter_Support.png)

[신규 경비 흉갑 정면·사선 확대](../Preview/HumanSheet/HumanSheet_Guard_Armor.png). 흉갑은 첨부 사진을 기준으로 폭·상부 목 파임·하단 아치부터 새로 제작했다. 외곽선과 판 두께, 눌린 모서리는 메시로 잡았다. 가슴 명찰, 어깨와 허벅지 줄무늬, 고정점·각인·표면 마모는 1024² 공유 아틀라스의 Base Color와 Normal, ORM으로 표현한다. 탐험가·의무병의 도색, 주민·중장 외골격의 가슴 명찰, 광부의 흉갑 홈, 연구원의 표식에도 같은 아틀라스를 사용한다.

## 텍스처

직물·외장·피부·장비 아틀라스용 Base Color·Normal·ORM 12장과 알파 채널을 가진 헤어 카드용 아틀라스 1장을 Blender와 GLB에 포함했다. 전부 **1024×1024 PNG**다. 직업별 컬러 패널은 모델의 색상 데이터로, 장비의 인쇄·음각 표식은 아틀라스로 표현했다. 바이저와 일부 카트리지 전면은 반투명 재질이다.

LOD0 헤어는 두피 형태 위에 모발 결을 따라 휘어진 폴리곤 카드를 배치했다. 아틀라스에 모발 카드용 알파 컷아웃 7개와 불투명한 두피용 텍스처 1개를 담았다. LOD1·LOD2와 그림자 단계는 불투명한 외곽 형상으로 전환한다. LOD2에는 눈·입 표시와 실루엣에 영향을 주는 어깨판·다리 보호판을 남겼다.

| 재질 | Base Color · Normal · ORM |
|---|---|
| 직물 | [Base Color](../Export/Textures/HumanSheet/HumanSheet_Fabric_BaseColor.png) · [Normal](../Export/Textures/HumanSheet/HumanSheet_Fabric_Normal.png) · [ORM](../Export/Textures/HumanSheet/HumanSheet_Fabric_ORM.png) |
| 외장 | [Base Color](../Export/Textures/HumanSheet/HumanSheet_Shell_BaseColor.png) · [Normal](../Export/Textures/HumanSheet/HumanSheet_Shell_Normal.png) · [ORM](../Export/Textures/HumanSheet/HumanSheet_Shell_ORM.png) |
| 피부 | [Base Color](../Export/Textures/HumanSheet/HumanSheet_Skin_BaseColor.png) · [Normal](../Export/Textures/HumanSheet/HumanSheet_Skin_Normal.png) · [ORM](../Export/Textures/HumanSheet/HumanSheet_Skin_ORM.png) |
| 장비 표면 | [Base Color](../Export/Textures/HumanSheet/HumanSheet_Equipment_BaseColor.png) · [Normal](../Export/Textures/HumanSheet/HumanSheet_Equipment_Normal.png) · [ORM](../Export/Textures/HumanSheet/HumanSheet_Equipment_ORM.png) |
| 헤어 카드 | [모발 RGBA 아틀라스](../Export/Textures/HumanSheet/HumanSheet_HairCards_BaseColor.png) |

Base Color는 sRGB, Normal은 선형 탄젠트 공간 +Y, ORM은 선형 R=AO·G=거칠기·B=금속성이다. 직물·외장·피부의 UV는 반복하고, 장비와 헤어 카드는 아틀라스의 여백 안에서 샘플링한다. GLB의 `COLOR_0`은 선형 색상이다. 비압축 RGBA8로 13장을 올리는 경우 텍스처 할당은 기본 레벨 약 52MiB, 전체 밉을 포함하면 약 69.3MiB다.

## 검수

강체 파츠와 캐릭터의 v4 삼각형 상한, 인덱스·UV·탄젠트·정규화된 4본 이하 스킨 가중치와 23본의 부모 관계·역바인드를 검사했다. 같은 조명과 45도 카메라에서 상세 원본과 LOD, 대기·걷기·뛰기·조준·회피·지원 자세를 확인했다. 마스터 바디 연결성, 형태 변형 단면, 소켓 행렬과 Blender 재가져오기 결과는 [검수 보고서](../Validation/human_sheet_v4_report.json)에 기록했다.
