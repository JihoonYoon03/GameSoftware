"""Export the canonical budgeted parts including modifier-only armature links."""
import bpy,json
from pathlib import Path
from mathutils import Matrix
ROOT=Path(__file__).resolve().parents[1]
catalog=json.loads((ROOT/'Validation/size_budget_catalog_staged.json').read_text(encoding='utf-8'))
scene=bpy.data.scenes.new('Budget_Geometry_Export');bpy.context.window.scene=scene
objects={bpy.data.objects[a['blender_object']] for a in catalog['assets']}
for ob in list(objects):
    for modifier in ob.modifiers:
        if modifier.type=='ARMATURE' and modifier.object:objects.add(modifier.object)
    parent=ob.parent
    while parent:objects.add(parent);parent=parent.parent
for ob in objects:
    scene.collection.objects.link(ob);ob.hide_set(False);ob.hide_viewport=False
    if ob.type=='ARMATURE':
        if ob.animation_data:ob.animation_data.action=None;ob.animation_data.use_nla=False
        for bone in ob.pose.bones:bone.matrix_basis=Matrix.Identity(4)
for ob in scene.objects:ob.select_set(True)
bpy.context.view_layer.update()
path=ROOT/'Validation/Budget_Geometry.glb';staged=path.with_name('Budget_Geometry.staging.glb')
bpy.ops.export_scene.gltf(filepath=str(staged),export_format='GLB',use_selection=True,use_active_scene=True,export_animations=False,
                          export_skins=True,export_def_bones=False,export_yup=True,export_extras=True,export_materials='EXPORT',
                          export_apply=False,export_texcoords=True,export_normals=True,export_tangents=True)
staged.replace(path)
print('BUDGET_GEOMETRY_BANK_EXPORTED',len(catalog['assets']),flush=True)
