"use strict";

const byId = (id) => document.getElementById(id);
let snapshot = null;
let previous = null;
let selectedNpcId = null;
let sequenceChangedAt = 0;
let consecutiveFailures = 0;
const events = [];
const format = (value, digits = 0) => typeof value === "number" && Number.isFinite(value)
  ? value.toLocaleString("ko-KR", { maximumFractionDigits: digits }) : "—";
const position = (value) => value ? format(value.x, 1) + ", " + format(value.y, 1) : "—";
const groupName = (id) => Number.isInteger(id) && id >= 0 ? "구조 무리 " + (id + 1) : "동료·주민";
const groupFor = (npc) => snapshot?.mine?.rescue_groups?.find((group) => group.id === npc.group_id);

// Game strings stay text, including dialogue containing HTML-like characters.
function node(tag, text, className) {
  const element = document.createElement(tag);
  if (text !== undefined) element.textContent = text;
  if (className) element.className = className;
  return element;
}
function metric(label, value) {
  const row = node("div", undefined, "metric");
  row.append(node("span", label), node("strong", value));
  return row;
}
function facts(entries) {
  const list = node("dl", undefined, "facts");
  for (const [label, value] of entries) list.append(node("dt", label), node("dd", value));
  return list;
}
function addEvent(message) {
  events.unshift({ time: new Date().toLocaleTimeString("ko-KR", { hour12: false }), message });
  if (events.length > 100) events.length = 100;
}
function trackChanges(data) {
  if (!previous || previous.session_id !== data.session_id) {
    events.length = 0;
    selectedNpcId = null;
    addEvent("게임 세션 연결");
  } else if (previous.sequence !== data.sequence) {
    if (previous.current_region_id !== data.current_region_id) addEvent("지역 이동 / " + data.current_area);
    else if (previous.current_area !== data.current_area) addEvent("장소 이동 / " + data.current_area);
    if (previous.quests.tutorial_stage !== data.quests.tutorial_stage)
      addEvent("튜토리얼 / " + data.quests.tutorial_stage);
    if (previous.paused !== data.paused) addEvent(data.paused ? "게임 일시정지" : "게임 재개");
    if (previous.counts.mob_alive !== data.counts.mob_alive)
      addEvent("생존 몹 " + previous.counts.mob_alive + " → " + data.counts.mob_alive);
    if (previous.counts.npc_total !== data.counts.npc_total)
      addEvent("배치 NPC " + previous.counts.npc_total + " → " + data.counts.npc_total);
    const oldNpcs = new Map(previous.npcs.map((npc) => [npc.id, npc]));
    for (const npc of data.npcs) {
      const old = oldNpcs.get(npc.id);
      if (old && (old.activity !== npc.activity || old.following !== npc.following))
        addEvent(npc.name + " / " + npc.activity_label + (npc.following ? " · 동행 설정" : ""));
    }
    if (previous.quests.main_quest !== data.quests.main_quest)
      addEvent("광산 조사 / " + data.quests.main_quest);
  }
  if (!previous || previous.session_id !== data.session_id || previous.sequence !== data.sequence)
    sequenceChangedAt = performance.now();
  previous = data;
}
function renderSummary(data) {
  const perf = data.performance;
  const values = [
    ["배치 NPC", format(data.counts.npc_total), "현재 생성된 전체 배치"],
    ["현재 지역 NPC", format(data.counts.npc_current_region), "AI 갱신 중 " + data.counts.npc_simulating + "명"],
    ["생존 몹", format(data.counts.mob_alive), "현재 지역 " + data.counts.mob_current_region_alive + "기"],
    ["재출현 대기", format(data.counts.mob_respawning), "전체 정원 " + data.counts.mob_total + "기"],
    ["FPS", format(perf?.fps, 1), perf ? "최근 완료된 1초 구간" : "첫 계측 구간 대기"],
    ["평균 드로우 콜", format(perf?.draw_calls_per_frame?.avg, 1), "프레임 기준"],
  ];
  byId("summary").replaceChildren(...values.map(([label, value, help]) => {
    const card = node("article");
    card.append(node("span", label), node("strong", value), node("small", help));
    if (label === "FPS" && perf?.below_target_fps) card.querySelector("strong").className = "warn";
    return card;
  }));
}
function renderRegions(data) {
  byId("regions").replaceChildren(...data.regions.map((region) => {
    const card = node("article", undefined, "region" + (region.current ? " current" : ""));
    const title = node("div");
    title.append(node("h3", region.name), node("span",
      !region.loaded ? "미생성" : region.current ? "현재 지역" : "보관 상태",
      "badge" + (region.current ? " live" : "")));
    card.append(title, node("p", region.mood, "mood"), facts([
      ["배치 인구", region.population + "명"],
      ["성향 · 설정", region.disposition],
      ["AI 갱신", region.simulation_active ? "진행 중" : "정적 NPC 또는 갱신 중지"],
      ["전력", region.loaded ? region.power : "—"],
      ["관문 신호", region.loaded ? region.gate_signal : "—"],
      ["전투", "생존 " + region.mob_alive + " / 재출현 대기 " + region.mob_respawning],
      ["구조 성과", "발견 " + format(data.counts.rescued_people) + "명 / 화면 밖 철수 " + format(data.counts.returned_people) + "명"],
      ["구역 크기", region.map_size_label ?? region.map_width + " × " + region.map_height],
    ]));
    const rumors = node("ul", undefined, "rumors");
    if (region.loaded) for (const rumor of region.rumors) {
      const item = node("li", rumor.text);
      item.append(node("small", rumor.source + " · " + (rumor.confirmed ? "확인됨" : "미확인 소문")));
      rumors.append(item);
    }
    else rumors.append(node("li", "진입 후 관측 정보 표시", "muted"));
    card.append(rumors);
    return card;
  }));
  const filter = byId("region-filter");
  const ids = [...filter.options].slice(1).map((option) => option.value).join(",");
  if (ids !== data.regions.map((region) => region.id).join(",")) {
    const selection = filter.value;
    filter.replaceChildren(new Option("전체 지역", ""),
      ...data.regions.map((region) => new Option(region.name, region.id)));
    filter.value = selection;
  }
}
function renderRescueGroups(data) {
  const groups = data.mine?.rescue_groups ?? [];
  byId("rescue-groups").replaceChildren(...(groups.length ? groups.map((group) => {
    const card = node("article", undefined, "panel station");
    card.append(node("h3", groupName(group.id) + " · " + group.name),
      node("span", group.rescued ? "구조 확인 · 귀환로 철수" : "구조 요청 중", "badge"),
      facts([["인원", group.members + "명"], ["위치 · 개발 관측", position(group.position)]]));
    return card;
  }) : [node("p", "광산 진입 후 구조 요청 무리가 표시됩니다.", "empty")]));
}
function renderNpcs() {
  if (!snapshot) return;
  const regionId = byId("region-filter").value;
  const activity = byId("activity-filter").value;
  const search = byId("search").value.trim().toLocaleLowerCase("ko-KR");
  const regions = new Map(snapshot.regions.map((region) => [region.id, region.name]));
  const filtered = snapshot.npcs.filter((npc) => (!regionId || npc.region_id === regionId)
    && (!activity || npc.activity === activity)
    && (npc.name + " " + npc.role + " " + (npc.personality ?? "") + " " + groupName(npc.group_id)
      + " " + (groupFor(npc)?.name ?? "")).toLocaleLowerCase("ko-KR").includes(search));
  byId("npc-result-count").textContent = filtered.length + " / " + snapshot.npcs.length + "명";
  const rows = filtered.map((npc) => {
    const row = node("tr");
    if (npc.id === selectedNpcId) row.className = "selected";
    const nameCell = node("td");
    const button = node("button", npc.name, "npc-name");
    button.type = "button";
    button.dataset.npcId = npc.id;
    button.setAttribute("aria-label", npc.name + " 상세 보기");
    button.addEventListener("click", () => { selectedNpcId = npc.id; renderNpcs(); });
    nameCell.append(button, node("small", npc.role));
    const stateCell = node("td");
    stateCell.append(node("span", npc.activity_label, "activity " + npc.activity),
      node("small", npc.simulation_active ? (npc.following ? "동행 설정 · AI 갱신" : "AI 갱신") : "정적 배치 / 갱신 중지"));
    const teamCell = node("td", groupName(npc.group_id));
    teamCell.append(node("small", groupFor(npc)?.name ?? "구조 대상 외"));
    row.append(nameCell, node("td", regions.get(npc.region_id)), stateCell,
      teamCell, node("td", npc.personality ?? "미설정"), node("td", position(npc.position), "coords"),
      node("td", npc.route_remaining + "칸"));
    return row;
  });
  if (!rows.length) {
    const row = node("tr"), cell = node("td", "조건에 맞는 NPC가 없습니다.", "empty");
    cell.colSpan = 7;
    row.append(cell);
    rows.push(row);
  }
  // Replacing rows should not steal the keyboard user's selected NPC button.
  const focusedId = document.activeElement?.dataset.npcId;
  byId("npc-table").replaceChildren(...rows);
  if (focusedId) {
    const button = [...byId("npc-table").querySelectorAll("button")].find((item) => item.dataset.npcId === focusedId);
    button?.focus({ preventScroll: true });
  }
  renderNpcDetail();
}
function renderNpcDetail() {
  const npc = snapshot.npcs.find((item) => item.id === selectedNpcId);
  const panel = byId("npc-detail");
  if (!npc) {
    panel.replaceChildren(node("h3", "인물 상세"), node("p", "목록에서 이름을 선택하세요.", "muted"));
    return;
  }
  panel.replaceChildren(node("h3", npc.name), facts([
    ["역할", npc.role], ["행동", npc.activity_label], ["패턴", npc.behavior_pattern],
    ["구조 무리", groupName(npc.group_id)], ["동행", npc.following ? "동행 중" : "해당 없음"],
    ["현재 위치", position(npc.position)], ["목적지", position(npc.destination)],
    ["남은 경로", format(npc.route_remaining) + "칸"],
    ["AI 갱신", npc.simulation_active ? "진행 중" : "중지 / 정적 배치"],
  ]));
}
function renderEnvironment(data) {
  byId("world-status").replaceChildren(
    metric("현재 위치", data.current_area + " (" + position(data.player.position) + ")"),
    metric("게임 상태", data.paused ? "일시정지" : "진행 중"),
    metric("성장", "레벨 " + data.player.level + " / 경험치 " + data.player.experience),
    metric("체력", data.player.health + " / " + data.player.max_health),
    metric("보유품", "회복 키트 " + data.player.recovery_kits + " / 부품 " + data.player.salvage),
    metric("몹 처치 · 드롭", data.player.kills + "회 / " + data.counts.drops + "개"),
    metric("튜토리얼", data.quests.tutorial_stage),
    metric("전망대", data.quests.in_observatory ? "관측실 내부" : data.quests.observatory_descended ? "관측 후 하층 복귀" : "진입 전 / 하층"),
    metric("관문 신호", data.quests.gate_signal_heard ? "관측 기록 확보" : "미조사"),
    metric("동쪽 전력", data.quests.mara_power_restored ? "복구 완료" : "복구 전"),
    metric("광산 조사", data.quests.main_quest),
    metric("권총", data.player.magazine_rounds + " / " + data.player.magazine_capacity + (data.player.reloading ? " · 재장전" : "")),
    metric("탄퍼짐", format(data.player.spread_radians, 3) + " rad"),
    metric("구조", format(data.mine?.rescued_people) + "명 / 화면 밖 철수 " + format(data.mine?.returned_people) + "명"),
    metric("스캐너", format(data.mine?.scanner_seconds, 1) + "초 / 대기 " + format(data.mine?.scanner_cooldown, 1) + "초"),
  );
  const travel = data.jet_travel;
  if (travel) byId("world-status").append(
    metric("개인 바이크", travel.active ? "야외 직접 운전 중" : "주차 / 도보 이동"),
    metric("속도", format(travel.speed_kmh, 1) + " km/h"),
    metric("주차 / 주행 위치", format(travel.position?.x, 1) + ", " + format(travel.position?.y, 1)),
    metric("도시 간 기준", format(travel.city_spacing) + " 월드 단위 · 약 " + format(travel.nominal_city_travel_seconds) + "초"),
  );
  const perf = data.performance;
  if (!perf) {
    byId("performance").replaceChildren(node("p", "첫 성능 보고 구간을 기다립니다.", "muted"));
    return;
  }
  const cpu = perf.cpu_scopes_ms;
  byId("performance").replaceChildren(
    metric("드로우 콜 마지막 / 평균 / 최대",
      format(perf.draw_calls_per_frame.last) + " / " + format(perf.draw_calls_per_frame.avg, 1) + " / " + format(perf.draw_calls_per_frame.max)),
    metric("프레임 간격 p95 / p99", format(perf.frame_interval_ms.p95, 2) + " / " + format(perf.frame_interval_ms.p99, 2) + " ms"),
    metric("GPU 평균", format(perf.gpu_elapsed_ms?.avg, 2) + " ms"),
    metric("NPC 갱신 평균", format(cpu.npc_update?.avg, 3) + " ms"),
    metric("NPC 길 찾기 평균", format(cpu.npc_navigation?.avg, 3) + " ms"),
    metric("광산 조명 평균", format(cpu.mine_lighting?.avg, 3) + " ms"),
    metric("JSON 생성 평균", format(cpu.monitor_snapshot?.avg, 3) + " ms"),
    metric("NPC 경로 요청 / 방문 셀", format(perf.counters.npc_path_requests) + " / " + format(perf.counters.npc_path_visited_cells)),
    metric("메모리 · 작업 집합", format(perf.process_memory_bytes?.working_set / 1048576, 1) + " MiB"),
  );
}
function renderEvents() {
  byId("events").replaceChildren(...events.map((event) => {
    const item = node("li");
    item.append(node("time", event.time), node("span", event.message));
    return item;
  }));
}
function render(data) {
  renderSummary(data);
  renderRegions(data);
  renderRescueGroups(data);
  renderNpcs();
  renderEnvironment(data);
  renderEvents();
  byId("notice").textContent = data.notice || data.current_area + " · 상태 수신 중";
  if (byId("raw-json").parentElement.open) byId("raw-json").textContent = JSON.stringify(data, null, 2);
  byId("download").disabled = false;
}
async function poll() {
  let delay = 500;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 2000);
  try {
    const response = await fetch("/api/state", { cache: "no-store", signal: controller.signal });
    if (!response.ok) throw new Error("HTTP " + response.status);
    const data = await response.json();
    if (data.schema !== "game_monitor_v1" || !data.ready || !Array.isArray(data.npcs)
      || !Array.isArray(data.regions) || !data.counts || !data.player) throw new Error("지원하지 않는 상태 메시지");
    const recovered = consecutiveFailures > 0;
    trackChanges(data);
    if (recovered) addEvent("연결 복구");
    snapshot = data;
    consecutiveFailures = 0;
    render(data);
    const stale = performance.now() - sequenceChangedAt > 3000;
    byId("connection-status").textContent = stale ? "상태 갱신 지연" : data.paused ? "연결됨 · 게임 일시정지" : "실시간 연결됨";
    byId("connection-status").dataset.state = stale ? "error" : "live";
    byId("connection-meta").textContent = "수신 " + new Date().toLocaleTimeString("ko-KR", { hour12: false }) + " · #" + data.sequence;
  } catch (error) {
    if (consecutiveFailures === 0) {
      addEvent("게임 연결 끊김 / 자동 재접속");
      renderEvents();
    }
    consecutiveFailures += 1;
    delay = Math.min(10000, 1000 * consecutiveFailures);
    byId("connection-status").textContent = "연결 대기 · 자동 재접속";
    byId("connection-status").dataset.state = "error";
    byId("connection-meta").textContent = "게임 실행과 주소를 확인하세요. " + error.message;
    byId("notice").textContent = snapshot ? "연결이 끊겼습니다. 화면은 마지막 수신 상태입니다."
      : "게임을 실행하고 http://127.0.0.1:17707/ 주소로 접속하세요.";
  } finally {
    clearTimeout(timeout);
    setTimeout(poll, delay); // Keep just one in-flight request.
  }
}
for (const id of ["region-filter", "activity-filter", "search"])
  byId(id).addEventListener(id === "search" ? "input" : "change", renderNpcs);
byId("clear-events").addEventListener("click", () => { events.length = 0; renderEvents(); });
byId("new-window").addEventListener("click", () =>
  window.open(location.protocol === "file:" ? "http://127.0.0.1:17707/" : "/", "_blank", "popup,width=1440,height=950,noopener"));
byId("raw-json").parentElement.addEventListener("toggle", () => {
  if (snapshot) byId("raw-json").textContent = JSON.stringify(snapshot, null, 2);
});
byId("download").addEventListener("click", () => {
  if (!snapshot) return;
  const blob = new Blob([JSON.stringify(snapshot, null, 2)], { type: "application/json;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const link = node("a");
  link.href = url;
  link.download = "world-" + snapshot.session_id + "-" + snapshot.sequence + ".json";
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});
if (location.protocol === "file:") {
  byId("connection-status").textContent = "로컬 서버 주소로 열어 주세요";
  byId("connection-meta").textContent = "http://127.0.0.1:17707/";
  byId("notice").textContent = "게임을 실행한 뒤 브라우저에서 http://127.0.0.1:17707/ 주소를 여세요.";
} else {
  poll();
}
