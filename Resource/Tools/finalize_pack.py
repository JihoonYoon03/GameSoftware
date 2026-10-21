"""Finish metadata and confirm the reviewed deliverable inventory."""
import json,ast
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):return json.loads((ROOT/p).read_text(encoding='utf-8'))
catalog=read('Documentation/asset_catalog.json')
profiles=read('Documentation/body_profiles.json');access=read('Documentation/accessory_sockets.json')
BODY_PROFILES=profiles;ACCESSORY_META=access['accessories']
tree=ast.parse((ROOT/'Tools/human_design_v2.py').read_text(encoding='utf-8'))
node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='accessory_offset')
exec(compile(ast.Module(body=[node],type_ignores=[]),'accessory_fitting','exec'),globals())
roles=list(access['default_sets'])
fitting={build:{role:{name:accessory_offset(name,build,role) for name in ACCESSORY_META} for role in roles} for build in profiles}
(ROOT/'Documentation/accessory_fitting_offsets.json').write_text(json.dumps(fitting,indent=2),encoding='utf-8')
source=read('Validation/source_report.json');exports=read('Validation/export_report.json');roundtrip=read('Validation/roundtrip_report.json');design=read('Validation/design_report.json')
assert not source['issues'] and exports['all_checks_passed'] and roundtrip['all_checks_passed'] and design['all_checks_passed']
assert len(profiles)==6 and len(roles)==9 and len(ACCESSORY_META)==34
assert design['distinct_outfit_geometries']==9 and design['vehicle_parts']==122
for sex in ['Male','Female']:
    m=[design['body_profiles'][sex+'_'+mass] for mass in ['Lean','Normal','Heavy']]
    assert m[0]['waist_width_m']<m[1]['waist_width_m']<m[2]['waist_width_m']
    assert m[0]['waist_depth_m']<m[1]['waist_depth_m']<m[2]['waist_depth_m']
for mass in ['Lean','Normal','Heavy']:
    male=design['body_profiles']['Male_'+mass];female=design['body_profiles']['Female_'+mass]
    assert male['chest_width_m']>female['chest_width_m']
    assert female['hip_width_m']>male['hip_width_m']
expected=['Source/Erma_Resource_Library.blend','Preview/00_asset_overview.jpg','Preview/06_body_profiles.png','Preview/07_outfit_purposes.png','Preview/08_outfits_grayscale.png','Preview/09_vehicles.png','Preview/10_vehicle_variants.png','Preview/11_vehicle_exploded.png','Preview/Walk.gif','Preview/Run.gif','Preview/Turn_Left_90.gif','Preview/Attack_Melee.gif','Preview/Attack_Fire.gif','Preview/Jump_Full.gif','Preview/Vehicle_Access.gif']
assert all((ROOT/f).is_file() and (ROOT/f).stat().st_size>0 for f in expected)
assert len(catalog['exports'])==len(list((ROOT/'Export').rglob('*.glb')))
ids={a['id'] for a in catalog['assets']}
for record in catalog['exports']:assert set(record['mesh_parts'])<=ids
report={'schema':'erma_delivery_v2','blender':'4.5.2 LTS','body_profiles':6,'outfit_purposes':9,'fitted_outfit_sets':54,'independent_accessories':34,'vehicle_types':5,'vehicle_parts':122,'unique_mesh_parts':len(catalog['assets']),'max_triangles_per_part':exports['max_triangles_per_mesh'],'glb_files':len(catalog['exports']),'roundtrip_files':len(roundtrip['files']),'all_checks_passed':True,'source':'Source/Erma_Resource_Library.blend','preview':'Preview/00_asset_overview.jpg','game_code_modified':False}
(ROOT/'Validation/final_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
