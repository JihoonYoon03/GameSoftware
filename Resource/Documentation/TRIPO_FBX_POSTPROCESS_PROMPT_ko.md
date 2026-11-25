# 2번 프롬프트 · 범용 Tripo `.fbx` → Blender 후가공 → 게임용 `.glb` 익스포트

너는 Blender 4.x 파이썬(`bpy`) 기반 3D 게임 테크니컬 아티스트다.
Tripo3D에서 생성한 원본 `.fbx` 파츠를 현재 게임 엔진(OpenGL 4.5 / v4 36B 압축 정점 / R4 `skinSlot` 공유 / Mixamo 72본 표준 스켈레톤) 규격에 맞게 후가공하고, 작업 `.blend` 저장 및 게임용 `.glb` 익스포트까지 수행하는 범용 Blender Python 스크립트를 작성해줘.

[파츠 설정 변수 — 작업 시 이 블록만 수정]
- PART_CATEGORY = "<파츠 분류 폴더명: 예) Clothes / Hair / Accessories / Weapons / Vehicles / Props / Buildings>"
- EXPORT_CATEGORY = "<Export 하위 폴더명: 예) HumanV4 / Characters / Vehicles / Aircraft / Scenery / MeasuredBuildings / Modules>"
- PART_NAME = "<파일명(확장자 제외): 예) Frontier_Coat_LOD0>"
- SYMMETRIC_LEFT_INPUT = False  # 작업 지시에 대칭이라고 명시한 착용 파츠만 True. 입력 FBX는 캐릭터 기준 왼쪽 한쪽 분량이다.
- PART_MODE = "<아래 3가지 중 택1>"
  1) "SKINNED_OUTFIT" : 2계층 의복(코트·상의·하의·전신) 또는 3계층 스킨 파츠(헤어·망토)
  2) "RIGID_SOCKET"   : 3계층 강체 부착 파츠(헬멧·고글·배낭·견갑·파우치·무기)
  3) "STATIC_WORLD"   : 정적 월드/차량/건물/소품 파츠
- SOCKET_NAME = "<RIGID_SOCKET일 때만 지정: Head / Chest / Back / Pelvis / HandR / HandL / Muzzle>"
- TARGET_TRIS = <LOD0 목표 삼각형 수: 의복 2200~3500 / 헤어 1200~2500 / 소형 소켓 400~1200 / 배낭·무기 1200~2800 / 차량 3000~8000 / 건물 2500~12000>
- MASTER_RIG_BLEND = r"C:\Users\wlgns\Documents\Github\GameSoftware\Resource\Source\Characters\Master_Mixamo_Base.blend"  # SKINNED_OUTFIT일 때 마스터 인체·Mixamo Armature 원본 경로

대칭 파츠는 분류 폴더와 관계없이 PART_MODE를 `SKINNED_OUTFIT`으로 설정하고 22본 Mixamo 마스터 리그를 사용한다.

[고정 프로젝트 경로 규칙]
- Blender 작업 저장(.blend): rf"C:\Users\wlgns\Documents\Github\GameSoftware\Resource\Source\{PART_CATEGORY}\{PART_NAME}.blend"
- Tripo 원본 입력(.fbx):     rf"C:\Users\wlgns\Documents\Github\GameSoftware\Resource\Tripo\{PART_CATEGORY}\{PART_NAME}.fbx"
- 게임용 최종 출력(.glb):    rf"C:\Users\wlgns\Documents\Github\GameSoftware\Resource\Export\{EXPORT_CATEGORY}\{PART_NAME}.glb"
(상위 폴더가 없으면 스크립트에서 `os.makedirs(..., exist_ok=True)`로 자동 생성한다.)

[공통 1단계: Tripo FBX 임포트 및 정규화]
1. `PART_MODE == "SKINNED_OUTFIT"`이면 `MASTER_RIG_BLEND`를 먼저 열고(원본 마스터 `Armature`와 `MasterBody` 유지), 그 외 모드는 빈 씬에서 시작한다.
2. `bpy.ops.import_scene.fbx(filepath=TRIPO_FBX_PATH, use_custom_normals=True, ignore_leaf_bones=True)`로 Tripo 원본 `.fbx`를 임포트한다.
3. Tripo가 임의로 생성한 더미 Armature/Empty/카메라/조명이 있으면 삭제(단, 마스터 `Armature`는 절대 삭제 금지)하고, 임포트된 파츠 메시만 단일 오브젝트 `PART_NAME`으로 병합(Join)한 뒤 `Apply All Transforms(Location/Rotation/Scale)` 및 FBX cm→m 스케일 오차(100배/0.01배)를 자동 보정한다.
4. `Recalculate Normals Outside`, 중복 정점 병합(`Merge by Distance = 0.0005m`), 불필요한 Alpha 블렌딩/과도한 Emission 제거 및 `Principled BSDF` PBR 연결(BaseColor/Normal/Roughness/Metallic)을 검증한다.

