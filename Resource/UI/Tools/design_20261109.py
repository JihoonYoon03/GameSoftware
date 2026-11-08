"""CQ-02A~E UI artwork. Read-only state fixtures; no gameplay or archive mutations."""
import ctypes
from ctypes import wintypes
import json

def apply_activity_design(g):
    globals().update({k:v for k,v in g.items() if not k.startswith('__')})
    prior=json.loads((ROOT/'Source/text_flows.json').read_text(encoding='utf-8'))['flows']
    flows=[]
    api=ctypes.WinDLL('gdi32')
    api.CreateCompatibleDC.argtypes=[wintypes.HDC];api.CreateCompatibleDC.restype=wintypes.HDC
    api.CreateFontW.argtypes=[ctypes.c_int]*5+[wintypes.DWORD]*8+[wintypes.LPCWSTR];api.CreateFontW.restype=wintypes.HANDLE
    api.SelectObject.argtypes=[wintypes.HDC,wintypes.HANDLE];api.SelectObject.restype=wintypes.HANDLE
    api.GetTextExtentPoint32W.argtypes=[wintypes.HDC,wintypes.LPCWSTR,ctypes.c_int,ctypes.POINTER(wintypes.SIZE)]
    dc=api.CreateCompatibleDC(None);fonts={}
    def width(s,size,weight):
        px={12:14,13:14,14:16,15:16,16:18,17:18,18:20}.get(size,size)
        if (px,weight) not in fonts: fonts[px,weight]=api.CreateFontW(-px,0,0,0,weight,0,0,0,1,0,0,4,0,'Malgun Gothic')
        old=api.SelectObject(dc,fonts[px,weight]);r=wintypes.SIZE()
        api.GetTextExtentPoint32W(dc,s,len(s),ctypes.byref(r));api.SelectObject(dc,old)
        return r.cx
    def wrap(source,w,size=20,weight=400,role='activity'):
        source=source.replace('\u2212','-');lines=[];s=''
        for ch in source:
            if ch=='\n':lines.append(s);s='';continue
            if s and width(s+ch,size,weight)>w-7:
                cut=s.rfind(' ')+1
                if cut>len(s)*.35:lines.append(s[:cut]);s=s[cut:]
                else:lines.append(s);s=''
            s+=ch
        lines.append(s)
        flows.append(dict(source=source,lines=lines,width=w,size=size,weight=weight,role=role,lossless=''.join(lines)==source.replace('\n',''),ellipsis=False))
        return lines
    def block(source,x,y,w,size=20,lh=30,color=None,weight=400,role='activity'):
        rows=wrap(source,w,size,weight,role)
        return ''.join(text(t,x,y+lh*i,size,color,weight,width=w) for i,t in enumerate(rows)),lh*len(rows)
    def button(label,x,y,w=132,h=34,state='default'):
        bg='#24464b' if state in ('selected','hover') else '#132c36'
        b=rect(x,y,w,h,bg,5)
        if state in ('selected','hover','pressed'):
            b+=rect(x+.5,y+.5,w-1,h-1,'none',5,extra=f'stroke="{C["accent"]}" stroke-width="{2 if state=="hover" else 1}"')
        if state=='selected':b+=rect(x,y+7,3,h-14,C['accent'],1)
        rows=wrap(label,w-20,14,600,'activity_button')
        if len(rows)!=1:raise ValueError(f'Button needs more space: {label}')
        b+=text(label,x+w/2,y+h/2+6,14,C['disabled'] if state=='disabled' else C['ink'],600,'middle',w-20)
        return b
    def marker(name,x,y,size=20,col=None):return icon(name,x,y,size,col or C['accent'])
    def base(local=False,page='01 / 01',danger=False):
        b=backdrop('bright')+panel(64,40,1152,720)
        b+=text('에르마 · 활동 기록',96,80,14,C['accent'],600)+text('조사 기록',96,122,32,weight=700)
        b+=text('현재 행성의 활동 · F5 저장 / F9 저장·불러오기',96,155,14,C['muted'],width=1088)
        b+=button('이야기 임무',96,179,168,38,'default' if local else 'selected')
        b+=button('지역 의뢰',276,179,168,38,'selected' if local else 'default')
        b+=text('수락한 의뢰만 표시' if local else '참여와 추적은 별개',624,204,14,C['muted'],width=528)
        b+=line(590,236,590,658,'#2a4650')
        b+=text('목록 '+page,96,651,14,C['muted'])
        b+=button('이전',392,629,78,34,'default' if page.startswith('02') else 'disabled')+button('다음',480,629,78,34,'disabled')
        b+=button('이전',944,629,100,34,'disabled')+button('다음',1056,629,100,34,'disabled')
        b+=line(96,679,1184,679,'#2a4650')
        b+=text('교전 중 · 기록을 읽어도 시간은 계속 흐릅니다.' if danger else '현장 기록 유지 · 제출과 보고는 현장에서 진행',96,721,14,C['amber'] if danger else C['muted'],width=700)
        b+=button('닫기 · Esc',1032,697,152,36)
        return b

    additions={
      'activity_track':'<path d="m12 3 9 9-9 9-9-9z"/><circle cx="12" cy="12" r="3"/><path d="M12 0v5M24 12h-5M12 24v-5M0 12h5"/>',
      'activity_untracked':'<path d="m12 3 9 9-9 9-9-9z"/><path d="M8 12h8"/>',
      'activity_active':'<circle cx="12" cy="12" r="9"/><path d="m10 7 7 5-7 5z"/>',
      'activity_paused':'<circle cx="12" cy="12" r="9"/><path d="M9 8v8M15 8v8"/>',
      'activity_complete':'<circle cx="12" cy="12" r="9"/><path d="m7 12 4 4 7-8"/>',
      'activity_offer':'<circle cx="12" cy="12" r="9"/><path d="M12 7v10M7 12h10"/>',
      'activity_prerequisite':'<path d="M3 4h14v17H3zM6 8h6M6 12h4M15 10h7v8h-7zM17 10V7a2 2 0 0 1 4 0v3"/>',
      'activity_measure':'<path d="M3 12h3l2-7 4 14 4-14 2 7h3M3 21h18"/>',
      'activity_submit':'<path d="M4 3h12v18H4zM8 7h5M8 11h4M13 16h9M18 12l4 4-4 4"/>',
      'activity_payment_hold':'<path d="M3 6h18v15H3zM3 6l15-4v4M15 11v5M18 11v5M6 12h3"/>',
      'activity_restore':'<path d="M5 7a9 9 0 1 1-2 9M5 2v5h5M9 12l3 3 6-7"/>',
      'activity_map':'<path d="m3 5 6-2 6 3 6-2v16l-6 2-6-3-6 2zM9 3v16M15 6v16"/>',
    }
    ICONS.update(additions)
    styles={'active':('activity_active','진행 중',C['accent'],'진행 보류'),
            'paused':('activity_paused','보류',C['amber'],'재개'),
            'completed':('activity_complete','완료',C['muted'],'완료'),
            'unaccepted':('activity_offer','미수락',C['muted'],'수락'),
            'locked':('activity_prerequisite','선행 조사 필요',C['muted'],'수락 불가')}
    story_names=['전망대의 목소리','광산 구조 신호','빛나는 개척지','남겨 둔 불빛']
    def story(state='normal',danger=False):
        b=base(False,danger=danger)
        states=['active','active','locked','paused'];selected=1
        if state=='completed':states[1]='completed'
        if state=='prerequisite':selected=2
        if state=='focus_other':selected=0
        if state=='unaccepted':states[3]='unaccepted';selected=3
        for i,(name,s) in enumerate(zip(story_names,states)):
            y=232+i*94;chosen=i==selected;ic,lab,col,act=styles[s]
            b+=rect(96,y,472,86,'#203f47' if chosen else '#122a34',6)
            if chosen:b+=rect(96,y+12,3,62,C['accent'],1)
            b+=text(name,118,y+29,18,weight=600,width=416)
            b+=marker(ic,118,y+55,18,col)+text(lab,144,y+70,14,col,width=130)
            b+=button(act,284,y+48,116,32,'disabled' if s in ('locked','completed') else 'default')
            tracked=i==(0 if state=='completed' else 1) and s=='active'
            b+=button('추적 중' if tracked else '목표 추적',412,y+48,134,32,'selected' if tracked else 'disabled' if s!='active' else 'default')
        b+=text('선행 조사' if state=='prerequisite' else '완료 기록' if state=='completed' else '선택한 기록' if state in ('focus_other','unaccepted') else '현재 추적 중',624,260,14,C['accent'],600)
        b+=text(story_names[selected],624,305,26,weight=700,width=528)
        if state=='prerequisite':
            t,h=block('먼저 현재 조사 안내에 따라 필요한 현장 기록을 확인해야 합니다. 선행 조사가 끝나면 이 임무를 수락할 수 있습니다.',624,359,528,20,32,role='prerequisite_full');b+=t
            t,h=block('미수락 상태에서는 추적할 수 없습니다. 다른 진행 중 임무는 그대로 유지됩니다.',624,409+h,528,16,28,C['muted'],role='prerequisite_tracking');b+=t
        elif state=='completed':
            t,h=block('현장 보고를 마쳐 완료된 임무입니다. 완료 기록은 남으며 다시 수락하거나 보상을 중복으로 받지 않습니다.',624,359,528,20,32,role='completion_full');b+=t
            t,h=block('표시 목표는 남아 있는 진행 중 임무로 돌아갑니다. 완료된 활동은 추적하지 않습니다.',624,409+h,528,16,28,C['muted'],role='completion_fallback');b+=t
        else:
            b+=text('다음 행동',624,353,14,C['accent'],600)
            action='전망대에서 수신한 기록을 확인한다.' if selected==0 else '현장에서 수락 조건과 기록을 확인한다.' if selected==3 else '확보한 좌표 기록을 리아에게 보고한다.'
            t,h=block(action,624,390,528,20,32,role='tracked_story_action');b+=t
            b+=button('지도 보기',624,418+h,160,38,'default' if states[selected]=='active' else 'disabled')
            t,h2=block('목표 추적은 화면에 표시할 활동을 고르는 동작입니다. 다른 임무의 참여 상태나 현장 기록을 바꾸지 않습니다.',624,528,528,16,28,C['muted'],role='track_vs_participation');b+=t
        b+=text('기록 01 / 01',624,651,14,C['muted'])
        return b

    FIXTURES=[]
    def add(name,label,ids,body,fixture):
        scene(name,label,ids,body);FIXTURES.append(dict(screen=name,**fixture))
    add('50_story_tracking','이야기 임무 / 참여와 추적 분리',['UI-04','UI-09','CQ-02A'],story,dict(kind='story',participation=['active','active','not_accepted','paused'],tracked='mine'))
    add('51_story_completed','이야기 임무 / 완료와 추적 해제',['UI-04','UI-09','CQ-02E'],lambda:story('completed'),dict(kind='story',completed='mine',tracked='signal',reward_from_render=False))
    add('52_story_prerequisite','선행 조사 / 전체 조건 안내',['UI-09','CQ-02A'],lambda:story('prerequisite'),dict(kind='story',eligible=False,track_enabled=False))
    add('53_story_danger','기록 열람 / 교전 중 경고',['UI-09','CQ-02A'],lambda:story(danger=True),dict(kind='story',danger=True,world_paused=False))

    tasks=['중계 잡음 측정','전력 분기 진단','환경 센서 점검']
    def regional(mode='measure',count=3,page=1,long=False):
        b=base(True,f'{page:02d} / {2 if page==2 else 1:02d}')
        if count==0:
            b+=marker('empty',282,311,54,C['muted'])
            t,h=block('수락한 지역 의뢰가 없습니다.',126,408,400,20,30,role='regional_empty');b+=t
            t,h=block('현장에서 의뢰를 수락하면 현재 행성의 목록에 표시됩니다.',126,454,400,16,28,C['muted'],role='regional_empty_note');b+=t
            b+=text('자유 탐사',624,286,26,weight=700)+text('현재 가능한 조사와 주변 지역을 확인하세요.',624,338,18,C['muted'],width=528)
            return b
        names=['헤일로 외곽','동부 중계 구역','광산 접근 구역']
        if long:names[0]='헤일로 동부 고가와 광산 접근로 사이의 외곽 수신망 구역'
        if page==2:names=['남부 중계 구역','외곽 현장 접수 구역','헤일로 북부']
        for i in range(min(count,3)):
            y=232+i*122;selected=i==0
            b+=rect(96,y,472,112,'#203f47' if selected else '#122a34',6)
            if selected:b+=rect(96,y+12,3,88,C['accent'],1)
            rows=wrap(names[i],410,16,600,'regional_name')
            for j,t in enumerate(rows):b+=text(t,118,y+29+j*24,16,weight=600,width=410)
            # Name and task are separate; the short command never wraps vertically.
            task=tasks[i%3]
            if len(rows)>1:
                b+=text('제출 대기 · '+task if mode=='submit' else '측정 중 · '+task,118,y+88,14,C['muted'],width=280)
            else:
                b+=text(task,118,y+55,14,C['muted'],width=410)
                b+=marker('activity_submit' if mode=='submit' else 'activity_measure',118,y+78,18,C['accent'])
                b+=text('제출 대기' if mode=='submit' else '측정 중',144,y+94,14,C['accent'],width=180)
            b+=button('지도 보기',412,y+69,134,32)
            if selected:b+=marker('activity_track',538,y+12,17)
        region=names[0]
        b+=marker('activity_restore' if page==2 else 'activity_track',624,242,22)
        b+=text('복원된 추적 활동' if page==2 else '현재 추적 중',658,261,14,C['accent'],600)
        head,h=block(region,624,308,528,24,34,weight=700,role='regional_detail_title');b+=head
        b+=text(tasks[0],624,323+h,16,C['muted'])
        by=370+h
        message='측정 기록을 현장 접수 단말에 제출한다.' if mode=='submit' else '현장 센서에서 중계 잡음을 측정한다.'
        t,bodyh=block(message,624,by,528,20,32,role='regional_action');b+=t
        note='측정 기록은 보관 중입니다. 현장 제출이 끝나야 정산과 완료가 처리됩니다.' if mode=='submit' else '목록에서 의뢰 이름을 선택하면 추적합니다. 수락과 기록 제출은 현장에서 진행합니다.'
        t,noteh=block(note,624,by+bodyh+30,528,16,28,C['muted'],role='regional_action_note');b+=t
        b+=text('기록 01 / 01',624,651,14,C['muted'])
        return b
    for name,label,mode,count,page,long in [
      ('54_regional_empty','지역 의뢰 / 0개','measure',0,1,False),
      ('55_regional_measure','지역 의뢰 / 측정 중','measure',3,1,False),
      ('56_regional_submit','지역 의뢰 / 기록 제출 대기','submit',1,1,False),
      ('57_regional_restored','복원된 추적 / 뒤쪽 목록 페이지','submit',3,2,False),
      ('58_regional_long_name','긴 지역명 / 전체 이름 보존','measure',3,1,True),
      ('59_regional_one','지역 의뢰 / 한 항목','measure',1,1,False)]:
        add(name,label,['UI-09','CQ-02B','CQ-02C'],lambda mode=mode,count=count,page=page,long=long:regional(mode,count,page,long),dict(kind='regional',accepted_count=count+(3 if page==2 else 0),list_page=page,phase=mode,restored=page==2,remote_accept=False))

    def map_context():
        b=backdrop('bright')+vitals(show_ammo=False)
        b+=hud_icon('activity_track',32,38,24)+hud_text('추적 · 중계 잡음 측정',68,58,18,width=430)
        b+=hud_text('현장 접수 단말에 측정 기록 제출',68,88,16,C['ink'],400,width=430)
        b+=hud_text('J 조사 기록',68,116,14,C['muted'],400)
        b+=minimap()
        b+=marker('activity_track',1094,114,24,C['accent'])
        b+=hud_text('추적 목표',1248,324,16,C['accent'],anchor='end',width=260)
        b+=marker('destination',1039,354,22,C['amber'])+hud_text('지정 목적지 · 240 m',1248,375,16,C['amber'],anchor='end',width=260)
        b+=hud_text('목표를 바꿔도 지정 목적지는 유지됩니다.',1248,409,14,C['muted'],400,'end',360)
        return b
    add('60_hud_tracking_and_pin','HUD / 추적 목표와 지정 목적지 공존',['UI-04','UI-05','UI-06','CQ-02B'],map_context,dict(kind='hud',tracked_marker='cyan_diamond',user_marker='gold_pin',user_pin_preserved=True,route_line=False))
    def mine_exit():
        b=backdrop('mine')+vitals()+minimap('mine')
        b+=hud_icon('activity_track',32,38,24)+hud_text('지상 활동 추적',68,58,18,width=430)
        b+=hud_text('먼저 광산 출구로 이동한다.',68,88,16,C['ink'],400,width=430)
        b+=marker('exit',1080,350,22)+hud_text('출구 방향',1248,375,16,C['accent'],anchor='end',width=260)
        b+=hud_text('미탐험 지형은 표시하지 않습니다.',1248,409,14,C['muted'],400,'end',350)
        return b
    add('61_mine_surface_tracking','광산 / 지상 활동은 출구 안내',['UI-04','UI-05','CQ-02B'],mine_exit,dict(kind='hud',context='mine',target='known_exit',hidden_terrain_exposed=False))

    def notice_pages(prefix,title,source,ic,fixture):
        rows=wrap(source,294,16,400,'notice_full_'+prefix)
        parts=[rows[i:i+4] for i in range(0,len(rows),4)];names=[]
        for pi,part in enumerate(parts):
            def draw(pi=pi,part=part):
                b=backdrop('bright')+vitals(show_ammo=False)+minimap()
                b+=hud_icon(ic,918,319,22,C['amber'])
                b+=hud_text(title,954,340,16,C['amber'],600,width=294)
                for j,t in enumerate(part):b+=hud_text(t,954,372+j*26,16,C['ink'],400,width=294)
                if len(parts)>1:b+=hud_text(f'{pi+1} / {len(parts)} · 자동 다음',954,402+len(part)*26,14,C['muted'],400,width=294)
                return b
            name=f'{prefix}_{pi+1:02d}';names.append(name)
            add(name,title+f' / {pi+1}페이지',['UI-15','CQ-02D'],draw,dict(kind='notice',page=pi+1,total=len(parts),**fixture))
        if len(names)>1:
            for i,n in enumerate(names): SCENES[n]['pagination']=dict(previous=names[i-1] if i else None,next=names[i+1] if i+1<len(names) else None)
    notice_pages('62_settlement_hold','의뢰 정산 보류','크레딧 소지 한도로 보상을 받을 수 없습니다. 측정 기록은 유지되며, 여유가 생기면 이 단말에서 다시 제출할 수 있습니다.','activity_payment_hold',dict(reward_applied=False,record_preserved=True,phase='submit',automatic_completion=False))
    notice_pages('63_planet_failure','행성 준비 실패','세이라로 이동할 준비를 완료하지 못했습니다. 실제 원인: 지역 데이터 준비 작업이 취소되었습니다. 현재 행성의 위치와 활동 기록은 유지됩니다.','warning',dict(example_error=True,exact_cause_preserved=True,no_success_popup=True))

    def legend_sheet():
        b=rect(0,0,1280,800,C['bg'])+heading('활동 상태','상태와 행동을 분리합니다.','같은 색이어도 기호·문구·버튼의 역할로 구분할 수 있습니다.')
        states=list(styles.items())
        for i,(key,(ic,label,col,action)) in enumerate(states):
            y=222+i*83
            b+=marker(ic,96,y+10,28,col)+text(label,145,y+32,20,col,600,width=250)
            b+=text('기록 상태',440,y+32,16,C['muted'])+button(action,660,y,180,48,'disabled' if key in ('completed','locked') else 'default')
            b+=button('목표 추적',876,y,220,48,'default' if key=='active' else 'disabled')
        b+=text('추적 중은 참여 상태가 아닙니다. 완료는 현장 보고와 정산 성공 뒤에만 표시합니다.',96,706,16,C['muted'],width=1088)
        return b
    add('64_activity_state_legend','활동 상태 / 참여·추적·잠금·완료',['UI-04','UI-09','CQ-02A','CQ-02E'],legend_sheet,dict(kind='legend',states=list(styles),color_only=False))
    add('65_story_focus_and_tracking','선택한 기록과 추적 목표 분리',['UI-09','CQ-02A'],lambda:story('focus_other'),dict(kind='story',selected='signal',tracked='mine',participation_changed=False))
    add('66_restore_legacy_fallback','구형 저장 / 유효한 이야기 임무로 복원',['UI-09','CQ-02C'],story,dict(kind='restore',archive_formats=list(range(1,8)),fallback='active_story',progress_preserved=True))
    add('67_after_completion_list','완료 후 이야기 목록 / 중복 보상 없음',['UI-09','CQ-02E'],lambda:story('completed'),dict(kind='story',completed='mine',fallback='signal',duplicate_reward=False))
    def activity_map():
        b=backdrop()+panel(56,40,1168,720)
        b+=text('지역 약도',88,91,28,weight=700)+text('헤일로 주변 · 북쪽 고정 · 지상',88,118,14,C['muted'])
        b+=rect(80,148,818,510,'#142a31',5)
        b+='<path d="M110 594 860 217M160 180 860 550M350 650 740 150" stroke="#405b65" stroke-width="20" fill="none"/>'
        b+=marker('player',476,456,30,C['ink'])+text('현재 위치',514,482,14)
        b+=marker('activity_track',640,300,30)+text('접수 단말 · 추적 목표',678,326,14,C['accent'],width=210)
        for x,y in [(244,260),(334,544)]:b+=marker('activity_untracked',x,y,22,C['muted'])
        b+=marker('destination',729,516,30,C['amber'])+text('지정 목적지',769,542,14,C['amber'])
        b+=text('N ↗',866,186,20,C['accent'],600,'end')
        b+=text('목표와 목적지',930,185,22,weight=700)
        for i,(ic,label,col) in enumerate([('activity_track','현재 추적 목표',C['accent']),('activity_untracked','다른 수락한 의뢰',C['muted']),('destination','지정 목적지',C['amber']),('player','현재 위치',C['ink'])]):
            b+=marker(ic,932,220+i*52,24,col)+text(label,970,241+i*52,14,col,width=220)
        t,h=block('측정이 끝나면 목표는 현장 접수 단말로 바뀝니다. 지정 목적지는 유지됩니다.',930,480,250,16,28,C['muted'],role='map_target_preserved');b+=t
        b+=text('자동 경로 없음',930,620,14,C['muted'],width=250)
        b+=text('좌클릭 목적지 설정 · 우클릭 해제',88,710,16,C['muted'],width=750)+button('닫기 · M / Esc',998,683,186,42)
        return b
    add('68_map_activity_markers','지도 / 추적·다른 의뢰·지정 목적지',['UI-06','CQ-02B'],activity_map,dict(kind='map',other_accepted_markers=True,pin_preserved=True,route_line=False))
    add('69_story_not_accepted','미수락 / 수락과 추적 비활성',['UI-09','CQ-02A'],lambda:story('unaccepted'),dict(kind='story',selected='mara',participation='unaccepted',tracked='mine',selected_track_enabled=False))
    def symbol_sheet():
        b=rect(0,0,1280,800,C['bg'])+heading('활동 기호','작은 기호와 짧은 상태 문구','동적 문구와 분리 · 상태는 색상과 형태로 함께 구분')
        labels=['추적 중','추적하지 않음','진행 중','보류','완료','미수락','선행 조사 필요','측정 중','기록 제출','정산 보류','추적 복원','지도 보기']
        for i,(name,label) in enumerate(zip(additions,labels)):
            x=96+(i%4)*280;y=240+(i//4)*160
            b+=rect(x,y,248,132,C['panel'],6)+marker(name,x+18,y+16,32)
            b+=text(label,x+18,y+82,18,weight=600,width=212)
            b+=text(name,x+18,y+112,12,C['muted'],width=212)
        return b
    add('70_activity_symbols','활동 기호 / 신규 12종',['COMMON'],symbol_sheet,dict(kind='symbol_reference',text_baked=False,icons=list(additions)))

    # A transparent separable emphasis layer; dynamic text is never baked in it.
    for name,col in [('activity_row_focus',C['accent']),('activity_row_warning',C['amber'])]:
        EXTRA_COMPONENTS.append((name,rect(.75,.75,470.5,110.5,'none',6,extra=f'stroke="{col}" stroke-width="1.5"')+rect(1,12,3,88,col,1),472,112,'activity_overlay_fixed_or_nine_slice'))
    EXTRA_COMPONENTS.append(('activity_page_dot','<circle cx="5" cy="5" r="3.5" fill="white"/>',10,10,'page_position_tint'))

    # Latest activity views appear first in the review viewer without removing previous work.
    newest={k:v for k,v in SCENES.items() if k[:2].isdigit() and int(k[:2])>=50}
    g['SCENES']={**newest,**{k:v for k,v in SCENES.items() if k not in newest}}
    g['EXTRA_COMPONENTS']=EXTRA_COMPONENTS
    dump('Source/text_flows.json',dict(version='1.3.0',font='Malgun Gothic',flows=prior+flows,policy='Lossless wrap and numbered pages; do not ellipsize required text'))
    dump('Source/activity_review_states.json',dict(version='1.0',fixtures=FIXTURES,simulated_only=True,read_only=True))
    dump('Source/activity_design_contract.json',dict(version='1.0',request_sections=['CQ-02A','CQ-02B','CQ-02C','CQ-02D','CQ-02E'],
        journal=dict(frame=[64,40,1152,720],list=[96,232,472,368],detail=[624,236,560,422],story_row=[472,86],story_stride=94,regional_row=[472,112],regional_stride=122,action_button_min_width=116,pager_button_min_width=78,list_pager=[392,629,166,34],body_pager=[944,629,212,34],page_label_baseline=651,close_y=697,body_min_px=18),
        tracking=dict(selection_does_not_change_participation=True,only_active_story_trackable=True,regional_only_accepted_current_planet=True,no_remote_accept_submit_reward=True),
        map=dict(tracked='cyan diamond + crosshair',user_destination='gold pin',preserve_user_pin=True,rotate_actor_only=True,north_fixed_clockwise_degrees=45,hide_unexplored=True,no_automatic_route_line=True),
        restore=dict(format=8,legacy_formats=list(range(1,8)),open_window_saved=False,pointer_saved=False,pressed_saved=False,derive_tab_and_page_from_activity=True,normal_restore_popup=False,invalid_reference='clear tracking only; keep progress and currency'),
        settlement=dict(credits_reward=100,full_credit_limit='defer full payment and keep records; no partial payment',complete_only_after_payment=True,no_reward_during_render=True),
        completion=dict(common_predicate='StoryMissionProgress::CanComplete',requires_participation_and_facts=True,explicit_final_dialogue=True,rejected_final_dialogue_resumable=True,duplicate_reward=False,debug_completion_ready_in_hud=False),
        integration=False,samples_are_not_new_game_content=True))
    api.DeleteObject.argtypes=[wintypes.HANDLE];api.DeleteDC.argtypes=[wintypes.HDC]
    for f in fonts.values():api.DeleteObject(f)
    api.DeleteDC(dc)
