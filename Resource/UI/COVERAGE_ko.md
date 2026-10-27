# UI 요구사항 제작 대응표

2026-10-26 게임 연결 기준으로 갱신했다. 조작·배포·제한과 실제 검증 범위는 [게임 연결 기록](INTEGRATION_ko.md)을 따른다. 아래 상태 수와 대표 화면은 제작 계약/샘플 목록이다.

상태별 배경·기호·게이지는 공통 부품으로 재사용하고 문구·수치는 게임 데이터로 채웁니다. 아래 상태 명세는 `Source/ui_state_catalog.json`, 좌표 계약은 `Source/layouts.json`에 있습니다.

| ID | UI | 상태 수 | 대표 화면 | 연결 상태 |
|---|---|---:|---|---|
| UI-01 | 플레이어 상태 | 4 | [01_city](Preview/01_city.png), [02_combat](Preview/02_combat.png), [26_notifications](Preview/26_notifications.png), [33_exploration](Preview/33_exploration.png), [34_low_health](Preview/34_low_health.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-02 | 무기 · 전투 | 4 | [01_city](Preview/01_city.png), [02_combat](Preview/02_combat.png), [34_low_health](Preview/34_low_health.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-03 | 조준 | 5 | [02_combat](Preview/02_combat.png), [34_low_health](Preview/34_low_health.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-04 | 목표 추적 | 8 | [01_city](Preview/01_city.png), [07_chapter](Preview/07_chapter.png), [33_exploration](Preview/33_exploration.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-05 | 미니맵 · 목적지 | 6 | [01_city](Preview/01_city.png), [03_bike](Preview/03_bike.png), [04_mine](Preview/04_mine.png), [33_exploration](Preview/33_exploration.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-06 | 전체 지도 | 6 | [10_city_map](Preview/10_city_map.png), [11_mine_map](Preview/11_mine_map.png), [12_chapter_map](Preview/12_chapter_map.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-07 | 상호작용 · 월드 표식 | 11 | [01_city](Preview/01_city.png), [07_chapter](Preview/07_chapter.png), [25_interactions](Preview/25_interactions.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-08 | 대화 · 수신 · 무전 | 10 | [05_dialogue](Preview/05_dialogue.png), [24_local_dialogue](Preview/24_local_dialogue.png), [31_dialogue_page](Preview/31_dialogue_page.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-09 | 조사 기록 · 이력 | 5 | [06_journal](Preview/06_journal.png), [13_history](Preview/13_history.png), [14_empty](Preview/14_empty.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-10 | 스캐너 | 5 | [04_mine](Preview/04_mine.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-11 | 동료 · 구조 | 7 | [04_mine](Preview/04_mine.png), [07_chapter](Preview/07_chapter.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-12 | 수배 · 악명 | 10 | [02_combat](Preview/02_combat.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-13 | 바이크 | 7 | [03_bike](Preview/03_bike.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-14 | 교통 | 4 | [01_city](Preview/01_city.png), [26_notifications](Preview/26_notifications.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-15 | 공통 알림 | 8 | [02_combat](Preview/02_combat.png), [05_dialogue](Preview/05_dialogue.png), [07_chapter](Preview/07_chapter.png), [26_notifications](Preview/26_notifications.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-16 | 의료 이송 | 5 | [08_medical](Preview/08_medical.png), [27_transfer](Preview/27_transfer.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-17 | 일시정지 | 6 | [09_pause](Preview/09_pause.png), [28_pause_mine](Preview/28_pause_mine.png), [29_pause_city](Preview/29_pause_city.png) | 게임 연결 / 실제 플레이 값 사용 |
| UI-18 | 타이틀 | 4 | [15_title](Preview/15_title.png) | 게임 연결 / 메뉴 동작 추가 |
| UI-19 | 옵션 | 6 | [16_options](Preview/16_options.png) | 게임 연결 / 메뉴 동작 추가 |
| UI-20 | 보유 · 성장 요약 | 1 | [17_summary](Preview/17_summary.png) | 게임 연결 / 메뉴 동작 추가 |
| UI-21 | 보급 교환 | 5 | [18_supply](Preview/18_supply.png) | 게임 연결 / 메뉴 동작 추가 |
| UI-22 | 중요 행동 확인 | 6 | [19_confirm](Preview/19_confirm.png) | 게임 연결 / 메뉴 동작 추가 |
| UI-23 | 맥락별 도움말 | 6 | [20_help](Preview/20_help.png) | 게임 연결 / 메뉴 동작 추가 |


모든 상태를 별도의 완성 화면으로 만든 것은 아닙니다. 반복되는 상태는 동일 부품·라벨·아이콘 교체로 구성하며, 기본/포커스/눌림/선택/비활성/경고, 스캔·교환·주행·동료·전달 방식 등은 21·22·25·26번 갤러리에 모았습니다.
제작 검토 화면의 수치와 문구는 예시이며, 실제 적용 결과·화자·공개 조건·가격은 게임에서 전달해야 합니다.
