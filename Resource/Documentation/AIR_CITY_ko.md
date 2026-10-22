# 비행체 · 도시 · 도로 · 야외 프롭 확장

2026-10-22 · Blender 4.5.2 LTS · 에르마/헤일로 SF 개척지용 조립식 로우 폴리 자산.

기존 팩의 청록·아이보리·산업용 황토색 외장, 금속 골조, 청록 발광 장치와 선행 문명의 보라색 흔적을 재사용했습니다. 비행체 4종의 A/B 예제 8개와 배경 조립체 34종을 추가했습니다. 새 독립 메시 **218개(비행체 82 / 배경 136)**는 삼각형 변환 후 **최대 352개**입니다. 완성 모델은 여러 파트를 유지하므로 모델 전체 합계가 500개를 넘을 수 있습니다.

디자인 수정본 `sf_field_drive_v2`에서는 노출된 팬·로터 축·가로대를 없앴습니다. 비행체 추진부는 3개의 분절된 외장 코일과 안쪽 발광 아크, 공중에 떠 있는 코어로 구성합니다. 날개의 분리된 외장판, 기울어진 꼬리 필드 베인, 엔진의 발광 띠로 미래형 기술을 표현했습니다. 충전기는 비접촉 유도 방식, 통신 중계기는 분절된 부유 배열, 정류장은 갈라진 쉘 지붕, 태양광 장치는 네 장의 광자 수집 패널로 바꿨습니다.

