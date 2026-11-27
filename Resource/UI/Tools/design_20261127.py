"""UI-26 to UI-30 components and editable review scenes."""
import math
from part3_map_preview import make_preview

ICONS = {
    "interact_talk": '<path d="M3 4h18v12H11l-5 4v-4H3zM7 9h10m-10 3h6"/>',
    "interact_hack": '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m-4 6h4m12-6h4m-4 6h4m-12-3 2 2 3-4"/>',
    "interact_salvage": '<path d="M3 8l9-5 9 5v10l-9 4-9-4zM3 8l9 5 9-5m-9 5v9M8 6l8 5"/>',
    "interact_door": '<path d="M4 21V3h14v18M4 3l9 3v15H3m6-8h1m7 0h5m-2-2 2 2-2 2"/>',
    "interact_vehicle": '<path d="M4 12l3-6h10l3 6v6H4zm0 0h16m-13 3h2m6 0h2M6 18v2m12-2v2"/>',
    "interact_quest": '<path d="M5 22V3h14l-3 5 3 5H5m6-7v2m0 2h.01"/>',
    "map_planet_view": '<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18"/>',
    "map_space_view": '<circle cx="12" cy="12" r="2"/><ellipse cx="12" cy="12" rx="10" ry="5" transform="rotate(-35 12 12)"/><path d="M18 4v4m-2-2h4"/>',
    "map_cluster_subquest": '<path d="M19 9c0 5-7 12-7 12S5 14 5 9a7 7 0 0 1 14 0zM12 6v6M9 9h6"/>',
}


