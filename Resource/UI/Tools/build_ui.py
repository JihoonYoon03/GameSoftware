"""Build the editable, text-free Erma UI kit and Korean review screens. Stdlib only."""
from pathlib import Path
import json
import math
import html
import re
import hashlib

ROOT = Path(__file__).resolve().parents[1]
for folder in ('Source/Icons', 'Source/HUDIcons', 'Source/Components', 'Source/Screens', 'Export/Icons',
               'Export/HUDIcons', 'Export/Components', 'Export/Atlases', 'Preview', 'Validation'):
    (ROOT / folder).mkdir(parents=True, exist_ok=True)

C = dict(bg='#0a1821', panel='#142b34', edge='#65818c', ink='#f4f6f1', muted='#d4e1e4',
         accent='#a6e6cf', amber='#ffe1a6', danger='#ffc3ba', blue='#bcddff', disabled='#a3b6bf')
ICONS = {
    'health': '<path d="M12 5v14M5 12h14"/>',
    'heal': '<path d="M5 4h14v16H5zM9 2h6M12 8v8M8 12h8"/>',
    'pistol': '<path d="M3 6h17v6H10l-2 7H4l2-9H3zM15 7v4M11 12v3h3v-3"/>',
    'magazine': '<path d="M8 3h9l-2 18H6zM9 7h5M8 11h5M8 15h4"/>',
    'reload': '<path d="M19 8A8 8 0 1 0 20 15M19 3v5h-5"/>',
    'infinite': '<path d="M12 12C5 2 0 8 4 14c4 5 7-3 10-5 8-7 11 8 4 7-3 0-4-2-6-4z"/>',
    'objective': '<path d="M5 21V3h13l-3 4 3 4H5"/>',
    'investigate': '<circle cx="10" cy="10" r="6"/><path d="m15 15 6 6M10 7v6M7 10h6"/>',
    'dialogue': '<path d="M3 4h18v13H9l-5 4v-4H3zM7 9h10M7 13h6"/>',
    'radio': '<path d="M5 9h14v12H5zM8 6l10-4M8 13h3M8 17h3"/><circle cx="16" cy="15" r="1.5"/>',
    'terminal': '<path d="M3 4h18v13H3zM8 21h8M12 17v4M7 8l3 3-3 3M12 14h5"/>',
    'record': '<path d="M6 3h12v18H6zM9 7h6M9 11h6M9 15h4"/>',
    'history': '<path d="M4 8a8 8 0 1 1-1 8M4 3v5h5M12 7v6l4 2"/>',
    'scan': '<circle cx="12" cy="12" r="9"/><path d="M12 3v9l6 5M3 12h3M12 18v3M18 12h3"/><circle cx="8" cy="8" r="1"/>',
    'signal': '<path d="M12 20v-8M8 8a6 6 0 0 1 8 0M4 4a12 12 0 0 1 16 0"/><circle cx="12" cy="10" r="1.5"/>',
    'rescue': '<path d="M3 6h18v14H3zM8 6V3h8v3M12 10v6M9 13h6"/>',
    'companion': '<circle cx="12" cy="7" r="4"/><path d="M4 21v-3a8 8 0 0 1 16 0v3M4 21h16"/>',
    'injured': '<circle cx="8" cy="6" r="3"/><path d="M5 21v-7a3 3 0 0 1 6 0v7M16 9v8M12 13h8"/>',
    'medical': '<path d="M3 21V6h18v15M8 21v-5h8v5M12 3v8M8 7h8"/>',
    'wanted': '<path d="m12 2 3 6 7 1-5 5 1 7-6-3-6 3 1-7-5-5 7-1z"/>',
    'reputation': '<path d="M12 2 21 6v7c0 5-9 9-9 9s-9-4-9-9V6zM8 12l3 3 6-7"/>',
    'credit': '<path d="m12 2 9 5v10l-9 5-9-5V7zM16 8h-5l-3 4 3 4h5"/>',
    'parts': '<path d="m14 3 1 6 6 1-1 6-6-1-1 6-6-1 1-6-6-1 1-6 6 1 1-6z"/>',
    'bike': '<circle cx="5" cy="17" r="4"/><circle cx="19" cy="17" r="4"/><path d="m5 17 5-9 5 9H5M15 4h3l1 13M7 8h6"/>',
    'boost': '<path d="m13 2-9 12h7l-1 8 10-13h-8z"/>',
    'reverse': '<path d="m10 5-7 7 7 7M3 12h17M20 6v12"/>',
    'speed': '<path d="M3 19a10 10 0 1 1 18 0M12 14l5-7M6 16h1M6 8l1 1M17 16h1"/><circle cx="12" cy="14" r="2"/>',
    'walk': '<circle cx="13" cy="4" r="2"/><path d="m8 22 4-8 4 8M12 14l1-7-6 4M13 7l3 5h4"/>',
    'stop': '<path d="m8 2 8 0 6 6v8l-6 6H8l-6-6V8zM8 12h8"/>',
    'warning': '<path d="m12 2 10 19H2zM12 8v6M12 17v1"/>',
    'check': '<path d="m4 12 5 6L21 5"/>',
    'close': '<path d="m5 5 14 14M19 5 5 19"/>',
    'lock': '<path d="M5 10h14v11H5zM8 10V6a4 4 0 0 1 8 0v4M12 14v3"/>',
    'arrow': '<path d="m4 12 16 0M14 5l7 7-7 7"/>',
    'chevron': '<path d="m8 4 8 8-8 8"/>',
    'destination': '<path d="M19 9c0 6-7 13-7 13S5 15 5 9a7 7 0 1 1 14 0z"/><circle cx="12" cy="9" r="2"/>',
    'player': '<path d="m12 2 8 19-8-5-8 5z"/>',
    'home': '<path d="m2 11 10-9 10 9M5 9v12h14V9M10 21v-7h4v7"/>',
    'gate': '<path d="M3 21V5l9-3 9 3v16M7 21V8l5-2 5 2v13M10 16h4"/>',
    'power': '<path d="M12 2v10M6 5a9 9 0 1 0 12 0"/>',
    'supply': '<path d="m3 7 9-4 9 4v14H3zM3 7l9 4 9-4M12 11v10"/>',
    'exit': '<path d="M10 3H3v18h7M9 12h13M17 7l5 5-5 5"/>',
    'pause': '<path d="M7 3v18M17 3v18"/>',
    'settings': '<path d="M12 2v4M12 18v4M2 12h4M18 12h4M5 5l3 3M16 16l3 3M5 19l3-3M16 8l3-3"/><circle cx="12" cy="12" r="6"/><circle cx="12" cy="12" r="2"/>',
    'mouse': '<rect x="5" y="2" width="14" height="20" rx="7"/><path d="M12 2v8M5 10h14"/>',
    'keyboard': '<rect x="2" y="5" width="20" height="14" rx="2"/><path d="M5 9h1M9 9h1M13 9h1M17 9h1M5 13h1M9 13h1M13 13h1M17 13h1M7 16h10"/>',
    'transport': '<path d="M3 7h11v11H3zM14 11h4l3 4v3h-7M6 4h5"/><circle cx="7" cy="19" r="2"/><circle cx="18" cy="19" r="2"/>',
    'investigation_site': '<path d="M3 21V8h7V3h11v18M6 12h1M6 16h1M14 7h3M14 11h3M14 15h3"/>',
    'unknown_signal': '<path d="M4 8a11 11 0 0 1 16 0M7 11a7 7 0 0 1 10 0M10 14a3 3 0 0 1 4 0M12 18h.01"/><path d="m2 21 20-2" stroke-dasharray="2 3"/>',
    'level': '<path d="m4 14 8-10 8 10M4 21l8-10 8 10"/>',
    'attack': '<path d="m4 20 15-16 1 6L8 21M5 14l5 5M3 22l3-3"/>',
    'defense': '<path d="M12 2 21 6v7c0 5-9 9-9 9s-9-4-9-9V6zM12 6v12"/>',
    'empty': '<path d="M3 7h18v14H3zM7 7V3h10v4M8 13h8"/>',
}

def esc(s): return html.escape(str(s), quote=True)
def write(path, value):
    (ROOT / path).write_text(value, encoding='utf-8')