형태 참고는 [No Man’s Sky의 공식 Interceptor 자료](https://www.nomanssky.com/interceptor-update/)와 [Star Citizen의 공식 ARGO SRV 디자인 소개](https://robertsspaceindustries.com/en/comm-link/transmission/16971-The-ARGO-SRV)입니다. 기체 분절과 산업용 모듈의 조형을 참고했으며 메시·텍스처·로고는 가져오지 않았습니다. 제작물은 이 팩의 조립 규격과 팔레트에 맞춘 독자 형상입니다.

## 파일과 Blender 장면

- `Source/Erma_Resource_Library.blend`: 기존 자산과 함께 편집 가능한 전체 원본. 기본 장면은 `10_Aircraft`.
- `00_Library` → `RESOURCE_LIBRARY` → `10_Air_City_Expansion`: 공통 원점에 있는 실제 교체 파트. 같은 종류의 모든 선택지를 동시에 표시하면 겹칩니다.
- `10_Aircraft` / `10b_Aircraft_Variants`: 4종의 기본 / 대체 조합.
- `10c_Aircraft_Exploded`: Warden 파트 분해 배치. 분해도 위치는 실제 조립 좌표가 아닙니다.
- `11_City_Frontier_Props` / `11b_Road_Modules`: 프롭 도감과 도로 모듈 도감.
- `12_Airfield_Outpost`: 에어택시·착륙장·정류장·전력·물류 시설을 조합한 배치 예제.
- `13_Field_Tech_Details`: 부상 필드·비접촉 충전기·차량 공중 신호기·보행자 바닥 홀로그램을 가까이 확인하는 장면.
- `Export/Aircraft`: 조립 GLB 8개. `Parts`에 독립 교체 GLB 82개.
- `Export/Scenery`: 배경 조립 GLB 34개. `Parts`에 독립 파트 GLB 136개.
- `Preview/12_aircraft.png`부터 `17_field_tech_details.png`: 렌더 7개. 도로 도감은 `15b_road_modules.png`.

## 비행체

| 타입 | 설계 길이 / 날개폭(m) | 용도·실루엣 | 대체 적재 모듈 |
|---|---|---|---|
| Kite_Courier | 6.4 / 7.8 | 1인승 운송기, 후퇴익과 2개 부상 필드 | 강화 화물 보관함 |
| Manta_AirTaxi | 7.2 / 8.6 | 4좌석, 4개 부상 필드, 밀폐 승객실 | 의료 후송실 |
| Mule_Skycrane | 9.2 / 9.6 | 4개 부상 필드, 외측 붐과 하부 적재실 | 개방형 적재 플랫폼 |
| Warden_Survey | 7.8 / 9.0 | 분절된 주익, 2개 짐벌 필드, 하부 탐사 센서 | 보라색 분석 렌즈·측면 장비 |

설계 치수는 프레임 기준이며 꼬리 베인·부상 필드·선택 모듈의 실제 외곽은 다를 수 있습니다. 조립 파일의 좌표를 그대로 사용하십시오.

`aircraft_assemblies.json`의 `types`에 슬롯과 가능한 파트, `default_parts`와 `alternate_parts`가 있습니다. 한 타입 안에서 각 슬롯의 파트를 하나씩 선택합니다. 캐노피(유리/강화), 좌우 엔진(효율/부스트), 적재 모듈(기본/대체)을 독립적으로 바꿀 수 있습니다. 선체·좌우 날개·부상 필드·좌석·꼬리날개·착륙 스키드·정비 해치도 별도 메시입니다. Mule의 외측 붐은 별도 파트입니다.

**단위는 미터, Blender +Z 위 / +X 전방**이며 기존 지상 차량과 같습니다. GLB는 +Y 위 / +X 전방으로 변환합니다. 게임 기획의 1월드 단위=2m를 적용할 때만 미터 좌표를 0.5배 합니다. 타입마다 크기와 부착 위치가 달라 서로 다른 타입의 날개·캐노피를 직접 혼용하지 않습니다.

파트 원점은 모두 해당 비행체의 공통 지면 중심입니다. 별도 GLB 파트를 불러올 때 위치를 재중앙화하지 마십시오. 리그·본 이름·바인드 포즈를 공유해 조립하고, GLB별로 들어 있는 중복 리그는 하나로 재바인딩합니다. 완성 GLB는 이미 조립되어 있으며 메시 이름을 기준으로 교체할 수 있습니다. 새 추진 파트 이름은 `LiftField_*`이며 이전 `LiftDuct_*`는 카탈로그와 내보내기 폴더에서 교체했습니다.

## 리그와 동작

각 타입은 **17본**, 정규화된 스킨 가중치, 30fps 키프레임을 사용합니다. `root`, `hull`, `canopy`, `cargo`, `sensor`, `driver_socket`, `passenger_socket`, 좌우 `wing`, `engine`, `lift_front`, `lift_rear`, `gear` 본을 포함합니다. 실제 부상 필드 수는 기체별로 다르므로 공통 리그의 일부 본에 연결된 메시가 없는 타입도 있습니다. 좌석 소켓은 탑승 참조점이며 탑승자 포즈는 별도 작업입니다.

| 클립 | 길이 | 용도 |
|---|---|---|
| Aircraft_Reference | 1/30초 | 착륙 스키드 전개 상태의 바인드 포즈 |
| Flight_Idle / Cruise | 각 2초, 반복 | 부상 필드·추진기 짐벌의 미세 안정화 |
| Boost | 1초, 반복 | 더 큰 추진기 짐벌 변화 |
| Bank_Left / Bank_Right | 각 1초 | 좌우 롤 후 기준 자세 복귀 |
| LandingGear_Deploy / Retract | 각 1초 | 착륙 스키드 전개/수납 |
| Canopy_Open / Close | 각 1초 | 캐노피 개폐 |
| Cargo_Open / Close | 각 1초 | 별도 정비 해치 개폐 |

비행 이동은 제자리 클립이며 실제 위치·속도·고도는 게임 Actor가 제어합니다. `Flight_Idle`, `Cruise`, `Boost`는 부상 코일과 추진기의 짐벌 동작입니다. 기계식 로터는 없습니다. 화물 클립은 적재실 전체가 아닌 `ServiceHatch`를 움직입니다. 비행 중 롤은 지면에서 띄워 재생하십시오.

Blender 라이브러리 리그의 NLA 트랙은 기본 음소거 상태입니다. Action Editor에서 `AIRCRAFT_타입__클립` 하나를 선택해 재생합니다. 모든 트랙을 동시에 켜지 마십시오. 갤러리는 형태 확인용 고정 배치이며 애니메이션은 원본 리그나 완성 GLB에서 확인합니다.

## 배경 조립체 34종

| 분류 | 자산 |
|---|---|
| 도로·보행 | Road_2Lane_4m, Road_4Lane_4m, Road_6Lane_4m, Road_Crossroad_12m, Road_TJunction_12m, Road_Curve90_R9m, City_Sidewalk_2x4m, Road_SpeedTable_4m, Road_CrashBarrier_4m |
| 도시 시설 | City_Streetlight_4m, City_TrafficSignal, City_PedestrianHologram_6m, City_Bollard, City_Bench, City_Planter, City_TransitShelter, City_Wayfinding, City_ChargePost |
| 산업·기반 시설 | Utility_Transformer, Utility_CargoContainer_6m, Utility_HVAC, Utility_PipeRack_4m, Utility_CommRelay, Utility_SolarArray, Utility_WaterTank |
| 비행장 | Airfield_LandingPad_16m, Airfield_Beacon |
| 탐사·광산 | Frontier_SurveyTripod, Frontier_FieldShelter, Frontier_MiningConveyor |
| 유적·자연 | Ruin_ObserverMonolith, Ruin_FracturedArch, Nature_PrismBloom, Nature_SailTree |

`scenery_assemblies.json`과 공통 `assembly_recipes.json`에 파트 목록이 있습니다. 프롭은 베이스·골조·표면·장비·발광부를 분리했으며 모두 같은 원점에 놓으면 조립됩니다. 배경 프롭에는 스켈레톤을 넣지 않았습니다. 신호등 상태 변경과 컨테이너 문 회전은 게임에서 독립 메시·재질을 제어하는 방식으로 연결합니다.

## 차량 공중 신호와 보행자 바닥 홀로그램

`City_TrafficSignal`은 **차량 전용**이며 중심 높이 Z=5.25m에 떠 있습니다. 발판·기둥·보행자 신호 헤드는 없습니다. 공통 원점은 도로 지면이고 표시 면은 Blender -Y 방향입니다. `HoverHousing`, `FieldStabilizers`, `StopGlyph`, `WaitGlyph`, `GoGlyph`의 5개 파트로 조립합니다.

기본 조립은 정지 표시만 빨간 `Signal_Stop` 재질로 켜져 있습니다. 대기·진행은 비발광 `Glass_Opaque` 상태입니다. 상태 변경 시 `scenery_assemblies.json`의 `signal_states`에서 지정한 파트에 각각 `Signal_Stop` / `Glow_Amber` / `Glow_Cyan`을 적용하고 나머지 두 표시는 `Glass_Opaque`로 바꿉니다. 하나의 활성 상태만 표시하는 자산 규칙이며 실제 교통 로직은 이 리소스 작업에 포함하지 않았습니다.

`City_PedestrianHologram_6m`은 바닥 투사기 4개·보행 그리드·안내선으로 구성하며 전체 높이가 0.15m 이하입니다. 보행자 표시는 도로 표면에만 있습니다. 12m 교차로의 횡단보도도 같은 청록 발광 노면 방식으로 맞췄습니다. `12_Airfield_Outpost`와 `13_Field_Tech_Details`에서 공중 차량 신호와 바닥 보행자 표시를 함께 확인할 수 있습니다.

## 도로 연결 규격

직선은 **길이 4m, 차선 폭 3m, 진행 축 +X**입니다. 2/4/6차선 전체 폭은 각각 6/12/18m입니다. 원점에서 X=-2와 +2가 접합면이므로 X 방향으로 4m씩 반복 배치합니다. 각 차선의 노면, 중앙·차선 표식, 가장자리 표식은 별도 파트입니다. 표면 높이는 Z=0, 표식은 그 위에 얇게 올라갑니다.

12×12m 사거리와 T자 교차로는 4차선 도로용입니다. 접합면은 X=±6과 Y=±6이며, T자의 +Y 쪽은 닫힌 가장자리입니다. 2/6차선 도로의 폭 전환용 테이퍼는 포함하지 않았습니다.

90도 곡선은 **2차선용**, 중심선 반경 9m, 안쪽 반경 6m, 바깥쪽 12m입니다. 다른 모듈과 달리 원점이 원호 중심이며 1사분면에 형상이 있습니다. 중심선 접합점은 (9,0), (0,9), 접선은 각각 Y축과 X축입니다. 직선의 원점·회전을 이 접합점에 맞춰 연결합니다. `City_Sidewalk_2x4m`은 4×2m, 가드레일·파이프랙·컨베이어는 길이 4m 반복 모듈입니다.

착륙장은 4×4m 바닥 16개를 조립한 **16×16m** 데크입니다. 원점은 데크 중심이며 현대적인 H 표시를 분절된 원형 착륙 유도 레티클로 바꿨습니다. 레티클과 네 모서리 유도등은 별도 파트입니다.

## 검증과 재생성

`Validation/air_city_source_report.json`은 새 파트의 삼각형 수·퇴화 면·유효 좌표·UV·가중치를 검사합니다. 공통 `Validation/export_report.json`은 전체 GLB의 실제 메시별 삼각형 수, 법선·UV·스킨·클립을 검사합니다. `Validation/air_city_roundtrip_report.json`은 비행체 A/B 8개와 대표 배경 8개를 Blender로 재가져와 본·메시 수, 실제 변형, 반복 경계, 개폐 클립의 반대 끝점, 주요 도로/데크 치수를 검사합니다. 차량 신호기가 공중에 있고 보행자 표시가 바닥 높이에만 있는지도 확인합니다.

제작 도구는 `Tools/build_air_city_expansion.py`입니다. 기존 원본을 열어 이 확장에 속한 데이터만 갱신하고 기존 자산은 보존합니다. 같은 확장 파트를 수작업으로 수정한 경우 별도 원본으로 보관한 뒤 실행하십시오. 기존 전체 팩 제작 도구를 처음부터 실행했다면 이 확장 도구도 다시 실행해야 합니다.

```text
blender --background --python Resource/Tools/build_air_city_expansion.py
python Resource/Tools/verify_resource_pack.py
blender --background --factory-startup --python Resource/Tools/verify_air_city_roundtrip.py
```

이번 확장은 Resource 안에만 저장했습니다. 게임 코드·런타임 캐시·현재 월드 배치는 변경하지 않았으며, 게임에서의 비행 조작·충돌·LOD·탑승·이펙트·성능 검증은 포함하지 않았습니다.
