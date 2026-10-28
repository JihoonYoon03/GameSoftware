"""Build UI state/binding handoff and reusable layout slots. No game code changes."""
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parents[1]
def save(name,data): (ROOT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def entry(name,states,bindings,icons,rules=()):
    return dict(name=name,states=states.split('|'),bindings=bindings.split('|'),icons=icons.split('|'),rules=list(rules))

UI={
'UI-01':entry('플레이어 상태','normal|critical|healing|growth_summary','health|max_health|healing_applied|level|experience','health|heal|level', ['Show current then maximum. Never reverse their positions.','Show actual applied healing, not the requested amount.']),
'UI-02':entry('무기 · 전투','loaded|empty|reloading|reload_complete','weapon_name|rounds|magazine_size|reload_progress|reload_remaining','pistol|magazine|reload|infinite',['Spare magazines are unlimited. No finite reserve counter.','Reload remaining must come from game data, not a preview timer.']),
'UI-03':entry('조준','ready|spread_increasing|spread_recovering|fire_blocked|menu_cursor','cursor_canvas_position|aim_angle|spread|can_fire','pistol|lock',['Expand four arms by current angular spread projected into screen space.','Hide the aiming reticle while menu cursor has focus.']),
'UI-04':entry('목표 추적','main_active|optional_active|optional_complete|site_unvisited|site_investigated|report_pending|comparison_pending|chapter_confirmed','main_objective|next_action|accepted_optional_objectives|generator_state|convoy_state|facility_state|report_state|comparison_state','objective|investigate|check',['Never show optional kill count as a mandatory rescue goal.','Chapter conclusion is confirmed only after final dialogue has been read.']),
'UI-05':entry('미니맵 · 목적지','city|mine|chapter|destination_inside|destination_outside|no_destination','map_context|explored_geometry|player_position|player_yaw|eligible_objectives|destination|distance','player|destination|objective|unknown_signal',['Map rotation is fixed 45 degrees clockwise: north northeast.','Rotate only player/bike. Clip every map primitive and icon to circle.','Exact destination arrow differs from the approximate rescue triangle.']),
'UI-06':entry('전체 지도','city|mine|chapter|destination_set|destination_clear|unexplored','map_context|explored_geometry|eligible_facilities|eligible_objectives|destination','home|medical|power|transport|investigation_site|gate|exit',['Left click sets destination in eligible explored space. Right click clears it. M/Esc closes.','No auto-route line. Do not reveal unexplored terrain or targets.']),
'UI-07':entry('상호작용 · 월드 표식','dialogue|investigate|power|collect|rescue|mount|dismount|enter|exit|supply|unavailable','target_name|available_action|interaction_key|disabled_reason|target_screen_position','dialogue|investigate|power|parts|rescue|bike|exit|supply|injured',['Show one highest-priority currently available action.','Do not show normal dialogue for an injured NPC.','World marker must respect visibility/quest eligibility.']),
'UI-08':entry('대화 · 수신 · 무전','local_safe|radio_danger|terminal|received_record|page_next|choice_default|choice_selected|choice_disabled|selection_blocked|medical_input_blocked','speaker|channel|body_pages|page_index|page_count|choices|selected_choice|can_confirm','dialogue|radio|terminal|record|lock',['Important dialogue never expires automatically.','E/Enter advances once per press; numbers select; E/Enter confirms.','Consume input before world interactions and shooting.','Local safe dialogue locks progression; danger radio does not stop combat time.']),
'UI-09':entry('조사 기록 · 이력','journal|history|empty|multiple_pages|interrupted_dialogue','known_facts|next_tasks|read_lines|speaker|channel|page_index|page_count|interruption_hint','record|history|empty',['Only known facts and read dialogue. Do not reveal unjoined companions.','N-0 gate and N-0/17 facility are distinct.','J/H toggles; PageUp/PageDown pages; Esc closes.']),
'UI-10':entry('스캐너','ready|scanning|cooldown|signal_found|no_signal','cooldown_seconds|scan_progress|signal_direction_approx|signal_distance_band|signal_lifetime','scan|signal|unknown_signal',['Approximate direction and distance only. No exact hidden map point.','Scan does not reveal terrain. Signal edge marker is triangular.']),
'UI-11':entry('동료 · 구조','joined|injured|treating|recovered|rescue_confirmed|withdrawing|safe_arrival','joined_companions|companion_condition|treatment_progress|rescue_context|confirmed_count|withdrawing_count|arrived_count','companion|injured|rescue|medical',['Separate repeatable mine rescues from fixed five convoy survivors.','Show only actually joined companions.']),
'UI-12':entry('수배 · 악명','wanted_0|wanted_1|wanted_2|wanted_3|wanted_4|wanted_5|heat_increasing|heat_cooling|notoriety|trade_surcharge','wanted_stars|heat|heat_max|notoriety|reputation|effective_price','wanted|reputation',['Wanted is current pursuit; notoriety persists between cities in the current session.','Do not imply persistent disk saving.','Filled/outline stars plus numeric level; heat threshold data supplied by game.']),
'UI-13':entry('바이크','mounted|cruise|boost|reverse|stopped|dismount_allowed|dismount_blocked','mounted|speed_world_units|meters_per_world_unit|boosting|reversing|dismount_reason','bike|speed|boost|reverse',['speed_kmh = abs(speed_world_units) * meters_per_world_unit * 3.6','Do not add fuel, battery or boost energy.','Reverse is labelled separately; very low speeds must not disappear when rounded.']),
'UI-14':entry('교통','walk|wait|collision_risk|collision_hit','nearby_crossing|actual_pedestrian_signal|vehicle_risk|applied_damage','walk|stop|warning',['Use the same actual signal state as world traffic.','Collision result reports actual applied health loss.']),
'UI-15':entry('공통 알림','acquired|healed|rewarded|trade_success|insufficient|objective_updated|region_entered|returned','notice_type|notice_text|applied_amount|priority|coalesce_key','parts|heal|credit|supply|warning|objective|gate|home',['Coalesce repeated battle notices into a single slot.','Do not overlay dialogue or important objective. Keep narrative dialogue indefinite.']),
'UI-16':entry('의료 이송','fading_out|in_transfer|arrived|actual_losses|resume_hint','hospital_name|transfer_progress|lost_levels|lost_credits|lost_parts|journal_retained|interrupted_dialogue','medical|level|credit|parts',['Common flow regardless of defeat cause. No game-over or load-save screen.','Losses are actual clamped values after application.','Block all gameplay input until transfer ends.']),
'UI-17':entry('일시정지','resume|city_restart|mine_restart|chapter_return|help|exit','current_region|restart_action|session_is_unsaved','pause|keyboard|exit',['Esc resumes. R means reset in city, mine restart in mine, return to Halo in chapter.','Do not expose developer region shortcuts 1/2.']),
'UI-18':entry('타이틀','new_game|options|controls|exit','session_state','gate|settings|keyboard|exit',['Design candidate only; no Continue or Load without permanent saves.']),
'UI-19':entry('옵션','panel_alpha|border_alpha|content_alpha|cartoon|render_scale|antialiasing','ui_panel_alpha|ui_border_alpha|ui_content_alpha|cartoon_shading|render_scale|msaa','settings',['Opacity is alpha, transparency is 1-alpha. Keep independent layers.','Text size/UI scale are proposals, not currently supported game features.']),
'UI-20':entry('보유 · 성장 요약','summary','level|experience|attack|defense|credits|parts|healing_kits','level|attack|defense|credit|parts|heal',['Summary only. No inventory grid, equipment slots or weapon switching.']),
'UI-21':entry('보급 교환','available|insufficient_parts|at_capacity|trade_complete|surcharge','effective_price|parts_owned|kits_owned|kit_capacity|trade_result','supply|parts|heal|reputation',['Current action is instant E exchange. New menu/confirmation needs separate wiring.','Compute and show actual reputation-adjusted price; never hardcode preview cost.']),
'UI-22':entry('중요 행동 확인','new_game|reset_progress|exit_session|chapter_departure|cancel|confirm','action_label|consequences|can_confirm','warning|exit|check',['Default focus is cancel. No irreversible action on initial focus.','Explicitly explain that this session is not permanently saved.']),
'UI-23':entry('맥락별 도움말','walk|combat|bike|mine_scan|map|dialogue','available_actions|context','keyboard|mouse',['Only supported bindings for current context. No developer teleport shortcuts.'])}

layouts={
 'health_hud':dict(panel=False,icon=[32,736,24,24],current=[66,735],maximum=[118,734],gauge=[66,748,220,12],critical_prompt=[32,690],critical_key='G',critical_condition='health/max_health <= 0.25',current_px=28,draw_order='track,clipped_fill,icon,current,max,optional_critical',anchor='bottom_left'),
 'ammo_hud':dict(panel=False,icon=[1040,723,28,28],current=[1182,746],maximum=[1248,745],current_px=38,pips=dict(origin=[1090,757],size=[9,10],stride=13,count=12),reload=[1090,757,152,10],reserve='infinity icon at [1064,754], unlimited',reload_prompt=[1248,705],anchor='bottom_right',hide_when='weapon unavailable, bike mounted, safe dialogue, modal menu'),
 'scanner_hud':dict(panel=False,center=[976,656],radius=21,stroke=4,key='F',label=[1010,652],approximate_signal=[1010,682],arc='start at -90 degrees; sweep 360*progress',anchor='bottom_right_above_ammo'),
 'wanted_hud':dict(panel=False,stars_origin=[558,35],star_stride=35,star_size=25,heat=[558,73,165,6],label=[640,106],anchor='top_center',visibility='wanted > 0 or active cooling notice'),
 'speed_hud':dict(panel=False,value=[1176,749],unit=[1248,748],value_px=44,boost=[1115,695],anchor='bottom_right_replace_ammo',resource_gauge=False),
 'objective':dict(panel=False,title=[68,58],context=[68,85],chapter_detail=[68,112],max_text_width=395,max_primary_lines=1,details='J record; do not add a permanent card'),
 'minimap':dict(rect=[1040,32,208,264],center=[1144,139],clip_radius=82,north_degrees_clockwise=45,actor_heading_only=True,marker_inset=12,max_text_width=168),
 'interaction':dict(rect=[450,514,380,48],target_and_action=[500,545],key=[788,526],max_text_width=273,visible_only='nearby actionable target'),
 'notice':dict(panel=False,icon=[918,321,24,24],body=[954,342],max_text_width=294,details='actual amount in one short event line; coalesce duplicates'),
 'dialogue':dict(rect=[208,390,864,310],speaker=[270,462],body_top_left=[232,482],body_baselines=[502,534,566],body_width=816,body_font_size=20,line_height=32,max_body_lines=3,choice=[232,582,816,46],choice_secondary_baseline=654,page=[232,682],controls_y=662,speaker_max_width=575),
 'journal':dict(rect=[176,72,928,656],list_rect=[200,160,270,450],body_rect=[510,160,566,460],max_text_width=526,controls_y=674),
 'full_map':dict(rect=[56,40,1168,720],map_rect=[80,126,818,548],legend_rect=[930,160,260,420],controls_y=705),
 'pause':dict(rect=[416,112,448,576],row_rect=[440,220,400,46],row_spacing=58),
 'modal':dict(rect=[312,130,656,540],body_width=600,row_height=46),
}
save('Source/layouts.json',dict(canvas=[1280,800],coordinates='top-left; text coordinates are baselines',layouts=layouts,
    transform='scale=min(viewport_w/1280,viewport_h/800); offset=(viewport-canvas*scale)/2; apply to drawing and inverse to input',
    text_policy=dict(font='Malgun Gothic',body_px=20,metadata_min_px=14,line_height=32,paragraph_lines=3,wrap='measure actual glyph advances; break by Korean syllable or word; retain punctuation',
                     overflow='paginate body and choice lists without losing characters; wrap long speaker to two lines and reflow header; wrap large numeric summary instead of shrinking below 13px',
                     panel_expansion='Grow or paginate when translated text exceeds slot. Do not truncate mandatory dialogue.'),
    visibility=dict(radio='keep health and ammunition at stable bottom anchors; dialogue ends at y=700',local_dialogue='suppress combat HUD during safe dialogue',
                    notice_with_dialogue='queue notice until dialogue closes; urgent warning moves to free top slot',
                    layer_order=['world_markers','aim','panel_background','panel_border','icon','text','modal','medical_fade']),
    timing=dict(narrative='user_advanced_no_expiry',notice_seconds=4,coalesce_same_type_seconds=1.5,
                reduced_motion='steady critical indicator; no blinking text',note='Proposed UI timing only; game integration is separate.')))
UI['UI-24']=entry('원형 무기 선택','unarmed_equipped|pistol_owned|pistol_unowned|primary_empty|hover_available|hover_unavailable|release_commit|cancel_keep_equipped','equipped_slot|hovered_slot|pistol_owned|wheel_visible|pointer_offset','unarmed|pistol|slot_empty|key_release|mouse_right', ['Q hold opens and pauses; release commits available candidate; Esc cancels.','Unowned secondary never shows a pistol icon or future item name.','No click confirmation; invalid candidate keeps the equipped slot.'])
UI['UI-25']=entry('스테이지 시험 진입','default|keyboard_focus|mouse_hover|pressed|back','test_stage|focus|hover|pressed|return_screen','stage|gate|pistol|bike', ['Six stage/unlock checkpoints only.','Show progress reset and mandatory-story completion before selection.','No extra confirmation, save slots, continue or future chapters.'])
UI['UI-02']['states']+='|unarmed|pistol_hip|pistol_aim'.split('|')[1:]
UI['UI-03']['rules']+=['Aim only while RMB held with a pistol. Hip fire follows actor facing.','No aim-charge or animation-weight resource.']
UI['UI-23']['rules']+=['Q hold/release selects weapons; G heals. Retire old Q-heal/Q-quit labels.']
layouts['weapon_wheel']=dict(center=[640,390],inner_radius=87,outer_radius=224,sector_span_degrees=114,sector_centers_degrees=dict(primary=-90,secondary=30,unarmed=150),labels='separate runtime text, no baked labels',input='Q hold + mouse; release commit; Esc cancel',layer_order=['dim','sectors','equipped_marker','hover_outline','icons','labels','instructions'])
layouts['stages']=dict(rect=[64,48,1152,704],rows_origin=[96,211],row_size=[652,52],row_stride=62,description_rect=[824,214,328,350],warning=[96,636],back=[96,678,170,52])
layouts['text_flow']=dict(source='Source/text_flows.json',wrap='actual Malgun Gothic glyph advances',overflow='expand or retain every line across numbered pages',ellipsis=False,notice_lines_per_page=4,notice_seconds_per_page=6,short_notice_seconds=4,objective_lines_per_page=2,objective_seconds_per_page=5)
# Persist the new layout entries as well as the existing template contract.
layout_document=json.loads((ROOT/'Source/layouts.json').read_text(encoding='utf8'))
layout_document['layouts']=layouts
save('Source/layouts.json',layout_document)
manifest=json.loads((ROOT/'ui_manifest.json').read_text(encoding='utf8'))
for id,data in UI.items():
    data['presentation']='hud_primitives' if id in ['UI-01','UI-02','UI-03','UI-04','UI-05','UI-07','UI-10','UI-11','UI-12','UI-13','UI-14','UI-15'] else 'screen_or_modal'
    data['dedicated_status_panel']=False if data['presentation']=='hud_primitives' else None
    if data['presentation']=='hud_primitives': data['implementation_document']='HUD_IMPLEMENTATION_ko.md'
    data['priority']='current_play' if int(id[-2:])<=17 else 'existing_menu' if int(id[-2:])<=23 else 'new_design_for_existing_feature'
    data['preview_screens']=[n for n,s in manifest['screens'].items() if id in s['requirements']]
    data['delivery']='shared text-free assets + named state specification + composed editable review screen'
save('Source/ui_state_catalog.json',UI)
lines=['# UI 요구사항 제작 대응표','','2026-10-28 디자인 v1.2. 기존 UI-01~23의 게임 연결 기록은 `INTEGRATION_ko.md`를 보존합니다. 이번 새 디자인이 모든 게임 화면에 다시 연결되었다는 뜻은 아닙니다. UI-24/25는 기존 임시 기능에 사용할 전용 디자인을 납품합니다.','','상태·데이터·노출 조건은 `Source/ui_state_catalog.json`, 좌표는 `Source/layouts.json`, 원문 보존 검토는 `Source/text_flows.json`을 확인합니다.','','| ID | UI | 상태 수 | 대표 화면 | 제작/연결 구분 |','|---|---|---:|---|---|']
for id,d in UI.items():
    p=', '.join(f'[{n}]({manifest["screens"][n]["png"]})' for n in d['preview_screens'])
    lines.append(f'| {id} | {d["name"]} | {len(d["states"])} | {p} | '+('기존 게임 연결 유지 / 디자인 갱신' if int(id[-2:])<=23 else '전용 디자인 납품 / 임시 게임 표현 교체용')+' |')
lines+=['','','모든 상태를 별도의 완성 화면으로 만든 것은 아닙니다. 반복되는 상태는 동일 부품·라벨·아이콘 교체로 구성하며, 기본/포커스/눌림/선택/비활성/경고, 스캔·교환·주행·동료·전달 방식 등은 21·22·25·26번 갤러리에 모았습니다.','제작 검토 화면의 수치와 문구는 예시이며, 실제 적용 결과·화자·공개 조건·가격은 게임에서 전달해야 합니다.']
(ROOT/'COVERAGE_ko.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
print(f'Wrote {len(UI)} UI contracts and {len(layouts)} layout templates')
