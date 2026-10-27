import bpy,json,collections
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
plan=json.loads((ROOT/'Validation/size_budget_plan.json').read_text())
rows={}
for name,p in plan['parts'].items():
    if p['source_triangles']<=p['groups'][0]['budget']:continue
    ob=bpy.data.objects[p['source_object']];me=ob.data;me.calc_loop_triangles();attr=me.attributes.get('authoring_component')
    if not attr:continue
    components=collections.defaultdict(list)
    for t in me.loop_triangles:components[attr.data[t.polygon_index].value].append(t)
    data=[]
    for feature,tt in components.items():
        vv={v for t in tt for v in t.vertices};lo=[min(me.vertices[v].co[k] for v in vv) for k in range(3)];hi=[max(me.vertices[v].co[k] for v in vv) for k in range(3)]
        data.append(dict(feature=feature,triangles=len(tt),materials=sorted({me.materials[me.polygons[t.polygon_index].material_index].name for t in tt}),dimensions=[b-a for a,b in zip(lo,hi)],area=sum(t.area for t in tt),normals=len({tuple(round(x,3) for x in t.normal) for t in tt})))
    rows[name]=data
(ROOT/'Validation/silhouette_component_audit.json').write_text(json.dumps(rows,indent=2))
for name in ['Aircraft_Warden_Survey_LiftField_Front_L','Outfit_HeavyCombat_Male_Heavy_Trouser_L','Body_Hand_L','Face_Stern','Vehicle_Shuttle_Canopy_Panoramic_Glazing','Accessory_Mining_PowerRig']:
    print(name,json.dumps(rows.get(name)),flush=True)
