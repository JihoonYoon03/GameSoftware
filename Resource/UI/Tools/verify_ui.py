"""Meaningful asset, glyph-coverage, contrast and contract checks for this kit."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import json
import hashlib
import xml.etree.ElementTree as ET
import sys
sys.stdout.reconfigure(encoding='utf-8')

ROOT=Path(__file__).resolve().parents[1]
def read(name): return json.loads((ROOT/name).read_text(encoding='utf8'))
def save(name,data): (ROOT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
manifest=read('ui_manifest.json')
catalog=read('Source/ui_state_catalog.json')
report=read('Validation/browser_report.json')
issues=[]
files=[]
for group in ['icons','hud_icons','components']:
    for name,asset in manifest[group].items():
        source=ROOT/asset['svg'];target=ROOT/asset['png']
        if not source.exists() or not target.exists(): issues.append('Missing asset: '+name);continue
        tree=ET.fromstring(source.read_text(encoding='utf8'))
        if tree.findall('.//{http://www.w3.org/2000/svg}text'): issues.append('Text baked in asset: '+name)
        files.extend([asset['svg'],asset['png']])
if set(catalog)!={f'UI-{i:02d}' for i in range(1,26)}: issues.append('Incomplete UI coverage')
text_flows=read('Source/text_flows.json')['flows']
if any(not f['lossless'] or ''.join(f['lines'])!=f['source'].replace('\n','') for f in text_flows): issues.append('Text flow dropped source characters')
locked=ET.fromstring((ROOT/'Source/Screens/35_weapon_wheel_locked.svg').read_text(encoding='utf8'))
if any('권총' in ''.join(e.itertext()) for e in locked.findall('.//{http://www.w3.org/2000/svg}text')): issues.append('Unowned weapon revealed in locked wheel')
for id,entry in catalog.items():
    if not entry['preview_screens']: issues.append('No preview for '+id)
    if not set(entry['icons']).issubset(manifest['icons']): issues.append('Unknown icon in '+id)
if report['overflowCount'] or report['overlapCount'] or report['pageErrors']: issues.append('Browser geometry/console checks failed')
if not all(a['hasAlpha'] for a in report['assets']): issues.append('Asset without alpha')
activity=read('Source/activity_design_contract.json')
if activity['request_sections']!=['CQ-02A','CQ-02B','CQ-02C','CQ-02D','CQ-02E']:issues.append('Missing current activity request')
for cq in activity['request_sections']:
    if not any(cq in s['requirements'] for s in manifest['screens'].values()):issues.append('Missing activity review '+cq)
atlas=read('Validation/20261109/atlas_report.json')
if atlas['status']!='PASS' or atlas['legacy_count']!=127 or atlas['sprite_count']!=sum(len(manifest[k]) for k in ['icons','hud_icons','components']):issues.append('Complete atlas validation failed')
if read('Validation/preview_navigation.json')['status']!='PASS':issues.append('Preview navigation failed')
for name,s in manifest['screens'].items():
    if not (ROOT/s['svg']).exists() or not (ROOT/s['png']).exists():issues.append('Missing review screen '+name)
if {s['name'] for s in report['scenes']}!=set(manifest['screens']):issues.append('Stale browser screen report')
if read('Validation/20261109/backdrop_report.json')['status']!='PASS':issues.append('Backdrop resource validation failed')

# GDI checks the specified font's glyph indices, rather than trusting fallback rendering.
inventory=read('Validation/text_inventory.json')
characters=sorted(set(''.join(e['text'] for e in inventory)))
gdi=ctypes.WinDLL('gdi32',use_last_error=True)
gdi.CreateCompatibleDC.argtypes=[wintypes.HDC];gdi.CreateCompatibleDC.restype=wintypes.HDC
gdi.CreateFontW.argtypes=[ctypes.c_int]*5+[wintypes.DWORD]*8+[wintypes.LPCWSTR]
gdi.CreateFontW.restype=wintypes.HANDLE
gdi.SelectObject.argtypes=[wintypes.HDC,wintypes.HANDLE];gdi.SelectObject.restype=wintypes.HANDLE
gdi.GetGlyphIndicesW.argtypes=[wintypes.HDC,wintypes.LPCWSTR,ctypes.c_int,ctypes.POINTER(wintypes.WORD),wintypes.DWORD]
gdi.GetGlyphIndicesW.restype=wintypes.DWORD
gdi.DeleteObject.argtypes=[wintypes.HANDLE];gdi.DeleteDC.argtypes=[wintypes.HDC]
dc=gdi.CreateCompatibleDC(None)
font=gdi.CreateFontW(-18,0,0,0,400,0,0,0,1,0,0,4,0,'Malgun Gothic')
old=gdi.SelectObject(dc,font)
sample=''.join(characters);indices=(wintypes.WORD*len(sample))()
result=gdi.GetGlyphIndicesW(dc,sample,len(sample),indices,1)
missing=[dict(character=c,codepoint=f'U+{ord(c):04X}') for c,index in zip(sample,indices) if index==65535]
gdi.SelectObject(dc,old);gdi.DeleteObject(font);gdi.DeleteDC(dc)
if result==0xffffffff: issues.append('Font glyph inspection failed')
if missing: issues.append('Missing font glyphs')

def rgb(hex_): return tuple(int(hex_[i:i+2],16)/255 for i in (1,3,5))
def composite(fg,bg,alpha): return tuple(a*alpha+b*(1-alpha) for a,b in zip(fg,bg))
def lum(c):
    s=[x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in c]
    return sum(x*y for x,y in zip(s,[.2126,.7152,.0722]))
def contrast(a,b):
    x,y=sorted([lum(a),lum(b)]);return (y+.05)/(x+.05)
contrast_results=[]
for a in [.6,.8,.94,1]:
    for underlying in ['#ffffff','#000000','#83958e','#080e14']:
        bg=composite(rgb(manifest['palette']['bg']),rgb(underlying),a)
        for color in ['ink','muted','accent','amber','danger','blue']:
            fg=composite(rgb(manifest['palette'][color]),bg,.85)
            ratio=contrast(fg,bg)
            contrast_results.append(dict(panel_alpha=a,content_alpha=.85,underlying=underlying,color=color,ratio=round(ratio,3),passes=ratio>=4.5))
            if a>=manifest['opacity']['background']['min'] and ratio<4.5: issues.append(f'Insufficient contrast {color} over {underlying} at alpha {a}: {ratio}')
summary=dict(status='PASS' if not issues else 'FAIL',issues=issues,
    icon_count=len(manifest['icons']),hud_icon_count=len(manifest.get('hud_icons',{})),component_count=len(manifest['components']),screen_count=len(manifest['screens']),
    requirement_count=len(catalog),state_count=sum(len(e['states']) for e in catalog.values()),
    glyphs=dict(font='Malgun Gothic',unique_characters=len(characters),missing=missing,gdi_result=int(result)),
    geometry=dict(overflow=report['overflowCount'],text_overlaps=report['overlapCount'],page_errors=report['pageErrors']),
    contrast=dict(target=4.5,colors='Active informational text only; disabled UI is excluded.',
                  content_alpha=.85,panel_min=manifest['opacity']['background']['min'],results=contrast_results,
                  note='Solid-color compositing estimate; anti-aliasing edges, patterned game backgrounds, live renderer and color vision are not certified.'),
    text_flow_count=len(text_flows),text_lossless=all(f['lossless'] for f in text_flows),
    activity_sections=activity['request_sections'],legacy_sprites_preserved=atlas['legacy_count'],
    scope='Resource/browser checks only for 2026-11-09. Game code, executable and model Runtime pack unchanged; native renderer integration not tested.')
save('Validation/asset_report.json',summary)
supported=[r['ratio'] for r in contrast_results if r['panel_alpha']>=manifest['opacity']['background']['min']]
review=['# UI 디자인·원문 보존 검수 기록', '', '2026-11-09 / ERMA FIELD INTERFACE v1.3', '',
    'CQ-02A~E와 기존 UI-01~25를 함께 검사했습니다. 요청서의 예시 치수보다 사용자의 가시성·시인성·간결한 HUD·원문 보존 기준을 우선했습니다.', '',
    f'**{summary["status"]}** — 글리프 누락 {len(missing)}, 지정 폭/화면 이탈 {report["overflowCount"]}, 텍스트 겹침 {report["overlapCount"]}, 브라우저 오류 {len(report["pageErrors"])}.', '',
    f'- 일반 아이콘 {summary["icon_count"]}종 / 외곽선 HUD 아이콘 {summary["hud_icon_count"]}종 / 공통 부품 {summary["component_count"]}종 / 검토 화면 {summary["screen_count"]}개.',
    f'- 맑은 고딕 고유 문자 {len(characters)}종의 GDI 글리프 인덱스를 검사했습니다.',
    f'- {len(text_flows)}개 줄바꿈 원문을 재조합해 글자 누락이 없음을 확인했습니다. 이야기 원문의 말줄임 문장부호는 보존합니다.',
    '- 긴 지역명 기록을 1280×800 / 1920×1080 / 2560×1440에서 비율 유지 렌더했습니다.',
    '- 밝은 도시·광산·활동 기록·긴 지역명·목표 HUD를 패널 알파 60/80/94/100%에서 렌더했습니다. 60%는 지원 하한 밖 스트레스 조건입니다.',
    f'- 배경 80~100% / 내용 85%의 단색 합성 대비 최저 {min(supported):.3f}:1. 실제 패턴 배경 전부를 인증하는 수치는 아닙니다. HUD 외곽선은 별도 보호 수단입니다.',
    '- 대화·기존 알림·행성 준비 실패의 다음/이전 페이지와 패널 조절·배경 토글을 브라우저에서 검사했습니다.',
    '- 기존 127개 PNG의 파일 해시·RGBA·크기·ID를 보존했습니다. 통합 아틀라스의 145개 영역은 원본과 픽셀이 같고 4px 여백 및 선형 필터 이웃 간섭 검사를 통과했습니다.', '',
    '## 육안 검토', '',
    '이야기/지역 기록, 긴 지역명, 밝은 HUD, 정산 보류 원문 전체, 지도와 상태 갤러리를 원래 크기로 확인했습니다. 참여 상태·추적 버튼·상세 기록의 정렬선을 분리하고 버튼의 한글을 한 줄로 유지했습니다. 복원 표시는 일반 추적 상태를 재사용하며 별도 승인 팝업을 만들지 않습니다. 체력·탄약은 게이지·기호·수치로 유지합니다.', '',
    '## 검증 범위', '',
    '이 검사는 제작 SVG·PNG·독립 브라우저 미리보기와 오프라인 배경 비교에 대한 것입니다. 저장/보상/최종 대화의 게임 실행 검증을 대신하지 않습니다. 검토 상태 JSON은 읽기 전용 예시이며 게임 기록이나 재화를 바꾸지 않습니다.',
    '정적 배경은 현재 Runtime의 모델 삼각형을 읽어 숨겨진 WGL 문맥에서 별도 캡처했습니다. 실제 게임의 활성 렌더 백엔드·MSAA·후처리와의 픽셀 일치나 성능 향상을 보장하지 않습니다. Runtime 팩은 읽기 전후 해시가 같습니다.',
    '이번 작업은 Resource/UI 안에서만 수행했습니다. 게임 코드·사용자 실행 파일·Runtime 팩을 변경하지 않았고 네이티브 배치/입력/옵션 연결 검증은 통합 후 필요합니다.', '',
    '[전체 화면](../Preview/20261109/contact_sheet.png) · [활동 계약](../Source/activity_design_contract.json) · [브라우저 검사](browser_report.json) · [글리프·대비 검사](asset_report.json) · [아틀라스 검사](20261109/atlas_report.json)']
(ROOT/'Validation/REVIEW_ko.md').write_text('\n'.join(review)+'\n',encoding='utf-8')
hashes={}
for folder in ['Source','Export','Preview']:
    for file in sorted((ROOT/folder).rglob('*')):
        if file.is_file(): hashes[file.relative_to(ROOT).as_posix()]=hashlib.sha256(file.read_bytes()).hexdigest()
save('Validation/sha256.json',hashes)
print(json.dumps({k:v for k,v in summary.items() if k not in ('contrast',)},ensure_ascii=False,indent=2))
if issues: raise SystemExit(1)
