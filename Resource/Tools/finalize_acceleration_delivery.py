"""Record verified animation delivery and refresh documentation without touching Runtime."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):return json.loads((ROOT/p).read_text(encoding='utf-8'))
checks={k:read('Validation/'+p) for k,p in {
    'authoring':'acceleration_authoring.json','motion':'acceleration_motion_report.json',
    'equipment':'acceleration_equipment.json','glb_preservation':'acceleration_glb_preservation.json',
    'roundtrip':'acceleration_roundtrip_report.json','projects':'acceleration_project_preservation.json'}.items()}
assert all(v['all_checks_passed'] for v in checks.values())
clips=read('Documentation/animation_clips.json')['HUMAN_7H']
report=dict(date='2026-10-29',revision='acceleration_v1',previous_human_clips=63,new_human_clips=19,
    total_human_clips=len(clips),characters_updated=len(checks['glb_preservation']['files']),
    equipment_assemblies_checked=len(checks['equipment']['characters']),
    roundtrip_characters=len(checks['roundtrip']['characters']),root_motion=False,
    old_actions_preserved=checks['authoring']['old_actions_preserved'],
    geometry_skin_hash_preserved=checks['authoring']['geometry_before']==checks['authoring']['geometry_after'],
    bind_preserved=checks['authoring']['bind_preserved'],game_code_modified=False,runtime_cache_modified=False,
    clips=list(checks['authoring']['clips']),preview='Preview/Acceleration/delivery_overview.jpg',
    contract='Documentation/acceleration_blending.json',all_checks_passed=True)
(ROOT/'Validation/acceleration_delivery_summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

path=ROOT/'README.md';raw=path.read_bytes()
try:text=raw.decode('utf-8');encoding='utf-8'
except UnicodeDecodeError:text=raw.decode('cp949');encoding='cp949'
text=text.replace('공통 인체 **63개 스켈레탈 클립**(기존 43개 상체 개선 + 조준 이동 16개 + 권총 운반/재장전 4개)',
                  '공통 인체 **82개 스켈레탈 클립**(기존 63개 보존 + 가감속/스프린트 19개)')
section='''## 가감속·스프린트 추가 - 2026-10-29

기존 인체 63클립과 23본·메시·스킨·장비를 보존하고 **가감속 19클립을 추가해 총 82개**입니다. 걷기/달리기/스프린트 출발·정지는 좌우 발 선택지가 있고, 달리기에서 걷기로 감속 및 달리기와 스프린트 사이의 전환을 제공합니다. 스프린트 기준 속도는 4.2m/s입니다. 기존 캐릭터 GLB 10개와 Blender 원본 두 파일에 반영했습니다. 게임 코드와 Runtime 캐시는 변경하지 않았습니다.

[제작·연결 안내](Documentation/ACCELERATION_ko.md) · [속도·위상 계약](Documentation/acceleration_blending.json) · [출발](Preview/Acceleration/Run_Start_L.gif) · [제동](Preview/Acceleration/Run_Stop_L.gif) · [스프린트](Preview/Acceleration/Sprint.gif)

![가감속 검토](Preview/Acceleration/delivery_overview.jpg)

'''
if '## 가감속·스프린트 추가 - 2026-10-29' not in text:
    where=text.index('## 권총')
    text=text[:where]+section+text[where:]
path.write_bytes(text.encode(encoding))

for name in ['Documentation/CIVIC_ANIMATION_CHECKPOINT.md','Documentation/CIVIC_ANIMATION_GUIDE_ko.md','Documentation/PISTOL_LAYERS_ko.md','Documentation/AIM_LOCOMOTION_ko.md']:
    path=ROOT/name;text=path.read_text(encoding='utf-8');lines=text.splitlines(keepends=True)
    note='\n> 2026-10-29: 기존 63개를 보존하고 [가감속·스프린트 19개](ACCELERATION_ko.md)를 추가해 현재 인체는 총 82클립입니다. 아래는 이전 납품 당시의 기록이며 기존 계약은 유지합니다.\n'
    if note.strip() not in text:text=lines[0]+note+'\n'+''.join(lines[1:]);path.write_text(text,encoding='utf-8')
path=ROOT/'AGENTS.md';text=path.read_text(encoding='utf-8')
text=text.replace('총 63클립이며, 차량 반응 액션 21개를 유지한다.','총 82클립(기존 63 + 가감속 19)이며, 차량 반응 액션 21개를 유지한다.')
note='\n- 2026-10-29 가감속 확장은 `Tools/acceleration_motion.py`, `build_acceleration_animations.py`, `publish_acceleration_animations.py`가 소유한다. 기존 63개를 보존해 총 82클립이다. `Documentation/ACCELERATION_ko.md`, `acceleration_blending.json`을 따른다. 전환 종료는 실제 저장된 타깃 루프의 위상과 일치시킨다. 무장 시 새 하체와 기존 권총 운반/조준 상체를 합성하며, 비무장 기본 모션에는 무기를 표시하지 않는다. 원본 두 파일에 44번 검토 장면을 추가하고 기존 진입 장면을 유지한다.\n'
if note.strip() not in text:text+=note;path.write_text(text,encoding='utf-8')
print('ACCELERATION_DELIVERY_COMPLETE',json.dumps(report,ensure_ascii=True))
