"""Publish the current budget policy; retain older reports as dated history."""
import json
from pathlib import Path
from part_budget_policy import MIN_BUDGET
ROOT=Path(__file__).resolve().parents[1]
cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'));assets=cat['assets']
source=json.loads((ROOT/'Validation/size_budget_source_report.json').read_text())['summary']
plan=json.loads((ROOT/'Validation/size_budget_plan.json').read_text())
maximum=max(a['triangles'] for a in assets);caps=sorted({a['triangle_budget'] for a in assets})
final=sum(a['triangles'] for a in assets);before=source['source_triangles']
def save(path,text):(ROOT/path).write_text(text,encoding='utf-8')
save('Documentation/POLYGON_BUDGET_ko.md',f'''# 전체 메시의 폴리곤 예산 — 2026-10-27

**납품 파트당 최대 200삼각형. 한도를 맞추기 위한 파트 분할·추가 GLB 생성은 금지합니다.** 기존 파트 하나의 모든 재질 구간을 합산합니다. 사각형은 내보내기 때 삼각형 2개로 계산합니다.

현재 **{len(assets):,}파트 / GLB {len(cat['exports']):,}개**이며, 이번 변경으로 추가된 모델 파일은 **0개**입니다. 파트 ID·경로·조립 파트 수·장면 노드 수를 유지했습니다. 실제 최대값은 **{maximum}삼각형**, 전체 고유 파트 합계는 **{before:,} → {final:,}삼각형**입니다. 한도보다 적은 파트에 면을 채우지 않습니다.

## 크기에 따른 상한

최적화 전의 공통 바인드 좌표에서 각 파트 경계 상자의 대각선 길이 D를 측정했습니다. 실제 단일 파트 중 가장 큰 `Transit_AirGuide_Curve90_R64_EdgeTicks-1`의 **89.392709m**를 고정 기준 R로 사용합니다. 가장 큰 파트도 실제로는 156삼각형이며 200개를 채우지 않습니다.

프로젝트 상한은 `4 × floor(({MIN_BUDGET} + {200-MIN_BUDGET} × log(1 + D/0.005) / log(1 + R/0.005)) / 4)`를 {MIN_BUDGET}~200 범위로 제한합니다. 외곽 곡면을 지킬 수 있도록 크기별 예산을 200 이내에서 재배분했습니다. 작은 부품의 상한은 낮추면서 얼굴·손의 디테일이 큰 통행로 때문에 사라지지 않도록 로그 배분을 사용했습니다. 현재 적용 상한은 **{', '.join(map(str,caps))}삼각형**이며 각 파트의 정확한 수치는 [예산 목록](part_polygon_budgets.json)에 있습니다. 이 공식은 이번 프로젝트의 제작 기준입니다.

로우 폴리 전체에 통용되는 ‘파트당 300폴리곤’ 표준은 확인되지 않았습니다. [Khronos 실시간 자산 제작 지침](https://github.com/KhronosGroup/3DC-Asset-Creation/blob/main/asset-creation-guidelines/RealtimeAssetCreationGuidelines.md)은 필요한 형상에 폴리곤을 배분하고 작은 디테일은 표면 정보로 표현하는 원칙을 다룹니다. [Unity LOD 안내](https://unity.com/glossary/level-of-detail)도 거리·화면 크기에 따른 디테일 조절을 설명합니다. 따라서 다른 숫자를 업계 표준으로 대체하지 않고 사용자의 200 상한을 적용했습니다.

## 디테일과 파일 수를 지키는 제작 순서

1. 필요한 실루엣·작동부·유리 패널·관절·패널 경계 등 디테일을 먼저 제작하고 편집 원본을 보관합니다.
2. 중복·불필요한 평면 분할·항상 가려진 내부 면부터 정리합니다.
3. 같은 파트 안에서 면 배치를 다시 정리합니다. 재질 경계, 스킨 가중치 영역, 접속점과 보행 폭을 보호합니다. 임의 비율의 일괄 감축을 기본으로 사용하지 않습니다.
4. 그래도 초과하면 나사·봉제선·작은 홈·트림 등의 부가 요소를 먼저 통째로 제거합니다. 외곽 곡선을 먼저 거칠게 만들지 않으며, 특히 추진장치의 원래 곡선 분할을 보존합니다. 한도 충족 때문에 기존 파트를 잘라 새 파트·파일을 만들지 않습니다.
5. 실제 GLB를 검사하고 비교 렌더와 움직이는 스킨을 확인합니다. 이번 단계는 디테일 손실을 일부 허용하므로 과거의 ‘외부 표면 무손실’ 판정을 현재 결과에 그대로 적용하지 않습니다.

부상 필드 16파트는 세 갈래 외장과 중심 코어의 원래 곡선 면을 그대로 복원했습니다. 각 188삼각형이며 실제 GLB의 삼각형 집합을 원본과 대조합니다. 작은 볼트·홈을 제거하고 발광은 기존 내벽의 재질로 표현해 별도 띠 형상에 쓰이던 면을 줄였습니다. 캐릭터·의복도 나사와 봉제선부터 제거하고 주 형상의 앞/옆 윤곽을 보호합니다. 차량 유리는 패널과 실링의 윤곽을 유지합니다. 평평한 페인트·홀로그램은 불필요한 솔리드 뒷면을 줄였습니다.

도로의 노면 접속 위치·끝단 접선·보행 유효 폭·경사·원래 충돌 영역은 유지합니다. 땅에 묻힌 면을 지워 달라진 시각 경계는 `render_bounds`에 따로 기록하며, `collision_envelope`를 시각 메시 크기에 맞춰 줄이지 않습니다.

## 원본과 납품본

`Source/Erma_Resource_Library.blend`의 `asset_id`가 있는 납품 파트와 모든 조립 예제, `Source/Civic_Animations.blend`, `Source/RoadPlan_Modules.blend`, 모든 Export GLB에 같은 형상을 반영했습니다. 조립 레시피·본·오프셋은 유지합니다. 인물 GLB 10개에는 상체 개선과 16개 방향별 조준 이동을 별도로 반영해 각 59클립이며, 나머지 애니메이션 GLB 25개의 클립 표본은 변경 전과 같습니다.

편집용 상세 원본 장면(17/24/25/36)과 `Source/Archives/Pre200Delivery_20261026.zip`은 비교·복구용입니다. 상세 원본에는 200 상한을 강제하지 않으며 납품 파트로 내보내지 않습니다. 이것을 더 가벼운 납품본 대신 사용하지 마십시오.

## 확인 자료

- [전후 비교: 장비·캐릭터](../Preview/PolygonBudget/armor.png), [얼굴·손](../Preview/PolygonBudget/face_hand.png), [부상 필드](../Preview/PolygonBudget/lift_field.png), [유리](../Preview/PolygonBudget/glazing.png)
- [크기·한도·실제 수치](part_polygon_budgets.json), [실제 GLB 보존 검사](../Validation/size_budget_validation.json), [원본 적용 보고서](../Validation/size_budget_source_report.json)
- `Tools/verify_resource_pack.py`, `Tools/verify_size_budgets.py`, `Tools/verify_road_plan.py`가 현재 납품 규격을 검사합니다.

이전 상세 제작 문서의 500 상한·492/448 최대값과 무손실 비교 수치는 당시의 기록입니다. 현재 수치는 이 문서와 예산 목록이 우선합니다. 제작 도구를 다시 실행해 200을 넘는 초안을 만들었으면 같은 파트 안에서 추가 정리한 뒤 검증을 통과시켜야 합니다.
''')
path=ROOT/'AGENTS.md';text=path.read_text(encoding='utf-8')
start=text.index('사용자가 정정한 제작 원칙:');end=text.index('\n## 제작 순서')
text=text[:start]+'''사용자가 정정한 제작 원칙: **디테일을 먼저 제작한 뒤 불필요한 폴리곤을 정리한다. 납품 파트당 최대 200삼각형이며, 작을수록 디테일을 유지하는 선에서 상한을 낮춘다. 한도를 맞추려고 파트를 쪼개거나 GLB 파일을 늘리지 않는다. 불가피하면 작은 장식 디테일을 일부 줄인다.**

현재 기준은 `Documentation/POLYGON_BUDGET_ko.md`, 파트별 실제 상한은 `Documentation/part_polygon_budgets.json`이다. 단일 원래 파트의 모든 재질 프리미티브를 합산한다. 보편적인 파트당 300폴리곤 업계 표준을 가정하지 않는다.
'''+text[end:]
text=text.replace('4. 숫자에 맞추는 전역 Decimate나 임의 비율 감축을 기본으로 사용하지 않는다. 외부 표면 비교에서 손상이 발견되면 해당 정리를 되돌린다.','4. 숫자에 맞추는 전역 Decimate나 임의 비율 감축을 기본으로 사용하지 않는다. 같은 파트 안에서 곡률·재질·관절·접속을 보호하며 면 배치를 정리하고 전후를 비교한다.')
text=text.replace('5. 최종 파트가 500삼각형을 넘으면 실제 교체·정비·조립 가능한 기능 단위로 재설계하거나 토폴로지를 수동 정리한다. 삼각형 개수로 임의 조각을 나누거나 디테일을 무조건 삭제하지 않는다. 반대로 500개를 채우기 위한 불필요한 면도 추가하지 않는다.','5. 최종 파트가 크기별 상한(최대 200삼각형)을 넘으면 같은 파트의 토폴로지를 정리한다. 그래도 초과하면 작은 볼트·홈·봉제선·실링 높이와 곡면 분할 수를 줄인다. 한도를 맞추기 위한 파트 분할·companion 생성·추가 모델 파일은 금지한다. 상한을 채우기 위한 면도 추가하지 않는다.')
text=text.replace('작은 볼트·홈·봉제선·실링 높이와 곡면 분할 수를 줄인다.','나사·봉제선·작은 홈·트림을 먼저 통째로 제거하고, 외곽 곡면과 실루엣을 보호한다. 특히 추진장치의 원래 곡선 분할을 보존한다.')
text=text.replace('인체는 기존 15개를 유지한 43개 클립이며, 차량 반응 액션 21개를 추가했다.','인체는 기존 43개에 조준 이동 16개를 더한 59개 클립이며, 차량 반응 액션 21개를 유지한다. 방향별 이동은 `Documentation/aim_locomotion_blending.json`을 따르며 몸의 조준 방향을 고정하고 위상을 공유한다.')
path.write_text(text,encoding='utf-8')
path=ROOT/'README.md';text=path.read_text(encoding='utf-8')
text=text.replace('외부 표면을 보존하며 불필요한 면을 정리합니다. 500삼각형은 파트별 납품 상한이며 최소 폴리곤 제작 목표가 아닙니다.','불필요한 면을 정리합니다. 현재 파트별 상한은 최대 200삼각형이며 작은 파트는 더 낮게 배분합니다. 한도를 맞추려고 파트를 나누거나 모델 파일을 늘리지 않으며, 불가피하면 작은 디테일을 일부 줄입니다. [현재 예산·적용 결과](Documentation/POLYGON_BUDGET_ko.md).')
text=text.replace('**파트당 최대 492삼각형**: 삼각형 변환 후에도 요청한 500개 이하',f'**파트당 실제 최대 {maximum}삼각형**: 크기별 상한 {min(caps)}~200, 파트·GLB 수 유지')
path.write_text(text,encoding='utf-8')
path=ROOT/'Documentation/README_ko.md';text=path.read_text(encoding='utf-8')
text=text.replace('전체 파트의 최대값은 492삼각형입니다.',f'현재 전체 파트의 최대값은 {maximum}삼각형입니다. [2026-10-26 크기별 예산과 분할 금지 정책](POLYGON_BUDGET_ko.md)이 이전 수치보다 우선합니다.')
text=text.replace('보존 비교 → 500삼각형 이하 검증','전후 비교 → 크기별 상한(최대 200삼각형) 검증')
text=text.replace('최대 500개입니다. 완성 캐릭터·건물 전체의 합계를 500개로 제한하는 규격은 아닙니다.','최대 200개이며 작은 파트는 더 낮게 제한합니다. 한도를 맞추려고 파트를 나누지 않으며 필요한 경우 장식을 일부 줄입니다. 완성 조립체의 합계에 적용하는 규격은 아닙니다.')
path.write_text(text,encoding='utf-8')
for name in ['MODELING_PIPELINE_ko.md','ROAD_PLAN_ko.md','CHARACTER_DETAIL_ko.md','REFINED_CITY_ko.md','AIR_CITY_ko.md','VEHICLES_ko.md']:
    path=ROOT/'Documentation'/name;text=path.read_text(encoding='utf-8');lines=text.splitlines()
    note='> 2026-10-26 갱신: 현재 납품본은 [크기별 최대 200삼각형·분할 금지 규칙](POLYGON_BUDGET_ko.md)을 적용했습니다. 아래 500 상한·이전 최대값·무손실 수치는 상세 제작 당시의 기록이며, 현재 수치는 예산 목록을 따릅니다.'
    if note not in text:lines[1:1]=['',note,'']
    path.write_text('\n'.join(lines)+'\n',encoding='utf-8')
path=ROOT/'Documentation/MODELING_PIPELINE_ko.md';text=path.read_text(encoding='utf-8')
marker='## 2026-10-22 상세 제작 기록 (현재 납품 상한 적용 전)'
if marker not in text:text=text.replace('2026-10-22 · `detail_first_lossless_cleanup_v1`',marker+'\n\n2026-10-22 · `detail_first_lossless_cleanup_v1`')
path.write_text(text,encoding='utf-8')
path=ROOT/'Documentation/CIVIC_ANIMATION_GUIDE_ko.md';text=path.read_text(encoding='utf-8');lines=text.splitlines()
note='> 2026-10-27 갱신: 편집 원본과 GLB는 [실루엣 우선·최대 200삼각형 정책](POLYGON_BUDGET_ko.md)을 적용했습니다. 인체는 기존 43개를 유지·개선하고 조준 이동 16개를 더한 59클립입니다. [상체·방향별 이동 안내](AIM_LOCOMOTION_ko.md)를 함께 확인합니다.'
if note not in text:lines[1:1]=['',note,'']
path.write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('BUDGET_DOCUMENTATION_UPDATED',maximum,final,flush=True)
