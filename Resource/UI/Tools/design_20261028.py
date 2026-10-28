"""Art-directed v1.2 compositions and lossless Korean typesetting, using real Windows font metrics."""
import ctypes
from ctypes import wintypes
import math

def apply_design(g):
    globals().update({k:v for k,v in g.items() if not k.startswith('__')})
    global FLOWS, METRICS, EXTRA_COMPONENTS
    FLOWS=[]; EXTRA_COMPONENTS=[]
    api=ctypes.WinDLL('gdi32')
    api.CreateCompatibleDC.argtypes=[wintypes.HDC];api.CreateCompatibleDC.restype=wintypes.HDC
    api.CreateFontW.argtypes=[ctypes.c_int]*5+[wintypes.DWORD]*8+[wintypes.LPCWSTR];api.CreateFontW.restype=wintypes.HANDLE
    api.SelectObject.argtypes=[wintypes.HDC,wintypes.HANDLE];api.SelectObject.restype=wintypes.HANDLE
    api.GetTextExtentPoint32W.argtypes=[wintypes.HDC,wintypes.LPCWSTR,ctypes.c_int,ctypes.POINTER(wintypes.SIZE)]
    dc=api.CreateCompatibleDC(None); fonts={}
    def measure(s,size,weight=400):
        pixels={12:14,13:14,14:16,15:16,16:18,17:18,18:20}.get(size,size)
        key=(pixels,weight)
        if key not in fonts:fonts[key]=api.CreateFontW(-pixels,0,0,0,weight,0,0,0,1,0,0,4,0,'Malgun Gothic')
        old=api.SelectObject(dc,fonts[key]);box=wintypes.SIZE()
        api.GetTextExtentPoint32W(dc,s,len(s),ctypes.byref(box));api.SelectObject(dc,old)
        return box.cx
    def wrap(source,width,size=20,weight=400,role='body'):
        source=source.replace('\u2212','-')
        lines=[];line_=''
        for ch in source:
            if ch=='\n':lines.append(line_);line_='';continue
            if line_ and measure(line_+ch,size,weight)>width-6:
                split=line_.rfind(' ')+1
                if split>len(line_)*.35:
                    lines.append(line_[:split]);line_=line_[split:]
                else:
                    lines.append(line_);line_=''
            line_+=ch
        lines.append(line_)
        FLOWS.append(dict(role=role,source=source,lines=lines,width=width,size=size,weight=weight,
                          lossless=''.join(lines)==source.replace('\n',''),ellipsis=False))
        return lines
    def flow(s,x,y,w,size=20,lh=30,color=None,weight=400,role='body'):
        rows=wrap(s,w,size,weight,role)
        return ''.join(text(row,x,y+i*lh,size,color,weight,width=w) for i,row in enumerate(rows)),len(rows)*lh
    def frame(title,desc='',section='개척지 기록',x=64,y=48,w=1152,h=704):
        b=rect(0,0,1280,800,C['bg'])
        b+=panel(x,y,w,h)+text(section,x+32,y+38,14,C['accent'],600)
        b+=text(title,x+32,y+84,32,weight=700,width=w-64)
        if desc: b+=text(desc,x+32,y+116,16,C['muted'],width=w-64)
        b+=line(x+32,y+138,x+w-32,y+138,'#2e4852')
        return b
    def item(label,x,y,w=620,selected=False,ico=None,trailing=None,h=52):
        b=rect(x,y,w,h,'#223e46' if selected else '#132830',6)
        if selected:b+=rect(x,y+10,3,h-20,C['accent'],1)
        if ico:b+=icon(ico,x+16,y+14,24,C['accent'] if selected else C['muted'])
        tx=x+(54 if ico else 20);available=w-(tx-x)-24-(100 if trailing else 0)
        labelrows=wrap(label,available,16,600,'selection_row')
        for i,t in enumerate(labelrows):b+=text(t,tx,y+32+i*24,16,C['ink'],600,width=available)
        if trailing:b+=text(trailing,x+w-18,y+32,14,C['accent'],500,'end')
        return b
    def roundpoint(cx,cy,r,a):
        a=math.radians(a);return cx+r*math.cos(a),cy+r*math.sin(a)
    def sector(cx,cy,ri,ro,angle,span=114):
        a,b=angle-span/2,angle+span/2
        p1,p2=roundpoint(cx,cy,ro,a),roundpoint(cx,cy,ro,b)
        p3,p4=roundpoint(cx,cy,ri,b),roundpoint(cx,cy,ri,a)
        return f'M{p1[0]:.2f} {p1[1]:.2f}A{ro} {ro} 0 0 1 {p2[0]:.2f} {p2[1]:.2f}L{p3[0]:.2f} {p3[1]:.2f}A{ri} {ri} 0 0 0 {p4[0]:.2f} {p4[1]:.2f}Z'

    ICONS.update({
        'unarmed':'<path d="M5 11V7a2 2 0 0 1 4 0V5a2 2 0 0 1 4 0v1a2 2 0 0 1 4 0v2a2 2 0 0 1 4 0v7l-3 6H8l-4-6a2 2 0 0 1 3-3l3 3V8M9 8v3M13 7v4M17 9v3"/>',
        'slot_empty':'<path d="M8 3H3v5M16 3h5v5M21 16v5h-5M8 21H3v-5M8 12h8"/>',
        'mouse_right':'<rect x="5" y="2" width="14" height="20" rx="7"/><path d="M12 2v8h7M5 10h7"/><path d="M14 4h2v3h-2z" fill="currentColor"/>',
        'key_release':'<path d="M4 12v9h16v-9M12 3v12M7 8l5-5 5 5"/>',
        'stage':'<path d="M3 4h18v16H3zM3 9h18M8 4v16M12 13l5 2-5 3z"/>',
    })

    def wheel(owned=True,hover='pistol',equipped='hand'):
        b=backdrop('mine')+rect(0,0,1280,800,'#02090e',extra='opacity=".75"')
        b+=text('장비',640,79,14,C['accent'],600,'middle')+text('무기 선택',640,125,32,weight=700,anchor='middle')
        b+=text('가리킨 뒤 Q를 놓으면 장착됩니다.',640,157,16,C['muted'],anchor='middle')
        for name,angle,cx,cy,title in [('empty',-90,640,236,'주 무장'),('pistol',30,780,466,'보조 무장'),('hand',150,500,466,'맨손')]:
            available=name=='hand' or (name=='pistol' and owned)
            pointed=hover==name;active=equipped==name
            fill='#244c50' if pointed and available else '#283034' if pointed else '#112630'
            stroke=C['accent'] if pointed and available else C['amber'] if pointed else '#3f5b66'
            b+=f'<g class="panel-fill"><path d="{sector(640,390,87,224,angle)}" fill="{fill}"/></g>'
            b+=f'<path d="{sector(640,390,87,224,angle)}" fill="none" stroke="{stroke}" stroke-width="{3 if pointed else 1}"/>'
            col=C['ink'] if available else C['muted']
            ic='unarmed' if name=='hand' else 'pistol' if available else 'slot_empty'
            b+=icon(ic,cx-18,cy-37,36,col)+text(title,cx,cy+23,16,col,600,'middle',180)
            status='현재 장착' if active else '권총' if name=='pistol' and owned else '근접 공격' if name=='hand' else '비어 있음' if name=='empty' else '미보유'
            b+=text(status,cx,cy+49,14,C['accent'] if active else C['muted'],400,'middle',180)
            if active:b+=icon('check',cx+42,cy-43,18,C['accent'])
        available=hover=='hand' or (hover=='pistol' and owned)
        center_title='맨손' if hover=='hand' else '권총' if available else '선택 불가'
        b+=text(center_title,640,387,24,weight=700,anchor='middle',width=150)
        b+=text('현재 장착 유지' if not available else 'Q 놓아서 선택',640,416,14,C['muted'],anchor='middle',width=160)
        b+=hint('Q 홀드','마우스로 가리키기',264,676)+hint('Q 해제','장착',646,676)+hint('Esc','취소',882,676)
        b+=text('게임 일시정지 · 빈 슬롯과 취소는 기존 장착 유지',640,742,14,C['muted'],anchor='middle',width=1100)
        return b

    scene('35_weapon_wheel_locked','무기 선택 / 해금 전',['UI-24'],lambda:wheel(False,'hand'))
    scene('36_weapon_wheel_pistol','무기 선택 / 권총 가리킴',['UI-24'],lambda:wheel(True,'pistol'))
    scene('37_weapon_wheel_empty','무기 선택 / 빈 슬롯 유지',['UI-24'],lambda:wheel(True,'empty'))
    scene('38_weapon_wheel_equipped','무기 선택 / 권총 장착 중',['UI-24'],lambda:wheel(True,'hand','pistol'))
    scene('39_weapon_wheel_unowned','무기 선택 / 미보유 슬롯',['UI-24'],lambda:wheel(False,'pistol'))

    def stage_screen():
        b=frame('스테이지 시작','시작 지점과 기능 해금 지점만 모았습니다.','테스트 진입')
        stages=['서장 · 헤일로 시작','전망대 · 스테이지 시작','바이크 해금 · 광산 출발','권총 해금 · 광산 입구','광산 · 스테이지 시작','1장 · 빛나는 개척지 시작']
        for i,s in enumerate(stages):b+=item(s,96,211+i*62,652,i==3,'stage',f'{i+1:02d}')
        b+=line(780,211,780,567,'#2e4852')+icon('pistol',824,222,36,C['accent'])
        detail,h=flow('권총 해금 · 광산 입구',824,306,328,24,34,weight=700,role='stage_detail');b+=detail
        detail,h2=flow('광산 입구 야외에서 시작합니다. 권총을 보유하지만 기본 장착은 맨손입니다.',824,326+h,328,16,28,C['muted'],role='stage_description');b+=detail
        warning,h=flow('선택 시 현재 진행이 초기화됩니다. 이전 필수 스토리는 완료 처리됩니다.',96,636,1088,16,27,C['amber'],role='stage_warning');b+=warning
        b+=item('뒤로',96,678,170,False,'chevron')+text('W/S · 방향 키 이동   Enter 선택   Esc 뒤로',1176,711,14,C['muted'],400,'end',840)
        return b
    scene('40_stages','스테이지 시험 진입 / 6개 지점',['UI-25'],stage_screen)

    def weapon_hud(state='unarmed',kind='bright'):
        b=backdrop(kind)+objective(kind='mine' if kind=='mine' else 'city')+minimap('mine' if kind=='mine' else 'city')
        if state=='unarmed':
            b+=vitals(show_ammo=False)+hud_icon('unarmed',1120,717,30)+hud_text('맨손',1248,741,24,anchor='end')
            b+=hud_text('좌클릭 근접 · Q 무기 선택',1248,767,14,C['muted'],400,'end',300)
        else:
            b+=vitals(ammo=0 if state=='reload' else 8,reloading=state=='reload')
            if state in ('aim','reload'):b+=reticle('blocked' if state=='reload' else 'ready',26 if state=='aim' else 16)
            else:
                b+='<circle cx="640" cy="380" r="4" fill="#07131c" stroke="#fff" stroke-width="1.5"/>'
                b+=hud_icon('mouse_right',1085,661,21)+hud_text('우클릭 홀드 · 조준',1248,683,14,C['muted'],400,'end',240)
        return b
    scene('41_unarmed','맨손 HUD / 탄약 숨김',['UI-02','UI-03'],lambda:weapon_hud())
    scene('42_pistol_hip','권총 비조준 / 기본 포인터',['UI-02','UI-03'],lambda:weapon_hud('hip'))
    scene('43_pistol_aim','우클릭 조준 / 실제 탄퍼짐',['UI-02','UI-03'],lambda:weapon_hud('aim','mine'))
    scene('44_pistol_reload','재장전 / 사격 불가',['UI-02','UI-03'],lambda:weapon_hud('reload','mine'))

    def hero():
        b=backdrop('mine')+rect(0,0,1280,800,'#07131c',extra='opacity=".68"')
        b+=line(96,120,162,120,C['accent'],3)+text('에르마 · 헤일로',96,166,16,C['accent'],600)
        b+=text('침묵하는 관문',90,253,54,weight=700,width=1080)
        b+=text('신호가 닿은 곳에서, 이야기가 시작된다.',96,299,20,C['muted'],width=1000)
        for i,(label,ic) in enumerate([('새 게임','gate'),('스테이지 목록 · 테스트','stage'),('옵션','settings'),('조작 안내','keyboard'),('종료','exit')]):
            b+=item(label,96,366+i*58,430,i==0,ic)
        b+=text('현재 진행은 실행 세션에만 유지됩니다.',96,727,14,C['muted'])
        return b
    scene('15_title','타이틀 / 여백과 정보 위계',['UI-18'],hero)

    def help_screen():
        b=frame('조작 안내','현재 장면에서 사용할 수 있는 동작만 안내합니다.','필드 가이드')
        controls=[('WASD','이동 · 이동 방향 회전'),('우클릭 홀드','커서 조준'),('좌클릭','장착 무기 공격'),('Q 홀드 + 마우스','무기 선택 · Q 해제로 장착'),
                  ('V / R','근접 공격 / 재장전'),('G','회복 · 광산과 1장'),('E','대화 · 조사 · 승하차'),('F / M','광산 스캔 / 전체 지도'),
                  ('J / H','조사 기록 / 대사 이력'),('Esc','일시정지 · 닫기')]
        for i,(k,label) in enumerate(controls):
            x=96+(i%2)*564;y=219+(i//2)*76
            b+=text(k,x,y,16,C['accent'],600,width=520)
            block,h=flow(label,x,y+29,500,16,25,C['ink'],role='help_action');b+=block
        b+=line(96,612,1184,612,'#2e4852')
        block,h=flow('Q는 무기 선택, G는 회복입니다. 대화는 E/Enter로 다음 페이지를 읽고, 숫자로 선택한 뒤 확정합니다.',96,650,1088,16,27,C['muted'],role='help_note');b+=block
        b+=hint('Esc','뒤로',1060,699)
        return b
    scene('20_help','조작 도움말 / 최신 Q·G 입력',['UI-23'],help_screen)

    def dialogue_pages():
        source='관문 N-0의 신호와 외곽 설비 N-0/17의 기록은 서로 달라. 둘을 같은 장소로 보고 조사하면 안 돼. 먼저 발전기의 수신 기록을 확인하자. 확인하지 못한 내용은 조사 기록에 추정으로 남겨 둘게. 현장에서 확인한 사실과 다음 행동을 빠짐없이 기록한 뒤, 안전한 곳에서 나머지 기록도 다시 읽어 보자.'
        rows=wrap(source,800,20,400,'dialogue_body_full')
        pages=[rows[i:i+3] for i in range(0,len(rows),3)]
        choice='조사 기록을 끝까지 확인한 뒤, 세 현장의 발전기·운송대·폐설비에서 수집한 내용을 대조하고 헤일로 수신망에서 리아에게 보고한다.'
        choice_rows=wrap(choice,754,16,600,'long_choice_full')
        names=[]
        for idx,body in enumerate(pages):
            def compose(idx=idx,body=body):
                b=backdrop('mine')+vitals()+minimap('mine')
                last=idx==len(pages)-1
                choice_height=24*len(choice_rows)+28 if last else 0
                height=100+len(body)*32+(choice_height+20 if last else 0)+68
                top=700-height
                b+=panel(208,top,864,700-top)
                b+=icon('radio',232,top+24,24,C['blue'])+text('리아',270,top+47,20,weight=700)
                b+=text('무전 · 전투 계속',1048,top+46,14,C['blue'],400,'end')
                b+=line(232,top+67,1048,top+67,'#2e4852')
                for i,t in enumerate(body):b+=text(t,232,top+107+i*32,20,width=800)
                if last:
                    cy=top+100+len(body)*32+18;ch=24*len(choice_rows)+28
                    b+=rect(232,cy,816,ch,'#223e46',6)+rect(232,cy+8,3,ch-16,C['accent'],1)
                    b+=text('1',248,cy+31,16,C['accent'],600)
                    for i,t in enumerate(choice_rows):b+=text(t,282,cy+31+i*24,16,weight=600,width=754)
                b+=text(f'{idx+1:02d} / {len(pages):02d}',232,678,14,C['muted'])
                b+=hint('E / Enter','확정' if last else '다음 페이지',824,660)
                return b
            name='05_dialogue' if idx==0 else f'45_dialogue_page_{idx+1:02d}'
            scene(name,f'긴 대화 / {idx+1}페이지 · 본문 보존',['UI-08'],compose)
            names.append(name)
        for i,name in enumerate(names):
            SCENES[name]['pagination']=dict(previous=names[i-1] if i else None,next=names[i+1] if i+1<len(names) else None)
        FLOWS[-1]['pages_note']='Every body line and choice line is rendered across named review screens; no tail is discarded.'
    dialogue_pages()

    def record_screen():
        b=frame('조사 기록','확인한 사실과 다음 행동을 구분합니다.','기록실')
        b+=item('관문 N-0의 수신 기록',96,210,290,True,'record')+item('발전기 조사',96,272,290,False,'power')+item('운송대 구조',96,334,290,False,'rescue')
        b+=line(418,210,418,648,'#2e4852')+text('관문과 외곽 설비는 다른 대상이다.',454,237,24,weight=700,width=698)
        b+=text('확인한 사실',454,292,14,C['accent'],600)
        block,h=flow('N-0은 관문을 가리키고 N-0/17은 외곽 설비의 식별 번호다. 아직 조사하지 않은 내용과 추정은 확인한 사실로 기록하지 않는다.',454,328,698,20,32,role='journal_fact');b+=block
        b+=text('현재 할 일',454,370+h,14,C['accent'],600)
        block,h2=flow('세 현장의 기록을 대조하고, 헤일로 수신망에서 리아에게 조사 결과를 보고한다.',454,409+h,698,20,32,role='journal_next_action');b+=block
        b+=text('중단한 대화는 안전한 곳에서 다시 말을 걸어 재개할 수 있습니다.',454,619,16,C['muted'],width=698)
        b+=hint('PageUp / PageDown','페이지',96,693)+hint('J / Esc','닫기',1050,693)
        return b
    scene('06_journal','조사 기록 / 문단과 여백 재설계',['UI-09'],record_screen)

    def options_screen():
        b=frame('옵션','정보의 크기와 대비를 해치지 않는 범위에서 조절합니다.','화면 설정')
        for i,(label,value,ratio) in enumerate([('패널 배경','94%',.94),('테두리','100%',1),('HUD · 글자 · 아이콘','100%',1)]):
            y=239+i*104
            b+=text(label,96,y,20,weight=600)+text(value,602,y,20,C['accent'],600,'end')
            b+=bar(96,y+26,506,ratio,C['accent'],6)
        b+=line(654,211,654,557,'#2e4852')
        for i,(label,value) in enumerate([('카툰 명암','켜짐'),('월드 렌더 해상도','기본'),('안티앨리어싱','켜짐')]):
            y=239+i*104
            b+=text(label,704,y,20,weight=600)+item(value,704,y+23,448,i==1,None)
        b+=text('월드를 저해상도로 그려도 HUD 글자와 기호는 선명하게 유지됩니다.',96,602,16,C['muted'],width=1088)
        b+=text('설정은 현재 실행 세션과 새 게임에 유지됩니다.',96,635,16,C['muted'],width=1088)
        b+=item('뒤로',96,678,170,False,'chevron')+text('A/D · 방향 키 조절',1176,711,14,C['muted'],400,'end')
        return b
    scene('16_options','옵션 / 조절 항목과 그래픽 분리',['UI-19'],options_screen)

    def summary_screen():
        b=frame('보유 · 성장','현재 플레이어의 보유량과 성장 상태입니다.','플레이어')
        b+=icon('level',96,218,36,C['accent'])+text('레벨 7',150,252,32,weight=700)
        b+=text('경험치 420 / 800',96,297,20,C['muted'])+bar(96,321,1056,.525,C['accent'],8)
        stats=[('credit','크레딧','12,480'),('parts','거래 부품','18'),('heal','회복 키트','3'),('attack','공격 / 방어','24 / 12'),('reputation','악명 / 거래 할증','42 / +50%')]
        for i,(ic,label,value) in enumerate(stats):
            y=379+i*52;b+=icon(ic,96,y-20,24,C['muted'])+text(label,136,y,20)+text(value,1152,y,24,C['ink'],600,'end')
            b+=line(136,y+17,1152,y+17,'#233d46')
        b+=item('뒤로',96,678,170,False,'chevron')
        return b
    scene('17_summary','보유·성장 / 항목과 수치 정렬',['UI-20'],summary_screen)

    def pause_screen(region='1장'):
        b=backdrop()+rect(0,0,1280,800,'#02090e',extra='opacity=".62"')+panel(376,56,528,688)
        b+=text(region,408,98,14,C['accent'],600)+text('일시정지',408,144,32,weight=700)
        labels=['계속하기','스테이지 목록 · 테스트','옵션','보유 · 성장 정보','조작 안내','헤일로 귀환 · 기록 유지' if region=='1장' else '광산 재시작' if region=='광산' else '다시 시작 · 진행 초기화','종료']
        for i,s in enumerate(labels):b+=item(s,408,178+i*64,464,i==0,None)
        b+=text('현재 진행은 실행 세션에만 유지됩니다.',408,687,14,C['muted'],width=464)
        b+=text('Esc 계속하기 · 방향 키 선택 · Enter 확인',408,717,14,C['muted'],width=464)
        return b
    scene('09_pause','일시정지 / 1장 귀환',['UI-17'],pause_screen)
    scene('28_pause_mine','일시정지 / 광산 재시작',['UI-17'],lambda:pause_screen('광산'))
    scene('29_pause_city','일시정지 / 도시 다시 시작',['UI-17'],lambda:pause_screen('도시'))

    def history_screen():
        b=frame('대사 이력','이미 읽은 내용만 전달 방식과 함께 보관합니다.','기록실')
        for i,(who,channel,body) in enumerate([('리아','무전','수신 기록은 확인했어. 다음은 발전기야.'),('발전기 제어 단말','단말','보조 전력 복구를 확인했습니다.'),('리아','현장 대화','아직은 결론을 내리지 말자. 세 현장의 기록을 더 대조해야 해.')]):
            y=229+i*128
            b+=text(channel,96,y,14,C['accent'],600)+text(who,246,y,20,weight=600,width=900)
            block,h=flow(body,246,y+44,906,20,30,role='history_full');b+=block
            b+=line(246,y+84,1152,y+84,'#2e4852')
        b+=hint('PageUp / PageDown','페이지',96,693)+hint('H / Esc','닫기',1050,693)
        return b
    scene('13_history','대사 이력 / 전달 방식과 본문 분리',['UI-09'],history_screen)

    def unlock_notice():
        b=weapon_hud('unarmed')
        b+=icon('pistol',918,322,28,C['accent'])
        a,h=flow('권총을 받았습니다. Q를 누른 채 마우스로 보조 무장을 가리키고, Q를 놓으면 장착됩니다.',958,343,290,16,26,role='unlock_notice_full');b+=a
        return b
    scene('46_unlock_notice','해금 알림 / 전체 안내 보존',['UI-15','UI-23'],unlock_notice)

    def notice_pages():
        s='헤일로 수신망에 조사 기록을 전달했습니다. 발전기·운송대·폐설비의 현장 기록을 모두 확인한 다음 리아에게 보고하세요. 확인한 기록은 의료 이송 뒤에도 유지됩니다. 아직 읽지 않은 중요한 대화는 안전한 곳에서 이어 읽을 수 있습니다.'
        rows=wrap(s,294,16,400,'notice_all_pages')
        names=[]
        for pi in range((len(rows)+3)//4):
            def compose(pi=pi):
                b=weapon_hud('hip')+icon('record',918,321,24,C['accent'])
                body=rows[pi*4:pi*4+4]
                for j,t in enumerate(body):b+=hud_text(t,954,342+j*26,16,C['ink'],400,width=294)
                b+=hud_text(f'{pi+1} / {(len(rows)+3)//4} · 자동 다음',954,370+len(body)*26,14,C['muted'],400,width=294)
                return b
            name=f'47_notice_page_{pi+1:02d}'
            scene(name,f'긴 알림 / {pi+1}페이지 · 생략 없음',['UI-15'],compose)
            names.append(name)
        for i,name in enumerate(names):
            SCENES[name]['pagination']=dict(previous=names[i-1] if i else None,next=names[i+1] if i+1<len(names) else None)
    notice_pages()

    # State designs are separable; labels remain runtime text.
    for state,fill,stroke in [('default','#112630','#3f5b66'),('hover','#244c50',C['accent']),('equipped','#173b3d',C['accent']),('unavailable','#202c32','#6f8187')]:
        EXTRA_COMPONENTS.append((f'wheel_sector_{state}',f'<path d="{sector(232,232,87,224,-90)}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>',464,464,'radial_runtime_geometry_reference'))
    EXTRA_COMPONENTS.append(('cursor_unarmed','<circle cx="16" cy="16" r="4" fill="#07131c" stroke="white" stroke-width="1.5"/>',32,32,'pointer_no_spread'))
    EXTRA_COMPONENTS.append(('cursor_hip','<circle cx="16" cy="16" r="3" fill="white" stroke="#07131c" stroke-width="2"/>',32,32,'pointer_no_aim_ring'))
    dump('Source/text_flows.json',dict(version='1.2.0',font='Malgun Gothic',flows=FLOWS,policy='Lossless wrap/expand/paginate; never ellipsize required information'))
    g['EXTRA_COMPONENTS']=EXTRA_COMPONENTS
    g['scene']('23_icons','아이콘 라이브러리 / 신규 조작 포함',['COMMON'],g['icons_sheet'])
    api.DeleteObject.argtypes=[wintypes.HANDLE];api.DeleteDC.argtypes=[wintypes.HDC]
    for font in fonts.values():api.DeleteObject(font)
    api.DeleteDC(dc)
