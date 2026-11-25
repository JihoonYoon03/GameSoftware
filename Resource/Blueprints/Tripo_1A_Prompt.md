# 1-A. Tripo 정투영 도면 제작 프롬프트

## INPUT — 아래 항목만 파츠에 맞게 수정해서 사용

- PART_CATEGORY: <예: Clothes / Hair / Accessories / Weapons / Vehicles / Props / Buildings>
- PART_NAME: <예: Frontier_Tactical_Coat / Medic_Backpack / Pulse_Rifle>
- MOUNT_TYPE: <아래 중 택1>
  * SKINNED_GARMENT (의복·코트·상의·하의·전신슈트) -> 중립 회색 1.8m T-포즈 마네킹 착용 4뷰
  * HEAD_PART (헤어·헬멧·고글·마스크) -> 중립 회색 무표정 두상(Head) 마네킹 착용 4뷰
  * STANDALONE (배낭·파우치·무기·소품·차량·건물) -> 마네킹 없이 단독 오브젝트 4뷰
- VIEW_SET: <4-view (Front, Back, Left Side, Right Side) / 빈 의복 단독일 경우 2-view (Front, Back)>
- DESIGN_DESCRIPTION:
  <여기에 파츠의 형태, 색상, 소재, 스트랩·버클·트임·기계 디테일 등을 구체적으로 작성>

## SYSTEM PROMPT

Create a clean 1:1 square 2x2 grid orthographic turnaround sheet (Top-Left: Front, Top-Right: Back, Bottom-Left: Left Side, Bottom-Right: Right Side) for the game asset "<PART_NAME>" (<PART_CATEGORY>, <MOUNT_TYPE>) described below.

DESIGN SPECIFICATION:
<DESIGN_DESCRIPTION>

CRITICAL MULTI-VIEW CONSISTENCY RULES:

- Arrange the 4 views strictly inside 4 equal quadrants (2x2 grid) with generous pure white (#FFFFFF) padding around every quadrant so T-pose hands and silhouettes NEVER touch or cross quadrant borders. Use strictly orthographic cameras with the same scale and the same ground or center baseline relative to each quadrant.
- Every strap, seam, buckle, vent, panel line, asymmetry, silhouette, and color zone MUST match 100% continuously across Front, Back, Left Side, and Right Side views (no vanishing straps, no mismatched hems or proportions).
- If MOUNT_TYPE is SKINNED_GARMENT:
  * Wear the garment on a neutral matte-gray featureless 1.8m adult mannequin in strict horizontal T-pose (arms straight out at 90 degrees).
  * In Left Side and Right Side views, NEVER cut off the sleeves into empty armholes; show the full foreshortened T-pose sleeve cleanly extending toward the camera.
  * Keep coat/skirt splits, collar openings, and cuffs visibly separated from the inner body/trousers (never draw a solid fused cone).
- If MOUNT_TYPE is HEAD_PART:
  * Mount on a neutral matte-gray featureless mannequin head/neck bust centered identically across all views.
- If MOUNT_TYPE is STANDALONE:
  * Display the isolated object cleanly centered in every view with realistic mechanical joints and closed clean surfaces.

RENDERING STYLE (OPTIMIZED FOR TRIPO 3D RECONSTRUCTION):

- Realistic matte PBR 3D game asset studio render (Star Citizen / Starfield sci-fi frontier aesthetic).
- Even flat soft studio lighting, subtle realistic surface roughness and fabric/metal wear only.
- STRICTLY NO black ink outlines, NO 2D comic line art, NO cel-shading, NO cross-hatching, NO cast floor shadows, NO text labels, NO watermarks, NO extra background props.

AFTER IMAGE GENERATION:

- For VIEW_SET 4-view, split the generated master sheet at its exact horizontal and vertical midpoints. Export top-left as `01_front.png`, bottom-left as `02_left.png`, top-right as `03_back.png`, and bottom-right as `04_right.png`.
- Put the four PNGs in a folder named `<PART_NAME>`. Use identical square 1024 × 1024 canvases with pure white backgrounds, preserving aspect ratio, common scale, and ground or center baseline. Keep full silhouettes and white clearance. When a silhouette touches a quadrant border, regenerate the sheet before exporting.
- When VIEW_SET is explicitly 2-view for an empty standalone garment, use only the two upper quadrants for the front and back. Export only those two views.

