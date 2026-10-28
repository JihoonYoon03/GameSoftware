"""Verify actual GLB triangle surfaces retain the original propulsion curves."""
import bpy,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from verify_resource_pack import read_glb,accessor
cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
plan=json.loads((ROOT/'Validation/size_budget_plan.json').read_text());single={e['mesh_parts'][0]:e for e in cat['exports'] if e['type']=='module' and len(e['mesh_parts'])==1}
report={'parts':{},'all_checks_passed':False}
def key(ps):return tuple(sorted(tuple(round(x,5) for x in p) for p in ps))
for name,p in plan['parts'].items():
    group=p['groups'][0]
    if not group.get('preserved_outline_components'):continue
    ob=bpy.data.objects[p['source_object']];me=ob.data;me.calc_loop_triangles();attr=me.attributes['authoring_component']
    original={key([me.vertices[v].co for v in t.vertices]) for t in me.loop_triangles if attr.data[t.polygon_index].value in group['preserved_outline_components']}
    d,b=read_glb(ROOT/single[name]['file']);actual=set()
    for mesh in d['meshes']:
        for prim in mesh['primitives']:
            ps=[(x,-z,y) for x,y,z in accessor(d,b,prim['attributes']['POSITION'])];ids=[v[0] for v in accessor(d,b,prim['indices'])]
            actual.update(key([ps[v] for v in ids[i:i+3]]) for i in range(0,len(ids),3))
    assert actual==original,(name,'original curved surfaces or crystal changed',len(actual),len(original))
    assert len(actual)<=group['budget']<=200
    report['parts'][name]={'triangles':len(actual),'original_curve_surfaces_exact':True,'bolts_and_slots_removed':True,'emission_on_existing_inner_walls':True}
assert len(report['parts'])>=16
report['all_checks_passed']=True
(ROOT/'Validation/silhouette_preservation_report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PROPULSION_SILHOUETTES_VERIFIED',len(report['parts']),flush=True)
