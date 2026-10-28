# iss02_Refactoring — 구조 분석과 성능 리팩토링

작성: 2026-10-28. 기능·대사·퀘스트·충돌 규칙·아트는 유지한다. 빌드와 게임 실행은 사용자가 수행한다.
이 문서의 개선량은 코드/데이터에서 확인한 작업량이다. 새로운 실행 시간이나 FPS 측정 결과가 아니다.

## 1. 변경 전에 확인한 전체 구조

외부 Dependencies와 생성 파일은 수정 대상에서 제외했다. 실제 실행 경로는 다음과 같다.

```text
SimpleGame.main
  GLUT 창 / GLEW
  GameRenderer.Initialize
    ResourceAssets: v3 바이너리 → 공유 애니메이션 뱅크 / 불변 메시
    ModelLibrary: 콘텐츠 종류 → 모델 바인딩
    Renderer: UI PNG / 공통 재질 / 셰이더 / FBO
  Game.Reset
    GameLevels.Reset → Overworld.Populate
    RoadNetwork / TransitLayout / CityPlan / WorldScenery / CityResidents
    CivicJustice.Reset → 충돌 구조 / 치안 상태
  GameMonitor.Initialize → 초기 상태 / 모델 도감 / 기존 HTTP 스레드
  GLUT 이벤트 루프
    Game.Tick → Game.Update → 모니터 발행 → 다음 타이머 예약
    GameRenderer.Draw → 배경 → 월드 → 후처리 → 광산 암부 → HUD → Present
```

| 계열 | 확인한 역할과 의존성 | 이번 판단 |
|---|---|---|
| Game / Weapon | 입력, 대화·선택지, 무기 선택, 카메라, 공통 업데이트 순서 | 갱신 순서·판정 유지. 타이머 예약과 문자 레이아웃 책임 분리 |
| Level | 광산/1장 수명, 타일 맵, 전투, 구조, 동료, 퀘스트 | 난수·맵·전투 규칙 유지. 기존 회귀 검사 대상 |
| Overworld / TransitLayout | 도로망, 신호, 지상/항공 교통, 도시 배치, 런타임 도로 조립 | 기존 공간 셀·정적 조립 캐시 유지. 아트 납품 후 대체 계약 작성 |
| Civic / ResidentAI | 치안, 피격, 의료 이송, 물리 충돌, 주민 A* | 공유 게임 상태는 메인 스레드 소유. A* 임시 메모리 재사용 |
| Actor / SceneGraph / Scene | 안정적인 객체 이름, 부모·자식, 경계 무효화, 컬링, 레이어/깊이/생성순 정렬 | 순서와 객체 수명 유지. 배치 추적용 이름 복제 제거 |
| Drawing / Hud | 월드 투영·공통 출력, HUD·지도·대화·메뉴 | 알파/글꼴/문구·UV 유지. 문자열 레이아웃 반복 측정 제거 |
| Renderer / RenderQueue | 3D 인스턴스, 그림자, 2D 배칭, FBO/MSAA, 후처리, GPU 해제 | 모델 배치·업로드·캐시를 최적화. 패스 순서 유지 |
| ModelLibrary / ResourceAssets / Animation | 모델 식별, v3 로딩·검증, 공유 뱅크, 본 보간·FK·스키닝 | 활성 블렌드 그래프만 로컬 표본 준비. 몸체/헤어 중복 샘플 제거 |
| RenderAssets / AssetCache / RenderCache | PNG 디코딩·아틀라스, GDI 문자, 디스크 캐시, 기존 2D 메시 캐시 | 픽셀 왕복·중복 보관 제거. GPU 자원 해제 책임 명확화 |
| Profiler / Monitoring / monitor | CPU/GPU 계측, 상태·모델 JSON, 로컬 HTTP·대시보드 | 스레드 수 유지. 큰 응답 복사를 잠금 밖으로 이동. 실제 시작 단계 로그 추가 |
| tools / vcxproj / tests | 원본 GLB 변환, 캐시 검증, 증분 준비·배포, CPU/GPU 회귀 검사 | 파일 포맷/변환기/배포 계약 유지. 기존 검사에 회귀 사례 추가 |