def dump(path, value): write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')
def svg(body, w, h, defs=''):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{defs}{body}</svg>'
def icon(name, x, y, size=24, color=None):
    return f'<g class="ui-icon" transform="translate({x} {y}) scale({size/24})" fill="none" stroke="{color or C["accent"]}" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round">{ICONS[name]}</g>'
def rect(x,y,w,h,fill,rx=0,extra=''):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" {extra}/>'
def line(x,y,x2,y2,color=None,width=1):
    return f'<path d="M{x} {y}H{x2}" stroke="{color or C["edge"]}" stroke-width="{width}"/>' if y==y2 else f'<path d="M{x} {y}L{x2} {y2}" stroke="{color or C["edge"]}" stroke-width="{width}"/>'

TEXT_AUDIT=[]
PANEL_AUDIT=[]
SCREEN=''
def text(s,x,y,size=16,color=None,weight=400,anchor='start',width=None):
    # y is baseline; data-max-width makes validation use the actual browser glyph metrics.
    # Malgun Gothic has no U+2212; use its supported ASCII minus explicitly.
    s=str(s).replace('\u2212','-')
    size={12:14,13:14,14:16,15:16,16:18,17:18,18:20}.get(size,size)
    TEXT_AUDIT.append(dict(screen=SCREEN,text=str(s),size=size))
    bound = f' data-max-width="{width}"' if width else ''
    return f'<text x="{x}" y="{y}" fill="{color or C["ink"]}" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}"{bound}>{esc(s)}</text>'
def panel(x,y,w,h,title=None,subtitle=None,accent=None):
    a=accent or C['accent']
    PANEL_AUDIT.append(dict(screen=SCREEN,x=x,y=y,w=w,h=h))
    p=f'<g class="panel-fill">{rect(x,y,w,h,C["bg"],10)}</g>'
    border=rect(x+.5,y+.5,w-1,h-1,'none',10,extra='stroke="'+C['edge']+'" stroke-opacity=".65"')
    p+=f'<g class="panel-border">{border}{line(x+20,y+1,x+56,y+1,a,2)}</g>'
    if title:
        p+=text(title,x+20,y+33,17,a,700,width=w-40)
    if subtitle:
        p+=text(subtitle,x+20,y+57,13,C['muted'],width=w-40)
    return p
def key(label,x,y,w=None):
    w=w or max(28,sum(14 if ord(c)>127 else 9 for c in label)+16)
    return rect(x,y,w,25,C['panel'],4,extra=f'stroke="{C["edge"]}"')+text(label,x+w/2,y+17,12,C['ink'],700,'middle',w-6)
def hint(label,desc,x,y):
    w=max(28,sum(14 if ord(c)>127 else 9 for c in label)+16)
    return rect(x-4,y-3,w+len(desc)*13+22,31,C['bg'],4)+key(label,x,y,w)+text(desc,x+w+9,y+18,13,C['muted'])
def badge(label,x,y,color=None,w=None):
    color=color or C['accent']; w=w or len(label)*14+26
    return rect(x,y,w,28,C['panel'],4)+text(label,x+12,y+19,13,color,700,width=w-20)
def bar(x,y,w,ratio,color=None,h=6):
    return rect(x,y,w,h,'#354b54',h/2)+rect(x,y,max(0,min(1,ratio))*w,h,color or C['accent'],h/2)

def hud_text(s,x,y,size=20,color=None,weight=700,anchor='start',width=None):
    return '<g class="hud-text" paint-order="stroke fill" stroke="#02080d" stroke-width="3" stroke-linejoin="round">'+text(s,x,y,size,color,weight,anchor,width)+'</g>'

def hud_icon(name,x,y,size=24,color=None,filled=False):
    # A black silhouette beneath each tintable icon preserves its edge on bright scenery.
    b=f'<g class="hud-symbol" transform="translate({x} {y}) scale({size/24})" fill="none" stroke-linecap="round" stroke-linejoin="round">'
    fill=(color or C['ink']) if filled else 'none'
    b+=f'<g stroke="#02080d" stroke-width="5.5">{ICONS[name]}</g><g class="ui-icon" stroke="{color or C["ink"]}" stroke-width="2.1" fill="{fill}">{ICONS[name]}</g></g>'
    return b

def hud_bar(x,y,w,ratio,color=None,h=10):
    return rect(x-2,y-2,w+4,h+4,'#02080d',3)+bar(x,y,w,ratio,color,h)
def row(label,x,y,w=400,state='default',trailing='',ico=None):
    colors=dict(default=C['ink'],hover=C['accent'],pressed=C['ink'],selected=C['accent'],disabled=C['disabled'],warning=C['amber'])
    col=colors[state]
    b=rect(x,y,w,46,'#203c46' if state in ('selected','hover','pressed') else C['panel'],5)
    if state=='selected': b+=rect(x,y,3,46,C['accent'])
    b+=icon('lock' if state=='disabled' else ico or 'chevron',x+12,y+12,21,col)
    b+=text(label,x+44,y+29,15,col,700 if state=='selected' else 400,width=w-80-(len(trailing)*14 if trailing else 0))
    if trailing: b+=text(trailing,x+w-14,y+29,13,col,400,'end')
    return b
def heading(kicker,title,desc=None):
    b=text(kicker,72,97,13,C['accent'],700)+text(title,72,143,32,C['ink'],700)
    if desc: b+=text(desc,72,174,15,C['muted'],width=1120)
    return b

def backdrop(kind='city'):
    # Deliberately schematic review backgrounds, never represented as live game captures.
    if kind=='mine':
        b=rect(0,0,1280,800,'#080e14')
        for i in range(7):
            x=160+i*133; y=190+(i%3)*58
            b+=f'<path d="m{x} {y} 116-55 87 72-114 58z" fill="#202d32" stroke="#334149"/>'
        b+=f'<path d="M210 650 440 520 610 610 880 460 1050 550" fill="none" stroke="#30464c" stroke-width="76"/>'
        b+=f'<path d="M210 650 440 520 610 610 880 460 1050 550" fill="none" stroke="#42716d" stroke-width="2"/>'
    else:
        b=rect(0,0,1280,800,'#83958e' if kind=='bright' else '#334a50')
        b+=f'<path d="M-90 650 1360-50M-20 280 1000 880" stroke="#5d7172" stroke-width="90"/>'
        b+=f'<path d="M-90 650 1360-50M-20 280 1000 880" stroke="#9aaba0" stroke-width="2" stroke-dasharray="12 20"/>'
        for x,y,w,h in [(300,190,170,120),(720,120,210,130),(850,440,170,160),(230,410,150,90),(560,520,120,130),(1080,240,140,130)]:
            b+=f'<path d="M{x} {y}l{w} {-w*.48}v{h}l{-w} {w*.48}z" fill="#556a6b"/>'
            b+=f'<path d="M{x} {y}l{w} {-w*.48} 65 34 {-w} {w*.48}z" fill="#a5b2a9"/>'
            b+=f'<path d="M{x+w} {y-w*.48}l65 34v{h}l-65-34z" fill="#41565c"/>'
            b+=line(x+16,y+28,x+w-16,y-w*.48+28,C['accent'],2)
    b+=f'<ellipse cx="640" cy="435" rx="17" ry="8" fill="#061218" opacity=".5"/>'
    b+=icon('player',628,404,24,C['ink'])
    return '<g class="world-background">'+b+'</g>'

def vitals(hp=86,ammo=8,reloading=False,show_ammo=True):
    col=C['danger'] if hp<=25 else C['accent']
    b='<g data-hud="health">'+hud_icon('health',32,736,24,col)
    b+=hud_text(hp,66,735,28,col)+hud_text('/ 100',118,734,16,C['ink'],width=110)
    b+=hud_bar(66,748,220,hp/100,col,12)
    if hp<=25: b+=hud_text('위험 · G 회복',32,690,16,C['danger'],width=242)
    b+='</g>'
    if not show_ammo: return b
    b+='<g data-hud="ammo">'+hud_icon('pistol',1040,723,28)
    b+=hud_text(f'{ammo:02d}',1182,746,38,C['amber'] if ammo==0 else C['ink'],anchor='end')
    b+=hud_text('/ 12',1248,745,22,C['ink'],anchor='end')
    b+=hud_icon('infinite',1064,754,18)
    for i in range(12):
        b+=rect(1090+i*13,757,9,10,C['ink'] if i<ammo else '#1a303d',2,extra='stroke="#02080d" stroke-width="2"')
    if reloading:
        b+=hud_text('재장전',1248,705,16,C['amber'],anchor='end')+hud_bar(1090,757,152,.56,C['amber'],10)
    elif ammo==0: b+=hud_text('R 재장전',1248,705,16,C['amber'],anchor='end')
    return b+'</g>'

