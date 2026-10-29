"""Compare the transferred actions with source skin, then check all characters."""
import bpy,ast,json,sys,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from acceleration_motion import SPECS
from verify_resource_pack import read_glb,accessor
meta=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))
recipes=json.loads((ROOT/'Documentation/assembly_recipes.json').read_text(encoding='utf-8'))
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
MATS={m.name:m for m in bpy.data.materials};PROTOTYPES={a['id']:bpy.data.objects.get(a.get('blender_object',a['id'])) for a in catalog['assets']}
tree=ast.parse((ROOT/'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='clone_actor'],type_ignores=[]),'actor_helpers','exec'),globals())
tree=ast.parse((ROOT/'Tools/verify_civic_animations.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ['bind','load']],type_ignores=[]),'roundtrip_helpers','exec'),globals())
def bound(ob):
    e=ob.evaluated_get(bpy.context.evaluated_depsgraph_get());me=e.to_mesh();ps=[e.matrix_world@v.co for v in me.vertices]
    out=tuple(fn(p[k] for p in ps) for fn in [min,max] for k in range(3));e.to_mesh_clear();return out
scene=bpy.data.scenes.new('Acceleration_Source_Comparison');scene.render.fps=30;bpy.context.window.scene=scene
rig=bpy.data.objects['HUMAN_7H'];scene.collection.objects.link(rig)
# Compare animation independently of legacy recipe fitting offsets: preserve the
# delivered bind vertices, then drive those vertices with the authoring rig.
doc,buf=read_glb(ROOT/'Export/Characters/Player_Explorer.glb');bind_vertices={}
for node in doc['nodes']:
    if 'mesh' not in node:continue
    assert not any(k in node for k in ['translation','rotation','scale','matrix'])
    names=[doc['nodes'][j]['name'] for j in doc['skins'][node['skin']]['joints']];rows=[]
    for prim in doc['meshes'][node['mesh']]['primitives']:
        aa=prim['attributes'];ps=accessor(doc,buf,aa['POSITION']);jj=accessor(doc,buf,aa['JOINTS_0']);ww=accessor(doc,buf,aa['WEIGHTS_0']);a=doc['accessors'][aa['WEIGHTS_0']]
        divisor=65535 if a.get('normalized') and a['componentType']==5123 else 255 if a.get('normalized') and a['componentType']==5121 else 1
        rows.extend((Vector((p[0],-p[2],p[1])),[(names[j],w/divisor) for j,w in zip(js,ws) if w]) for p,js,ws in zip(ps,jj,ww))
    bind_vertices[node['extras']['asset_id']]=rows
expected={}
for name in SPECS:
    action=bpy.data.actions['HUMAN_7H__'+name]
    for i in range(5):
        bind(scene,rig,action,i/4);e=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
        transforms={b.name:e.pose.bones[b.name].matrix@b.matrix_local.inverted() for b in rig.data.bones};values={}
        for pid,rows in bind_vertices.items():
            ps=[sum((transforms[n]@p*w for n,w in ws),Vector()) for p,ws in rows]
            values[pid]=tuple(fn(p[k] for p in ps) for fn in [min,max] for k in range(3))
        expected[(name,i)]=values
report={'characters':{},'geometry_reference':'unchanged delivered GLB bind vertices driven by the authoring rig','max_source_reimport_bounds_error_m':0,'all_checks_passed':False}
for name,info in recipes.items():
    if not isinstance(info,dict) or 'body' not in info:continue
    if '--only' in sys.argv and name!=sys.argv[sys.argv.index('--only')+1]:continue
    scene,rig,objects,actions=load('Export/Characters/'+name+'.glb')
    assert len(rig.data.bones)==23 and set(actions)=={c['clip'] for c in meta['HUMAN_7H']}
    row={}
    for clip,info in SPECS.items():
        base,end,loop=info["source"],info["frames"],info["loop"]
        action=actions[clip];duration=(action.frame_range[1]-action.frame_range[0])/30
        assert abs(duration-end/30)<1e-5
        first=None;low=999;movement=0;root_error=0
        for i in range(5):
            bind(scene,rig,action,i/4);pose={b.name:b.matrix.copy() for b in rig.pose.bones}
            if first is None:first=pose
            movement=max(movement,max((pose[n].translation-first[n].translation).length for n in pose))
            root_error=max(root_error,max(abs(pose['root'][r][c]-first['root'][r][c]) for r in range(4) for c in range(4)))
            for ob in objects:
                bounds=bound(ob);low=min(low,bounds[2])
                if name=='Player_Explorer':
                    error=max(abs(a-b) for a,b in zip(bounds,expected[(clip,i)][ob['asset_id']]))
                    if error>report['max_source_reimport_bounds_error_m']:
                        report['max_source_reimport_bounds_error_m']=error
                        report['worst_source_bounds_sample']={'clip':clip,'phase':i/4,'part':ob['asset_id'],'source':expected[(clip,i)][ob['asset_id']],'actual':bounds}
            if i==4:seam=max(abs(pose[n][r][c]-first[n][r][c]) for n in pose for r in range(4) for c in range(4))
        assert low>=-.005 and root_error<2e-5 and movement>.0001,(name,clip,low,root_error,movement)
        if loop:assert seam<2e-4,(name,clip,'loop seam',seam)
        row[clip]=dict(duration_seconds=duration,min_z_m=low,root_error=root_error,loop_error=seam if loop else None)
    report['characters'][name]=row
    print('ACCELERATION_ROUNDTRIP_CHECKED',name,flush=True)
(ROOT/'Validation/acceleration_roundtrip_diagnostic.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['max_source_reimport_bounds_error_m']<.0005,report.get('worst_source_bounds_sample')
report['all_checks_passed']=True
(ROOT/'Validation/acceleration_roundtrip_report.json').write_text(json.dumps(report,indent=2)+'\n')
print('ACCELERATION_ROUNDTRIP_VERIFIED',len(report['characters']),report['max_source_reimport_bounds_error_m'],flush=True)