프레임마다 Scene이 배치를 방문하고 Actor 콜백을 갱신한다. 컬링은 렌더 제출만 줄이며 시뮬레이션을
중지하지 않는다. 월드 3D는 동일 메시·포즈의 인스턴스를 모아 바닥 → 그림자 → 표면 순서로 출력한다.
라벨/발광 등은 모델 패스 뒤에, HUD는 기존 painter order로 합성한다. RenderQueue의 연속 상태 배칭과
3.3 미지원 CPU 경로를 유지했다. 정적 배치를 모두 증분화하거나 오픈 월드 스트리밍을 구현한 것은 아니다.

## 2. 적용한 변경

### 시작 준비와 메모리

- `LayeredAnimation::PrepareGraph`: 리그의 현재 idle/walk/run/aim/fire/melee/carry/reload 및 방향 표본이
  참조하는 클립만 로컬 본 표본으로 변환한다. 모든 원본 클립의 행렬·총구 데이터는 그대로 유지한다.
  실제 렌더 중 처음 사용하면서 변환하는 지연 초기화는 추가하지 않았다.
- 현재 로컬 v3 팩의 인체 공유 뱅크는 64클립이다. 로컬 변환은 **64→26클립, 1,652→725프레임**이며
  `BonePose`의 10개 float 기준 저장량은 **1,519,840→667,000바이트**다. 신규 콘텐츠에서 다른 클립을
  블렌드 그래프에 연결할 때 `PrepareGraph`의 참조 목록과 회귀 검사를 함께 갱신한다.
- 공통 재질 4개를 CPU에서 생성한 뒤 GPU로 올리고 `glGetTexImage`로 다시 읽던 경로를 제거했다.
  동일 픽셀을 CPU에서 최종 아틀라스에 조립하고 한 번 업로드한다. 캐시 파일 버전·픽셀 공식은 유지한다.
- UI PNG는 최종 아틀라스의 해당 영역으로 바로 디코딩한다. 기존 127개 PNG의 별도 RGBA 버퍼 합계
  **5,126,768바이트**와 후속 복사를 없앴다. 최종 아틀라스는 기존과 같은 2048×2048이며, 크기 확장 중
  벡터 재할당과 디코더 내부 버퍼까지 포함한 전체 최대 메모리를 실측한 수치는 아니다.
- `RenderAssets::Data`가 텍스처 해제를 소유하여 생성자 중간 실패에서도 이미 만든 텍스처를 정리한다.
- GPU 메시의 정점 레이아웃을 `ResourceVertex`로 통일했다. imported 메시를 바로 업로드하고 VBO
  소유자에는 정점 수만 남긴다. 충돌·CPU 폴백·모델 도감이 사용하는 ResourceAssets의 CPU 원본은 유지한다.

### 프레임 처리

- `FrameSchedule`: 업데이트가 끝난 시점부터 고정 16ms를 추가하던 예약을 60Hz 절대 목표 시각으로
  바꿨다. 실제 경과 시간과 기존 50ms 시뮬레이션 상한은 유지한다. 장시간 지연은 재기준화하며
  무제한 catch-up 루프나 busy wait를 사용하지 않는다. GLUT/OS 타이머 정밀도와 Present 지연은 여전히 실측 대상이다.
- 모델 배치 키: 포즈당 2,048바이트 행렬을 문자열로 소유하던 방식을 비소유 해시로 바꿨다.
  후보마다 메시·무장 상태·포즈 유무와 **전체 행렬 바이트를 다시 비교**하므로 해시 충돌로 배치를 합치지 않는다.
- 배치별 인스턴스 벡터와 오버레이 벡터의 용량을 재사용한다. 현재 패스의 활성 배치 수만 순회하며
  제출이 끝나면 인스턴스와 메시 참조를 비운다. 저장 용량은 해당 Renderer 수명 동안의 최대 사용량이다.
