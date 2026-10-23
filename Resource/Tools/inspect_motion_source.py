import bpy,json
r=bpy.data.objects['HUMAN_7H']
for n in ['Body_Male_Normal_Pelvis','Body_Female_Heavy_Pelvis']:
    print(n,[[min(v.co[i] for v in bpy.data.objects[n].data.vertices),max(v.co[i] for v in bpy.data.objects[n].data.vertices)] for i in range(3)])
print('MOTION_INSPECT',json.dumps({'scenes':[s.name for s in bpy.data.scenes],'actions':[a.name for a in bpy.data.actions if 'HUMAN' in a.name],'meshes':{n:{'bounds':[[min(v.co[i] for v in bpy.data.objects[n].data.vertices),max(v.co[i] for v in bpy.data.objects[n].data.vertices)] for i in range(3)],'weights':[g.name for g in bpy.data.objects[n].vertex_groups]} for n in ['Boot_L','Body_Hand_R','Weapon_CoilPistol_35cm','Vehicle_JetBike_Seat_Tandem']}},indent=2))
