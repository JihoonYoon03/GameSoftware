# UI 요구사항 제작 대응표

2026-11-09 디자인 v1.3. CQ-02A~E의 활동 추적·저장 복원·정산 보류·현장 완료 상태를 반영했습니다. 기존 UI-01~25와 게임 연결 이력은 보존합니다. 이번 납품은 리소스와 디자인 계약이며 게임 코드·실행 파일·Runtime 팩은 변경하지 않았습니다.

후속 적용(2026-11-09): 사용자 지시로 활동 기록·새 상태 아이콘·145개 사전 제작 아틀라스·정적 배경 레이어를 게임 소스에 연결했습니다. 아래 표의 제작 구분은 납품 당시 이력이며 최신 연결은 [게임 통합 기록](INTEGRATION_ko.md#후속-게임-통합--2026-11-09)을 따릅니다. 빌드/실제 화면·입력·성능 검증은 대기입니다.

상태·데이터·노출 조건은 `Source/ui_state_catalog.json`, 좌표는 `Source/layouts.json`, 원문 보존 검토는 `Source/text_flows.json`, 활동 계약은 `Source/activity_design_contract.json`을 확인합니다.

| ID | UI | 상태 수 | 대표 화면 | 제작/연결 구분 |
|---|---|---:|---|---|
| UI-01 | 플레이어 상태 | 4 | [01_city](Preview/20261109/01_city.png), [02_combat](Preview/20261109/02_combat.png), [26_notifications](Preview/20261109/26_notifications.png), [33_exploration](Preview/20261109/33_exploration.png), [34_low_health](Preview/20261109/34_low_health.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-02 | 무기 · 전투 | 7 | [01_city](Preview/20261109/01_city.png), [02_combat](Preview/20261109/02_combat.png), [34_low_health](Preview/20261109/34_low_health.png), [41_unarmed](Preview/20261109/41_unarmed.png), [42_pistol_hip](Preview/20261109/42_pistol_hip.png), [43_pistol_aim](Preview/20261109/43_pistol_aim.png), [44_pistol_reload](Preview/20261109/44_pistol_reload.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-03 | 조준 | 5 | [02_combat](Preview/20261109/02_combat.png), [34_low_health](Preview/20261109/34_low_health.png), [41_unarmed](Preview/20261109/41_unarmed.png), [42_pistol_hip](Preview/20261109/42_pistol_hip.png), [43_pistol_aim](Preview/20261109/43_pistol_aim.png), [44_pistol_reload](Preview/20261109/44_pistol_reload.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-04 | 목표 추적 | 8 | [50_story_tracking](Preview/20261109/50_story_tracking.png), [51_story_completed](Preview/20261109/51_story_completed.png), [60_hud_tracking_and_pin](Preview/20261109/60_hud_tracking_and_pin.png), [61_mine_surface_tracking](Preview/20261109/61_mine_surface_tracking.png), [64_activity_state_legend](Preview/20261109/64_activity_state_legend.png), [01_city](Preview/20261109/01_city.png), [07_chapter](Preview/20261109/07_chapter.png), [33_exploration](Preview/20261109/33_exploration.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-05 | 미니맵 · 목적지 | 6 | [60_hud_tracking_and_pin](Preview/20261109/60_hud_tracking_and_pin.png), [61_mine_surface_tracking](Preview/20261109/61_mine_surface_tracking.png), [01_city](Preview/20261109/01_city.png), [03_bike](Preview/20261109/03_bike.png), [04_mine](Preview/20261109/04_mine.png), [33_exploration](Preview/20261109/33_exploration.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-06 | 전체 지도 | 6 | [60_hud_tracking_and_pin](Preview/20261109/60_hud_tracking_and_pin.png), [68_map_activity_markers](Preview/20261109/68_map_activity_markers.png), [10_city_map](Preview/20261109/10_city_map.png), [11_mine_map](Preview/20261109/11_mine_map.png), [12_chapter_map](Preview/20261109/12_chapter_map.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-07 | 상호작용 · 월드 표식 | 11 | [01_city](Preview/20261109/01_city.png), [07_chapter](Preview/20261109/07_chapter.png), [25_interactions](Preview/20261109/25_interactions.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-08 | 대화 · 수신 · 무전 | 10 | [05_dialogue](Preview/20261109/05_dialogue.png), [24_local_dialogue](Preview/20261109/24_local_dialogue.png), [31_dialogue_page](Preview/20261109/31_dialogue_page.png), [45_dialogue_page_02](Preview/20261109/45_dialogue_page_02.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-09 | 조사 기록 · 이력 | 18 | [50_story_tracking](Preview/20261109/50_story_tracking.png), [51_story_completed](Preview/20261109/51_story_completed.png), [52_story_prerequisite](Preview/20261109/52_story_prerequisite.png), [53_story_danger](Preview/20261109/53_story_danger.png), [54_regional_empty](Preview/20261109/54_regional_empty.png), [55_regional_measure](Preview/20261109/55_regional_measure.png), [56_regional_submit](Preview/20261109/56_regional_submit.png), [57_regional_restored](Preview/20261109/57_regional_restored.png), [58_regional_long_name](Preview/20261109/58_regional_long_name.png), [59_regional_one](Preview/20261109/59_regional_one.png), [64_activity_state_legend](Preview/20261109/64_activity_state_legend.png), [65_story_focus_and_tracking](Preview/20261109/65_story_focus_and_tracking.png), [66_restore_legacy_fallback](Preview/20261109/66_restore_legacy_fallback.png), [67_after_completion_list](Preview/20261109/67_after_completion_list.png), [69_story_not_accepted](Preview/20261109/69_story_not_accepted.png), [06_journal](Preview/20261109/06_journal.png), [13_history](Preview/20261109/13_history.png), [14_empty](Preview/20261109/14_empty.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-10 | 스캐너 | 5 | [04_mine](Preview/20261109/04_mine.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-11 | 동료 · 구조 | 7 | [04_mine](Preview/20261109/04_mine.png), [07_chapter](Preview/20261109/07_chapter.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-12 | 수배 · 악명 | 10 | [02_combat](Preview/20261109/02_combat.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-13 | 바이크 | 7 | [03_bike](Preview/20261109/03_bike.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-14 | 교통 | 4 | [01_city](Preview/20261109/01_city.png), [26_notifications](Preview/20261109/26_notifications.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-15 | 공통 알림 | 10 | [62_settlement_hold_01](Preview/20261109/62_settlement_hold_01.png), [63_planet_failure_01](Preview/20261109/63_planet_failure_01.png), [63_planet_failure_02](Preview/20261109/63_planet_failure_02.png), [02_combat](Preview/20261109/02_combat.png), [07_chapter](Preview/20261109/07_chapter.png), [26_notifications](Preview/20261109/26_notifications.png), [46_unlock_notice](Preview/20261109/46_unlock_notice.png), [47_notice_page_01](Preview/20261109/47_notice_page_01.png), [47_notice_page_02](Preview/20261109/47_notice_page_02.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-16 | 의료 이송 | 5 | [08_medical](Preview/20261109/08_medical.png), [27_transfer](Preview/20261109/27_transfer.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-17 | 일시정지 | 6 | [09_pause](Preview/20261109/09_pause.png), [28_pause_mine](Preview/20261109/28_pause_mine.png), [29_pause_city](Preview/20261109/29_pause_city.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-18 | 타이틀 | 4 | [15_title](Preview/20261109/15_title.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-19 | 옵션 | 6 | [16_options](Preview/20261109/16_options.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-20 | 보유 · 성장 요약 | 1 | [17_summary](Preview/20261109/17_summary.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-21 | 보급 교환 | 5 | [18_supply](Preview/20261109/18_supply.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-22 | 중요 행동 확인 | 6 | [19_confirm](Preview/20261109/19_confirm.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-23 | 맥락별 도움말 | 6 | [20_help](Preview/20261109/20_help.png), [46_unlock_notice](Preview/20261109/46_unlock_notice.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-24 | 원형 무기 선택 | 8 | [35_weapon_wheel_locked](Preview/20261109/35_weapon_wheel_locked.png), [36_weapon_wheel_pistol](Preview/20261109/36_weapon_wheel_pistol.png), [37_weapon_wheel_empty](Preview/20261109/37_weapon_wheel_empty.png), [38_weapon_wheel_equipped](Preview/20261109/38_weapon_wheel_equipped.png), [39_weapon_wheel_unowned](Preview/20261109/39_weapon_wheel_unowned.png) | 전용 디자인 납품 / 임시 게임 표현 교체용 |
| UI-25 | 스테이지 시험 진입 | 5 | [40_stages](Preview/20261109/40_stages.png) | 전용 디자인 납품 / 임시 게임 표현 교체용 |


모든 상태를 별도의 완성 화면으로 만든 것은 아닙니다. 반복되는 상태는 동일 부품·라벨·아이콘 교체로 구성하며, 기본/포커스/눌림/선택/비활성/경고, 스캔·교환·주행·동료·전달 방식 등은 21·22·25·26번 갤러리에 모았습니다.
제작 검토 화면의 수치와 문구는 예시이며, 실제 적용 결과·화자·공개 조건·가격은 게임에서 전달해야 합니다.

## 최신 추가 요청

| 요청 | 제작 화면 |
|---|---|
| CQ-02A | [50_story_tracking](Preview/20261109/50_story_tracking.png), [52_story_prerequisite](Preview/20261109/52_story_prerequisite.png), [53_story_danger](Preview/20261109/53_story_danger.png), [64_activity_state_legend](Preview/20261109/64_activity_state_legend.png), [65_story_focus_and_tracking](Preview/20261109/65_story_focus_and_tracking.png), [69_story_not_accepted](Preview/20261109/69_story_not_accepted.png) |
| CQ-02B | [54_regional_empty](Preview/20261109/54_regional_empty.png), [55_regional_measure](Preview/20261109/55_regional_measure.png), [56_regional_submit](Preview/20261109/56_regional_submit.png), [57_regional_restored](Preview/20261109/57_regional_restored.png), [58_regional_long_name](Preview/20261109/58_regional_long_name.png), [59_regional_one](Preview/20261109/59_regional_one.png), [60_hud_tracking_and_pin](Preview/20261109/60_hud_tracking_and_pin.png), [61_mine_surface_tracking](Preview/20261109/61_mine_surface_tracking.png), [68_map_activity_markers](Preview/20261109/68_map_activity_markers.png) |
| CQ-02C | [54_regional_empty](Preview/20261109/54_regional_empty.png), [55_regional_measure](Preview/20261109/55_regional_measure.png), [56_regional_submit](Preview/20261109/56_regional_submit.png), [57_regional_restored](Preview/20261109/57_regional_restored.png), [58_regional_long_name](Preview/20261109/58_regional_long_name.png), [59_regional_one](Preview/20261109/59_regional_one.png), [66_restore_legacy_fallback](Preview/20261109/66_restore_legacy_fallback.png) |
| CQ-02D | [62_settlement_hold_01](Preview/20261109/62_settlement_hold_01.png), [63_planet_failure_01](Preview/20261109/63_planet_failure_01.png), [63_planet_failure_02](Preview/20261109/63_planet_failure_02.png) |
| CQ-02E | [51_story_completed](Preview/20261109/51_story_completed.png), [64_activity_state_legend](Preview/20261109/64_activity_state_legend.png), [67_after_completion_list](Preview/20261109/67_after_completion_list.png) |