[모드별 2단계: 형태 가공 · 피팅 · 리깅]

CASE A) PART_MODE == "SKINNED_OUTFIT":

1. `MasterBody`(신장 1.8m, Mixamo T-포즈) 바운딩과 관절 높이(어깨·손목·허리·골반)에 맞춰 파츠의 위치·회전·스케일을 정밀 피팅한다.
2. [마네킹 자동 제거 및 속 파내기 — 3단계 파이프라인]
   - (2-1) Tripo Part Segmentation 대응: 임포트된 오브젝트가 여러 서브메시로 나뉘어 있다면, 먼저 Join하지 말고 각 오브젝트의 바운딩/중심 위치와 텍스처 평균 채도(Saturation)를 검사해 마네킹 전용 파트(머리·손·다리·발 등)를 자동 격리/삭제한 뒤 남은 의복 파츠만 병합한다.
   - (2-2) 단일 통짜 메시인 경우 3중 마네킹 필터로 제거 대상 면(Face)을 검출한다:
     * [규칙 1 · 본 좌표 외곽 컷] 마스터 Armature의 본 월드 좌표를 기준으로, 현재 파츠에 불필요한 노출 신체를 자동 선택한다:
       - 의복(Clothes): `mixamorig:Head`~`Neck` 상단(옷깃 위로 돌출된 머리), `LeftHand`/`RightHand` 손목 커프 바깥(양손), 의복 최하단 밑단 아래(종아리·발) 영역.
       - 헤어/헬멧(Hair/Head): `mixamorig:Neck` 이하 몸통 전체 및 얼굴 전면(`MasterBody` 얼굴 표면 거리 < 0.008m).
     * [규칙 2 · UV 텍스처 무채색 회색 + MasterBody 근접 판별] 코트 트임(슬릿) 사이의 허벅지나 목 안쪽처럼 바운딩으로 자르기 어려운 영역은, 각 면(Face)의 UV 중심 좌표에서 BaseColor 이미지 픽셀(HSV)을 샘플링한다. `S < 0.07` 이고 `0.38 < V < 0.72`(균일 무채색 회색 마네킹 톤)이면서 동시에 `MasterBody` 표면과의 최근접 거리가 `0.014m` 이하인 면을 마네킹 노출부로 판별한다.
     * [규칙 3 · 아일랜드(연결 덩어리) 정리] 마네킹 면 삭제 후 본체에서 떨어져 나온 고립 조각(전체 면 수의 2% 미만 소형 아일랜드)은 자동 삭제한다.
   - (2-3) 안전장치 옵션(`AUTO_DELETE_MANNEQUIN = True/False`):
     * `False`일 때는 바로 지우지 않고 검출된 정점을 `_MANNEQUIN_DELETE` 버텍스 그룹(및 별도 Face Set)에 저장해 Blender Edit Mode에서 눈으로 확인 후 `X → Faces`로 즉시 삭제할 수 있게 한다.
     * `True`일 때는 자동 삭제 후, 열린 경계 루프(목구멍·소매 끝·하단 트임)의 들쭉날쭉한 톱니 경계에 가벼운 `Boundary Smooth`를 적용하고 내부 캡이 메워져 있으면 뚫어 얇은 쉘로 정리한다.
3. 피부 클리핑 방지를 위해 상체/팔 영역에는 외곽 여유(`0.006~0.010m`)를 확보하되 코트 자락은 다리에 달라붙지 않게 제외한다.
   - `SYMMETRIC_LEFT_INPUT=True`이면 왼쪽 파츠를 마스터의 왼쪽 착용 위치에 먼저 피팅한다. 이를 복제해 마스터 인체의 정중면(X=0)을 기준으로 X축 미러 복사한다. 오른쪽 복사본의 변환을 적용하고 면 법선 방향을 확인한 뒤, Object Mode에서 양쪽을 선택해 `Ctrl+J`(`bpy.ops.object.join()`)로 단일 메시 오브젝트 `PART_NAME`에 병합한다. 양쪽 합계에 `TARGET_TRIS`를 적용한다.
