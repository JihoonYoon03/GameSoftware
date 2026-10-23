"""Create a normal, directly openable .blend containing only the owned road kit."""
from pathlib import Path
import bpy

def style_elevation():
    scene=bpy.data.scenes.get('35_RoadPlan_Side_Elevation')
    if not scene:return
    for ob in scene.objects:
        if ob.type=='MESH' and ob.name.endswith('_Ground'):ob.hide_render=True
    mat=bpy.data.materials.get('RoadPlan_Dimension_White') or bpy.data.materials.new('RoadPlan_Dimension_White')
    mat.use_nodes=True;p=mat.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(.86,.91,.95,1)
    p.inputs['Emission Color'].default_value=(.86,.91,.95,1);p.inputs['Emission Strength'].default_value=.7
    for ob in scene.objects:
        if ob.type=='FONT':ob.data.materials.clear();ob.data.materials.append(mat)
    collection=next(c for c in scene.collection.children if c.name.endswith('_Display'))
    segments=[((0,-5,-1.5),(110,-5,-1.5)),((0,-5,-2.0),(0,-5,.5)),((110,-5,-2),(110,-5,8.4)),
              ((112,-5,0),(112,-5,8.4)),((111.4,-5,0),(112.6,-5,0)),((111.4,-5,8.4),(112.6,-5,8.4))]
    for i,points in enumerate(segments):
        name=f'RoadPlan_DimensionLine_{i}'
        if bpy.data.objects.get(name):continue
        data=bpy.data.curves.new(name,'CURVE');data.dimensions='3D';data.bevel_depth=.045;data.bevel_resolution=0
        spline=data.splines.new('POLY');spline.points.add(1)
        for point,co in zip(spline.points,points):point.co=(*co,1)
        data.materials.append(mat)
        ob=bpy.data.objects.new(name,data);ob['expansion']='road_plan_v1';collection.objects.link(ob)

def write_project(path,tag='road_plan_v1'):
    # The master must already be saved: all pruning below is in-memory and is
    # saved to the dedicated project only.
    owned=[s for s in bpy.data.scenes if s.get('expansion')==tag]
    library=bpy.data.scenes.get('RoadPlan_Library') or bpy.data.scenes.new('RoadPlan_Library')
    library['expansion']=tag;library.unit_settings.system='METRIC'
    lib=bpy.data.collections['20_RoadPlan_Modules']
    if lib.name not in library.collection.children:library.collection.children.link(lib)
    if library not in owned:owned.append(library)
    bpy.context.window.scene=bpy.data.scenes['29_RoadPlan_Ground_Connections']
    for s in list(bpy.data.scenes):
        if s not in owned:bpy.data.scenes.remove(s)
    keep={o for s in owned for o in s.objects}
    for o in list(bpy.data.objects):
        if o not in keep:bpy.data.objects.remove(o,do_unlink=True)
    collections=set()
    def gather(c):
        collections.add(c)
        for child in c.children:gather(child)
    for s in owned:gather(s.collection)
    for c in list(bpy.data.collections):
        if c not in collections:bpy.data.collections.remove(c)
    bpy.data.orphans_purge(do_local_ids=True,do_linked_ids=False,do_recursive=True)
    bpy.context.preferences.filepaths.save_version=0
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA'
    bpy.ops.wm.save_as_mainfile(filepath=str(Path(path)),compress=True)

if __name__=='__main__':
    root=Path(__file__).resolve().parents[1]
    master=root/'Source/Erma_Resource_Library.blend'
    bpy.ops.wm.open_mainfile(filepath=str(master));entry=bpy.context.scene.name
    style_elevation()
    bpy.context.window.scene=bpy.data.scenes[entry]
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(master),compress=True)
    write_project(root/'Source/RoadPlan_Modules.blend')
    print('ROAD_PLAN_EDITABLE_PROJECT_SAVED',flush=True)
