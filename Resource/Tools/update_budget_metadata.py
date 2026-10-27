"""Record reduced render bounds separately from retained physical envelopes."""
import json,zipfile
from pathlib import Path
from verify_resource_pack import read_glb,accessor
ROOT=Path(__file__).resolve().parents[1]
cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
assets={a['id']:a for a in cat['assets']};single={e['mesh_parts'][0]:e for e in cat['exports'] if e['type']=='module' and len(e['mesh_parts'])==1}
for pid,a in assets.items():
    d,b=read_glb(ROOT/single[pid]['file']);points=[(x,-z,y) for m in d['meshes'] for p in m['primitives'] for x,y,z in accessor(d,b,p['attributes']['POSITION'])]
    a['render_bounds']={key:[fun(p[k] for p in points) for k in range(3)] for key,fun in [('min',min),('max',max)]}
(ROOT/'Documentation/asset_catalog.json').write_text(json.dumps(cat,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
path=ROOT/'Documentation/road_plan_modules.json';kit=json.loads(path.read_text(encoding='utf-8'))
baseline=json.loads(zipfile.ZipFile(ROOT/'Source/Archives/Pre200Delivery_20261026.zip').read('Documentation/road_plan_modules.json'))
for name,module in kit['modules'].items():
    assert module['collision_envelope']==baseline['modules'][name]['collision_envelope']
    module['render_bounds']={pid:assets[pid]['render_bounds'] for pid in module['parts']}
kit['part_budget_revision']=cat['part_budget_revision'];kit['triangles_per_part_limit']=200
kit['render_collision_bounds_policy']='Render bounds may omit ground-supported undersides; collision_envelope retains the original physical contract.'
path.write_text(json.dumps(kit,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
manifest=dict(revision=cat['part_budget_revision'],counting='triangles after triangulation, summed across material primitives in the original part',max_triangles=200,
              reference=json.loads((ROOT/'Validation/part_budget_audit.json').read_text())['largest_reference'],
              split_for_budget=False,original_parts=len(assets),new_part_files=0,export_files=len(cat['exports']),
              final_triangles=sum(a['triangles'] for a in assets.values()),parts={pid:{k:a[k] for k in ['triangles','triangle_budget','size_diagonal_m','render_bounds']} for pid,a in assets.items()})
(ROOT/'Documentation/part_polygon_budgets.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print('BUDGET_METADATA_UPDATED',len(assets),flush=True)