def objective(chapter=False,kind='city'):
    subtitle='1장 / 빛나는 개척지' if chapter else ('광산 / 반복 구조' if kind=='mine' else '프롤로그 / 침묵하는 관문')
    title='세 현장을 조사하고 리아에게 보고' if chapter else ('광산의 구조 신호를 조사' if kind=='mine' else '헤일로 수신망에서 리아와 대화')
    b='<g data-hud="objective">'+hud_icon('objective',32,38,23,C['accent'])
    b+=hud_text(title,68,58,18,width=395)
    b+=hud_text('조사 1 / 3 · 보고 대기' if chapter else ('F 스캔 · 근사 신호 조사' if kind=='mine' else '가까이 다가가 E 대화'),68,85,14,C['muted'],400,width=395)
    if chapter:
        b+=hud_text('운송대 철수 중',68,112,14,C['muted'],400,width=395)
    return b+'</g>'

def minimap(kind='city'):
    b='<g data-hud="minimap">'
    b+=f'<defs><clipPath id="miniclip"><circle cx="1144" cy="139" r="82"/></clipPath></defs>'
    b+='<g clip-path="url(#miniclip)"><g class="panel-fill">'+rect(1062,57,164,164,'#182e37')+'</g>'
    if kind=='mine':
        b+=f'<path d="M1080 185 1134 155 1160 170 1210 130" stroke="#658888" stroke-width="24" fill="none"/>'
        b+=text('미탐험',1155,98,12,C['disabled'])
    else:
        b+=f'<path d="M1030 190 1248 80M1070 45 1210 236" stroke="#617d83" stroke-width="15" fill="none"/>'
        for x,y in ((1090,110),(1166,143),(1120,180)):
            b+=f'<path d="m{x} {y} 18-9 19 10-18 9z" fill="#97aaa4"/>'
    b+='</g>'+f'<circle cx="1144" cy="139" r="83" fill="none" stroke="{C["edge"]}"/>'
    b+=icon('player',1133,128,22,C['ink'])
    if kind!='mine': b+=icon('destination',1188,94,20,C['amber'])
    b+=hud_text('N ↗',1217,52,12,C['accent'],700,'end')
    b+=hud_text('광산 · 탐험 구간' if kind=='mine' else '헤일로',1144,246,14,anchor='middle',width=206)
    b+=hud_text('M 지도',1144,272,14,C['muted'],400,'middle',206)
    return b+'</g>'

def prompt(action='대화',target='리아',ico='dialogue'):
    b=f'<g class="panel-fill">{rect(450,514,380,48,C["bg"],5)}</g>'
    return b+icon(ico,464,526,23)+text(f'{target} · {action}',500,545,16,weight=700,width=273)+key('E',788,526)

def reticle(state='ready',spread=15):
    color=C['amber'] if state!='ready' else C['ink']; x,y=640,380
    b=f'<g class="ui-icon" fill="none" stroke="{color}" stroke-width="2.5" filter="url(#hud-shadow)">'
    if state=='menu':
        b+='<path d="M638 366v25l6-6 5 9 4-2-5-9h9z"/>'
    else:
        for dx,dy in ((-1,0),(1,0),(0,-1),(0,1)):
            b+=f'<path d="M{x+dx*spread} {y+dy*spread}l{dx*7} {dy*7}"/>'
        b+=f'<circle cx="640" cy="380" r="2" fill="{color}"/>'
        if state!='ready': b+='<path d="m632 372 16 16M648 372l-16 16"/>'
    return b+'</g>'

def common(kind='city',chapter=False,hp=86,ammo=8,reloading=False):
    return backdrop(kind)+objective(chapter,kind)+minimap(kind)+vitals(hp,ammo,reloading)

def wanted_hud(stars=3,heat=.64):
    b='<g data-hud="wanted">'
    for i in range(5): b+=hud_icon('wanted',558+i*35,35,25,C['amber'] if i<stars else C['muted'],filled=i<stars)
    b+=hud_bar(558,73,165,heat,C['amber'],6)
    return b+hud_text(f'수배 {stars} · 추격 중',640,106,14,C['amber'],anchor='middle',width=290)+'</g>'

def scanner_hud():
    b='<g data-hud="scanner">'+f'<circle cx="976" cy="656" r="21" fill="#07131c" stroke="#02080d" stroke-width="5"/>'
    b+=f'<circle cx="976" cy="656" r="21" fill="none" stroke="{C["blue"]}" stroke-width="4" stroke-dasharray="79 132" transform="rotate(-90 976 656)"/>'
    b+=hud_text('F',976,663,18,C['blue'],anchor='middle')
    return b+hud_text('스캔 중',1010,652,16,C['blue'],width=228)+hud_text('신호 감지 · 약 40–60 m',1010,682,14,C['ink'],width=238)+'</g>'

def compact_notice(label,ico='check',x=918,y=342,color=None):
    return hud_icon(ico,x,y-21,24,color or C['accent'])+hud_text(label,x+36,y,16,color or C['ink'],width=294)

def combat_screen():
    return common(hp=18,ammo=0,reloading=True)+reticle('blocked',28)+wanted_hud()+hud_text('보안대 접근',640,145,16,C['amber'],anchor='middle')

def bike_screen():
    b=backdrop()+objective()+minimap()+vitals(show_ammo=False)
    b+=hud_icon('bike',1038,725,28)+hud_text('238',1176,749,44,anchor='end')+hud_text('km/h',1248,748,18,anchor='end')
    b+=hud_icon('boost',1080,674,23,C['accent'])+hud_text('부스트',1115,695,16,C['accent'],width=133)
    b+=hud_text('정차 후 E 하차',1248,648,14,C['muted'],anchor='end',width=260)
    return b+hud_text('↗ 1.2 km',1248,324,18,C['amber'],anchor='end',width=240)

def mine_screen():
    b=common('mine')+reticle()+scanner_hud()
    b+=hud_icon('injured',32,130,23,C['amber'])+hud_text('리아 · 중상 / 회복 중',68,150,16,C['amber'],width=335)
    b+=hud_icon('rescue',32,168,23,C['blue'])+hud_text('구조 4 / 12 · 철수 중 1',68,188,14,C['muted'],width=335)
    b+=hud_icon('unknown_signal',1204,365,28,C['blue'])+hud_text('△ 구조 신호',1248,424,14,C['blue'],anchor='end',width=240)
    return b

def chapter_screen():
    b=common(chapter=True)+prompt('교환','운송대 보급','supply')
    b+=hud_icon('rescue',32,143,23,C['blue'])+hud_text('운송대 5 / 5 · 도착 2',68,164,16,C['blue'],width=335)
    return b+compact_notice('선택 의뢰 완료',x=1000,y=340)

def footer(s): return text(s,640,780,13,C['muted'],400,'middle',1100)
def modal(title,sub='',x=248,y=140,w=784,h=520):
    return rect(0,0,1280,800,'#031016',extra='opacity=".62"')+panel(x,y,w,h,title,sub)

def dialogue(mode='radio',choices=True):
    kind='mine' if mode=='radio' else 'city'
    b=backdrop(kind)
    if mode=='radio': b+=minimap(kind)+vitals()
    b+=panel(208,390,864,310,'수신 기록 / 안전한 곳에서 계속 읽을 수 있습니다.' if mode=='radio' else '현장 대화 / 진행 일시정지',accent=C['blue'])
    b+=icon('radio' if mode=='radio' else 'dialogue',232,440,24,C['blue'])
    b+=text('헤일로 외곽 수신망 담당자 · 리아',270,462,18,C['ink'],700,width=575)
    b+=badge('무전 · 전투 계속' if mode=='radio' else '현장 대화',865,436,C['blue'],183)
    b+=text('관문 N-0의 신호와 외곽 설비 N-0/17의 기록은 서로 달라.',232,502,18,width=816)
    b+=text('같은 장소로 보고 조사하면 안 돼. 먼저 발전기의 기록을 확인하자.',232,534,18,width=816)
    b+=text('확인하지 못한 내용은 조사 기록에 추정으로 남겨 둘게.',232,566,18,width=816)
    if choices:
        b+=row('기록을 먼저 확인한다',232,582,816,'selected',trailing='1')
        b+=text('2  선택 불가 · 안전한 곳에서 선택',250,654,15,C['disabled'],width=540)
    b+=text('04 / 04' if choices else '02 / 04',232,682,13,C['muted'])
    if choices: b+=hint('1–2','선택',706,662)+hint('E / Enter','확정',849,662)
    else: b+=hint('E / Enter','다음 페이지',820,662)
    return b

