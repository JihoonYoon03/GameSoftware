# Tripo 멀티뷰 → Blender 후처리

[1-A 도면 프롬프트](../Blueprints/Tripo_1A_Prompt.md)로 저장한 방향별 PNG 네 장을 [파츠 목록](../Blueprints/Characters/Field_Medic/tripo_jobs.json)에 연결한다. 파츠 하나를 지정하면 Tripo 제출부터 Blender 납품까지 실행한다. 완료 목록에 있거나 최종 출력 경로에 파일이 있는 파츠는 작업을 막는다.

## 실행

저장소 루트에서 실행한다. `plan`은 파일과 설정을 확인하는 로컬 작업이다. `run`은 Tripo에 이미지를 보내고 크레딧이 드는 작업을 시작한다.

```powershell
python Resource/Tools/tripo_asset_pipeline.py plan
python Resource/Tools/tripo_asset_pipeline.py plan 11_KneeGuard_L
python Resource/Tools/tripo_asset_pipeline.py run 11_KneeGuard_L --astra-session <Astra의-Codex-CLI-세션-ID>
```

Tripo 개발자 키는 실행 프로세스의 `TRIPO_API_KEY` 환경 변수로 공급한다. 국제 계정은 기본 `ov`, 중국 본토 계정은 `--region cn`을 사용한다. 키를 소스나 작업 JSON에 넣지 않는다. Codex CLI에 로그인한 환경에서 Astra의 재개 가능한 세션 ID를 전달한다. `TRIPO_ASTRA_SESSION`으로 같은 ID를 지정할 수 있고, Codex가 PATH에 없으면 `--codex`로 실행 파일을 지정한다. 이 실행기는 멀티뷰·분리·텍스처 호출에 Tripo API를 사용하고, Astra 연결에는 Codex CLI의 세션 재개를 사용한다.

## 파츠 설정

- `tripo_faces`는 Tripo 원본 요청값이다. [Tripo 원본 권장 범위](../../TripoGuide.md)를 기준으로 입력한다.
- `target_tris`는 [리소스 폴리곤 예산](POLYGON_BUDGET_ko.md)에 따라 정한 게임용 GLB 삼각형 상한이다. 대칭 파츠는 병합한 좌우의 합계를 검사한다. 현 대기 목록의 좌우 손과 무릎 보호구는 한쪽 400을 합산해 800, 구조팩은 원화 치수 대각선 약 0.55m에서 소형 800으로 잡았다.
- `has_guide: true`는 회색 마네킹·두상 등 제거할 가이드가 있는 도면에 지정한다. 가이드 제거 뒤 사용할 `texture_description`도 적는다. 단독 파츠는 기본값 `false`로 처리한다.
- `mirror_left: true`는 착용자 기준 왼쪽 원본 하나에서 대칭 양쪽을 만들 때 지정한다. 마스터의 정중면 X=0을 기준으로 복제한 뒤 병합하고, 22본 Mixamo 마스터에서 양쪽에 웨이트를 전사한다. 완성 GLB는 1개다.
- 독립 장비는 `RIGID_SOCKET`과 소켓 이름을 지정한다. 실측 설계값이 있으면 `dimensions_wdh_m`에 폭·깊이·높이를 미터로 기록한다.

## 실행 순서와 Astra 입출력

1. 네 방향 PNG를 한 번씩 업로드한다. P2로 시드를 달리해 **삼각형 후보 4개**를 생성하며, 후보 단계에서는 메시 형태를 비교한다. 후보 GLB·미리보기·완료 시점을 작업 폴더에 저장한다.
2. Astra가 네 후보의 실루엣, 비율, 누락·중복, 후처리 가능성을 비교해 `selection.json`에 점수와 근거를 남긴다. **네 번째까지 완료한 뒤 최고 점수**를 선택한다. 동점이면 완료 시점이 가장 늦은 후보를 선택한다.
3. 가이드가 있는 경우 선택본에만 Tripo 메시 분리를 실행한다. Astra가 가이드 오브젝트와 섞여 남은 가이드 면을 지우고, 사용할 형상만 병합해 `clean.glb`와 `cleanup.json`을 저장한다. 실행기는 단일 메시를 확인해 해당 파일을 텍스처 입력으로 올린다. 그 외 파츠는 선택본에 바로 텍스처를 적용한다.
4. Astra가 PBR이 적용된 GLB를 받아 [Blender 후처리 규격](TRIPO_FBX_POSTPROCESS_PROMPT_ko.md)에 따라 피팅·정리·리깅·저폴리화를 진행한다. 원본 텍스처를 유지하도록 GLB에서 재질까지 불러온다. 작업 폴더에 `delivery.blend`, `delivery.glb`를 쓴다. 실행기가 단일 메시, 폴리곤 상한, UV·탄젠트, 임베디드 Base Color, 스킨 요구를 검사한 뒤 `Resource/Source`와 `Resource/Export`에 저장한다.

기본 작업 경로는 `.cache/tripo_pipeline/<파츠 ID>/`다. `--astra-session` 없이 실행하면 각 단계의 `*_request.json`으로 인계한다. Astra가 요청에 적힌 출력 파일을 저장하면 같은 명령으로 재개한다. Astra 세션을 연결한 실행에서는 단계마다 같은 세션을 순서대로 재개한다.

## 재개

트리포 작업 ID와 선택본은 작업 폴더에 기록한다. 대기 시간이 끝나면 같은 명령으로 이어갈 수 있다. 생성 요청 중 연결이 끊겨 결과를 알 수 없으면 Tripo 기록에서 해당 작업 ID를 확인하고 아래 명령으로 연결한 뒤 재개한다.

```powershell
python Resource/Tools/tripo_asset_pipeline.py adopt 11_KneeGuard_L --task-id <Tripo-작업-ID>
```

[Tripo 멀티뷰 API](https://developers.tripo3d.ai/en/docs/generation-multiview-to-model/p) · [Tripo 메시 분리](https://developers.tripo3d.ai/en/docs/mesh-segment) · [Tripo PBR 텍스처](https://developers.tripo3d.ai/en/docs/models-texture) · [Codex 세션 재개](https://learn.chatgpt.com/docs/non-interactive-mode)
