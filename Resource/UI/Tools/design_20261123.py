"""Resource layouts for the current movement, vehicle and world interactions."""

HUD_ADDITIONS = {
    "stamina": '<path d="M13 2 5 13h6l-1 9 9-12h-6z"/>',
    "carbine": '<path d="M2 8h10l2-2h6v3h3v2h-8l-1 7h-3l-1-7H7l-2 5H2V8z"/>',
    "ground_car": '<path d="m4 11 3-5h10l3 5v6H4zM4 11h16M6 17v2m12-2v2M7 14h2m6 0h2"/>',
    "flight": '<path d="m12 2 2 7 8 6v3l-8-3v3l2 2v2l-4-2-4 2v-2l2-2v-3l-8 3v-3l8-6z"/>',
    "landing": '<path d="M12 2v13m-5-5 5 5 5-5M3 20h18M3 17v5m18-5v5"/>',
    "security": '<path d="M12 2 3 6v6c0 5 4 8 9 10 5-2 9-5 9-10V6zM12 7v6m0 4h.01"/>',
}
MENU_ADDITIONS = {
    "checkpoint": '<path d="M4 3h13l3 3v15H4zM8 3v7h8V3M8 21v-7h8v7"/>',
    "relay": '<circle cx="12" cy="12" r="8"/><path d="M12 2v5m0 10v5M2 12h5m10 0h5m-8-2h-4v4h4z"/>',
}