def map_screen(kind='city'):
    b=backdrop(kind)+modal('지역 약도','광산 / 탐험한 구간만 표시' if kind=='mine' else '헤일로 주변 / 북쪽은 오른쪽 위',56,40,1168,720)
    b+=rect(80,126,818,548,'#142a31',5)
    if kind=='mine':
        b+=f'<path d="M160 570 300 500 420 558 570 480 690 540 820 475" fill="none" stroke="#607e81" stroke-width="48"/>'
        b+=text('미탐험 영역',478,280,20,C['disabled'],400,'middle')
        b+=text('스캔은 지형을 공개하지 않습니다.',478,311,14,C['muted'],400,'middle')
    else:
        b+=f'<path d="M110 594 860 217M160 180 860 550M350 650 740 150" stroke="#56757d" stroke-width="24" fill="none"/>'
        for i in range(7):
            x=160+i*85; y=240+(i%3)*92
            b+=f'<path d="m{x} {y} 40-20 40 20-40 20z" fill="#8b9f9d"/>'
        b+=icon('home',272,286,28)+text('집',309,310,14)
        b+=icon('medical',638,406,26)+text('의료소',674,430,14)
    b+=text('N ↗',850,163,19,C['accent'],700,'end')
    b+=icon('player',476,456,30,C['ink'])
    if kind=='mine':
        b+=icon('destination',680,510,30,C['amber'])+text('목적지',718,535,14,C['amber'])
    else:
        b+=icon('destination',735,249,32,C['amber'])+text('목적지',782,275,14,C['amber'])
    b+=text('범례',930,160,18,C['ink'],700)
    for i,(ic,label,col) in enumerate([('player','현재 위치',C['ink']),('destination','지정 목적지',C['amber']),('objective','현재 목표',C['accent']),('medical','의료소',C['accent']),('unknown_signal','근사 구조 신호',C['blue'])]):
        b+=icon(ic,932,187+i*47,24,col)+text(label,970,207+i*47,14,width=220)
    b+=text('경로 자동 안내 없음',930,486,14,C['muted'],width=260)
    b+=text('현재 진행 대상만 표시',930,514,14,C['muted'],width=260)
    b+=hint('좌클릭','목적지 설정',82,705)+hint('우클릭','목적지 해제',344,705)+hint('M / Esc','닫기',993,705)
    return b

SCENES={}
def scene(name,label,ids,body):
    global SCREEN
    SCREEN=name
    TEXT_AUDIT[:]=[entry for entry in TEXT_AUDIT if entry['screen']!=name]
    PANEL_AUDIT[:]=[entry for entry in PANEL_AUDIT if entry['screen']!=name]
    # body callable ensures all validation entries are attributed correctly.
    content=body()
    style='<defs><filter id="hud-shadow" x="-100%" y="-100%" width="300%" height="300%"><feDropShadow dx="0" dy="1" stdDeviation="1" flood-color="#000" flood-opacity="1"/></filter></defs><style>text{font-family:"Malgun Gothic",sans-serif}text,.ui-icon{opacity:var(--content-opacity,1)}[data-hud]{opacity:var(--content-opacity,1)}[data-hud] text,[data-hud] .ui-icon{opacity:1}.panel-fill{opacity:var(--panel-opacity,.94)}.panel-border{opacity:var(--border-opacity,1)}</style>'
    SCENES[name]=dict(label=label,ids=ids,svg=svg(content,1280,800,style))

