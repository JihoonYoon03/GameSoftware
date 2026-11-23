# 공유 재질 glTF 시범 납품 · 2026-11-23

갱신된 그래픽 요청의 GFX-01·03 첫 단계입니다. 기존 9종의 형상과 조립 구조를 사용해 외부 공유 이미지를 참조하는 편집·검토용 glTF를 만들었습니다. Blender의 [공유 텍스처 작업 원본](../Source/Shared_Texture_Pilot.blend)과 같은 도장 합금·노출 금속·의복·보도 표면을 파일 수준에서 공유합니다. 피부는 원래 피부색을 유지한 미세 질감의 시범 옵션을 추가했습니다.

| 검토 장면 | glTF 시범 리소스 | 유지한 입력 |
|---|---|---|
| 거주 건물 | [Habitat](../Export/MaterialPilot/habitat.gltf) | 원본 벽·창·설비, 콘크리트/패널 맵, 유리 알파 |
| 30° R18 도로 | [차도](../Export/MaterialPilot/curve.gltf) · [보도](../Export/MaterialPilot/sidewalk.gltf) · [페인트](../Export/MaterialPilot/paint.gltf) | 차도·보도·차선의 독립 파트, 기존 곡선 UV와 접속 위상 |
| 8.4m 상승 램프 | [램프 노면](../Export/MaterialPilot/ramp.gltf) | 표면 세 구간, 접속 좌표와 기존 UV |
| 차량 | [Coupe](../Export/MaterialPilot/coupe.gltf) | 16본·14클립, 조립/가동 노드, 유리와 발광 상태 |
| 플레이어 | [Explorer](../Export/MaterialPilot/explorer.gltf) | HUMAN_7H 23본·98클립, 스킨·바인드와 부착 노드 |
| 광산 | [암석](../Export/MaterialPilot/mine_rock.gltf) · [지지대](../Export/MaterialPilot/mine_support.gltf) | 기존 암석 맵과 지지대 발광 재질 |

glTF는 `Export/MaterialPilot`에서 옆의 BIN과 공유 이미지의 상대 경로를 함께 사용합니다. [재질·텍스처·파일 명세](material_pilot.json)에는 안정 ID, 해시, 슬롯·스킨 참조, 맵 경로와 예상 바이트가 있습니다. 원본 내부 이미지 9개를 공유 PNG로 연결하기 전에 디코딩 픽셀을 비교했습니다. 같은 멤버의 화상은 여러 모델에서 같은 경로를 참조합니다.

## 제작과 검수

- BaseColor는 sRGB, Normal과 ORM은 선형입니다. 노멀은 tangent-space +Y이며 원래 `TANGENT.w`를 사용합니다. AO와 roughness/metallic은 같은 ORM 이미지의 R과 G/B를 읽습니다. 유리 알파, 발광 상수, 동적 색의 원래 값은 glTF에 남겨 두었습니다. [Khronos glTF 재질 규격](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)
- 텍스처는 반복 타일과 repeat/trilinear 기본값으로 연결했습니다. 시범본은 도로 및 보도 접속부의 기존 UV 위상을 유지합니다. 기본 레벨부터 1px까지 검사한 색 맵은 선형광으로 축소했고, 노멀 벡터는 축소 후 정규화했습니다. PNG로 기본 레벨을 제공합니다. KTX2와 압축 밉 배포는 후속 통합 범위입니다.
- 9종의 모든 glTF 접근자 값과 노드·메시·스킨·애니메이션 데이터를 원본과 대조했습니다. 텍스처 적용 프리미티브의 UV 퇴화와 무효 탄젠트는 0건이며, 개별 원래 파트의 재질을 합산해 200삼각형 상한을 확인했습니다. [정적 검사 결과](../Validation/material_pilot.json)
- Blender 4.5.2로 총 12장(6개 장면의 전후)을 같은 카메라·조명·노출로 렌더했습니다. 곡선의 차선/보도, 램프 경사, 차량 유리, 인물 의복, 광산 조명 아래의 암석/금속을 확인했습니다. [전후 비교](../Preview/MaterialPilot/comparison.png) · [Blender 가져오기 및 촬영 기록](../Validation/material_pilot_blender.json)

## 시범 범위에서 확인할 비용

9개 텍스처 세트는 27개 맵 참조로 구성됩니다. 같은 콘텐츠 해시는 한 번만 계산했으며, 전부 RGBA8과 전체 밉으로 동시에 상주하면 약 82.7MiB입니다. 디스크 용량과 별개인 추정치입니다. 도로는 프리미티브별 중앙값 약 251~256 texel/m, 보도는 512 texel/m입니다. 현재 UV와 맵을 그대로 공유한 값이므로, 월드 표면의 128 texel/m 시작값과 비교할 때는 접속 위상을 유지한 저해상도 맵과 화면 판독성을 함께 검토해야 합니다.

기존 불투명 슬롯은 거주 건물 10개, 차량 6개, 인물 14개입니다. 이 시범에서는 원래 소재 구분을 유지했습니다. 공통 맵 공유가 슬롯 통합이나 배치 감소로 얼마나 이어지는지는 후속 렌더 통합에서 확인합니다. 실제 게임 로더·셰이더 연결과 성능 계측, GFX-02 실측 건축 및 후속 P1 자산은 남아 있습니다.

재생성은 저장소 루트에서 `python Resource/Tools/build_material_pilot.py`를 실행합니다. 공유 재질 PNG는 `Tools/build_texture_pilot.py`의 출력물을 먼저 사용할 수 있어야 합니다. Blender에서 `Tools/render_material_pilot.py`를 백그라운드 스크립트로 실행하면 같은 비교 장면이 재생성됩니다.
