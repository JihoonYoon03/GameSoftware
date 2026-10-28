"""Check final mesh instances in all three directly openable Blender projects."""
import bpy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'));assets={a['id']:a for a in cat['assets']}
report={'projects':{},'all_checks_passed':False}
for filename in ['Erma_Resource_Library.blend','Civic_Animations.blend','RoadPlan_Modules.blend']:
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Source'/filename));counts={};total=0;largest=0
    for ob in bpy.data.objects:
        if ob.type!='MESH' or not ob.get('asset_id'):continue
        pid=ob['asset_id'];assert pid in assets,(filename,pid)
        ob.data.calc_loop_triangles();n=len(ob.data.loop_triangles);a=assets[pid]
        assert n==a['triangles']<=a['triangle_budget']<=200,(filename,ob.name,n,a['triangles'])
        assert ob.get('budget_revision')==cat['part_budget_revision'],(filename,ob.name,'stale instance')
        counts[pid]=counts.get(pid,0)+1;total+=1;largest=max(largest,n)
        if ob.get('skeleton') or any(m.type=='ARMATURE' for m in ob.modifiers):
            assert all(abs(sum(g.weight for g in v.groups)-1)<.002 for v in ob.data.vertices),(filename,ob.name,'unbound vertex')
    if filename=='Erma_Resource_Library.blend':
        assert counts.keys()==assets.keys()
        assert bpy.context.scene.name=='16_Transit_Connected_Example'
    report['projects'][filename]=dict(unique_parts=len(counts),mesh_instances=total,max_triangles=largest,default_scene=bpy.context.scene.name)
report['all_checks_passed']=True
(ROOT/'Validation/size_budget_source_validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('BUDGET_SOURCE_PROJECTS_VERIFIED',json.dumps(report),flush=True)