scene('01_city','밝은 도시 / 기본 HUD',['UI-01','UI-02','UI-04','UI-05','UI-07','UI-14'],lambda:common('bright')+prompt()+badge('횡단 가능 · 보행 신호',432,32,w=300)+hint('J','조사 기록',892,734))
scene('02_combat','전투 HUD / 저체력 · 재장전',['UI-01','UI-02','UI-03','UI-12','UI-15'],combat_screen)
scene('03_bike','주행 HUD / 속도 · 부스트',['UI-05','UI-13'],bike_screen)
scene('04_mine','광산 HUD / 스캔 · 구조',['UI-05','UI-10','UI-11'],mine_screen)
scene('05_dialogue','긴 한글 대화 / 선택',['UI-08','UI-15'],lambda:dialogue())
scene('06_journal','조사 기록 / 알려진 사실',['UI-09'],lambda:backdrop()+modal('조사 기록','확인한 사실과 다음 행동 / J',176,72,928,656)+row('관문 N-0의 수신 기록',200,160,270,'selected')+row('발전기 조사',200,216,270)+row('운송대 구조',200,272,270)+text('관문과 외곽 설비는 다른 대상이다.',510,182,21,weight=700,width=560)+text('확인한 사실',510,234,14,C['accent'],700)+text('N-0/17은 외곽 설비의 식별 번호다.',510,268,17,width=548)+text('현재 할 일',510,330,14,C['accent'],700)+text('리아에게 조사 결과를 보고하고 기록을 대조한다.',510,364,17,width=548)+panel(510,464,566,118,'중단한 대화가 있습니다.',accent=C['amber'])+text('안전한 곳에서 상대에게 다시 말을 걸어 이어 읽으세요.',530,540,15,C['muted'],width=526)+hint('PageUp / PageDown','페이지',200,674)+hint('J / Esc','닫기',898,674)+text('01 / 03',1056,646,13,C['muted'],anchor='end'))
scene('07_chapter','1장 HUD / 조사 · 철수',['UI-04','UI-07','UI-11','UI-15'],chapter_screen)
scene('08_medical','의료 이송 / 실제 손실',['UI-16'],lambda:backdrop()+modal('의료 이송 완료','헤일로 의료소 / 현재 적용 결과',312,130,656,540)+icon('medical',608,218,64)+text('안전한 곳으로 복귀했습니다.',640,328,26,weight=700,anchor='middle',width=600)+text('레벨 −1     크레딧 −120     부품 −2',640,388,21,C['amber'],anchor='middle',width=600)+text('조사 기록과 확인한 사실은 유지됩니다.',640,438,17,anchor='middle',width=600)+text('중단한 대화는 안전한 곳에서 다시 말을 걸어 재개하세요.',640,476,15,C['muted'],anchor='middle',width=600)+row('계속하기',404,550,472,'selected',trailing='Enter'))
scene('09_pause','일시정지 / 지역별 복귀',['UI-17'],lambda:backdrop()+modal('일시정지','1장 / 빛나는 개척지',416,112,448,576)+row('계속하기',440,220,400,'selected',trailing='Esc')+row('헤일로로 귀환',440,278,400,trailing='R')+row('조작 도움말',440,336,400,ico='keyboard')+row('종료',440,394,400,'warning',ico='exit')+text('현재 지역에서는 R로 헤일로에 귀환합니다.',440,488,14,C['muted'],width=400)+text('세션의 진행은 영구 저장되지 않습니다.',440,518,14,C['amber'],width=400)+text('광산에서는 R의 기능이 광산 재시작으로 바뀝니다.',440,568,13,C['muted'],width=400))
scene('10_city_map','전체 지도 / 도시',['UI-06'],lambda:map_screen())
scene('11_mine_map','전체 지도 / 광산 비공개 영역',['UI-06'],lambda:map_screen('mine'))
scene('12_chapter_map','전체 지도 / 1장 현장',['UI-06'],lambda:map_screen()+badge('1장 현장 · 현재 조사 대상',126,172,C['accent'],362)+icon('power',420,328,30)+text('발전기',458,350,15)+icon('transport',628,525,30)+text('운송대',668,549,15)+icon('investigation_site',760,377,30)+text('폐설비',800,400,15))
scene('13_history','대사 이력 / 전달 방식',['UI-09'],lambda:backdrop('mine')+modal('대사 이력','읽은 내용만 표시 / H',224,80,832,640)+badge('무전',252,171,C['blue'],80)+text('리아',348,192,17,weight=700)+text('수신 기록은 확인했어. 다음은 발전기야.',252,241,18,width=776)+line(252,270,1028,270)+badge('단말',252,298,C['accent'],80)+text('발전기 제어 단말',348,319,17,weight=700)+text('보조 전력 복구를 확인했습니다.',252,368,18,width=776)+line(252,397,1028,397)+badge('현장 대화',252,425,C['muted'],116)+text('리아',388,446,17,weight=700)+text('아직은 결론을 내리지 말자. 기록을 더 대조해야 해.',252,495,18,width=776)+hint('PageUp / PageDown','페이지',252,660)+hint('H / Esc','닫기',872,660)+text('02 / 05',1028,620,13,C['muted'],anchor='end'))
scene('14_empty','조사 기록 / 내용 없음',['UI-09'],lambda:backdrop()+modal('조사 기록','J / 아직 확인한 기록이 없습니다.',224,120,832,560)+icon('empty',608,278,64,C['muted'])+text('기록된 내용이 없습니다.',640,400,26,weight=700,anchor='middle')+text('대화하거나 현장을 조사하면 확인한 사실이 여기에 남습니다.',640,447,17,C['muted'],anchor='middle',width=760)+hint('J / Esc','닫기',850,612))
scene('15_title','타이틀 / 후속 연결용',['UI-18'],lambda:backdrop('mine')+rect(0,0,1280,800,'#07151c',extra='opacity=".58"')+text('에르마 · 헤일로',96,160,16,C['accent'],700)+text('침묵하는 관문',90,238,58,weight=700,width=1000)+text('신호가 닿은 곳에서, 이야기가 시작된다.',96,292,18,C['muted'],width=900)+row('새 게임',96,407,356,'selected')+row('옵션',96,465,356,ico='settings')+row('조작 안내',96,523,356,ico='keyboard')+row('종료',96,581,356,ico='exit')+text('이어하기 / 불러오기는 제공하지 않습니다.',96,708,14,C['muted']))
scene('16_options','옵션 / 투명도 / 후속 연결용',['UI-19'],lambda:backdrop()+modal('옵션','UI 투명도는 필수 연결 항목입니다.',224,72,832,656)+text('패널 불투명도',260,190,18,weight=700)+text('94%',1004,190,21,C['accent'],700,'end')+bar(260,218,744,.94)+text('지원 제안 80–100% · 기본 94% / 글자·아이콘 별도',260,257,14,C['muted'],width=744)+text('테두리 불투명도',260,315,18,weight=700)+text('100%',1004,315,21,C['accent'],700,'end')+bar(260,343,744,1)+text('콘텐츠 불투명도',260,400,18,weight=700)+text('100%',1004,400,21,C['accent'],700,'end')+text('지원 제안 85–100% · 본문 가독성 유지',260,433,14,C['muted'],width=744)+row('카툰 명암',260,472,744,trailing='켜짐')+row('렌더 해상도',260,528,744,trailing='기본 / ½ / ¼')+row('안티앨리어싱',260,584,744,trailing='켜짐')+text('글자 크기와 UI 배율은 별도 구현 검토 항목입니다.',260,680,14,C['muted'],width=744))
scene('17_summary','보유 / 성장 요약 / 후속 연결용',['UI-20'],lambda:backdrop()+modal('보유 · 성장 정보','현재 데이터 요약',224,96,832,608)+icon('level',264,196,34)+text('레벨 7',312,224,27,weight=700)+text('경험치 420 / 800',264,270,16,C['muted'])+bar(264,290,752,.525)+row('공격 / 방어',264,338,752,trailing='24 / 12',ico='attack')+row('크레딧',264,394,752,trailing='12,480',ico='credit')+row('거래 부품',264,450,752,trailing='18',ico='parts')+row('회복 키트',264,506,752,trailing='3',ico='heal')+text('장비 슬롯과 아이템 격자는 현재 기능에 포함되지 않습니다.',264,628,14,C['muted'],width=752))
scene('18_supply','보급 교환 / 후속 연결용',['UI-21'],lambda:backdrop()+modal('운송대 보급 교환','현재 게임은 E로 즉시 교환 / 별도 창 연결 필요',280,112,720,576)+icon('parts',344,224,54)+icon('arrow',593,236,32)+icon('heal',814,224,54)+text('부품 3개',372,322,23,weight=700,anchor='middle')+text('회복 키트 1개',838,322,23,weight=700,anchor='middle')+text('기본 2개 → 평판 할증 적용 3개',640,384,17,C['amber'],anchor='middle',width=640)+text('보유 부품 18개 / 회복 키트 3개',640,428,17,anchor='middle',width=640)+row('교환 가능',320,486,640,'selected',trailing='E',ico='check')+text('부족 / 보유 상한 / 완료 상태는 부품 갤러리에서 확인',640,611,14,C['muted'],anchor='middle',width=640))
scene('19_confirm','중요 행동 확인 / 후속 연결용',['UI-22'],lambda:backdrop()+modal('현재 세션을 종료할까요?','진행은 영구 저장되지 않습니다.',360,224,560,352)+text('종료하면 이번 세션의 진행을 잃습니다.',392,348,18,width=496)+text('취소하면 현재 화면으로 돌아갑니다.',392,384,16,C['muted'],width=496)+row('취소 · 계속 플레이',392,440,236,'selected')+row('종료',644,440,244,'warning',ico='exit'))
scene('20_help','맥락별 조작 / 후속 연결용',['UI-23'],lambda:backdrop()+modal('조작 도움말','현재 가능한 행동만 표시합니다.',208,64,864,672)+row('도보 · 전투',240,149,800,'selected',ico='keyboard')+hint('WASD','이동',248,226)+hint('좌클릭','사격',638,226)+hint('V','근접 공격',248,280)+hint('R','재장전',638,280)+hint('Q','회복',248,334)+hint('E','조사 · 대화 · 회수',638,334)+hint('F','광산 스캔',248,388)+hint('M','전체 지도',638,388)+hint('J','조사 기록',248,442)+hint('H','대사 이력',638,442)+text('바이크: W 가속 · S 제동/후진 · A/D 조향 · Space 부스트',248,538,16,width=784)+text('대화: E/Enter 다음 · 숫자로 선택 후 Enter 확정',248,581,16,width=784)+text('지도: 좌클릭 목적지 설정 · 우클릭 해제 · M/Esc 닫기',248,624,16,width=784)+hint('Esc','닫기',900,677))

def state_gallery():
    b=rect(0,0,1280,800,C['bg'])+heading('COMPONENTS / 01','상태는 색과 형태로 함께 구분합니다.','동일한 패널·아이콘·키 안내를 모든 화면에 재사용합니다.')
    for i,(s,label) in enumerate([('default','기본'),('hover','포커스 / 가리킴'),('pressed','눌림'),('selected','선택'),('disabled','선택 불가'),('warning','주의 / 위험')]):
        y=224+i*72
        b+=text(label,72,y+30,15,C['muted'])+row('발전기 기록을 확인한다',272,y,494,s)
    for i,(label,val,col) in enumerate([('체력 · 회복',.86,C['accent']),('위험한 저체력',.18,C['danger']),('재장전 진행',.56,C['amber']),('수배 열도',.72,C['amber']),('스캐너 충전',.36,C['blue']),('경험치',.54,C['accent'])]):
        y=242+i*72
        b+=text(label,830,y,15,col)+bar(830,y+19,350,val,col)
    return b
scene('21_components','공통 부품 / 버튼 / 게이지',['COMMON'],state_gallery)

