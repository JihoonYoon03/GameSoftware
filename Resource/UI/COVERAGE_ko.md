# UI 요구사항 제작 대응표

2026-10-28 디자인 v1.2. 기존 UI-01~23의 게임 연결 기록은 `INTEGRATION_ko.md`를 보존합니다. 이번 새 디자인이 모든 게임 화면에 다시 연결되었다는 뜻은 아닙니다. UI-24/25는 기존 임시 기능에 사용할 전용 디자인을 납품합니다.

상태·데이터·노출 조건은 `Source/ui_state_catalog.json`, 좌표는 `Source/layouts.json`, 원문 보존 검토는 `Source/text_flows.json`을 확인합니다.

| ID | UI | 상태 수 | 대표 화면 | 제작/연결 구분 |
|---|---|---:|---|---|
| UI-01 | 플레이어 상태 | 4 | [01_city](Preview/01_city_20261028.png), [02_combat](Preview/02_combat_20261028.png), [26_notifications](Preview/26_notifications_20261028.png), [33_exploration](Preview/33_exploration_20261028.png), [34_low_health](Preview/34_low_health_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-02 | 무기 · 전투 | 7 | [01_city](Preview/01_city_20261028.png), [02_combat](Preview/02_combat_20261028.png), [34_low_health](Preview/34_low_health_20261028.png), [41_unarmed](Preview/41_unarmed_20261028.png), [42_pistol_hip](Preview/42_pistol_hip_20261028.png), [43_pistol_aim](Preview/43_pistol_aim_20261028.png), [44_pistol_reload](Preview/44_pistol_reload_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-03 | 조준 | 5 | [02_combat](Preview/02_combat_20261028.png), [34_low_health](Preview/34_low_health_20261028.png), [41_unarmed](Preview/41_unarmed_20261028.png), [42_pistol_hip](Preview/42_pistol_hip_20261028.png), [43_pistol_aim](Preview/43_pistol_aim_20261028.png), [44_pistol_reload](Preview/44_pistol_reload_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-04 | 목표 추적 | 8 | [01_city](Preview/01_city_20261028.png), [07_chapter](Preview/07_chapter_20261028.png), [33_exploration](Preview/33_exploration_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-05 | 미니맵 · 목적지 | 6 | [01_city](Preview/01_city_20261028.png), [03_bike](Preview/03_bike_20261028.png), [04_mine](Preview/04_mine_20261028.png), [33_exploration](Preview/33_exploration_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-06 | 전체 지도 | 6 | [10_city_map](Preview/10_city_map_20261028.png), [11_mine_map](Preview/11_mine_map_20261028.png), [12_chapter_map](Preview/12_chapter_map_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-07 | 상호작용 · 월드 표식 | 11 | [01_city](Preview/01_city_20261028.png), [07_chapter](Preview/07_chapter_20261028.png), [25_interactions](Preview/25_interactions_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-08 | 대화 · 수신 · 무전 | 10 | [05_dialogue](Preview/05_dialogue_20261028_r2.png), [24_local_dialogue](Preview/24_local_dialogue_20261028.png), [31_dialogue_page](Preview/31_dialogue_page_20261028.png), [45_dialogue_page_02](Preview/45_dialogue_page_02_20261028_r2.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-09 | 조사 기록 · 이력 | 5 | [06_journal](Preview/06_journal_20261028.png), [13_history](Preview/13_history_20261028.png), [14_empty](Preview/14_empty_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-10 | 스캐너 | 5 | [04_mine](Preview/04_mine_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-11 | 동료 · 구조 | 7 | [04_mine](Preview/04_mine_20261028.png), [07_chapter](Preview/07_chapter_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-12 | 수배 · 악명 | 10 | [02_combat](Preview/02_combat_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-13 | 바이크 | 7 | [03_bike](Preview/03_bike_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-14 | 교통 | 4 | [01_city](Preview/01_city_20261028.png), [26_notifications](Preview/26_notifications_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-15 | 공통 알림 | 8 | [02_combat](Preview/02_combat_20261028.png), [07_chapter](Preview/07_chapter_20261028.png), [26_notifications](Preview/26_notifications_20261028.png), [46_unlock_notice](Preview/46_unlock_notice_20261028.png), [47_notice_page_01](Preview/47_notice_page_01_20261028.png), [47_notice_page_02](Preview/47_notice_page_02_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-16 | 의료 이송 | 5 | [08_medical](Preview/08_medical_20261028.png), [27_transfer](Preview/27_transfer_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-17 | 일시정지 | 6 | [09_pause](Preview/09_pause_20261028.png), [28_pause_mine](Preview/28_pause_mine_20261028.png), [29_pause_city](Preview/29_pause_city_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-18 | 타이틀 | 4 | [15_title](Preview/15_title_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-19 | 옵션 | 6 | [16_options](Preview/16_options_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-20 | 보유 · 성장 요약 | 1 | [17_summary](Preview/17_summary_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-21 | 보급 교환 | 5 | [18_supply](Preview/18_supply_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-22 | 중요 행동 확인 | 6 | [19_confirm](Preview/19_confirm_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-23 | 맥락별 도움말 | 6 | [20_help](Preview/20_help_20261028.png), [46_unlock_notice](Preview/46_unlock_notice_20261028.png) | 기존 게임 연결 유지 / 디자인 갱신 |
| UI-24 | 원형 무기 선택 | 8 | [35_weapon_wheel_locked](Preview/35_weapon_wheel_locked_20261028.png), [36_weapon_wheel_pistol](Preview/36_weapon_wheel_pistol_20261028.png), [37_weapon_wheel_empty](Preview/37_weapon_wheel_empty_20261028.png), [38_weapon_wheel_equipped](Preview/38_weapon_wheel_equipped_20261028.png), [39_weapon_wheel_unowned](Preview/39_weapon_wheel_unowned_20261028.png) | 전용 디자인 납품 / 임시 게임 표현 교체용 |
| UI-25 | 스테이지 시험 진입 | 5 | [40_stages](Preview/40_stages_20261028.png) | 전용 디자인 납품 / 임시 게임 표현 교체용 |


모든 상태를 별도의 완성 화면으로 만든 것은 아닙니다. 반복되는 상태는 동일 부품·라벨·아이콘 교체로 구성하며, 기본/포커스/눌림/선택/비활성/경고, 스캔·교환·주행·동료·전달 방식 등은 21·22·25·26번 갤러리에 모았습니다.
제작 검토 화면의 수치와 문구는 예시이며, 실제 적용 결과·화자·공개 조건·가격은 게임에서 전달해야 합니다.