def apply_current_design(g):
    C = g["C"]
    scene, rect, line, text, panel, row, icon, key = (
        g[name] for name in ("scene", "rect", "line", "text", "panel", "row", "icon", "key")
    )
    backdrop, vitals, minimap, hud_text, hud_icon, hud_bar, reticle = (
        g[name] for name in ("backdrop", "vitals", "minimap", "hud_text", "hud_icon", "hud_bar", "reticle")
    )
    g["ICONS"].update(HUD_ADDITIONS)
    g["ICONS"].update(MENU_ADDITIONS)
    fixtures = {}
    flow_report = g["json"].loads((g["ROOT"] / "Source/text_flows.json").read_text(encoding="utf8"))

    def add(name, title, ids, body, **state):
        scene(name, title, ids, body)
        fixtures[name] = state

    def lines(source, rows, x, y, width, *, size=16, step=28, color=None):
        measured_source = "".join(rows)
        if measured_source != source:
            raise ValueError(f"Missing text in scene: {source}")
        flow_report["flows"].append(dict(source=source, lines=rows, width=width, size=size,
            weight=400, role="current_UI_body", lossless=True, ellipsis=False))
        return "".join(text(part, x, y + i * step, size, color, width=width)
                       for i, part in enumerate(rows))

    def movement(value, state, y=772):
        amber = value < 30
        tint = C["amber"] if amber else C["accent"]
        b = '<g data-hud="stamina">'
        b += hud_icon("stamina", 32, y - 10, 16, tint)
        b += hud_bar(66, y, 220, value / 100, tint, 6)
        b += line(132, y - 2, 132, y + 8, "#02080d", 2)
        if state:
            b += hud_text(state, 302, y + 7, 12, tint, 600, width=300)
        return b + "</g>"

    def mobility_case(kind, value, state):
        return backdrop(kind) + minimap("mine" if kind == "mine" else "city") + vitals(show_ammo=False) + movement(value, state)

    add("71_stamina_sprint", "도보 HUD / 달리기", ["UI-01", "UI-23"],
        lambda: mobility_case("bright", 62, ""), stamina=62, sprinting=True)
    add("72_stamina_low", "도보 HUD / 회피 불가", ["UI-01", "UI-23"],
        lambda: mobility_case("mine", 21, "회피에 필요한 스태미너 부족"), stamina=21, dodge_available=False)
    add("73_stamina_dodge", "도보 HUD / 회피 중 공격 제한", ["UI-01", "UI-03"],
        lambda: mobility_case("mine", 45, "회피 중 · 공격 제한"), stamina=45, dodging=True)

    def gun_hud(rounds, focus=False, reload=None):
        b = backdrop("mine") + minimap("mine") + vitals(show_ammo=False)
        b += movement(46 if focus else 78, "")
        b += '<g data-hud="carbine">'
        b += hud_icon("carbine", 1040, 722, 28)
        b += hud_text("펄스 카빈", 1040, 699, 14, width=120)
        b += hud_text(f"{rounds:02}", 1178, 744, 38, C["ink"] if rounds else C["amber"], anchor="end")
        b += hud_text("/ 24", 1248, 743, 22, anchor="end")
        b += hud_icon("infinite", 1056, 754, 16)
        if reload is None:
            for i in range(24):
                b += rect(1090 + i * 6.45, 756, 4.7, 10, C["ink"] if i < rounds else "#233c46", 1,
                          extra='stroke="#02080d" stroke-width="1"')
        else:
            b += hud_bar(1090, 756, 153, reload, C["amber"], 9)
        if focus:
            b += hud_text("전술 집중", 1248, 671, 14, C["accent"], anchor="end", width=170)
        if reload is not None or not rounds:
            b += hud_text("재장전 중" if reload is not None else "R 재장전", 1248, 699, 14, C["amber"], anchor="end", width=88)
        return b + "</g>" + reticle("blocked" if reload is not None or rounds == 0 else "ready", 18)

    add("74_carbine_focus", "펄스 카빈 / 전술 집중", ["UI-02", "UI-03"],
        lambda: gun_hud(18, True), rounds=18, magazine=24, focus=True)
    add("75_carbine_reload", "펄스 카빈 / 재장전", ["UI-02", "UI-03"],
        lambda: gun_hud(0, reload=.58), rounds=0, magazine=24, reload_progress=.58)
    add("76_carbine_empty", "펄스 카빈 / 빈 탄창", ["UI-02", "UI-03"],
        lambda: gun_hud(0), rounds=0, magazine=24)
    add("77_carbine_wheel", "무기 선택 / 주 무장 후보", ["UI-24"],
        lambda: g["current_weapon_wheel"](True, "empty", "pistol", primary=True), primary_owned=True)
    add("78_carbine_locked", "무기 선택 / 주 무장 해금 전", ["UI-24"],
        lambda: g["current_weapon_wheel"](True, "empty", "pistol"), primary_owned=False)

    def transport(mode, speed, condition, hp, max_hp, altitude=None, prompt=None):
        b = backdrop("bright") + minimap() + vitals(show_ammo=False)
        vehicle = "ground_car" if mode == "car" else "flight"
        action_color = C["amber"] if condition in ("착륙 불가", "착륙 중") else C["accent"]
        b += '<g data-hud="transport">'
        b += hud_icon(vehicle, 1022, 730, 22)
        b += hud_text(speed, 1172, 749, 42, anchor="end", width=120)
        b += hud_text("km/h", 1248, 747, 16, anchor="end", width=68)
        if altitude is not None and condition != "순항":
            b += hud_icon("landing", 1054, 654, 20, action_color)
        altitude_label = f"고도 {altitude} m  ·  " if altitude is not None else ""
        b += hud_text(altitude_label + condition, 1248, 671, 14, action_color, anchor="end", width=198)
        b += hud_text(f"내구도 {hp} / {max_hp}", 1248, 700, 13, anchor="end", width=215)
        b += hud_bar(1060, 709, 188, hp / max_hp, C["amber"] if hp / max_hp < .3 else C["accent"], 5)
        if prompt:
            b += hud_text(prompt, 1248, 784, 13, C["muted"], 500, "end", 310)
        return b + "</g>"

    add("79_car_reverse", "공용차 / 후진과 내구도", ["UI-13"],
        lambda: transport("car", "4.6", "후진", 106, 140), mounted="car", reverse=True)
    add("80_flight_cruise", "비행체 / 고고도 순항", ["UI-13", "UI-23"],
        lambda: transport("air", "504", "순항", 160, 160, 210), mounted="aircraft", cruise=True)
    add("81_flight_landing", "비행체 / 착륙 중", ["UI-07", "UI-13"],
        lambda: transport("air", "22", "착륙 중", 126, 160, 22, "E 착륙 취소"), landing_requested=True)
    add("82_flight_no_landing", "비행체 / 착륙 불가", ["UI-07", "UI-13"],
        lambda: transport("air", "44", "착륙 불가", 40, 160, 44), landing_available=False)
    add("83_flight_grounded", "비행체 / 착륙과 하차", ["UI-07", "UI-13"],
        lambda: transport("air", "0.0", "착륙", 126, 160, 0, "E 하차"), grounded=True)

    def security(level, heading, detail):
        b = backdrop("bright") + minimap() + vitals(show_ammo=False)
        b += '<g data-hud="security">'
        b += hud_icon("security", 526, 38, 21, C["amber"] if level else C["accent"])
        if level:
            b += hud_text("★" * level + "☆" * (5 - level), 640, 59, 24, C["amber"], anchor="middle", width=170)
            b += hud_bar(558, 71, 164, .2 if level == 1 else 1, C["amber"], 5)
        b += hud_text(heading, 640, 106, 15, C["amber"] if level else C["accent"], anchor="middle", width=390)
        b += hud_text(detail, 640, 134, 14, C["ink"], 500, "middle", 440)
        return b + "</g>"

    add("84_security_surrender", "수배 1단계 / 투항 조건 안내", ["UI-12", "UI-23"],
        lambda: security(1, "비무장 투항 가능", "지상에서 맨손으로 멈추고 순찰의 확인을 기다리세요."),
        wanted=1, surrender_eligible=True, requires_patrol_confirmation=True)
    add("85_security_interceptor", "수배 5단계 / 대공 요격 경고", ["UI-12", "UI-15"],
        lambda: security(5, "대공 요격 경계", "보안망 추적 중"), wanted=5, airborne=True)
    add("86_security_paid", "투항 확인 / 실제 벌금 정산", ["UI-12", "UI-15"],
        lambda: security(0, "비무장 투항 확인 · 수배 해제", "벌금 100 크레딧 정산"), wanted=0, fine=100)

    def relay_map(state):
        b = backdrop() + panel(56, 40, 1168, 720)
        b += text("행성 개관 · 에르마", 90, 108, 30, weight=700)
        b += text("휠 확대·축소  ·  C 내 위치  ·  P 행성 전체  ·  0 초기화", 90, 139, 14, C["muted"], width=670)
        b += '<defs><clipPath id="planet-demo"><circle cx="405" cy="416" r="240"/></clipPath></defs>'
        b += '<circle cx="405" cy="416" r="240" fill="#192c35" stroke="#628288" stroke-width="2"/>'
        b += '<g clip-path="url(#planet-demo)"><path fill="#395c5e" d="M210 161 390 180l32 81 118 44 88 97-131 39-20 105-140 16-79-60-75 45-42-166z"/><path fill="#28454a" d="m505 446 112-34 50 86-71 78-98-38z"/>'
        b += '<path d="M162 384h480m-480 108h480M350 172q-140 244 0 488M462 172q140 244 0 488" fill="none" stroke="#90b1ae" stroke-opacity=".24"/></g>'
        b += icon("player", 311, 412, 24, C["ink"]) + icon("destination", 493, 518, 23, C["amber"])
        b += icon("relay", 461, 310, 28, C["accent"]) + icon("relay", 256, 371, 22, C["muted"])
        b += text("N ↗", 596, 209, 18, C["ink"], 600)
        b += text("등록 중계환", 789, 194, 23, weight=700)
        b += text("L 필터  ·  발견 / 등록", 789, 224, 14, C["muted"], width=365)
        for i, city in enumerate(("헤일로 · 07", "외곽 주거 구역 · 12", "개척지 지원 구역 · 08")):
            b += row(city, 788, 244+i*54, 384, "disabled" if state=="prep" else "selected" if i==1 else "default", ico="relay")
        b += text("목록 1 / 2  ·  [ 이전   ] 다음" if state!="prep" else "목적지 준비 중", 788, 435, 14, C["muted"], width=384)
        b += row("중계 이동 취소 · X / Esc" if state=="prep" else "선택한 중계환으로 이동 · T", 788, 454, 384,
                 "disabled" if state=="blocked" else "warning" if state=="prep" else "selected", ico="relay")
        if state=="blocked":
            parts=["차량을 주차하고 내려 주세요. ", "차량은 출발지에 남습니다."]
        elif state=="prep":
            parts=["목적지 지형을 준비하고 있습니다. ", "취소하면 현재 위치에서 계속합니다."]
        else:
            parts=["현장 E 등록 · 탑승 중이면 주차 후 이동. ", "차량은 출발지에 남습니다."]
        b += lines("".join(parts), parts, 788, 541, 370, color=C["amber"] if state=="blocked" else C["muted"])
        b += text("청록 · 선택 중계환", 90, 690, 14, C["accent"], width=230)
        b += text("금색 핀 · 지정 목적지", 330, 690, 14, C["amber"], width=260)
        b += text("좌클릭 선택 · 우클릭 해제 · M 닫기" if state!="prep" else "준비가 끝날 때까지 다른 이동 입력이 잠깁니다.",
                  1170, 711, 14, C["muted"], anchor="end", width=540)
        return b

    for number, state, name in [(87, "ready", "목적지 선택"), (88, "blocked", "차량 탑승 중 제한"), (89, "prep", "목적지 준비 및 취소")]:
        add(f"{number}_relay_{state}", "행성 지도 / " + name, ["UI-06", "UI-15"],
            lambda state=state: relay_map(state), relay_state=state, route_line=False, fictional_layout=True)

    def checkpoint(state):
        b = backdrop() + panel(220, 90, 840, 620)
        b += icon("checkpoint", 258, 125, 34, C["accent"])
        b += text("월드 저장 · 불러오기", 312, 154, 28, weight=700)
        if state=="loading":
            b += text("저장된 월드를 준비하고 있습니다.", 264, 236, 23, weight=600)
            b += text("완료되면 저장된 시점부터 이어집니다.", 264, 279, 16, C["muted"], width=735)
            b += row("불러오기 취소 · Esc", 264, 350, 370, "warning", ico="exit")
        else:
            b += row("현재 월드 저장 · F5", 264, 204, 380, "disabled" if state=="restricted" else "selected", ico="checkpoint")
            b += row("저장된 월드 불러오기", 264, 262, 380, "disabled" if state=="empty" else "default", ico="activity_restore")
            if state=="empty":
                b += text("저장된 월드가 없습니다.", 264, 358, 18, C["muted"], width=735)
            elif state=="restricted":
                parts=["비행체를 착륙시키고 내린 뒤 저장하거나 ", "행성을 선택해 주세요."]
                b += lines("".join(parts), parts, 264, 358, 660, color=C["amber"])
            else:
                b += text("불러오면 마지막 저장 이후의 진행을 되돌립니다.", 264, 358, 18, C["muted"], width=735)
            b += text("저장 위치", 264, 468, 14, C["accent"], 600)
            parts=["%LOCALAPPDATA%\\GameSoftware\\Saves\\world.sav"]
            b += lines(parts[0], parts, 264, 501, 735, size=15, color=C["muted"])
            b += row("뒤로 · Esc", 264, 619, 224, "default", ico="exit")
        return b

    for number, state, name in [(90, "ready", "현재 월드"), (91, "empty", "저장 없음"),
                                (92, "restricted", "저장 제한"), (93, "loading", "불러오기 및 취소")]:
        add(f"{number}_checkpoint_{state}", "저장 / " + name, ["UI-17", "UI-18", "UI-22"],
            lambda state=state: checkpoint(state), checkpoint_state=state, slots=1)

    def profiles():
        b = backdrop() + panel(220, 90, 840, 620)
        b += icon("relay", 264, 125, 30, C["accent"])
        b += text("행성 선택 · 탐사 테스트", 312, 154, 28, weight=700)
        for i, (name, state) in enumerate((("에르마 · 현재 행성","disabled"), ("세이라","selected"), ("오르엔","default"))):
            b += row(name, 264, 207+i*65, 430, state, ico="relay")
        b += text("행성별 탐험 기록은 유지됩니다.", 264, 458, 18, C["accent"], width=745)
        b += text("지상에서 하차한 뒤 선택할 수 있습니다.", 264, 500, 16, C["muted"], width=745)
        b += row("뒤로 · Esc", 264, 619, 224, ico="exit")
        return b

    add("94_planet_profiles", "행성 선택 / 탐사 테스트", ["UI-17", "UI-22"], profiles,
        current_profile="erma", candidate="seira", distinct_from_relay_travel=True)
    add("95_flight_landing_ready", "비행체 / 착륙 가능", ["UI-07", "UI-13"],
        lambda: transport("air", "16", "착륙 가능", 138, 160, 9, "E 착륙"), landing_available=True)

    def crash_notice():
        b = backdrop("bright") + minimap() + vitals(hp=22, show_ammo=False)
        b += '<g data-hud="crash-notice">'
        b += hud_icon("warning", 909, 303, 24, C["amber"])
        b += hud_text("비행체 추락 폭발", 945, 323, 18, C["amber"], width=300)
        b += hud_text("주변 충격 피해가 발생했습니다.", 945, 353, 14, C["ink"], width=300)
        return b + "</g>"

    add("96_aircraft_crash_notice", "비행체 / 추락 경고", ["UI-13", "UI-15"], crash_notice,
        crashed=True, display="one common notice or existing medical transition")

    def flight_help():
        b = backdrop("bright") + panel(174, 90, 932, 620)
        b += text("공용 비행체 조작", 216, 154, 30, weight=700)
        for i, (binding, action) in enumerate((
            ("W / S","전진 / 감속"), ("A / D","조향"), ("Space / C","상승 / 하강"),
            ("Shift + W","고고도 순항"), ("X","제동"), ("E","착륙 / 착륙 취소 / 하차")
        )):
            x, y = 216+(i%2)*432, 222+(i//2)*92
            b += key(binding,x,y,117) + text(action,x+135,y+20,15,width=258)
            b += line(x,y+45,x+393,y+45,"#2d464e")
        b += text("착륙 가능 표시가 뜨면 E로 착륙할 수 있습니다.", 216, 548, 16, C["muted"], width=815)
        b += row("뒤로 · Esc", 216, 619, 224, ico="exit")
        return b

    add("97_flight_controls", "조작 안내 / 공용 비행체", ["UI-23"], flight_help, context="aircraft")

    g["dump"]("Source/text_flows.json", flow_report)
    g["dump"]("Source/current_play_ui.json", dict(schema="erma.ui.current_play", version=1,
        basis="GAME_DOCUMENTATION 42-52 and current game source",
        baseline_canvas=[1280, 800], native_integration="pending",
        dimensions=dict(stamina=[66,772,220,6], vehicle_durability=[1060,709,188,5],
                        relay_panel=[56,40,1168,720], checkpoint_panel=[220,90,840,620]),
        mapping=dict(stamina="PlayerMobility; threshold = DodgeCost / MaxStamina",
                     transport="PlayerAircraft, PlayerCar; height above local ground",
                     armed="Loadout.HasPrimary, Game.ActiveFirearm",
                     law="CivicJustice + SecurityResponse actual eligibility and timers",
                     relay="RelayTravel.Restriction + GameState.relayPreparing",
                     checkpoint="SaveSystem.Restriction, Available, Loading and Message"),
        fixtures=fixtures))
    g["SCENES"] = {**{name: g["SCENES"][name] for name in fixtures}, **g["SCENES"]}