- 모델 셰이더 uniform 위치를 링크 직후 한 번 구한다. 패스/배치마다 이름으로 드라이버를 조회하지 않는다.
- 머터리얼 레지스트리는 512개 LRU로 제한했다. 팔레트를 양자화하지 않는다. 축출된 머터리얼은 이를
  참조하는 Actor가 있는 동안 살아 있고, 다음 제출에서 현재 팔레트로 교체된다.
- `ResourcePoseSampler`: Renderer 소유 단일 항목 캐시가 동일 불변 리그·뱅크와 정확히 같은 그래프 입력을
  사용하는 연속 몸체/헤어 제출의 스키닝 행렬을 공유한다. 탈것과 비인체 경로는 기존 Sample을 호출한다.
  게임플레이의 총구 계산용 Sample은 상태 없는 원래 경로를 유지한다.
- Scene은 두 개의 이름 집합을 매 프레임 만들고 복사하는 대신 이름별 배치 세대를 기록한다.
  이름이 살아 있으면 저장 노드를 재사용하고, 이번 프레임에 없는 객체는 기존처럼 제거한다.
- 주민 A*는 세대 번호가 붙은 탐색 노드와 힙 저장소를 소유한다. 25,233노드 전체의 세 배열을 매 요청
  할당·초기화하지 않고 방문 노드만 초기화한다. 비용·휴리스틱·동률 순서·횡단 정보는 변경하지 않았다.
- `RenderAssets::WrapUtf8`가 기존 줄바꿈 알고리즘을 소유한다. 문자 폭/줄바꿈 결과를 각각 256개 LRU로
  보관하며 4,096바이트 초과 문자열은 캐시하지 않는다. 글꼴 크기·폭·원문이 키에 포함되고 원문 생략은 없다.
  `Dialogue::Wrap`은 호환 진입점으로 위임한다.

### 스레드와 계측

추가 스레드는 도입하지 않았다. GL·GDI·게임 상태·경로 탐색 scratch·렌더 캐시는 메인 스레드 소유다.
기존 HTTP 스레드는 완성된 불변 JSON을 `shared_ptr<const string>`으로 받아 처리한다. mutex 안에서는
포인터만 교환하고 큰 문자열의 복사·HTTP 응답 생성·이전 응답 해제는 밖에서 수행한다.

게임 콘솔의 `startup_ready` JSON에 창/GL 컨텍스트, 렌더 리소스, 월드, 모니터, 전체 준비 시간이 출력된다.
이 값은 이벤트 루프 진입 전까지이며 첫 프레임의 지연 GPU 업로드는 포함하지 않는다. 기존 StartupTests의
CPU 월드 준비 수치만으로 사용자가 느끼는 전체 시작 시간을 판단하지 않도록 구분했다.

## 3. 콘텐츠 확장 계약

- 새 콘텐츠는 기존 Level/Scene/Hud/Weapon 계열과 ModelLibrary의 안정적인 모델 식별자를 사용한다.
  프레임마다 임시 Model을 만들지 않는다. Renderer의 메시 캐시 키는 Model의 수명 동안 안정적인 주소다.
- 원본 메시·애니메이션 뱅크는 로딩 후 불변이다. 동적 상태는 Game/Level, 포즈 입력은 CharacterAnimation,
  GPU 수명은 Renderer가 소유한다. 리소스 레지스트리의 런타임 수정과 GPU 호출을 작업 스레드로 옮기지 않는다.
- 캐시 키가 사용하는 CharacterPose 입력을 추가하면 `ResourcePoseSampler`와 직접 Sample 비교 검사를 갱신한다.
  새로운 상체/하체 그래프 참조를 추가하면 `PrepareGraph`도 갱신한다.
- UI 원문을 이미지에 굽거나 알파를 고정하지 않는다. 새 화면도 공통 문자 측정/줄바꿈과 sprite ID를 사용한다.
- 도로 모델 변경은 통행·충돌·신호·고도 계약과 별도로 검증한다. 모델 교체로 기존 길이나 횡단 위치를 바꾸지 않는다.
- 요청서: [UI](../Resource/UI/PERFORMANCE_RESOURCE_REQUEST.md),
  [모델링·애니메이션](../Resource/RENDER_RESOURCE_REQUEST.md). 작성만 완료했으며 다른 에이전트에 메시지를 보내지는 않았다.

