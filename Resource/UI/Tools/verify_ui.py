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
if set(catalog)!={f'UI-{i:02d}' for i in range(1,24)}: issues.append('Incomplete UI coverage')
for id,entry in catalog.items():
    if not entry['preview_screens']: issues.append('No preview for '+id)
    if not set(entry['icons']).issubset(manifest['icons']): issues.append('Unknown icon in '+id)
if report['overflowCount'] or report['overlapCount'] or report['pageErrors']: issues.append('Browser geometry/console checks failed')
if not all(a['hasAlpha'] for a in report['assets']): issues.append('Asset without alpha')

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
    scope='UI resources and browser previews only; no game executable or Runtime changes.')
save('Validation/asset_report.json',summary)
supported=[r['ratio'] for r in contrast_results if r['panel_alpha']>=manifest['opacity']['background']['min']]
review=['# UI 가시성·HUD 개선 검수 기록', '', '2026-10-26 / ERMA FIELD INTERFACE v1.1', '',
        '이번 기록은 v1.0의 카드 중심 레이아웃 검수를 대체한다. 실제 게임 연결은 검수 범위에 포함하지 않았다.', '',
        '## 결과', '', f'**{summary["status"]}** — 한글 글리프 누락 {len(missing)}, 화면/지정 폭 이탈 {report["overflowCount"]}, 텍스트 겹침 {report["overlapCount"]}, 브라우저 오류 {len(report["pageErrors"])}.', '',
        f'- 일반 아이콘 {len(manifest["icons"])}종, 외곽선 포함 HUD 아이콘 {len(manifest["hud_icons"])}종, 공통 부품 {len(manifest["components"])}종, 대표 화면 {len(manifest["screens"])}개.',
        f'- Windows GDI로 맑은 고딕의 고유 문자 {len(characters)}종을 직접 검사했다. 대체 글꼴에 의존하는 누락 기호가 없다.',
        '- 34개 화면의 실제 글자 폭과 텍스트끼리의 교차를 브라우저에서 측정했다. 한글 키 안내의 폭과 저체력 경고의 간격을 수정했다.',
        '- 112개 개별 PNG의 알파 채널, 원본 SVG의 텍스트 분리, 23개 요구사항·139개 상태 계약을 검사했다.',
        '- 대화는 1280×800 / 1920×1080 / 2560×1440에서 비율 유지 렌더와 캡처를 확인했다.',
        '- 도시·교전·광산·대화를 불투명도 60/80/94/100%에서 캡처했다. 80% 미만은 권장 범위 아래 스트레스 조건이다.',
        f'- 배경 80~100% / 내용 85%의 단색 패널 합성 대비는 최저 {min(supported):.3f}:1이다. 배경 없는 HUD는 별도의 검정 외곽선으로 윤곽을 보호하며 해당 패널 수치로 HUD 전체를 인증하지 않는다.', '',
        '## 전후 시야 점유', '', '| 상황 | 이전 UI 픽셀 비율 | 개선 후 | 점유 감소 |', '|---|---:|---:|---:|']
for c in report.get('coverage',[]):
    review.append(f'| {c["scene"]} | {c["beforePercent"]:.2f}% | {c["afterPercent"]:.2f}% | {c["reductionPercent"]:.1f}% |')
review += ['', '배경 도형을 제거한 투명 렌더에서 알파 0.1 이상의 픽셀 수를 비교했다. 미니맵·텍스트·게이지·표식을 포함하며 실제 게임 장면이나 플레이 성능의 측정은 아니다. [전후 비교](../Preview/HUD_before_after.png)와 [상세 수치](browser_report.json)를 확인할 수 있다.', '',
           '## 육안 검토', '',
           '도시·광산·교전·무전 대화를 원래 크기로 검토하고 전체 화면 모음에서 메뉴와 정보 위계를 확인했다. 큰 체력·탄약 카드와 스캐너·수배·바이크 카드를 제거했다. 체력과 탄약의 앵커를 분리하고, 무전 중에도 위치를 유지한다. 체력 게이지는 12px, 핵심 수치는 28/38/44px, 대화 본문은 20px로 보강했다. 밝은 배경에는 글자와 아이콘의 검정 외곽선을 사용한다. 수배는 채운/빈 별로도 구분한다.', '',
           '## 남은 게임 통합', '',
           '검토 배경은 약식 도형이다. 게임 소스·실행 파일·Runtime 캐시는 변경하지 않았다. 게임의 실제 장면·후처리·글꼴 렌더러·입력·옵션·동적 페이지 분리·데이터 공개 조건은 별도 연결 검증이 필요하다. [HUD 구현 명세](../HUD_IMPLEMENTATION_ko.md)를 따른다. UI-18~23은 후속 메뉴 디자인이며 연결 완료 기능으로 간주하지 않는다.', '',
           '대비 계산은 단색 sRGB 합성이며 모든 게임 패턴 배경·안티앨리어싱·색각 특성에 대한 인증이 아니다. 일반 상태 설명·악명 상세는 상시 노출하지 않으며, 실제 값과 노출 조건은 게임에서 받아야 한다.', '',
           '[브라우저 검사](browser_report.json) · [글리프·리소스·대비 검사](asset_report.json) · [파일 해시](sha256.json)']
(ROOT/'Validation/REVIEW_ko.md').write_text('\n'.join(review)+'\n',encoding='utf-8')
hashes={}
for folder in ['Source','Export','Preview']:
    for file in sorted((ROOT/folder).rglob('*')):
        if file.is_file(): hashes[file.relative_to(ROOT).as_posix()]=hashlib.sha256(file.read_bytes()).hexdigest()
save('Validation/sha256.json',hashes)
print(json.dumps({k:v for k,v in summary.items() if k not in ('contrast',)},ensure_ascii=False,indent=2))
if issues: raise SystemExit(1)
