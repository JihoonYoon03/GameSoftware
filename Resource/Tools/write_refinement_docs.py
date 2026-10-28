"""Current entry points for the silhouette and directional motion delivery."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def edit(name,replacements):
    p=ROOT/name;s=p.read_text(encoding='utf-8')
    for a,b in replacements:
        if a=='## 클립과 연결' and '## 조준 상태의 방향별 이동' in s:continue
        s=s.replace(a,b)
    p.write_text(s,encoding='utf-8')
p=ROOT/'Documentation/CIVIC_ANIMATION_GUIDE_ko.md';s=p.read_text(encoding='utf-8')
s='\n'.join(line for line in s.splitlines() if not line.startswith('> 2026-10-26 형상 갱신:'))+'\n'
p.write_text(s,encoding='utf-8')
edit('README.md',[
    ('공통 인체 **43개 스켈레탈 클립**(기존 15개 유지·개선 + 새 동작 28개)','공통 인체 **59개 스켈레탈 클립**(기존 43개 유지·개선 + 조준 이동 16개)'),
    ('크기별 상한 172~200','크기별 상한 184~200')])
p=ROOT/'README.md';s=p.read_text(encoding='utf-8');heading='## 실루엣·상체·조준 이동 갱신 — 2026-10-27'
if heading not in s:
    section=heading+'\n\n추진장치 16파트는 작은 나사·홈을 제거하고 원래 외곽 곡면을 복원했습니다. 파트 분할 없이 각 188삼각형입니다. Mixamo 공개 미리보기를 참고해 걷기·달리기의 팔 벌어짐과 조준 그립을 수정했습니다. 기존 하체 동작을 유지하며 조준 걷기·달리기를 각각 8방향 추가했습니다.\n\n[조준 이동·360도 블렌딩 안내](Documentation/AIM_LOCOMOTION_ko.md) · [블렌딩 표본·연결 데이터](Documentation/aim_locomotion_blending.json) · [새 조준 옆걸음](Preview/Civic/Aim_Walk_Left.gif) · [상체 개선 전후](Preview/Civic/Before_After.gif) · [추진장치 비교](Preview/PolygonBudget/lift_field.png)\n\n'
    s=s.replace('## 도시 반응·모션 개선',section+'## 도시 반응·모션 개선',1)
p.write_text(s,encoding='utf-8')
edit('Documentation/CIVIC_ANIMATION_GUIDE_ko.md',[
    ('43개 인체 클립과 본 구조는 유지합니다.','기존 43개를 유지·개선하고 조준 이동 16개를 더한 59클립입니다. [상체·방향별 이동 안내](AIM_LOCOMOTION_ko.md)를 함께 확인합니다.'),
    ('HUMAN_7H는 기존 15개를 유지하고 28개를 더한 43개 클립이다.','HUMAN_7H는 기존 43개를 유지·개선하고 조준 걷기·달리기 16개를 더한 59개 클립이다.'),
    ('2026-10-26 최신 렌더로 GIF/접촉 시트 28개씩을 갱신했다. 두 Blender 원본의 기본 장면·카메라·43개 인체 액션과 검토 장면 5개의 실제 스킨 재생도 확인했다(`civic_project_report.json`).','2026-10-27 렌더에 상체 개선과 16개 방향별 이동을 반영했다. 두 Blender 원본에서 59개 인체 액션과 6개 검토 장면의 실제 스킨 재생을 확인한다(`civic_project_report.json`).'),
    ('- `41_Vehicle_Impact_Review`: 차량 5종 반동.','- `41_Vehicle_Impact_Review`: 차량 5종 반동.\n- `42_Aim_Directional_Review`: 조준 방향을 유지하는 8방향 걷기 비교. `Aim_Run_*`으로 달리기 비교 가능.'),
    ('## 클립과 연결','## 조준 상태의 방향별 이동\n\n`Aim_Walk_*` 8개와 `Aim_Run_*` 8개는 몸의 조준 방향을 유지하는 전진·후진·좌우·대각선 루프이다. 발 디딤 위상을 공유하며 [별도 연결 안내](AIM_LOCOMOTION_ko.md)와 `aim_locomotion_blending.json`의 속도 표본·접지 보정식을 따른다.\n\n## 클립과 연결')])
edit('Documentation/ANIMATION_GUIDE_ko.md',[
    ('인체는 기존 15개에 28개를 추가한 43개 클립입니다.','인체는 기존 43개를 유지·개선하고 조준 이동 16개를 추가한 59개 클립입니다. [상체 개선·8방향 걷기/달리기](AIM_LOCOMOTION_ko.md)를 참고합니다.')])
edit('Documentation/README_ko.md',[
    ('2026-10-23 현재 통행로 확장을 포함한 전체 독립 파트','2026-10-27 현재 통행로 확장을 포함한 전체 독립 파트')])
p=ROOT/'Documentation/CIVIC_ANIMATION_CHECKPOINT.md';s=p.read_text(encoding='utf-8');title='## 2026-10-27 추가 갱신'
if title not in s:
    pos=s.find('\n')+1;s=s[:pos]+'\n'+title+'\n\n기존 43개 상체를 개선하고 조준 이동 16개를 더한 59클립을 납품한다. 하체 보행 키와 23본을 유지한다. 검토 장면은 37~42의 6개이며 최신 규격은 [상체·방향별 이동 안내](AIM_LOCOMOTION_ko.md)와 [블렌딩 계약](aim_locomotion_blending.json)을 따른다. 추진장치는 실루엣 우선 정책으로 원래 곡선을 복원했다.\n\n## 이전 2026-10-26 제작 기록\n'+s[pos:]
p.write_text(s,encoding='utf-8')
print('REFINEMENT_DOCUMENTATION_UPDATED',flush=True)