def state_sheet():
    b=rect(0,0,1280,800,C['bg'])+heading('STATES / 02','상황별 상태와 입력 안내','샘플 수치는 상태 검토용이며 게임에서 받은 값으로 교체합니다.')
    data=[('스캐너','F 사용 가능','스캔 진행','충전 대기 4.2초','신호 발견','감지된 신호 없음'),
          ('권총','8 / 12 · 사격 가능','0 / 12 · 탄창 소진','재장전 중','재장전 완료','예비 탄창 무제한'),
          ('동료 · 구조','리아 · 동행','중상 · 회복 중','구조 확인','철수 중','안전 지점 도착'),
          ('보급 교환','교환 가능','부품 부족','보유 상한','교환 완료','평판 할증 적용'),
          ('주행 · 교통','정차 · E 하차','부스트 중','후진 1 km/h','보행 대기','차량 충돌 · 체력 −8'),
          ('대화 전달','현장 대화','무전 · 전투 계속','단말 메시지','수신 기록','이송 중 입력 불가')]
    for i,items in enumerate(data):
        y=226+i*83
        b+=text(items[0],72,y+24,15,C['accent'],700,width=140)
        for j,label in enumerate(items[1:]):
            x=220+j*197
            b+=rect(x,y,185,55,C['panel'],5)+text(label,x+12,y+32,12,C['ink'],width=162)
    b+=footer('수배 0–5단계 · 현재 추격은 열도 / 도시 간 남는 기록은 악명으로 구분')
    return b
scene('22_states','상태 목록 / 기능 맥락',['COMMON'],state_sheet)

