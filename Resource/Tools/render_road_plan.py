"""Render only the owned road-plan scenes from the saved master."""
from pathlib import Path
import sys
import bpy
ROOT=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'))
for name,file in [
    ('29_RoadPlan_Ground_Connections','32_road_plan_ground'),
    ('30_RoadPlan_Curves_Transition','33_road_plan_curves'),
    ('31_RoadPlan_Accessibility','34_road_plan_accessibility'),
    ('32_RoadPlan_Overpass_Assembly','35_road_plan_overpass'),
    ('33_RoadPlan_GradeSeparated_Crossing','36_road_plan_grade_separation'),
    ('34_RoadPlan_Structure_Details','37_road_plan_structure_details'),
    ('35_RoadPlan_Side_Elevation','38_road_plan_ramp_elevation')]:
    if '--elevation-only' in sys.argv and name!='35_RoadPlan_Side_Elevation':continue
    s=bpy.data.scenes[name];bpy.context.window.scene=s
    s.render.filepath=str(ROOT/'Preview'/f'{file}.png')
    bpy.ops.render.render(write_still=True)
    print('ROAD_PLAN_PREVIEW',file,flush=True)
print('ROAD_PLAN_PREVIEWS_COMPLETE',flush=True)