4. `Decimate`로 UV를 유지하며 `TARGET_TRIS` 이내로 감축 후 `Triangulate`한다.
5. **Mixamo 마스터 스켈레톤 계약 (절대 준수):**
   - 마스터 `Armature`의 본 개수·이름(`mixamorig:*`)·부모 순서·바인드 포즈(`inverseBindMatrices`)·스케일을 절대 변경하지 말고, 임의 트위스트본/보조본도 추가하지 않는다.
   - 파츠를 마스터 `Armature`에 바인딩(`ARMATURE` 부모 + `Armature Modifier`)한다.
   - `MasterBody` → 파츠로 `Data Transfer (Vertex Groups, Mapping='NEAREST_FACE_INTERP')` 웨이트 전사.
   - 대칭 파츠는 병합 후 `MasterBody`에서 양쪽 메시 전체로 웨이트를 전사한다. 왼쪽과 오른쪽 본을 각각 움직여 해당 쪽 파츠가 따라오는지 확인한다.
   - 코트 자락/하의 등 허리(`mixamorig:Hips`) 아래 영역은 `X > 0 / X < 0` 기준으로 좌/우 허벅지 본(`mixamorig:LeftUpLeg`, `mixamorig:RightUpLeg`) 영향이 교차 오염되지 않게 분리하고 경계부에 `Smooth(factor=0.4, repeat=2)` 적용.
   - 버클·단단한 장식 버텍스 그룹이 있으면 단일 지배 본에 100% 경직 웨이트를 지정하는 헬퍼 함수 제공.
   - v4 36B 압축 정점 호환 필수 후처리: `Limit Total(4)` → `Clean(0.01)` → `Normalize All`.

CASE B) PART_MODE == "RIGID_SOCKET":

1. 모든 Armature Modifier/Vertex Group/Skin 데이터를 제거해 완전한 강체 정적 메시로 만든다.
2. 소켓(`SOCKET_NAME`) 부착 기준점에 맞춰 오브젝트 원점을 로컬 `(0, 0, 0)`, 전방 `+X`, 상단 `+Z`로 정렬하고 실측 미터 크기로 맞춘 뒤 `Apply All Transforms`한다.
3. `TARGET_TRIS` 이내로 감축·삼각화한다.

CASE C) PART_MODE == "STATIC_WORLD":

1. 바닥면 최하단 중앙을 월드 원점 `(0, 0, 0)`(`Z=0` 접지), 정면을 `+X`로 정렬하고 실측 미터 단위로 스케일링 후 `Apply All Transforms`한다.
2. 불필요한 내부 보이지 않는 면을 정리하고 `TARGET_TRIS` 이내로 감축·삼각화한다.

[공통 3단계: 저장 · 익스포트 · 자동 검증 로그]
1. 작업 완료 상태를 `Resource\Source\<PART_CATEGORY>\<PART_NAME>.blend`로 저장한다(`bpy.ops.wm.save_as_mainfile`).
2. 익스포트 대상만 선택(`SKINNED_OUTFIT`은 파츠 메시 + 마스터 `Armature`, 나머지 모드는 파츠 메시 단독)해 `Resource\Export\<EXPORT_CATEGORY>\<PART_NAME>.glb`로 내보낸다:
   - `export_format='GLB'`, `use_selection=True`, `export_yup=True`, `export_apply=(PART_MODE != "SKINNED_OUTFIT")`
   - `export_skins=(PART_MODE == "SKINNED_OUTFIT")`, `export_animations=False`(모션 클립 중복 내장 금지)
   - `export_normals=True`, `export_tangents=True`
   - 대칭 파츠의 최종 출력은 좌우가 병합된 메시 1개와 22본 마스터 리그를 포함한 `{PART_NAME}.glb` 파일 1개다. 출력 GLB에서 양쪽 형상, 메시 수, 스킨의 조인트 수(정확히 22개)를 검증한다.
3. 완료 직후 콘솔에 다음 검증 리포트를 출력한다:
   - 저장된 `.blend` / `.glb` 경로 및 파일 크기
   - 삼각형 수(예산 대비 통과 여부), 버텍스 수, 바운딩 박스 크기(X/Y/Z m)
   - (`SKINNED_OUTFIT`인 경우) 본 개수(<=71), 정점당 최대 본 수(<=4), 미할당(weight sum == 0) 정점 수(0개여야 통과)