def icons_sheet():
    b=rect(0,0,1280,800,C['bg'])+heading('SYMBOL LIBRARY / 03','작은 크기에서도 구분되는 기호','24px 기준 · 단일 색상 · 개별 SVG / 투명 PNG / 아틀라스 제공')
    for i,name in enumerate(ICONS):
        x=66+(i%10)*120; y=218+(i//10)*87
        b+=rect(x,y,108,74,C['panel'],6)+icon(name,x+40,y+10,28)
        b+=text(name,x+54,y+60,9,C['muted'],anchor='middle',width=102)
    return b
scene('23_icons','아이콘 라이브러리',['COMMON'],icons_sheet)
scene('24_local_dialogue','현장 대화 / 진행 일시정지',['UI-08'],lambda:dialogue('local'))

def interactions():
    b=rect(0,0,1280,800,C['bg'])+heading('WORLD INTERACTION / 04','그때 가능한 행동을 표시합니다.','대상의 실제 상태를 받아 한 가지 우선 행동만 제시합니다. 중상 상태에서는 정상 대화를 표시하지 않습니다.')
    actions=[('dialogue','리아','대화'),('investigate','발전기 기록','조사'),('power','보조 전력','복구'),
             ('parts','거래 부품','회수'),('rescue','광산 구조 대상','구조'),('bike','개인 바이크','탑승'),
             ('exit','광산 출구','밖으로 나가기'),('supply','운송대 보급','보급 교환'),('injured','중상 주민','의료 회복 중 · 대화 불가')]
    for i,(ic,target,action) in enumerate(actions):
        x=64+(i%3)*390;y=228+(i//3)*154
        b+=panel(x,y,372,118)+icon(ic,x+20,y+24,28,C['amber'] if ic=='injured' else C['accent'])
        b+=text(target,x+64,y+43,14,C['muted'],width=284)+text(action,x+20,y+87,17,weight=700,width=322)
        if ic!='injured': b+=key('E',x+322,y+20)
    return b
scene('25_interactions','상호작용 / 월드 표식',['UI-07'],interactions)

def notification_sheet():
    b=rect(0,0,1280,800,C['bg'])+heading('NOTIFICATIONS / 05','알림은 대화와 목표의 공간을 침범하지 않습니다.','전투 반복 알림은 하나로 합치고, 중요한 대사는 사용자 입력까지 유지합니다.')
    items=[('parts','획득','거래 부품 +2'),('heal','회복','체력 +25 · 86 / 100'),('credit','보상','크레딧 +120'),
           ('supply','거래 완료','부품 −3 · 회복 키트 +1'),('warning','물자 부족','부품이 2개 부족합니다.'),('objective','목표 갱신','헤일로에 조사 결과 보고'),
           ('gate','지역 진입','광산 외곽'),('home','귀환','헤일로에 도착했습니다.'),('warning','피격 결과','차량 충돌 · 체력 −8')]
    for i,(ic,label,desc) in enumerate(items):
        x=64+(i%3)*390;y=228+(i//3)*154
        b+=panel(x,y,372,118)+icon(ic,x+20,y+25,25,C['amber'] if ic=='warning' else C['accent'])
        b+=text(label,x+61,y+44,17,weight=700,width=290)+text(desc,x+20,y+89,16,C['muted'],width=332)
    return b
scene('26_notifications','획득 / 회복 / 거래 / 피격 알림',['UI-01','UI-14','UI-15'],notification_sheet)
scene('27_transfer','의료 이송 중 / 입력 잠금',['UI-16'],lambda:rect(0,0,1280,800,'#050e14')+icon('medical',608,244,64)+text('의료 지원팀이 이송하고 있습니다.',640,402,28,weight=700,anchor='middle',width=1120)+text('헤일로 의료소로 이동 중',640,453,18,C['muted'],anchor='middle')+text('이송 중에는 조작할 수 없습니다.',640,513,15,C['amber'],anchor='middle'))
scene('28_pause_mine','광산 일시정지 / 재시작',['UI-17'],lambda:backdrop('mine')+modal('일시정지','광산 / 반복 구조',416,144,448,512)+row('계속하기',440,248,400,'selected',trailing='Esc')+row('광산 재시작',440,306,400,'warning',trailing='R')+row('조작 도움말',440,364,400,ico='keyboard')+row('종료',440,422,400,'warning',ico='exit')+text('R: 현재 광산을 새로 시작합니다.',440,534,15,C['amber'],width=400)+text('탐험 기록과 반복 구조 대상이 초기화됩니다.',440,566,14,C['muted'],width=400))
scene('29_pause_city','도시 일시정지 / 다시 시작',['UI-17'],lambda:backdrop()+modal('일시정지','도시 / 프롤로그',416,144,448,512)+row('계속하기',440,248,400,'selected',trailing='Esc')+row('다시 시작',440,306,400,'warning',trailing='R')+row('조작 도움말',440,364,400,ico='keyboard')+row('종료',440,422,400,'warning',ico='exit')+text('R: 현재 세션을 처음부터 다시 시작합니다.',440,534,15,C['amber'],width=400)+text('실행 전에 현재 진행을 잃는다는 확인이 필요합니다.',440,566,14,C['muted'],width=400))

def text_stress():
    b=rect(0,0,1280,800,C['bg'])+heading('TYPE VALIDATION / 06','긴 이름 · 긴 문장 · 큰 수치 검토','실제 맑은 고딕 글리프 폭으로 검사하고, 길이가 넘으면 줄바꿈·페이지 분리를 적용합니다.')
    b+=panel(64,214,1152,336,'헤일로 외곽 수신망 통신·보안·의료 지원 담당자 / 긴 화자명 예시')
    for i,s in enumerate(['N-0 관문과 N-0/17 외곽 설비는 서로 다른 대상입니다. 확인한 내용만 조사 기록에 남깁니다.',
                           '확인하지 못한 내용은 추정으로 남기고, 안전한 곳에서 중단된 대화를 다시 읽으세요.',
                           '가까이 다가가 기록을 대조한 다음, 헤일로 수신망에서 조사 결과를 보고한다.']):
        b+=text(s,88,308+i*34,18,width=1104)
    b+=row('1  기록을 끝까지 읽고, 세 현장의 조사 결과를 확인한 뒤 헤일로 수신망에서 리아와 대조한다.',88,436,1104,'selected')
    b+=text('가나다라마바사아자차카타파하 ABC xyz 0123456789 / · − → ↗ ★ ☆ △ ½ ¼',88,524,17,C['muted'],width=1104)
    b+=panel(64,582,1152,148)
    b+=text('체력 9,999 / 9,999',88,632,24,weight=700,width=410)
    b+=text('크레딧 999,999,999',724,632,24,weight=700,width=468)
    b+=text('경험치 999,999 / 999,999',88,686,22,width=550)
    b+=text('부품 999,999 · 회복 키트 999',724,686,22,width=468)
    return b
scene('30_text_stress','긴 한글 / 큰 수치 검토',['COMMON'],text_stress)
scene('31_dialogue_page','대화 / 다음 페이지',['UI-08'],lambda:dialogue('local',False))


def hud_parts():
    b=rect(0,0,1280,800,C['bg'])+heading('HUD / 01','게이지 · 아이콘 · 필요한 수치만 표시합니다.','체력·탄약·수배·스캐너·속도에 독립 패널을 사용하지 않습니다.')
    b+=hud_icon('health',88,252,32,C['accent'])+hud_text('86 / 100',142,280,28,C['accent'])+hud_bar(142,300,220,.86,C['accent'],12)
    b+=text('체력 · 현재/최대 + 가로 게이지',88,359,18,width=480)
    b+=hud_icon('pistol',710,252,32)+hud_text('08',825,288,38)+hud_text('/ 12',905,287,22)
    for i in range(12): b+=rect(768+i*13,306,9,10,C['ink'] if i<8 else '#253d49',2)
    b+=text('탄약 · 잔탄 강조 + 탄창 12칸',710,359,18,width=480)
    b+=hud_icon('wanted',88,434,32,C['amber'])+hud_bar(142,448,220,.64,C['amber'],8)
    b+=text('수배 · 단계 기호 + 열도',88,500,18,width=480)
    b+=hud_icon('scan',710,434,32,C['blue'])+hud_text('F',784,460,26,C['blue'])
    b+=text('스캐너 · F 진행 링 / 충전 링',710,500,18,width=480)
    b+=hud_icon('speed',88,577,32)+hud_text('238',142,612,38)+hud_text('km/h',244,612,18)
    b+=text('주행 · 속도 + 부스트/후진 기호',88,662,18,width=480)
    b+=hud_icon('injured',710,577,32,C['amber'])+hud_text('리아 · 회복 중',766,612,20,C['amber'])
    b+=text('동료 · 이름/상태 한 줄 / 변화 시 표시',710,662,18,width=480)
    return b
scene('32_hud_parts','HUD 부품 / 게이지·아이콘 구성',['COMMON'],hud_parts)
scene('33_exploration','탐험 HUD / 불필요한 전투 정보 숨김',['UI-01','UI-04','UI-05'],lambda:backdrop('bright')+objective()+minimap()+vitals(show_ammo=False))
scene('34_low_health','저체력 HUD / 위험 기호와 회복 키',['UI-01','UI-02','UI-03'],lambda:common('bright',hp=18,ammo=1)+reticle('ready',20))

from design_20261028 import apply_design
apply_design(globals())

# Text-free independent assets. All state colors can be supplied as draw-time tint.
COMPONENTS={}
def component(name,body,w,h,slice_=None,role='component'):
    content=svg(body,w,h)
    write(f'Source/Components/{name}.svg',content)
    COMPONENTS[name]=dict(svg=f'Source/Components/{name}.svg',png=f'Export/Components/{name}.png',width=w,height=h,role=role)
    if slice_: COMPONENTS[name]['nine_slice_ltrb']=slice_

component('panel_fill',rect(0,0,96,96,C['bg'],8),96,96,[12,12,12,12],'background')
component('panel_border',rect(.75,.75,94.5,94.5,'none',8,extra=f'stroke="{C["edge"]}" stroke-width="1.5"'),96,96,[12,12,12,12],'border')
component('panel_accent',line(0,2,30,2,C['accent'],3),32,4,role='decoration')
for state,col in [('default',C['edge']),('hover',C['accent']),('pressed',C['ink']),('selected',C['accent']),('disabled',C['disabled']),('warning',C['amber'])]:
    fill='#203c46' if state in ('hover','selected') else C['panel']
    if state=='pressed': fill='#345660'
    b=rect(.75,.75,158.5,46.5,fill,5,extra=f'stroke="{col}" stroke-width="1.5"')
    if state=='selected': b+=rect(1,8,3,32,col,1)
    if state=='disabled': b+=icon('lock',12,12,24,col)
    if state=='warning': b+=icon('warning',12,12,24,col)
    component('button_'+state,b,160,48,[12,12,12,12] if state not in ('disabled','warning') else None,'state')
component('gauge_track',rect(0,0,128,8,'#354b54',4),128,8,[4,4,4,4])
for name,col in [('health',C['accent']),('critical',C['danger']),('reload',C['amber']),('heat',C['amber']),('scan',C['blue']),('experience',C['accent'])]:
    component('gauge_'+name,rect(0,0,128,8,col,4),128,8,[4,4,4,4],'fill_crop_horizontal')
component('keycap',rect(1,1,46,30,C['panel'],4,extra=f'stroke="{C["edge"]}"'),48,32,[6,6,6,6])
component('divider',line(0,.5,128,.5,C['edge']),128,1)
component('minimap_ring','<circle cx="96" cy="96" r="94.5" fill="none" stroke="'+C['edge']+'" stroke-width="2"/>',192,192,role='fixed_aspect')
component('minimap_mask','<circle cx="96" cy="96" r="94" fill="white"/>',192,192,role='alpha_mask')
for state,gap in [('ready',8),('spread',16),('recovering',12),('blocked',10)]:
    b=''
    col=C['amber'] if state=='blocked' else C['ink']
    for dx,dy in ((-1,0),(1,0),(0,-1),(0,1)):
        b+=f'<path d="M{32+dx*gap} {32+dy*gap}l{dx*7} {dy*7}" stroke="{col}" stroke-width="2"/>'
    b+=f'<circle cx="32" cy="32" r="1.5" fill="{col}"/>'
    if state=='blocked': b+='<path d="m24 24 16 16M40 24 24 40" stroke="'+col+'" stroke-width="2"/>'
    component('reticle_'+state,b,64,64,role='reticle')
component('cursor_menu','<path d="M10 4v25l6-6 5 9 4-2-5-9h9z" fill="'+C['ink']+'" stroke="'+C['bg']+'" stroke-width="2"/>',40,40)
component('signal_edge','<path d="m24 3 21 34H3z" fill="'+C['bg']+'" stroke="'+C['blue']+'" stroke-width="2"/><path d="M24 14v10M24 28v2" stroke="'+C['blue']+'" stroke-width="2"/>',48,40,role='rotate_toward_approximate_signal')
component('destination_edge','<path d="m20 3 16 31-16-9L4 34z" fill="'+C['amber']+'" stroke="'+C['bg']+'" stroke-width="2"/>',40,40,role='rotate_toward_exact_destination')
component('fade_overlay',rect(0,0,16,16,'#050e14'),16,16,role='stretch_full_canvas_animate_alpha')
component('hud_health_track',rect(0,0,224,16,'#02080d',3)+rect(2,2,220,12,'#354b54',3),224,16,role='hud_track_no_panel')
component('hud_health_fill',rect(0,0,220,12,'#ffffff',3),220,12,role='hud_crop_and_tint')
component('hud_ammo_pip',rect(1,1,9,10,'#ffffff',2,extra='stroke="#02080d" stroke-width="2"'),11,12,role='hud_ammo_one_round')
component('hud_ammo_pip_empty',rect(1,1,9,10,'#1a303d',2,extra='stroke="#02080d" stroke-width="2"'),11,12,role='hud_ammo_empty_round')
component('hud_reload_track',rect(0,0,156,14,'#02080d',3)+rect(2,2,152,10,'#354b54',3),156,14,role='hud_reload_progress')
component('hud_reload_fill',rect(0,0,152,10,'#ffffff',3),152,10,role='hud_crop_and_tint')
component('hud_heat_track',rect(0,0,169,10,'#02080d',3)+rect(2,2,165,6,'#354b54',3),169,10,role='hud_heat_progress')
component('hud_heat_fill',rect(0,0,165,6,'#ffffff',3),165,6,role='hud_crop_and_tint')
component('hud_scan_track','<circle cx="24" cy="24" r="21" fill="#07131c" stroke="#02080d" stroke-width="5"/>',48,48,role='hud_scan_radial_track')
component('hud_scan_arc','<circle cx="24" cy="24" r="21" fill="none" stroke="white" stroke-width="4" stroke-dasharray="79 132" transform="rotate(-90 24 24)"/>',48,48,role='hud_radial_arc_dynamic_mesh_preferred')
component('hud_reticle_arm','<path d="M3 3h8" stroke="#02080d" stroke-width="6"/><path d="M3 3h8" stroke="white" stroke-width="2.5"/>',14,6,role='hud_four_independently_positioned_arms')
for name,body,w,h,role in EXTRA_COMPONENTS: component(name,body,w,h,role=role)
for name in ICONS:
    write(f'Source/Icons/{name}.svg',svg(icon(name,0,0,24,'#ffffff'),24,24))
HUD_ICON_NAMES='health pistol infinite reload objective wanted scan signal unknown_signal bike boost reverse speed injured rescue check warning companion destination player unarmed slot_empty mouse_right key_release'.split()
for name in HUD_ICON_NAMES:
    write(f'Source/HUDIcons/{name}.svg',svg(hud_icon(name,4,4,24,'#ffffff'),32,32))

for name,entry in SCENES.items(): write(f'Source/Screens/{name}.svg',entry['svg'])

manifest=dict(version='1.2.0',title='ERMA / FIELD INTERFACE',date='2026-10-28',
    canvas=dict(width=1280,height=800,safe_margin=32,scale='uniform_min_viewport_fit',letterbox=True),
    font=dict(family='Malgun Gothic',windows_path='C:/Windows/Fonts/malgun.ttf',redistributed=False,
              text_in_assets=False,encoding='UTF-8',body_px=20,metadata_px=14,line_height=32),
    palette=C,
    opacity=dict(parameter='opacity_is_alpha_not_transparency',background=dict(default=.94,min=.8,max=1),
                 border=dict(default=1,min=.8,max=1),content=dict(default=1,min=.85,max=1),
                 formula='output_alpha = asset_alpha * layer_opacity',
                 note='Existing game options use these ranges. New designs must keep independent layers; review exposes stress values down to 0.3.'),
    alpha=dict(export='straight RGBA',color_space='sRGB',blend='SRC_ALPHA, ONE_MINUS_SRC_ALPHA',
               tint='white icons are tintable; preserve full alpha, convert if using a premultiplied renderer'),
    icons={n:dict(svg=f'Source/Icons/{n}.svg',png=f'Export/Icons/{n}.png',width=48,height=48,
                   logical_size=24,stroke=2.1) for n in ICONS},components=COMPONENTS,
    hud_icons={n:dict(svg=f'Source/HUDIcons/{n}.svg',png=f'Export/HUDIcons/{n}.png',width=64,height=64,
                      logical_size=32,content_box=[4,4,24,24],black_outline_baked=True) for n in HUD_ICON_NAMES},
    hud=dict(implementation='HUD_IMPLEMENTATION_ko.md',uses_full_panel=False,
             health='icon + current/max + 220x12 clipped gauge',ammo='icon + 38px current + max + 12 pips',
             runtime_primitives=['health','ammo_pips','reload','heat','scan_arc','reticle_arms','speed'],
             render_text='runtime only; black 4px outside outline; independent content opacity'),
    atlas=dict(png='Export/Atlases/icons.png',metadata='Export/Atlases/icons.json',padding=4),
    screens={n:dict(label=v['label'],requirements=v['ids'],svg=f'Source/Screens/{n}.svg',png=f'Preview/{n}_20261028'+('_r2' if n=='05_dialogue' or n.startswith('45_dialogue_page_') else '')+'.png') for n,v in SCENES.items()},
    constraints=['No automatic route polyline','Map north is fixed northeast; rotate actor only',
                 'Mine geometry/targets only in explored and eligible state','Approximate signal is not an exact map pin',
                 'Unlimited spare magazines; no spare ammunition count','No bike fuel or energy resource',
                 'No persistent save/load UI','No unrevealed facts or unjoined companions',
                 'No interaction during medical transfer','Dialogue input must be consumed before world input'])
dump('ui_manifest.json',manifest)
dump('Validation/text_inventory.json',TEXT_AUDIT)
dump('Validation/panel_inventory.json',PANEL_AUDIT)

# Browser preview is self-contained and works directly from file:// without network/fetch.
viewer='''<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>에르마 UI · 제작 미리보기</title><style>
*{box-sizing:border-box}body{margin:0;background:#071016;color:#eef5ef;font:14px "Malgun Gothic",sans-serif}header{padding:20px 28px;border-bottom:1px solid #34515c;display:flex;gap:24px;align-items:center;flex-wrap:wrap}header b{color:#86e2ce;letter-spacing:.09em}select,button{background:#142a33;color:#eef5ef;border:1px solid #54717b;border-radius:5px;padding:9px;font:inherit}label{display:flex;align-items:center;gap:9px}input{accent-color:#86e2ce;width:110px}main{max-width:1440px;margin:auto;padding:20px}#stage{aspect-ratio:1.6;width:100%;border:1px solid #34515c;background:#0b1720}#stage svg{display:block;width:100%;height:100%}.note{color:#b2c7c9;line-height:1.8;margin:12px 0}footer{color:#8d9fa5;margin-top:20px}output{width:42px;font-variant-numeric:tabular-nums}button{cursor:pointer}:focus-visible{outline:3px solid #86e2ce;outline-offset:3px}
</style><header><b>ERMA / FIELD INTERFACE</b><select id="scene" aria-label="대표 상황"></select><label>패널 <input id="opacity" type="range" min="30" max="100" value="94"><output id="alpha">94%</output></label><label>콘텐츠 <input id="content" type="range" min="85" max="100" value="100"><output id="contentAlpha">100%</output></label><button id="background">배경 표시 전환</button></header>
<main><div id="stage" aria-label="UI 디자인 미리보기"></div><div class="note" id="description"></div><div class="note">제작용 예시 화면입니다. 수치·대사는 검토용이며 실제 게임 상태와 연결되어 있지 않습니다. 배경은 밝기·복잡도 비교용 약식 도형입니다.<br>기준 1280×800 / 한글 맑은 고딕 / 패널·테두리·내용 분리. 80% 미만 패널 값은 극단 조건 확인용입니다.</div><footer>SVG 원본 · 투명 PNG · 기호 아틀라스 · 연결 명세는 같은 폴더의 README_ko.md와 ui_manifest.json을 확인하세요.</footer></main>
<script>const scenes=SCENE_DATA;const select=document.querySelector('#scene'),stage=document.querySelector('#stage'),opacity=document.querySelector('#opacity'),content=document.querySelector('#content'),alpha=document.querySelector('#alpha'),contentAlpha=document.querySelector('#contentAlpha'),description=document.querySelector('#description');let backgroundVisible=true;
for(const [id,s] of Object.entries(scenes)){const o=document.createElement('option');o.value=id;o.textContent=s.label;select.append(o)}
function update(){stage.innerHTML=scenes[select.value].svg;stage.style.setProperty('--panel-opacity',opacity.value/100);stage.style.setProperty('--content-opacity',content.value/100);document.querySelectorAll('.world-background').forEach(e=>e.style.display=backgroundVisible?'':'none');alpha.value=opacity.value+'%';contentAlpha.value=content.value+'%';description.textContent=scenes[select.value].ids.join(' · ')}
select.onchange=update;opacity.oninput=update;content.oninput=update;document.querySelector('#background').onclick=()=>{backgroundVisible=!backgroundVisible;update()};
const requested=new URLSearchParams(location.search).get('scene');if(scenes[requested])select.value=requested;update();window.uiScenes=scenes;
</script></html>'''.replace('SCENE_DATA',json.dumps(SCENES,ensure_ascii=False).replace('</','<\\/'))
viewer=viewer.replace('<main>','<main><nav id="flowNav" hidden style="margin-bottom:12px;display:flex;gap:12px"><button id="flowPrev">이전 내용</button><button id="flowNext">다음 내용</button></nav>')
viewer=viewer.replace('</script>', '''
const flowNav=document.querySelector('#flowNav'),flowPrev=document.querySelector('#flowPrev'),flowNext=document.querySelector('#flowNext');
const originalUpdate=update;update=function(){originalUpdate();const p=scenes[select.value].pagination;flowNav.style.display=p?'flex':'none';flowPrev.disabled=!p?.previous;flowNext.disabled=!p?.next};
function turnPage(dir){const p=scenes[select.value].pagination;if(p?.[dir]){select.value=p[dir];update()}}
flowPrev.onclick=()=>turnPage('previous');flowNext.onclick=()=>turnPage('next');select.onchange=update;
document.addEventListener('keydown',e=>{if(e.target.tagName==='SELECT')return;if(e.key==='PageDown'||e.key==='Enter'){turnPage('next');e.preventDefault()}if(e.key==='PageUp'){turnPage('previous');e.preventDefault()}});update();
</script>''')
write('preview.html',viewer)
print(f'Built {len(ICONS)} icons, {len(COMPONENTS)} components, {len(SCENES)} screens in {ROOT}')