def apply(g, component):
    C = g["C"]
    scene, text, icon, line, rect, row, panel, key, backdrop = (
        g[name] for name in ("scene","text","icon","line","rect","row","panel","key","backdrop")
    )
    g["ICONS"].update(ICONS)
    parts = {}
    inset12 = [12] * 4
    ink, cyan, muted, gold = C["ink"], C["accent"], C["muted"], C["amber"]
    slate = "#0b1820"
    map_previews, map_budget = make_preview(g["ROOT"])

    def part(name, w, h, body, inset=None, role="fixed_aspect"):
        parts[name] = (w, h, body)
        component(name, body, w, h, inset, role)

    def border(w, h, radius=8, color="#79b5bd"):
        return rect(.5,.5,w-1,h-1,"none",radius,extra=f'stroke="{color}"')

    part("world_prompt_fill",96,40,rect(.5,.5,95,39,slate,8),inset12,"background_opacity")
    part("world_prompt_edge",96,40,border(96,40),inset12,"border_opacity")
    part("world_prompt_pill",96,40,rect(.5,.5,95,39,slate,8,extra='fill-opacity=".88"')+border(96,40)+
         rect(4,7,4,26,cyan),inset12,"combined_fallback; layered fill/edge plus runtime category stripe preferred")
    # Static E is vector geometry. The key remains readable without a font dependency.
    for active in (False,True):
        body=rect(1,1,30,30,"#24464d" if active else "#142831",6,extra=f'stroke="{cyan if active else "#69858b"}"')
        body+=line(7,26,25,26,"#071016",2)
        body+='<path d="M21 9H12v14h9v-2h-7v-4h6v-2h-6v-4h7z" fill="#f4f9f5"/>'
        part("world_keycap_e"+("_active" if active else ""),32,32,body,role="fixed_E_vector")

    focus='<ellipse cx="64" cy="32" rx="56" ry="23" fill="none" stroke="#08121a" stroke-width="4"/>'
    focus+='<ellipse cx="64" cy="32" rx="56" ry="23" fill="none" stroke="#b4eee1" stroke-width="1.5" stroke-dasharray="37 9"/>'
    focus+='<path d="M64 2v9m0 42v9M1 32h10m106 0h10" fill="none" stroke="#b4eee1" stroke-width="2"/>'
    part("world_focus_ring",128,64,focus,role="project_to_ground; transparent center")
    part("holo_wrist_emitter",64,64,
         '<circle cx="32" cy="32" r="25" fill="none" stroke="#a6e6cf" stroke-width="2" stroke-dasharray="31 8"/><circle cx="32" cy="32" r="17" fill="none" stroke="#4d858c"/><path d="M32 3v9m0 40v9M3 32h9m40 0h9" stroke="#a6e6cf" stroke-width="2"/>')

    ticks = '<circle cx="256" cy="256" r="217" fill="none" stroke="#4f777e"/><circle cx="256" cy="256" r="238" fill="none" stroke="#7dbcb8" stroke-width="1.5"/>'
    for i in range(72):
        ticks += f'<path d="M256 {10 if i%6==0 else 16}v{18 if i%6==0 else 7}" stroke="{cyan if i%6==0 else "#517e83"}" transform="rotate({i*5} 256 256)"/>'
    ticks += '<path d="M8 224v64m496-64v64M5 240h7m-10 16h14m-11 16h7m488-32h7m-10 16h14m-11 16h7" stroke="#a6e6cf" fill="none" stroke-width="2"/>'
    part("globe_bezel_ring",512,512,ticks)

    part("showcase_card_frame",128,128,border(128,128),inset12,"frame; live stage and separate title/price zones")
    turntable='<ellipse cx="128" cy="48" rx="114" ry="35" fill="none" stroke="#355c66" stroke-width="1.5"/>'
    turntable+='<path d="M34 35c56-24 142-23 188 0M35 63c53 23 137 25 184-1m-10-6 13 5-7 11M47 28 34 35l13 5" fill="none" stroke="#a6e6cf" stroke-width="2"/>'
    part("wardrobe_turntable_ring",256,96,turntable)
    for enabled in (True,False):
        part("part_chip_on" if enabled else "part_chip_off",48,32,rect(0,0,48,32,"#254650" if enabled else "#152832",5)+
             border(48,32,5,cyan if enabled else "#586f79"),[8]*4,"toggle; state mark and label are separate")

    grid=rect(0,0,256,128,"#0d202a",5)+border(256,128,5)
    grid+="".join(line(x,4,x,124,"#24414b") for x in range(16,256,16))
    grid+="".join(line(4,y,252,y,"#385e68" if y==64 else "#24414b") for y in range(16,128,16))
    part("minigame_oscilloscope_grid",256,128,grid,role="static grid; waveforms drawn live")
    rings=""
    for r in (44,76,108):
        rings+=f'<circle cx="128" cy="128" r="{r}" fill="none" stroke="#527983" stroke-width="2"/>'
        rings+="".join(f'<path d="M128 {123-r}v10" stroke="#8eb6b9" stroke-width="2" transform="rotate({i*45} 128 128)"/>' for i in range(8))
    part("minigame_cipher_ring",256,256,rings,role="dials; animate individual radii and key marks separately")
    track=rect(.5,.5,255,31,"#122d3b",6)+border(256,32,6)
    track+="".join(line(x,21,x,27,"#587681") for x in range(16,256,16))
    part("minigame_timing_track",256,32,track,role="track; hit zone center/width supplied by gameplay")
    part("minigame_timing_target",24,32,rect(0,2,24,28,"#b98741",3)+border(24,32,3,gold)+
         '<path d="M12 1v30" stroke="#fff0cc" stroke-width="2"/>',role="gold perfect-zone overlay; fit actual zone")
    part("minigame_circuit_node",48,48,
         '<path d="M8 8h32v24L24 44 8 32z" fill="#173742" stroke="#a6e6cf" stroke-width="2"/><path d="M24 12v21m-5-15h10" stroke="#e8f5f0" stroke-width="2"/>')

    def sprite(name,x,y,w=None,h=None,cls=""):
        sw,sh,body=parts[name]
        return (f'<svg x="{x}" y="{y}" width="{w or sw}" height="{h or sh}" '
                f'viewBox="0 0 {sw} {sh}" preserveAspectRatio="none" class="{cls}">{body}</svg>')

    def sliced(name,x,y,w,h,edge,cls=""):
        sw,sh,body=parts[name]
        result=f'<g class="{cls}">'
        source_x,source_y=(0,edge,sw-edge,sw),(0,edge,sh-edge,sh)
        dest_x,dest_y=(x,x+edge,x+w-edge,x+w),(y,y+edge,y+h-edge,y+h)
        for j in range(3):
            for i in range(3):
                sx,sy=source_x[i],source_y[j]
                vw,vh=source_x[i+1]-sx,source_y[j+1]-sy
                result+=(f'<svg x="{dest_x[i]}" y="{dest_y[j]}" width="{dest_x[i+1]-dest_x[i]}" '
                         f'height="{dest_y[j+1]-dest_y[j]}" viewBox="{sx} {sy} {vw} {vh}" '
                         f'preserveAspectRatio="none" overflow="hidden">{body}</svg>')
        return result+"</g>"

    def box(x,y,w,h,fill=slate):
        return (f'<g class="panel-fill">{rect(x,y,w,h,fill,8)}</g>'
                f'<g class="panel-border">{rect(x+.5,y+.5,w-1,h-1,"none",8,extra="stroke=\"#79b5bd\"")}</g>')

    def review(name,title,ids,body):
        scene(name,title,ids,body)

    flows=g["json"].loads((g["ROOT"]/"Source/text_flows.json").read_text(encoding="utf8"))

    def annotate(rows,x,y,w):
        flows["flows"].append(dict(source="".join(rows),lines=rows,lossless=True,ellipsis=False,
                                   width=w,size=16,weight=400,role="part3_ui"))
        return "".join(text(s,x,y+i*27,16,muted,width=w) for i,s in enumerate(rows))

    def interaction(label,category,kind,active=True):
        b=backdrop(kind)+g["vitals"](show_ammo=False)+g["minimap"]("mine" if kind=="mine" else "city")
        if not active:
            return b+icon(category,630,383,20,muted)
        b+=sprite("world_focus_ring",602,426,86,43,"ui-icon")
        labels=label if isinstance(label,list) else [label]
        h=56+27*(len(labels)-1)
        b+=sliced("world_prompt_fill",414,486,452,h,12,"panel-fill")
        b+=sliced("world_prompt_edge",414,486,452,h,12,"panel-border")
        b+=rect(421,499,4,h-26,cyan if category!="interact_quest" else gold,2,extra='class="ui-icon"')
        b+=icon(category,438,502,23)
        b+=annotate(labels,476,520,314)
        return b+sprite("world_keycap_e_active",815,498,cls="ui-icon")

    fixtures=[
        ("98_prompt_talk","대화",["리아에게 말 걸기"],"interact_talk","bright",True),
        ("99_prompt_terminal","단말 조사",["비상 배전반 점검"],"interact_hack","mine",True),
        ("100_prompt_salvage","회수",["프리즘 광맥 채굴"],"interact_salvage","mine",True),
        ("101_prompt_door","출입",["거주 구역으로 들어가기"],"interact_door","bright",True),
        ("102_prompt_vehicle","탑승",["공용 비행체 탑승"],"interact_vehicle","bright",True),
        ("103_prompt_quest","긴 임무명",["북동 잔해 고압 격벽에서 ", "차단 회로를 점검하기"],"interact_quest","mine",True),
        ("104_prompt_outside_range","범위 밖",[""],"interact_talk","bright",False),
    ]
    for name,title,label,category,kind,active in fixtures:
        review(name,"현장 안내 / "+title,["UI-07","UI-26"],
               lambda l=label,c=category,k=kind,a=active:interaction(l,c,k,a))

    def map_view(space=False):
        b=backdrop("mine")+panel(56,40,1168,720)
        b+=text("에르마 성계" if space else "에르마 · 행성 지도",94,111,30,weight=700)
        b+=icon("map_planet_view" if space else "map_space_view",906,83,28)
        b+=key("G",944,84,40)+text("행성 지도" if space else "성계 지도",997,104,16,width=160)
        if space:
            b+=box(94,153,696,500,"#0b1427")
            b+=f'<image x="96" y="155" width="692" height="496" preserveAspectRatio="none" href="{map_previews["nebula"]}" opacity=".65"/>'
            for a in (100,202,298):
                b+=f'<ellipse cx="435" cy="407" rx="{a}" ry="{a*.60}" fill="none" stroke="#3c586e"/>'
            b+=f'<circle cx="435" cy="407" r="22" fill="{gold}"/>'
            b+=text("항성",462,413,14,width=110)
            for x,y,r,texture,label in [(558,380,18,"Erma","에르마"),(602,520,14,"Seira","세이라"),(238,299,19,"Orren","오르엔")]:
                b+=f'<image x="{x-r}" y="{y-r}" width="{r*2}" height="{r*2}" href="{map_previews[texture]}"/>'
                b+=text(label,x+27,y+6,14,width=110)
        else:
            b+=sprite("globe_bezel_ring",148,135,512,512,"ui-icon")
            b+='<defs><clipPath id="planetClip"><circle cx="404" cy="391" r="208"/></clipPath></defs>'
            b+=f'<image x="196" y="183" width="416" height="416" href="{map_previews["erma"]}"/>'
            b+='<g clip-path="url(#planetClip)"><path d="M200 348h410m-420 99h430M398 182c-158 88-158 322 0 417M406 182c158 88 158 322 0 417" fill="none" stroke="#86b5ae" stroke-opacity=".39"/></g>'
            b+=icon("map_cluster_subquest",452,325,28,gold)+icon("destination",322,435,25,ink)
            b+=sprite("holo_wrist_emitter",117,583,48,48,"ui-icon")
        b+=text("선택한 행성" if space else "지도 표식",839,214,21,weight=700)
        if space:
            b+=text("에르마",839,267,26,cyan,700)
            b+=annotate(["현재 탐사 중인 행성입니다."],839,317,320)
        else:
            b+=icon("map_cluster_subquest",841,249,25,gold)+text("발견한 지역 의뢰",880,270,15,width=264)
            b+=icon("destination",841,300,25,ink)+text("지정 목적지",880,321,15,width=264)
        b+=text("드래그 회전 · 휠 확대 및 축소",96,699,14,muted,width=610)
        b+=text("M 닫기",1170,699,14,muted,anchor="end")
        return b

    review("105_holo_planet","지도 / 손목 투사 행성",["UI-27"],map_view)
    review("106_holo_space","지도 / 성계 전환",["UI-27"],lambda:map_view(True))

    def showroom(mode):
        b=backdrop("mine")+panel(56,40,1168,720)
        title={"wardrobe":"3D 외관 옷장","stash":"거처 보관함","shop":"시설 상점"}[mode]
        b+=text(title,95,111,30,weight=700)
        if mode=="wardrobe":
            b+=box(96,148,486,510)
            b+=sprite("wardrobe_turntable_ring",197,531,278,103,"ui-icon")
            b+='<g fill="#688891" stroke="#b4ced0" stroke-width="1"><path d="M314 246h44l8 51-12 20h-36l-12-20zM293 324l44-14 44 14 17 125-27 8-12-68v73h-45v-73l-15 68-23-8zM313 465h48l13 83h-28l-10-57-10 57h-28z"/></g>'
            b+=text("드래그 / A·D 회전",339,637,14,muted,anchor="middle")
            b+=text("장착 부위",666,180,21,weight=700)
            labels=["메딕 코트","카고 팬츠","전술 부츠","번 헤어","어깨 가드","무릎 가드",
                    "왼팔 스캐너","오른팔 가드","의료 파우치","구조 배낭","탄창 랙"]
            for index,label in enumerate(labels):
                x=666+250*(index%2); y=204+69*(index//2); on=index not in (4,8)
                b+=sliced("part_chip_on" if on else "part_chip_off",x,y,226,50,8,"panel-fill")
                b+=icon("check" if on else "slot_empty",x+12,y+14,20,cyan if on else muted)
                b+=text(label,x+44,y+31,14,width=96)
                b+=text("장착" if on else "해제",x+211,y+31,13,muted,anchor="end",width=54)
        elif mode=="shop":
            b+=box(95,151,480,487)
            b+='<g fill="#43646e" stroke="#84a8ad" stroke-width="2"><path d="M309 245h81l14 79-24 37h-63l-25-34zM247 414l71-51h63l76 51 38 149H218z"/></g>'
            b+=text("보급 상인",122,606,22,weight=700)
            b+=text("보유 크레딧 1,250",619,176,17,cyan,anchor="start",width=240)
            services=[("응급 회복 키트","보유 4개 · G 사용","40 Cr","heal",True),
                      ("전술 탄약 팩","총기 장착 시 탄약 보급 가능","25 Cr","magazine",False),
                      ("고철 부품 매각","정비 고철 1개 매각 · 보유 18개","+35 Cr","parts",True)]
            for i,(title,detail,amount,symbol,enabled) in enumerate(services):
                y=203+i*132
                b+=f'<g class="panel-fill">{rect(617,y,554,114,slate,8)}</g>'
                b+=sliced("showcase_card_frame",617,y,554,114,12,"panel-border")
                b+=rect(631,y+13,76,88,"#173541",6,extra='class="panel-fill"')
                b+=icon(symbol,651,y+36,36,cyan if enabled else muted)
                b+=text(title,729,y+44,18,ink if enabled else muted,700,width=289)
                b+=text(detail,729,y+78,14,muted,width=375)
                b+=text(amount,1148,y+44,17,gold if enabled else muted,600,"end",99)
            b+=text("Enter / 클릭 / 숫자키로 구매 및 이용",618,646,14,muted,width=550)
        else:
            tuples=[("크레딧","보관 1,250","예치","인출"),("회복 키트","보관 4개","보관","인출"),("회수 부품","보관 18개","전량 보관","전량 인출")]
            for i,(name,amount,primary,secondary) in enumerate(tuples):
                x=95+i*371
                b+=f'<g class="panel-fill">{rect(x,153,348,440,slate,8)}</g>'
                b+=sliced("showcase_card_frame",x,153,348,440,12,"panel-border")
                b+=rect(x+18,171,312,49,"#1b3440",5,extra='class="panel-fill"')
                b+=rect(x+18,456,312,54,"#1b3440",5,extra='class="panel-fill"')
                b+=text(name,x+34,204,20,weight=700,width=288)
                if name=="회복 키트":
                    b+=f'<g transform="translate({x+105} 315)"><path d="M10 17h123v81H10zM47 17V4h50v13" fill="#294e57" stroke="#79b5bd" stroke-width="3"/><path d="M72 42v30m-15-15h30" stroke="#d6ece7" stroke-width="5"/></g>'
                elif name=="크레딧":
                    b+=f'<g transform="translate({x+104} 315)" fill="#3c5559" stroke="#a9c1b9" stroke-width="3"><path d="M0 25 94 0l44 34-94 28zM0 25v21l44 36 94-27V34"/><path d="M24 62v19l44 30 71-23V75" fill="none"/></g>'
                else:
                    b+=f'<path d="M{x+98} 341l75-39 75 39-75 41z M{x+98} 341v56l75 40v-55m75-41v56l-75 40" stroke="#6eaaae" stroke-width="3" fill="#294e57"/>'
                b+=text(amount,x+174,491,18,cyan,600,"middle",290)
                b+=row(primary,x+18,524,152,ico="check")
                b+=row(secondary,x+178,524,152,ico="check")
        b+=text("Esc 닫기",1170,710,14,muted,anchor="end")
        return b

    for number,name,title in [(107,"wardrobe","외관 옷장"),(108,"stash","보관함"),(109,"shop","시설 상점")]:
        review(f"{number}_showcase_{name}","진열 UI / "+title,["UI-28"],lambda n=name:showroom(n))

    def console(mode):
        b=backdrop("mine")+panel(236,119,808,559)
        title={"signal":"탐사선 구조 신호 동조","cipher":"관측자 벽면 문양 해독","timing":"교두보 붕괴 잔해 해제","circuit":"비상 배전반 회로 우회"}[mode]
        b+=text(title,274,187,28,weight=700,width=710)
        if mode=="signal":
            b+=sprite("minigame_oscilloscope_grid",276,234,722,198,"ui-icon")
            for freq,phase,color,dash in [(2.4,.2,gold,'stroke-dasharray="8 7"'),(2.3,.8,cyan,"")]:
                coords=" ".join(f"{294+i*6.85:.2f},{333-62*math.sin(i/100*math.tau*freq+phase):.2f}" for i in range(101))
                b+=f'<polyline points="{coords}" fill="none" stroke="{color}" stroke-width="2.5" {dash}/>'
            b+=text("동조율 74%",277,478,20,cyan,700)
            b+=text("기준 파형 · 점선",996,478,15,gold,anchor="end")
            hint="A/D 주파수 · W/S 위상"
        elif mode=="cipher":
            b+=sprite("minigame_cipher_ring",490,208,300,300,"ui-icon")
            b+=f'<path d="M640 269a89 89 0 0 1 89 89" stroke="{cyan}" stroke-width="3" fill="none"/>'
            b+=f'<path d="M640 211v22m86 125h18m-144 40-12 12" stroke="{gold}" stroke-width="4" fill="none"/>'
            b+=text("선택 고리 2 / 3",853,312,16,cyan,width=174)
            hint="W/S 고리 선택 · A/D 회전"
        elif mode=="timing":
            b+=sprite("minigame_timing_track",277,302,721,46,"ui-icon")
            b+=sprite("minigame_timing_target",601,302,61,46,"ui-icon")
            b+=line(647,292,647,357,ink,4)
            b+=text("진행 2 / 4",277,423,20,cyan,700)+text("정밀 적중 1 · 오차 0",998,423,15,muted,anchor="end")
            hint="Space / 클릭 / E · 금색 구간에서 입력"
        else:
            for i,(label,val,target) in enumerate([("주전원",75,75),("안정기",40,55),("보조망",64,64)]):
                y=233+i*89
                b+=text(label,277,y,16,width=250)
                b+=text(f"{val}% / 목표 {target}%",998,y,14,cyan if val==target else gold,anchor="end")
                b+=rect(278,y+19,719,8,"#2e4956",4)+rect(278,y+19,719*val/100,8,cyan,4)
                b+=line(278+719*target/100,y+14,278+719*target/100,y+32,gold,3)
                b+=sprite("minigame_circuit_node",259+719*val/100,y+7,36,36,"ui-icon")
            hint="W/S 배선 선택 · A/D 출력 조절"
        b+=text(hint,276,553,16,width=720)
        b+=line(276,580,998,580,"#325461")
        b+=text("Space / Enter 확정",276,624,14,muted)
        b+=text("Tab 우회 · 보너스 없음",557,624,14,muted)
        b+=text("Esc 취소",998,624,14,muted,anchor="end")
        return b

    for number,mode,title in [(110,"signal","파형 동조"),(111,"cipher","문양 해독"),(112,"timing","정밀 추출"),(113,"circuit","회로 균형")]:
        review(f"{number}_console_{mode}","현장 콘솔 / "+title,["UI-29"],lambda m=mode:console(m))
    review("114_panel_seam_review","공용 패널 / 9분할 크기 비교",["UI-30"],
           lambda:backdrop("bright")+text("9분할 · 밝은 배경 테스트",96,109,31,weight=700)+
           sliced("world_prompt_pill",104,162,310,72,12)+sliced("world_prompt_pill",470,162,694,72,12)+sliced("world_prompt_pill",104,297,1060,126,12)+
           sliced("part_chip_on",104,495,273,54,8,"panel-fill")+sliced("part_chip_off",415,495,273,54,8,"panel-fill")+
           text("장착",130,531,16)+text("해제",440,531,16)+
           text("게임에서 사용할 잘림 경계를 좌표 그대로 고정합니다.",130,361,18,width=930))
    g["dump"]("Source/text_flows.json",flows)
    g["dump"]("Source/part3_map_art.json",dict(schema="erma.ui.part3.map_art",version=1,projected_preview_only=True,
                                              map_location_source="game; texture carries no topology or gameplay locations",maps=map_budget))
    g["SCENES"]={**{n:v for n,v in g["SCENES"].items() if int(n.split("_",1)[0])>=98},**g["SCENES"]}
    g["dump"]("Source/part3_ui.json",dict(schema="erma.ui.part3",version=1,
        requests=["UI-26","UI-27","UI-28","UI-29","UI-30"],
        layer_opacity={"fill":.88,"border":1,"content":1},keycap_glyph="fixed default E",
        limits_bytes={"single_texture":100000000,"atlas_png":4000000,"atlas_rgba8":16777216},
        slice_policy={"default":12,"part_chip_on":8,"part_chip_off":8,"padding":4,"alpha_zero_rgb_bleed":2},
        sprite_ids=[f"Components/{n}" for n in parts]+[f"Icons/{n}" for n in ICONS],
        runtime_integration="art resources; runtime layout wiring tracked separately"))
