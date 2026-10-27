"""Read-only physical-size and topology audit of every delivered part."""
import bpy,json,math,collections
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
scene=bpy.data.scenes['00_Library'];rows=[]
for a in cat['assets']:
    ob=bpy.data.objects.get(a['id'])
    if ob is None:
        candidates=[o for o in scene.objects if o.type=='MESH' and o.get('asset_id')==a['id']]
        assert candidates,a['id'];ob=min(candidates,key=lambda o:len(o.name))
    me=ob.data;me.calc_loop_triangles()
    lo=[min(v.co[i] for v in me.vertices) for i in range(3)]
    hi=[max(v.co[i] for v in me.vertices) for i in range(3)]
    dims=[hi[i]-lo[i] for i in range(3)]
    groups=collections.Counter(tuple(ob.vertex_groups[g.group].name for g in v.groups if g.weight>1e-6) for v in me.vertices)
    rows.append(dict(id=a['id'],object=ob.name,family=a['family'],slot=a['slot'],triangles=len(me.loop_triangles),
                     vertices=len(me.vertices),bounds=[lo,hi],dimensions_m=dims,diagonal_m=Vector(dims).length,
                     area_m2=sum(t.area for t in me.loop_triangles),attributes=[x.name for x in me.attributes],
                     weight_patterns={str(k):v for k,v in groups.items()}))
rows.sort(key=lambda r:-r['diagonal_m'])
report=dict(parts=len(rows),over_200=sum(r['triangles']>200 for r in rows),largest_reference=rows[0],parts_data=rows)
(ROOT/'Validation/part_budget_audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('PART_BUDGET_AUDIT',report['parts'],'OVER 200',report['over_200'],flush=True)
print('LARGEST',json.dumps(rows[:6]),flush=True)