## 4. 확인 결과와 사용자 빌드 후 검증

빌드·게임 실행·네이티브 테스트 실행·FPS 측정은 수행하지 않았다. 기존 C++17과 프로젝트 소스 목록을 유지한다.
회귀 검사 코드는 다음에 추가했다.

| 검사 | 확인하려는 회귀 |
|---|---|
| GameplayTests | 60Hz 예약의 누적 작업시간·장시간 지연, 한글 줄바꿈/폭 캐시 축출 뒤 결과 유지 |
| ResourceAssetsTests | 공유 뱅크 API 정정, 그래프 클립 사전 준비, 원본 반응 클립 보존, 캐시/직접 포즈 샘플 일치 |
| SceneGraphTests | 반복 배치 시 Actor 정체성, 임시 객체 제거, 빈 배치 정리 |
| RenderingTests | 대량→소량→빈 배치 후 같은 이미지, 기존 그림자/깊이/알파/인스턴싱 경로 |
| MonitorServerTests | 6MiB 도감을 교체하는 동안 읽는 응답의 완전성·불변 수명 |

기존 GameplayTests의 도시/주민 경로·광산·퀘스트, CivicTests, RoadPlanTests, MapHudTests,
MonitorServerTests의 대용량 응답/재시작과 RenderingTests 전체를 사용자 빌드 후 함께 확인한다.
이 문서가 이 테스트들의 통과를 뜻하지 않는다.
검사 빌드에서는 `assert`가 활성화되어 있어야 한다. RenderingTests의 benchmark JSON에
`p99_ms`, `over_budget_frames`, `sample_frames`를 추가했다. 숨겨진 WGL/`glFinish` 검사는 실제 창의
Present·타이머 주기를 측정하지 않으므로 사용자 게임의 연속 플레이 측정도 필요하다.

읽기 전용 `node tools/check-resource-pack.mjs` 검사는 기존 Runtime 캐시의 입력 불일치로 실패했다.
변경 구간에 저장소 clang-format을 적용했고 `git diff --check`, 프로젝트의 C++ 소스/헤더 51개 참조,
새 요청서/보고서의 상대 링크와 UTF-8 읽기를 확인했다. 이는 C++ 컴파일 검사를 대신하지 않는다.
`resourcePackStatus`에서 확인한 항목은 **SimpleGame/RoadPlanCatalog.h**다. 이 파일과 변환 입력은 이번 작업에서
수정하지 않았다. 기존 자동 리소스 준비를 통해 재생성해야 하며, 타임스탬프가 보존된 작업 폴더라면 사용자 빌드에서
`/p:ForceResourceValidation=true`를 사용해 SHA 검사를 강제할 수 있다. 여기서는 캐시를 재생성하지 않았다.

성능 비교는 동일 PC의 Release x64·동일 해상도/품질·동일 위치에서 한다. 첫 실행/재실행 시작 로그를 각각
3회 기록하고, 도시 교차로·바이크/고가·최대 수배·광산 전투·긴 대화에서 준비 프레임 후 30초 이상 측정한다.
평균뿐 아니라 프레임 간격 p95/p99, 16.67ms 초과 횟수, update/world/HUD/Present·GPU 비용을 함께 비교한다.
기존 기준과 동률 경로·맵/충돌 지문, 문구·장면·알파 결과를 먼저 맞춘 후 FPS 개선을 판정한다.

## 5. 남은 측정 대상

전체 소스 계열을 검토했지만 모든 파일의 형태를 바꾸지는 않았다. 기능 위험 없이 반복 비용을 줄일 수 있는
공통 경로에 변경을 집중했다. 아직 전체 정적 장면 배치의 증분 갱신, GPU pose palette/텍스트 glyph atlas,
월드 스트리밍, 첫 방문 모델 업로드 분산은 도입하지 않았다. 모니터 초기 ModelCatalog 직렬화도 메인 스레드에서
수행되므로 새 `monitor_ms`로 비용을 확인해야 한다. 지금은 변경 후 실측이 없으므로 60FPS 달성이나 시작 시간의
특정 감소율을 보장하지 않는다.
