"""Blender-only asset authoring. All outputs are confined to Resource.
Run: blender --background --factory-startup --python this_file.py
"""
import bpy, bmesh, math, json, random, struct, hashlib, sys
from pathlib import Path
from mathutils import Vector, Quaternion
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
assert ROOT.name == 'Resource'
for name in ['Source', 'Export/Characters', 'Export/Creatures', 'Export/Modules',
             'Export/Buildings', 'Export/Environment', 'Export/Textures', 'Preview',
             'Documentation', 'Validation']:
    (ROOT / name).mkdir(parents=True, exist_ok=True)
random.seed(1707)
bpy.context.preferences.filepaths.save_version=0
RUNTIME_RENDER_DEVICE='CPU'
try:
    cprefs=bpy.context.preferences.addons['cycles'].preferences
    cprefs.compute_device_type='OPTIX';cprefs.get_devices()
    for device in cprefs.devices:device.use=(device.type=='OPTIX')
    if any(d.type=='OPTIX' for d in cprefs.devices):RUNTIME_RENDER_DEVICE='GPU'
except Exception:pass
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
for block in list(bpy.data.collections):
    if block.name != 'Collection': bpy.data.collections.remove(block)
scene = bpy.context.scene
scene.name = '00_Library'
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1.0
scene.render.fps = 30
MATS, ASSETS, PROTOTYPES, RIGS, CLIPS = {}, [], {}, {}, {}
LIB = bpy.data.collections.new('RESOURCE_LIBRARY')
scene.collection.children.link(LIB)

def material(name, color, rough=.65, metal=0, emission=0):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Roughness'].default_value = rough
    p.inputs['Metallic'].default_value = metal
    if emission:
        p.inputs['Emission Color'].default_value = (*color, 1)
        p.inputs['Emission Strength'].default_value = emission
    MATS[name] = m
    return m

for args in [
    ('Skin_Warm', (.56,.31,.20), .74), ('Skin_Deep', (.24,.105,.07), .76),
    ('Skin_Light', (.83,.58,.40), .75), ('Skin_Cool', (.45,.29,.24), .75),
    ('Hair_Charcoal', (.027,.033,.044), .83), ('Hair_Copper', (.24,.075,.035), .85),
    ('Hair_Silver', (.38,.43,.47), .83), ('Eye_Dark', (.012,.021,.027), .35),
    ('Eye_White', (.70,.75,.70), .55), ('Cloth_Teal', (.035,.21,.24), .9),
    ('Cloth_Indigo', (.08,.11,.23), .9), ('Cloth_Ochre', (.61,.28,.065), .91),
    ('Cloth_Medical', (.67,.73,.68), .86), ('Cloth_Burgundy', (.30,.045,.07), .9),
    ('Rubber', (.026,.039,.046), .91), ('Steel', (.24,.32,.36), .35,.8),
    ('Alloy_Light', (.57,.65,.65), .32,.7), ('Copper', (.48,.21,.08), .4,.75),
    ('Ceramic', (.66,.69,.61), .52,.15), ('Warning', (.95,.47,.065), .58),
    ('Glow_Cyan', (.04,.70,.82), .3,.15,2.0),
    ('Glow_Amber', (1.0,.48,.12), .4,.1,1.4),
    ('Glow_Violet', (.39,.13,.9), .35,.2,2.0),
    ('Chitin', (.16,.105,.19), .6,.15), ('Tissue', (.36,.095,.14), .7),
    ('Bone_Ivory', (.65,.59,.43), .7), ('Alien_Leaf', (.08,.32,.29), .85),
    ('Alien_Petal', (.31,.13,.38), .7), ('Glass_Opaque', (.035,.12,.17), .21,.4),
    ('Solar', (.035,.075,.16), .3,.55), ('Wood_Composite', (.27,.16,.09), .9),
]: material(*args)

TEXTURES = []
def tile_material(name, base, rough, metal=0, kind='stone'):
    """Periodic analytic PBR textures; edge continuity follows periodic signals."""
    n = 512
    yy, xx = np.mgrid[0:n,0:n].astype(np.float32) / n
    h = (np.sin(2*np.pi*(3*xx+2*yy)) + .45*np.cos(2*np.pi*(11*xx-7*yy))
         + .21*np.sin(2*np.pi*(37*xx+29*yy)) + .08*np.cos(2*np.pi*(91*xx-73*yy))) / 1.74
    detail = h.copy()
    if kind == 'sand':
        h = .65*np.sin(2*np.pi*(8*xx+2*yy) + .4*np.sin(2*np.pi*yy)) + .15*h
    elif kind == 'grass':
        h = .5*h + .4*np.sin(2*np.pi*(64*xx+19*yy))*np.sin(2*np.pi*(17*xx-61*yy))
    elif kind == 'road':
        h = .25*h + .12*np.sin(2*np.pi*131*xx)*np.cos(2*np.pi*127*yy)
    elif kind == 'panel':
        seam = (np.minimum(xx,1-xx)<.012) | (np.minimum(yy,1-yy)<.012)
        h = .08*h - seam*.6
    elif kind == 'ruin':
        cracks = np.abs(np.sin(2*np.pi*(3*xx+yy))+.25*np.sin(2*np.pi*7*yy))<.045
        h = .3*h-cracks*.7
    rgb = np.clip(np.array(base)[None,None,:] * (1+.16*h[:,:,None]+.03*detail[:,:,None]),0,1)
    def save_image(suffix, pixels, noncolor=False):
        image = bpy.data.images.new(name+'_'+suffix,width=n,height=n,alpha=True)
        if noncolor: image.colorspace_settings.name='Non-Color'
        image.pixels.foreach_set(pixels.astype(np.float32).ravel())
        image.filepath_raw=str(ROOT/'Export/Textures'/f'{name}_{suffix}.png')
        image.file_format='PNG'; image.save(); image.pack()
        TEXTURES.append(str(Path(image.filepath_raw).relative_to(ROOT)))
        return image
    alpha=np.ones((n,n,1), dtype=np.float32)
    color=save_image('BaseColor',np.concatenate([rgb,alpha],axis=2))
    dx=(np.roll(h,-1,axis=1)-np.roll(h,1,axis=1))*.7
    dy=(np.roll(h,-1,axis=0)-np.roll(h,1,axis=0))*.7
    normals=np.dstack([-dx,-dy,np.ones_like(h)])
    normals/=np.linalg.norm(normals,axis=2)[:,:,None]
    normal=save_image('Normal',np.concatenate([normals*.5+.5,alpha],axis=2),True)
    orm=save_image('ORM',np.dstack([np.ones_like(h),np.clip(rough+h*.06,.05,1),np.full_like(h,metal),np.ones_like(h)]),True)
    m=material(name,base,rough,metal)
    nt=m.node_tree; p=nt.nodes.get('Principled BSDF')
    bc=nt.nodes.new('ShaderNodeTexImage'); bc.image=color; nt.links.new(bc.outputs['Color'],p.inputs['Base Color'])
    no=nt.nodes.new('ShaderNodeTexImage'); no.image=normal
    nm=nt.nodes.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value=.3
    nt.links.new(no.outputs['Color'],nm.inputs['Color']); nt.links.new(nm.outputs['Normal'],p.inputs['Normal'])
    oc=nt.nodes.new('ShaderNodeTexImage'); oc.image=orm
    split=nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(oc.outputs['Color'],split.inputs['Color'])
    nt.links.new(split.outputs['Green'],p.inputs['Roughness']); nt.links.new(split.outputs['Blue'],p.inputs['Metallic'])
    return m

for args in [
 ('Terrain_Regolith',(.27,.18,.13),.94,0,'stone'),
 ('Terrain_Dunes',(.58,.37,.20),.93,0,'sand'),
 ('Terrain_AlienGrass',(.10,.24,.20),.95,0,'grass'),
 ('Road_Basalt',(.09,.12,.14),.9,0,'road'),
 ('Rock_Mine',(.22,.25,.28),.88,0,'stone'),
 ('Habitat_Panels',(.46,.54,.53),.55,.45,'panel'),
 ('Habitat_Concrete',(.37,.39,.35),.92,0,'stone'),
 ('Ruin_Obsidian',(.065,.055,.12),.48,.25,'ruin'),
]: tile_material(*args)

class Geo:
    def __init__(self): self.v=[]; self.f=[]; self.m=[]; self.w=[]
    def add(self, verts, faces, mat, bone=None):
        offset=len(self.v); self.v.extend([tuple(v) for v in verts]); self.w.extend([bone]*len(verts))
        self.f.extend([tuple(i+offset for i in f) for f in faces]); self.m.extend([mat]*len(faces))
        return self
    def box(self,c,s,mat,bone=None,rot=None):
        c=Vector(c); sx,sy,sz=[v*.5 for v in s]
        v=[Vector((x*sx,y*sy,z*sz)) for x,y,z in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
        if rot: v=[rot@q for q in v]
        return self.add([c+q for q in v],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],mat,bone)
    def rings(self, centers, radii, mat, bone=None, sides=8, caps=True, axis=None):
        v=[]; faces=[]
        # Rings follow Z unless an axis is explicitly supplied.
        quat=Vector((0,0,1)).rotation_difference(Vector(axis).normalized()) if axis else None
        for c,r in zip(centers,radii):
            if isinstance(r,(int,float)): r=(r,r)
            for k in range(sides):
                a=2*math.pi*k/sides+math.pi/sides
                q=Vector((r[0]*math.cos(a),r[1]*math.sin(a),0))
                if quat: q=quat@q
                v.append(Vector(c)+q)
        for j in range(len(centers)-1):
            for k in range(sides):
                a=j*sides+k; b=j*sides+(k+1)%sides
                faces.append((a,b,b+sides,a+sides))
        if caps:
            faces += [tuple(reversed(range(sides))),tuple((len(centers)-1)*sides+k for k in range(sides))]
        return self.add(v,faces,mat,bone)
    def rod(self,a,b,r1,r2,mat,bone=None,sides=8):
        return self.rings([a,b],[r1,r2],mat,bone,sides,True,Vector(b)-Vector(a))
    def ellipsoid(self,c,s,mat,bone=None,sides=10,rings=6):
        c=Vector(c); centers=[]; radii=[]
        for j in range(rings+1):
            a=-math.pi/2+math.pi*j/rings
            # Tiny end rings avoid coincident pole vertices / zero-area triangles.
            rr=max(.025,math.cos(a))
            centers.append(c+Vector((0,0,s[2]*math.sin(a))))
            radii.append((s[0]*rr,s[1]*rr))
        return self.rings(centers,radii,mat,bone,sides)
    def make(self,name,collection,rig=None,slot='',family=''):
        mesh=bpy.data.meshes.new(name+'_Mesh'); mesh.from_pydata(self.v,[],self.f); mesh.update()
        obj=bpy.data.objects.new(name,mesh); collection.objects.link(obj)
        mats=list(dict.fromkeys(self.m))
        for m in mats: mesh.materials.append(MATS[m])
        for p,m in zip(mesh.polygons,self.m): p.material_index=mats.index(m)
        bm=bmesh.new(); bm.from_mesh(mesh); bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(mesh); bm.free()
        uv=mesh.uv_layers.new(name='UVMap')
        for poly in mesh.polygons:
            dominant=max(range(3),key=lambda k:abs(poly.normal[k]))
            axes=[a for a in range(3) if a!=dominant]
            for li in poly.loop_indices:
                co=mesh.vertices[mesh.loops[li].vertex_index].co
                uv.data[li].uv=(co[axes[0]]*.5,co[axes[1]]*.5)
        if rig:
            obj.parent=rig
            groups={}
            for idx,bone in enumerate(self.w):
                bone=bone or 'root'
                if bone not in groups: groups[bone]=obj.vertex_groups.new(name=bone)
                groups[bone].add([idx],1,'REPLACE')
            mod=obj.modifiers.new('Skin','ARMATURE'); mod.object=rig; mod.use_deform_preserve_volume=False
        mesh.calc_loop_triangles(); triangles=len(mesh.loop_triangles)
        assert triangles<=500,(name,triangles)
        obj['asset_id']=name; obj['slot']=slot; obj['family']=family; obj['triangle_count']=triangles
        obj['max_triangles']=500; obj['units']='meters'
        obj.asset_mark(); obj.asset_data.description=f'{family} / {slot} / {triangles} triangles. Resource modular kit.'
        ASSETS.append({'id':name,'family':family,'slot':slot,'vertices':len(mesh.vertices),'polygons':len(mesh.polygons),'triangles':triangles,'materials':mats,'skeleton':rig.name if rig else None})
        PROTOTYPES[name]=obj
        return obj

def collection(name):
    c=bpy.data.collections.new(name); LIB.children.link(c); return c

def armature(name, specs, col):
    data=bpy.data.armatures.new(name+'_Skeleton'); rig=bpy.data.objects.new(name,data); col.objects.link(rig)
    bpy.context.view_layer.objects.active=rig; rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for name_,head,tail,parent in specs:
        b=data.edit_bones.new(name_); b.head=head; b.tail=tail
        if parent: b.parent=data.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT'); rig.select_set(False)
    rig.show_in_front=True; data.display_type='STICK'
    rig['forward']='-Y'; rig['up']='Z'; rig['units']='meters'
    RIGS[name]=rig
    return rig

H=1.8; HH=H/7; NECK=H-HH
HUMAN_SPECS=[('root',(0,0,0),(0,0,.15),None),('pelvis',(0,0,.91),(0,0,1.06),'root'),
 ('spine',(0,0,1.06),(0,0,1.27),'pelvis'),('chest',(0,0,1.27),(0,0,1.46),'spine'),
 ('neck',(0,0,1.46),(0,0,NECK),'chest'),('head',(0,0,NECK),(0,0,H),'neck'),
 ('weapon_socket',(0,0,1.1),(0,-.12,1.1),'hand.R'),('back_socket',(0,.12,1.31),(0,.22,1.31),'chest')]
# Parents must precede their children.
HUMAN_SPECS=HUMAN_SPECS[:-2]
for suffix,sign in [('L',1),('R',-1)]:
    HUMAN_SPECS += [(f'clavicle.{suffix}',(0,0,1.42),(sign*.21,0,1.43),'chest'),
      (f'upper_arm.{suffix}',(sign*.21,0,1.43),(sign*.34,0,1.17),f'clavicle.{suffix}'),
      (f'forearm.{suffix}',(sign*.34,0,1.17),(sign*.43,-.015,.94),f'upper_arm.{suffix}'),
      (f'hand.{suffix}',(sign*.43,-.015,.94),(sign*.465,-.03,.83),f'forearm.{suffix}'),
      (f'thigh.{suffix}',(sign*.105,0,.96),(sign*.115,-.01,.55),'pelvis'),
      (f'shin.{suffix}',(sign*.115,-.01,.55),(sign*.115,0,.14),f'thigh.{suffix}'),
      (f'foot.{suffix}',(sign*.115,0,.14),(sign*.115,-.19,.075),f'shin.{suffix}')]
HUMAN_SPECS += [('weapon_socket',(-.465,-.03,.87),(-.465,-.047,.71),'hand.R'),
                ('back_socket',(0,.12,1.31),(0,.22,1.31),'chest'),
                ('muzzle',(-.465,-.047,.53),(-.465,-.047,.48),'weapon_socket')]
HC=collection('01_Human_Modules')
human=armature('HUMAN_7H',HUMAN_SPECS,HC)

def human_modules():
    for build,width,hip,limb in [('Standard',1,1,1),('Slender',.88,1.06,.86),('Broad',1.16,1.04,1.16)]:
        Geo().rings([(0,0,.91),(0,0,1.01),(0,0,1.08)],[(.16*hip,.105),(.145*hip,.10),(.125,.09)],'Cloth_Indigo','pelvis').make(f'Body_{build}_Pelvis',HC,human,'pelvis','human')
        Geo().rings([(0,0,1.03),(0,0,1.15),(0,0,1.28)],[(.125,.09),(.13,.085),(.165*width,.108)],'Skin_Warm','spine').make(f'Body_{build}_Abdomen',HC,human,'abdomen','human')
        Geo().rings([(0,0,1.25),(0,0,1.36),(0,0,1.43),(0,0,1.46)],[(.164*width,.108),(.195*width,.12),(.20*width,.105),(.10,.075)],'Skin_Warm','chest').make(f'Body_{build}_Chest',HC,human,'chest','human')
        for suf,sign in [('L',1),('R',-1)]:
            for part,bone,a,b,r1,r2 in [
              ('UpperArm','upper_arm',(sign*.21,0,1.43),(sign*.34,0,1.17),.066*limb,.051*limb),
              ('Forearm','forearm',(sign*.34,0,1.17),(sign*.43,-.015,.94),.052*limb,.034*limb),
              ('Thigh','thigh',(sign*.105,0,.96),(sign*.115,-.01,.55),.089*limb,.064*limb),
              ('Shin','shin',(sign*.115,-.01,.55),(sign*.115,0,.14),.064*limb,.041*limb)]:
                Geo().rod(a,b,r1,r2,'Skin_Warm',f'{bone}.{suf}').make(f'Body_{build}_{part}_{suf}',HC,human,f'{part.lower()}.{suf}','human')
    Geo().rod((0,0,1.44),(0,0,1.58),.059,.055,'Skin_Warm','neck').make('Body_Neck',HC,human,'neck','human')
    for suf,s in [('L',1),('R',-1)]:
        g=Geo().ellipsoid((s*.456,-.021,.878),(.045,.035,.065),'Skin_Warm',f'hand.{suf}',8,4)
        g.rod((s*.435,-.045,.905),(s*.416,-.062,.86),.017,.012,'Skin_Warm',f'hand.{suf}',6)
        g.make(f'Body_Hand_{suf}',HC,human,f'hand.{suf}','human')
        Geo().box((s*.115,-.06,.083),(.10,.245,.115),'Rubber',f'foot.{suf}').box((s*.115,-.07,.032),(.112,.25,.034),'Steel',f'foot.{suf}').make(f'Boot_{suf}',HC,human,f'foot.{suf}','human')
    for i,(label,rx,ry,skin) in enumerate([('Oval',.10,.09,'Skin_Warm'),('Angular',.108,.09,'Skin_Deep'),('Round',.112,.095,'Skin_Light'),('Narrow',.091,.084,'Skin_Cool')]):
        zs=[NECK,NECK+.035,NECK+.10,NECK+.19,NECK+.235,H]
        radii=[(.052,.055),(.078,.077),(rx,ry),(rx*.96,ry*.98),(rx*.76,ry*.8),(.03,.035)]
        g=Geo().rings([(0,0,z) for z in zs],radii,skin,'head',10)
        g.ellipsoid((-.105,0,NECK+.115),(.017,.025,.032),skin,'head',6,3)
        g.ellipsoid((.105,0,NECK+.115),(.017,.025,.032),skin,'head',6,3)
        g.make('Head_'+label,HC,human,'head','human')
    for name,eye_scale,brow in [('Neutral',1,0),('Alert',1.12,.006),('Stern',.8,-.005)]:
        g=Geo()
        for s in [-1,1]:
            g.box((s*.039,-.087,NECK+.139),(.029*eye_scale,.012,.014),'Eye_White','head')
            g.box((s*.038,-.095,NECK+.138),(.011,.006,.012),'Eye_Dark','head')
            g.box((s*.04,-.087,NECK+.160+brow),(.038,.013,.008),'Hair_Charcoal','head')
        g.rod((0,-.081,NECK+.13),(0,-.114,NECK+.101),.014,.008,'Skin_Warm','head',5)
        g.box((0,-.081,NECK+.058),(.045,.009,.008),'Tissue','head')
        g.make('Face_'+name,HC,human,'face','human')
    for hair in ['Buzz','SidePart','Curls','Bob','Ponytail','Bun']:
        g=Geo(); mat='Hair_Copper' if hair in ['Ponytail','Bun'] else 'Hair_Charcoal'
        g.rings([(0,.007,1.69),(0,.005,1.76),(0,.002,1.808)],[(.104,.096),(.105,.096),(.042,.041)],mat,'head',10)
        if hair=='SidePart': g.ellipsoid((-.036,-.038,1.778),(.085,.072,.044),mat,'head',8,3)
        if hair=='Curls':
            for k in range(6):
                a=k*math.tau/6; g.ellipsoid((.081*math.cos(a),.075*math.sin(a),1.754),(.038,.035,.04),mat,'head',6,3)
        if hair=='Bob':
            for s in [-1,1]: g.rod((s*.094,.017,1.735),(s*.10,.03,1.57),.046,.043,mat,'head',6)
            g.box((0,.085,1.645),(.15,.045,.16),mat,'head')
        if hair=='Ponytail':
            g.rod((0,.078,1.745),(0,.17,1.69),.043,.037,mat,'head',8)
            g.rod((0,.17,1.69),(0,.175,1.52),.037,.018,mat,'head',8)
            g.rod((0,.105,1.728),(0,.126,1.716),.044,.044,'Copper','head',8)
        if hair=='Bun': g.ellipsoid((0,.105,1.757),(.064,.065,.057),mat,'head',8,4)
        g.make('Hair_'+hair,HC,human,'hair','human')
    # Helmet hair clearance variants; closed guard helmet hides hair entirely.
    for name in ['Buzz','Ponytail','Bun']:
        g=Geo().box((0,.075,1.675),(.14,.05,.08),'Hair_Charcoal','head')
        if name=='Ponytail': g.rod((0,.09,1.71),(0,.16,1.52),.035,.018,'Hair_Copper','head')
        if name=='Bun': g.ellipsoid((0,.11,1.72),(.045,.052,.045),'Hair_Copper','head',8,4)
        g.make('Hair_HatClearance_'+name,HC,human,'hair','human')

    for role,cloth in [('Explorer','Cloth_Teal'),('Engineer','Cloth_Ochre'),('Guard','Cloth_Indigo'),('Medic','Cloth_Medical'),('Researcher','Cloth_Burgundy')]:
        for build,width,hip,limb in [('Standard',1,1,1),('Slender',.88,1.06,.86),('Broad',1.16,1.04,1.16)]:
            # Outer shell covers the base chest/abdomen; hide those base slots.
            g=Geo().rings([(0,0,1.035),(0,0,1.18),(0,0,1.34),(0,0,1.43),(0,0,1.465)],[(.139,.101),(.145,.102),(.204*width,.135),(.214*width,.12),(.105,.083)],cloth,'chest',8)
            g.box((0,-.128,1.34),(.14,.025,.14),'Steel','chest')
            g.box((0,-.144,1.35),(.10,.012,.026),'Glow_Cyan','chest')
            g.rod((0,0,1.45),(0,0,1.485),.073,.071,'Rubber','chest',8)
            if role=='Guard':
                g.box((-.12,-.125,1.28),(.08,.025,.17),'Alloy_Light','chest')
                g.box((.12,-.125,1.28),(.08,.025,.17),'Alloy_Light','chest')
            if role=='Medic':
                g.box((0,-.151,1.275),(.07,.01,.025),'Cloth_Burgundy','chest')
                g.box((0,-.152,1.275),(.025,.01,.07),'Cloth_Burgundy','chest')
            g.make(f'Outfit_{role}_{build}_Torso',HC,human,'outfit.torso','human')
            for suf,s in [('L',1),('R',-1)]:
                g=Geo().rod((s*.21,0,1.43),(s*.34,0,1.17),.076*limb,.059*limb,cloth,f'upper_arm.{suf}')
                g.ellipsoid((s*.23,0,1.405),(.09*limb,.078,.077),'Steel' if role=='Guard' else cloth,f'upper_arm.{suf}',8,3)
                g.make(f'Outfit_{role}_{build}_Sleeve_{suf}',HC,human,f'outfit.upperarm.{suf}','human')
                Geo().rod((s*.34,0,1.17),(s*.43,-.015,.94),.059*limb,.04*limb,cloth,f'forearm.{suf}').rod((s*.412,-.013,.99),(s*.43,-.015,.95),.045*limb,.043*limb,'Steel',f'forearm.{suf}').make(f'Outfit_{role}_{build}_Cuff_{suf}',HC,human,f'outfit.forearm.{suf}','human')
                Geo().rod((s*.105,0,.96),(s*.115,-.01,.55),.096*limb,.07*limb,cloth,f'thigh.{suf}').make(f'Outfit_{role}_{build}_Trouser_{suf}',HC,human,f'outfit.thigh.{suf}','human')
                Geo().rod((s*.115,-.01,.55),(s*.115,0,.14),.07*limb,.046*limb,'Rubber',f'shin.{suf}').box((s*.115,-.061,.49),(.105*limb,.035,.10),'Steel',f'shin.{suf}').make(f'Outfit_{role}_{build}_Greave_{suf}',HC,human,f'outfit.shin.{suf}','human')
        # Shared accessories independent of body width.
        g=Geo().box((0,.157,1.29),(.21,.105,.25),'Steel','back_socket')
        g.box((0,.215,1.29),(.135,.017,.125),'Glow_Amber','back_socket')
        g.rod((.08,.18,1.4),(.08,.18,1.57),.009,.006,'Steel','back_socket',6)
        g.make(f'Gear_{role}_Powerpack',HC,human,'gear.back','human')
    Geo().rings([(0,0,1.735),(0,0,1.79),(0,0,1.84)],[(.123,.113),(.113,.104),(.035,.04)],'Warning','head',10).box((0,-.11,1.754),(.22,.08,.025),'Steel','head').box((0,-.15,1.78),(.07,.03,.035),'Glow_Amber','head').make('Hat_Engineer',HC,human,'hat','human')
    Geo().rings([(0,0,1.71),(0,0,1.785),(0,0,1.82)],[(.115,.108),(.112,.104),(.045,.04)],'Cloth_Medical','head',10).box((0,-.105,1.76),(.055,.016,.018),'Cloth_Burgundy','head').box((0,-.106,1.76),(.018,.016,.05),'Cloth_Burgundy','head').make('Hat_Medic',HC,human,'hat','human')
    Geo().rings([(0,0,1.57),(0,0,1.69),(0,0,1.80),(0,0,1.835)],[(.095,.102),(.123,.115),(.115,.107),(.045,.043)],'Steel','head',10).box((0,-.115,1.699),(.19,.022,.075),'Glass_Opaque','head').box((0,-.13,1.713),(.17,.01,.012),'Glow_Cyan','head').make('Helmet_Guard',HC,human,'hat','human')
    Geo().box((0,.098,1.67),(.18,.04,.04),'Steel','head').rod((-.115,.01,1.66),(-.115,.01,1.73),.025,.025,'Steel','head',8).box((-.115,-.065,1.625),(.016,.11,.013),'Steel','head').make('Gear_Headset',HC,human,'gear.headset','human')
    g=Geo().box((-.465,-.1,.887),(.066,.22,.07),'Steel','weapon_socket').box((-.465,-.073,.855),(.045,.075,.10),'Rubber','weapon_socket').rod((-.465,-.14,.887),(-.465,-.37,.887),.036,.025,'Alloy_Light','weapon_socket',8).rod((-.465,-.24,.887),(-.465,-.29,.887),.039,.039,'Glow_Cyan','weapon_socket',8)
    pivot=Vector((-.465,-.03,.87));q=Quaternion((1,0,0),math.pi/2)
    g.v=[tuple(pivot+q@(Vector(v)-pivot)) for v in g.v]
    g.make('Weapon_CoilPistol_35cm',HC,human,'weapon','human')

human_modules()
exec(compile((ROOT/'Tools/human_design_v2.py').read_text(encoding='utf-8'),'human_design_v2.py','exec'),globals())
print('HUMAN_MODULES',len(ASSETS),flush=True)

CREATURES={}
def creature_modules(kind,legs):
    col=collection('02_'+kind)
    organic=kind=='RiftGrazer'; mystic=kind=='VeilWalker'; mechanical=kind=='RelaySentinel'
    body_z=.84 if organic else 1.08 if mystic else 1.12
    weapon_z=body_z+(.17 if organic else .27)
    specs=[('root',(0,0,0),(0,0,.15),None),('body',(0,0,body_z),(0,0,body_z+.25),'root'),
           ('head',(0,-.38,body_z+.1),(0,-.65,body_z+.17),'body'),
           ('jaw',(0,-.61,body_z-.03),(0,-.8,body_z-.08),'head'),
           ('weapon_socket',(0,-.52,weapon_z),(0,-.72,weapon_z),'head'),
           ('muzzle',(0,-.80 if organic else -.86,weapon_z),(0,-.90 if organic else -.96,weapon_z),'weapon_socket')]
    locations=[]
    for i in range(legs):
        s=1 if i%2==0 else -1; row=i//2
        y=0 if legs==2 else -.31+row*.62 if legs==4 else -.38+row*.38
        a=(s*(.21 if mechanical else .27),y,body_z-.1)
        b=(s*(.32 if mechanical else .65),y+.10,body_z*.5)
        c=(s*(.34 if mechanical else .8),y-.12,.10)
        specs += [(f'leg_upper.{i:02}',a,b,'body'),(f'leg_lower.{i:02}',b,c,f'leg_upper.{i:02}'),
                  (f'foot.{i:02}',c,(c[0],c[1]-.17,.055),f'leg_lower.{i:02}')]
        locations.append((a,b,c))
    if organic:
        specs += [('tail.01',(0,.38,.9),(0,.78,.88),'body'),('tail.02',(0,.78,.88),(0,1.14,.99),'tail.01')]
    if mystic:
        for i in range(3):
            specs += [(f'veil.{i}',(0,.02,1.35),(math.cos(i*math.tau/3)*.4,math.sin(i*math.tau/3)*.4,1.55),'body')]
    rig=armature(kind,specs,col)
    parts=[]
    def add(name,g,slot):
        obj=g.make(kind+'_'+name,col,rig,slot,kind); parts.append(obj.name); return obj
    if organic:
        add('Torso',Geo().ellipsoid((0,.06,.86),(.34,.53,.28),'Chitin','body',10,6),'torso')
        add('Carapace',Geo().ellipsoid((0,.13,1.035),(.29,.44,.15),'Bone_Ivory','body',10,4),'carapace')
        for variant in ['Antler','Crest']:
            g=Geo().ellipsoid((0,-.49,.96),(.22,.24,.18),'Chitin','head',8,5)
            g.ellipsoid((0,-.665,.87),(.145,.14,.09),'Tissue','head',8,4)
            for s in [-1,1]:
                g.rod((s*.13,-.52,1.04),(s*.27,-.43,1.31),.049,.007,'Bone_Ivory','head',6)
                if variant=='Antler': g.rod((s*.22,-.455,1.22),(s*.34,-.60,1.32),.027,.006,'Bone_Ivory','head',6)
                else: g.rod((s*.085,-.44,1.08),(s*.06,-.28,1.42),.055,.004,'Chitin','head',6)
                for j in range(2): g.ellipsoid((s*(.12+.035*j),-.67+j*.07,1.01),(.027,.023,.022),'Glow_Amber','head',6,3)
            add('Head_'+variant,g,'head')
        g=Geo().ellipsoid((0,-.66,.79),(.15,.15,.055),'Tissue','jaw',8,4)
        for j in range(6):
            a=math.pi*(j/5); g.rod((.12*math.cos(a),-.68-.105*math.sin(a),.81),(.105*math.cos(a),-.68-.10*math.sin(a),.875),.014,.002,'Bone_Ivory','jaw',5)
        add('Jaw',g,'jaw')
        for i in range(5):
            g=Geo().rod((0,-.19+i*.17,1.12),(0,-.20+i*.17,1.34+(i%2)*.09),.047,.003,'Bone_Ivory','body',6)
            add(f'Spine_{i:02}',g,'spine')
        add('TailA',Geo().rod((0,.38,.90),(0,.78,.88),.09,.05,'Chitin','tail.01'),'tail.01')
        add('TailB',Geo().rod((0,.78,.88),(0,1.14,.99),.052,.008,'Bone_Ivory','tail.02'),'tail.02')
        add('SporeEmitter',Geo().ellipsoid((0,-.70,1.01),(.08,.09,.075),'Glow_Amber','weapon_socket',8,4),'weapon')
    elif mystic:
        # Broken shell with a visible suspended core, rather than a literal known alien.
        add('Core',Geo().ellipsoid((0,0,1.16),(.16,.22,.24),'Glow_Violet','body',8,4),'core')
        for s in [-1,1]:
            g=Geo().rings([(s*.21,0,.86),(s*.27,.03,1.13),(s*.20,.035,1.42)],[(.12,.23),(.13,.30),(.07,.17)],'Ruin_Obsidian','body',5)
            add('Shell_'+('L' if s>0 else 'R'),g,'shell')
        for variant in ['Crown','Halo']:
            g=Geo().ellipsoid((0,-.40,1.26),(.115,.16,.12),'Ruin_Obsidian','head',6,4)
            g.box((0,-.556,1.26),(.025,.018,.11),'Glow_Cyan','head')
            for s in [-1,1]:
                g.rod((s*.075,-.4,1.31),(s*.20,-.34,1.61),.035,.006,'Ruin_Obsidian','head',5)
            if variant=='Halo':
                for j in range(6):
                    a=j*math.tau/6; b=(j+.8)*math.tau/6
                    g.rod((.23*math.cos(a),-.36,1.40+.23*math.sin(a)),(.23*math.cos(b),-.36,1.40+.23*math.sin(b)),.018,.018,'Copper','head',5)
            add('Head_'+variant,g,'head')
        for i in range(3):
            a=i*math.tau/3
            g=Geo().rod((math.cos(a)*.13,math.sin(a)*.13,1.35),(math.cos(a)*.37,math.sin(a)*.37,1.61),.054,.009,'Ruin_Obsidian',f'veil.{i}',5)
            add(f'VeilShard_{i}',g,f'veil.{i}')
        add('PhaseEmitter',Geo().ellipsoid((0,-.69,1.35),(.055,.075,.055),'Glow_Cyan','weapon_socket',6,4),'weapon')
    else:
        g=Geo().box((0,0,1.13),(.48,.37,.38),'Steel','body')
        g.box((0,-.202,1.13),(.31,.04,.21),'Ceramic','body')
        g.box((0,-.228,1.15),(.14,.018,.035),'Glow_Amber','body')
        for s in [-1,1]: g.rod((s*.28,0,1.13),(s*.28,0,1.30),.092,.092,'Copper','body',8)
        add('Chassis',g,'torso')
        for variant in ['Scanner','Armored']:
            g=Geo().box((0,-.40,1.34),(.30,.26,.18),'Alloy_Light','head')
            g.rod((0,-.54,1.34),(0,-.595,1.34),.065,.06,'Glow_Cyan','head',8)
            if variant=='Scanner':
                for s in [-1,1]: g.rod((s*.115,-.34,1.39),(s*.20,-.30,1.67),.012,.006,'Steel','head',6)
            else: g.box((0,-.40,1.45),(.36,.30,.07),'Steel','head')
            add('Head_'+variant,g,'head')
        g=Geo().box((0,.22,1.15),(.27,.12,.30),'Rubber','body')
        for s in [-1,1]: g.rod((s*.09,.29,1.08),(s*.09,.29,1.23),.035,.035,'Glow_Cyan','body',6)
        add('Battery',g,'back')
        g=Geo().rod((0,-.53,1.39),(0,-.90,1.39),.046,.029,'Steel','weapon_socket',8)
        for y in [-.66,-.74,-.82]: g.rod((0,y,1.39),(0,y-.025,1.39),.055,.055,'Copper','weapon_socket',8)
        add('CoilCannon',g,'weapon')
    for i,(a,b,c) in enumerate(locations):
        mat='Chitin' if organic else 'Ruin_Obsidian' if mystic else 'Steel'
        accent='Tissue' if organic else 'Glow_Violet' if mystic else 'Copper'
        r=.068 if organic else .04 if mystic else .085
        add(f'LegUpper_{i:02}',Geo().rod(a,b,r,r*.8,mat,f'leg_upper.{i:02}',6).ellipsoid(a,(r*1.3,r*1.3,r*1.3),accent,f'leg_upper.{i:02}',6,3),f'leg_upper.{i:02}')
        add(f'LegLower_{i:02}',Geo().rod(b,c,r*.85,r*.40,mat,f'leg_lower.{i:02}',6).ellipsoid(b,(r*1.2,r*1.2,r*1.2),accent,f'leg_lower.{i:02}',6,3),f'leg_lower.{i:02}')
        g=Geo()
        if mechanical: g.box((c[0],c[1]-.065,.075),(.21,.29,.12),'Rubber',f'foot.{i:02}').box((c[0],c[1]-.12,.14),(.16,.12,.04),'Warning',f'foot.{i:02}')
        else:
            for j in [-1,0,1]: g.rod((c[0],c[1],.12),(c[0]+j*.045,c[1]-.16-abs(j)*.015,.035),.025,.004,'Bone_Ivory' if organic else 'Copper',f'foot.{i:02}',5)
        add(f'Foot_{i:02}',g,f'foot.{i:02}')
    CREATURES[kind]={'rig':rig,'parts':parts,'legs':legs,'body_z':body_z,'locations':locations}

creature_modules('RiftGrazer',4)
creature_modules('VeilWalker',6)
creature_modules('RelaySentinel',2)
print('CREATURE_MODULES',len(ASSETS),flush=True)

def set_rotation(rig,bone,axis,angle):
    if bone not in rig.pose.bones: return
    p=rig.pose.bones[bone]; p.rotation_mode='QUATERNION'
    basis=p.bone.matrix_local.to_quaternion()
    p.rotation_quaternion=basis.inverted() @ Quaternion(Vector(axis),angle) @ basis

def add_rotation(rig,bone,axis,angle):
    if bone not in rig.pose.bones:return
    p=rig.pose.bones[bone]; basis=p.bone.matrix_local.to_quaternion()
    q=basis.inverted() @ Quaternion(Vector(axis),angle) @ basis
    p.rotation_quaternion=p.rotation_quaternion @ q

def world_offset(rig,bone,offset):
    p=rig.pose.bones[bone]
    p.location=p.bone.matrix_local.to_quaternion().inverted() @ Vector(offset)

def reset_pose(rig):
    for p in rig.pose.bones:
        p.rotation_mode='QUATERNION'; p.rotation_quaternion=(1,0,0,0); p.location=(0,0,0); p.scale=(1,1,1)

def ease(t): return t*t*(3-2*t)

def solve_chain(rig,upper,lower,end,target,pole,offset=(0,0,0),end_delta=None):
    """Bake analytic two-bone IK into ordinary deform-bone quaternions."""
    u=rig.data.bones[upper];l=rig.data.bones[lower];e=rig.data.bones[end]
    hip=u.head_local+Vector(offset);target=Vector(target)
    direction=target-hip;dist=direction.length;direction.normalize()
    a=(u.tail_local-u.head_local).length;b=(l.tail_local-l.head_local).length
    dist=max(abs(a-b)+.0001,min(a+b-.0001,dist))
    side=Vector(pole)-direction*Vector(pole).dot(direction)
    if side.length<1e-6:side=direction.cross(Vector((1,0,0)))
    side.normalize();along=(a*a-b*b+dist*dist)/(2*dist)
    knee=hip+direction*along+side*math.sqrt(max(0,a*a-along*along))
    ankle=hip+direction*dist
    du=(u.tail_local-u.head_local).normalized().rotation_difference((knee-hip).normalized())
    dl=(l.tail_local-l.head_local).normalized().rotation_difference((ankle-knee).normalized())
    qu=u.matrix_local.to_quaternion();ql=l.matrix_local.to_quaternion();qe=e.matrix_local.to_quaternion()
    rig.pose.bones[upper].rotation_quaternion=qu.inverted()@du@qu
    rig.pose.bones[lower].rotation_quaternion=ql.inverted()@du.inverted()@dl@ql
    rig.pose.bones[end].rotation_quaternion=qe.inverted()@dl.inverted()@(end_delta or Quaternion())@qe

CLIP_SPECS=[('Reference_A',1,False),('Turn_Base',1,False),('Idle',60,True),
 ('Walk',30,True),('Run',20,True),('Turn_Left_90',24,False),('Turn_Right_90',24,False),
 ('Attack_Melee',24,False),('Aim_Base',1,False),('Attack_Fire',12,False),
 ('Jump_Start',12,False),('Jump_Air',20,True),('Jump_Land',12,False),('Jump_Full',36,False),
 ('Walk_To_Run',24,False)]

def pose_human(rig,clip,t):
    pi=math.pi; cycle=.8*t+.2*t*t if clip=='Walk_To_Run' else t; q=math.tau*cycle
    if clip in ['Reference_A','Turn_Base']: return
    if clip=='Idle':
        world_offset(rig,'chest',(0,0,.008*math.sin(q)))
        set_rotation(rig,'head',(0,0,1),.035*math.sin(q))
    if clip in ['Walk','Run','Walk_To_Run']:
        blend=0 if clip=='Walk' else 1 if clip=='Run' else ease(t)
        amp=.36+.30*blend
        for suf,phase in [('L',0),('R',pi)]:
            a=q+phase; swing=math.cos(a)
            set_rotation(rig,'thigh.'+suf,(1,0,0),-amp*swing)
            set_rotation(rig,'shin.'+suf,(1,0,0),(.14+.65*blend)*max(0,-math.sin(a)))
            set_rotation(rig,'foot.'+suf,(1,0,0),amp*swing*.60-(.14+.65*blend)*max(0,-math.sin(a))*.5)
            set_rotation(rig,'upper_arm.'+suf,(1,0,0),amp*swing*.7)
            set_rotation(rig,'forearm.'+suf,(1,0,0),-.16-.65*blend)
        world_offset(rig,'pelvis',(0,0,(.018+.027*blend)*(1-math.cos(q*2))))
        set_rotation(rig,'chest',(1,0,0),.045+.09*blend)
        add_rotation(rig,'chest',(0,0,1),.065*math.sin(q))
    if clip.startswith('Turn_') and clip!='Turn_Base':
        s=1 if 'Left' in clip else -1
        set_rotation(rig,'root',(0,0,1),s*pi*.5*ease(t))
        set_rotation(rig,'chest',(0,0,1),s*.16*math.sin(pi*t))
        set_rotation(rig,'head',(0,0,1),s*.14*math.sin(pi*t))
        for suf,phase in [('L',0),('R',pi)]:
            a=math.tau*t+phase
            set_rotation(rig,'thigh.'+suf,(1,0,0),-.18*math.sin(a))
            set_rotation(rig,'shin.'+suf,(1,0,0),.32*max(0,math.sin(a)))
    if clip=='Attack_Melee':
        a=math.sin(pi*t)**2
        set_rotation(rig,'chest',(0,0,1),-.32*math.sin(q))
        set_rotation(rig,'upper_arm.R',(1,0,0),-1.5*a)
        add_rotation(rig,'upper_arm.R',(0,0,1),-.35*a)
        set_rotation(rig,'forearm.R',(1,0,0),-.7*math.sin(pi*t))
        set_rotation(rig,'upper_arm.L',(1,0,0),-.4*a)
        set_rotation(rig,'thigh.L',(1,0,0),-.15*a)
    if clip in ['Aim_Base','Attack_Fire']:
        # The support hand meets the pistol; all actions use the same A-pose bind.
        set_rotation(rig,'upper_arm.R',(1,0,0),-1.35)
        add_rotation(rig,'upper_arm.R',(0,0,1),-.18)
        set_rotation(rig,'forearm.R',(1,0,0),-.18)
        set_rotation(rig,'upper_arm.L',(1,0,0),-1.10)
        add_rotation(rig,'upper_arm.L',(0,0,1),.45)
        set_rotation(rig,'forearm.L',(1,0,0),-.65)
        if clip=='Attack_Fire':
            recoil=max(0,1-t*4)*math.sin(min(1,t*8)*pi*.5)
            add_rotation(rig,'forearm.R',(1,0,0),-.22*recoil)
            add_rotation(rig,'chest',(1,0,0),-.06*recoil)
    if clip.startswith('Jump_'):
        if clip=='Jump_Start': crouch=.22*math.sin(pi*t); lift=.09*t; fold=.3*t
        elif clip=='Jump_Air': crouch=0; lift=.09; fold=.3+.035*math.sin(q)
        elif clip=='Jump_Land': crouch=.20*math.sin(pi*t); lift=.09*(1-t); fold=.3*(1-t)
        else:
            crouch=.17*math.sin(pi*min(t/.23,1)) if t<.23 else .17*math.sin(pi*(t-.75)/.25) if t>.75 else 0
            lift=.48*math.sin(pi*(t-.16)/.68) if .16<t<.84 else 0
            fold=.45*math.sin(pi*t)
        world_offset(rig,'pelvis',(0,0,lift-crouch))
        for suf in ['L','R']:
            set_rotation(rig,'thigh.'+suf,(1,0,0),-crouch*2.5-fold)
            set_rotation(rig,'shin.'+suf,(1,0,0),crouch*5+fold*1.7)
            set_rotation(rig,'foot.'+suf,(1,0,0),-crouch*2)
            set_rotation(rig,'upper_arm.'+suf,(1,0,0),-.5*math.sin(pi*t))
    if clip in ['Walk','Run','Walk_To_Run']:
        fast=0 if clip=='Walk' else 1 if clip=='Run' else ease(t)
        bob=-.075-.065*fast+(.012+.028*fast)*(1-math.cos(q*2))
        world_offset(rig,'pelvis',(0,0,bob))
        for suf,phase in [('L',0),('R',.5)]:
            p=(cycle+phase)%1;half=.27+.14*fast
            y=-half+4*half*p if p<.5 else half-2*half*ease((p-.5)*2)
            z=(.085+.075*fast)*math.sin(pi*(p-.5)*2) if p>=.5 else 0
            foot=rig.data.bones['foot.'+suf].head_local.copy();foot.y+=y;foot.z+=z
            solve_chain(rig,'thigh.'+suf,'shin.'+suf,'foot.'+suf,foot,(0,-1,0),(0,0,bob))
    if clip.startswith('Jump_'):
        # Keep both feet above the contact plane throughout crouch and landing.
        for suf in ['L','R']:
            target=rig.data.bones['foot.'+suf].head_local+Vector((0,-fold*.08,lift+fold*.25))
            solve_chain(rig,'thigh.'+suf,'shin.'+suf,'foot.'+suf,target,(0,-1,0),(0,0,lift-crouch))
    if clip in ['Aim_Base','Attack_Fire']:
        recoil=0 if clip=='Aim_Base' else max(0,1-t*4)*math.sin(min(1,t*8)*pi*.5)
        # Aim pose is solved from wrist targets; the pistol's rest barrel points down.
        set_rotation(rig,'chest',(1,0,0),0)
        solve_chain(rig,'upper_arm.R','forearm.R','hand.R',(-.11,-.47+.025*recoil,1.32+.018*recoil),(-1,0,-.2),end_delta=Quaternion((1,0,0),-pi/2-.15*recoil))
        solve_chain(rig,'upper_arm.L','forearm.L','hand.L',(-.08,-.40,1.30),(1,0,-.2),end_delta=Quaternion((0,0,1),-.4)@Quaternion((1,0,0),-pi/2))
    if clip.startswith('Turn_') and clip!='Turn_Base':
        for suf,phase in [('L',0),('R',pi)]:
            a=q+phase
            target=rig.data.bones['foot.'+suf].head_local+Vector((0,.075*math.sin(a),.075*max(0,math.sin(a))))
            solve_chain(rig,'thigh.'+suf,'shin.'+suf,'foot.'+suf,target,(0,-1,0))

def pose_creature(rig,info,clip,t):
    cycle=.8*t+.2*t*t if clip=='Walk_To_Run' else t
    q=math.tau*cycle; pi=math.pi; legs=info['legs']; mystic=legs==6; mech=legs==2
    if clip in ['Reference_A','Turn_Base']:return
    if clip=='Idle':
        world_offset(rig,'body',(0,0,.018*math.sin(q)))
        set_rotation(rig,'head',(0,0,1),.09*math.sin(q))
    if clip in ['Walk','Run','Walk_To_Run']:
        fast=0 if clip=='Walk' else 1 if clip=='Run' else ease(t)
        for i in range(legs):
            # Diagonal pairs (4), alternating tripods (6), alternating feet (2).
            parity=(i%2+i//2)%2 if legs in [4,6] else i%2
            a=q+parity*pi; amp=.24+fast*.26
            set_rotation(rig,f'leg_upper.{i:02}',(1,0,0),-amp*math.cos(a))
            set_rotation(rig,f'leg_lower.{i:02}',(1,0,0),(.3+fast*.45)*max(0,math.sin(a)))
            set_rotation(rig,f'foot.{i:02}',(1,0,0),amp*.5*math.cos(a))
        world_offset(rig,'body',(0,0,(.012+.025*fast)*(1-math.cos(2*q))))
        set_rotation(rig,'body',(0,0,1),.04*math.sin(q))
    if clip.startswith('Turn_') and clip!='Turn_Base':
        s=1 if 'Left' in clip else -1
        set_rotation(rig,'root',(0,0,1),s*pi*.5*ease(t))
        set_rotation(rig,'head',(0,0,1),s*.2*math.sin(pi*t))
        for i in range(legs):
            a=q+((i%2+i//2)%2)*pi
            set_rotation(rig,f'leg_upper.{i:02}',(1,0,0),.18*math.sin(a))
            set_rotation(rig,f'leg_lower.{i:02}',(1,0,0),.3*max(0,math.sin(a)))
    if clip=='Attack_Melee':
        a=math.sin(pi*t)**2
        set_rotation(rig,'head',(1,0,0),.40*math.sin(q))
        set_rotation(rig,'jaw',(1,0,0),.65*a)
        set_rotation(rig,'body',(1,0,0),-.12*math.sin(q))
        for i in range(min(2,legs)):
            set_rotation(rig,f'leg_upper.{i:02}',(1,0,0),-.75*a)
            set_rotation(rig,f'leg_lower.{i:02}',(1,0,0),.3*a)
    if clip in ['Aim_Base','Attack_Fire']:
        set_rotation(rig,'head',(1,0,0),-.04)
        if clip=='Attack_Fire':
            a=max(0,1-t*3)*math.sin(min(1,t*8)*pi*.5)
            set_rotation(rig,'weapon_socket',(1,0,0),-.22*a)
            set_rotation(rig,'body',(1,0,0),-.08*a)
            if not mech: set_rotation(rig,'jaw',(1,0,0),.4*math.sin(pi*t))
    if clip.startswith('Jump_'):
        if clip=='Jump_Start': lift=.1*t-.14*math.sin(pi*t); fold=.3*t
        elif clip=='Jump_Air': lift=.1; fold=.3+.04*math.sin(q)
        elif clip=='Jump_Land': lift=.1*(1-t)-.14*math.sin(pi*t); fold=.3*(1-t)
        else: lift=.40*max(0,math.sin(pi*(t-.15)/.7)) if .15<t<.85 else -.10*math.sin(pi*t/.15) if t<.15 else -.1*math.sin(pi*(t-.85)/.15); fold=.5*math.sin(pi*t)
        world_offset(rig,'body',(0,0,lift))
        for i in range(legs):
            set_rotation(rig,f'leg_upper.{i:02}',(1,0,0),-fold)
            set_rotation(rig,f'leg_lower.{i:02}',(1,0,0),fold*1.5)
    if 'tail.01' in rig.pose.bones:
        set_rotation(rig,'tail.01',(0,0,1),.16*math.sin(q))
        set_rotation(rig,'tail.02',(0,0,1),.20*math.sin(q-.8))
    if mystic:
        for i in range(3): set_rotation(rig,f'veil.{i}',(0,0,1),.16*math.sin(q+i*math.tau/3))
    if clip in ['Walk','Run','Walk_To_Run']:
        fast=0 if clip=='Walk' else 1 if clip=='Run' else ease(t)
        bob=-.025+(.01+.012*fast)*(1-math.cos(q*2))
        set_rotation(rig,'body',(0,0,1),0);world_offset(rig,'body',(0,0,bob))
        for i in range(legs):
            parity=(i%2+i//2)%2 if legs in [4,6] else i%2
            p=(cycle+parity*.5)%1;half=(.20 if mech else .18)+.15*fast
            y=-half+4*half*p if p<.5 else half-2*half*ease((p-.5)*2)
            z=(.085+.075*fast)*math.sin(pi*(p-.5)*2) if p>=.5 else 0
            target=rig.data.bones[f'foot.{i:02}'].head_local+Vector((0,y,z))
            solve_chain(rig,f'leg_upper.{i:02}',f'leg_lower.{i:02}',f'foot.{i:02}',target,((1 if i%2==0 else -1)*.2,1,0),(0,0,bob))
    if clip.startswith('Jump_'):
        for i in range(legs):
            target=rig.data.bones[f'foot.{i:02}'].head_local+Vector((0,-fold*.06,max(0,lift)+fold*.2))
            solve_chain(rig,f'leg_upper.{i:02}',f'leg_lower.{i:02}',f'foot.{i:02}',target,((1 if i%2==0 else -1)*.2,1,0),(0,0,lift))
    if clip.startswith('Turn_') and clip!='Turn_Base':
        for i in range(legs):
            a=q+((i%2+i//2)%2)*pi
            target=rig.data.bones[f'foot.{i:02}'].head_local+Vector((0,.08*math.sin(a),.09*max(0,math.sin(a))))
            solve_chain(rig,f'leg_upper.{i:02}',f'leg_lower.{i:02}',f'foot.{i:02}',target,((1 if i%2==0 else -1)*.2,1,0))

def animate(rig,info=None):
    rig.animation_data_create(); actions=[]; metadata=[]
    for clip,frames,loop in CLIP_SPECS:
        action=bpy.data.actions.new(rig.name+'__'+clip); action.use_fake_user=True
        rig.animation_data.action=action
        for f in range(frames+1):
            t=f/frames; reset_pose(rig)
            if info:pose_creature(rig,info,clip,t)
            else:pose_human(rig,clip,t)
            for p in rig.pose.bones:
                p.keyframe_insert('rotation_quaternion',frame=f)
                p.keyframe_insert('location',frame=f)
        # Bake linear interpolation so GLB and Blender curves are identical.
        for fc in action.fcurves:
            for kp in fc.keyframe_points: kp.interpolation='LINEAR'
        action['loop']=loop; action['fps']=30; action['clip_name']=clip
        track=rig.animation_data.nla_tracks.new(); track.name=clip
        strip=track.strips.new(clip,0,action); strip.name=clip; strip.action_frame_start=0; strip.action_frame_end=frames
        strip.blend_type='REPLACE'; strip.extrapolation='NOTHING'; track.mute=True
        # NLA tracks unmuted only during export, source defaults to rest pose.
        actions.append(action)
        events=[]
        if clip in ['Walk','Run']:events=[{'phase':0,'event':'left_contact_or_tripod_A'},{'phase':.5,'event':'right_contact_or_tripod_B'}]
        if clip=='Attack_Melee':events=[{'phase':.46,'event':'melee_hit_window_start'},{'phase':.6,'event':'melee_hit_window_end'}]
        if clip=='Attack_Fire':events=[{'phase':.08,'event':'projectile_or_emitter_trigger'}]
        metadata.append({'clip':clip,'action':action.name,'fps':30,'start':0,'end':frames,'duration_seconds':frames/30,'loop':loop,'root_translation':'in_place','root_yaw_degrees':90 if clip=='Turn_Left_90' else -90 if clip=='Turn_Right_90' else 0,'events':events})
    rig.animation_data.action=None; reset_pose(rig)
    CLIPS[rig.name]=metadata
    return actions

animate(human)
for kind,info in CREATURES.items():animate(info['rig'],info)
print('ANIMATIONS',sum(len(x) for x in CLIPS.values()),flush=True)

SC=collection('03_Architecture_Modules'); EC=collection('04_Environment_Modules')
STATIC=[]
def static(name,g,slot='module',environment=False):
    obj=g.make(name,EC if environment else SC,None,slot,'environment' if environment else 'habitat')
    STATIC.append(name); return obj

# 2 m horizontal grid; 3.6 m habitat story. All individual modules <=500 triangles.
static('Hab_Floor_2m',Geo().box((0,0,-.10),(2,2,.20),'Habitat_Concrete').box((0,0,.015),(1.94,1.94,.03),'Habitat_Panels'),'floor')
static('Hab_Roof_2m',Geo().box((0,0,-.09),(2,2,.18),'Habitat_Panels').box((0,0,.025),(1.78,1.78,.05),'Steel'),'roof')
static('Hab_Wall_Solid_2m',Geo().box((0,0,1.8),(2,.18,3.6),'Habitat_Panels').box((0,-.105,1.0),(1.85,.035,.06),'Steel').box((0,-.105,3.15),(1.85,.035,.06),'Steel'),'wall')
g=Geo().box((0,0,.73),(2,.18,1.46),'Habitat_Panels').box((0,0,2.91),(2,.18,1.38),'Habitat_Panels')
for s in [-1,1]:g.box((s*.9,0,1.84),(.2,.18,.76),'Steel')
g.box((0,0,1.84),(1.6,.085,.76),'Glass_Opaque').box((0,-.10,2.25),(1.7,.04,.045),'Glow_Amber')
static('Hab_Wall_Window_2m',g,'wall')
g=Geo().box((0,0,2.875),(2,.2,1.45),'Habitat_Panels')
for s in [-1,1]:g.box((s*.8,0,1.075),(.4,.2,2.15),'Habitat_Panels').box((s*.63,-.055,1.075),(.10,.22,2.15),'Steel')
g.box((0,-.055,2.2),(1.36,.22,.10),'Steel')
static('Hab_Wall_Door_2m',g,'wall')
g=Geo().box((0,0,2.325),(2,.20,.35),'Habitat_Panels')
for s in [-1,1]:g.box((s*.8,0,1.075),(.4,.2,2.15),'Habitat_Panels').box((s*.63,-.055,1.075),(.10,.22,2.15),'Steel')
g.box((0,-.055,2.2),(1.36,.22,.10),'Steel')
static('Hab_Wall_Door_Low_2p5m',g,'wall')
static('Hab_Corner_3p6m',Geo().box((0,0,1.8),(.28,.28,3.6),'Steel').box((-.16,-.16,1.8),(.045,.045,3.6),'Glow_Cyan'),'corner')
static('Hab_Stack_Connector',Geo().box((0,0,0),(.42,.42,.24),'Steel').box((0,0,.18),(.24,.24,.12),'Warning'),'connector')
static('Hab_Slit_Window',Geo().box((0,0,0),(1.8,.10,.40),'Steel').box((0,-.065,0),(1.62,.04,.25),'Glass_Opaque').box((0,-.09,-.14),(1.65,.02,.018),'Glow_Cyan'),'window')
static('Hab_Pressure_Door',Geo().box((0,0,1.075),(1.16,.12,2.15),'Steel').box((0,-.07,1.35),(.6,.02,.12),'Glow_Cyan').box((.40,-.085,.95),(.08,.045,.22),'Warning'),'door')
static('Hab_Balcony_2m',Geo().box((0,0,-.09),(2,2,.18),'Steel').box((0,-.95,.90),(2,.06,.06),'Steel').box((0,-.95,.50),(2,.06,.04),'Steel').box((-.95,-.95,.5),(.06,.06,1),'Steel').box((.95,-.95,.5),(.06,.06,1),'Steel'),'balcony')
g=Geo()
for i in range(9):g.box((0,.16+i*.31,.10+i*.2),(1.20,.32,.20),'Steel')
g.rod((-.58,0,1.0),(-.58,2.9,2.8),.025,.025,'Steel',sides=6)
g.rod((.58,0,1.0),(.58,2.9,2.8),.025,.025,'Steel',sides=6)
static('Hab_Stair_HalfStory',g,'stair')
g=Geo().box((0,0,0),(1.8,1.3,.09),'Steel').box((0,-.03,.06),(1.72,1.20,.03),'Solar')
for x in [-.55,0,.55]:g.box((x,0,.085),(.018,1.22,.012),'Alloy_Light')
g.box((0,0,-.40),(.15,.15,.75),'Steel')
static('Hab_Solar_Array',g,'roof.gear')
g=Geo().box((0,0,.4),(1.4,.75,.8),'Steel')
for i in range(8):g.box((-.56+i*.16,-.40,.40),(.045,.07,.64),'Alloy_Light')
g.rod((.43,.10,.8),(.43,.10,1.08),.16,.14,'Copper',sides=8)
static('Hab_Heat_Exchanger',g,'roof.gear')
g=Geo().rod((0,0,0),(0,0,2.1),.085,.04,'Steel',sides=8).box((0,0,.10),(.45,.45,.20),'Steel')
for z in [1.35,1.8]:g.box((0,0,z),(.9,.06,.05),'Alloy_Light')
g.ellipsoid((0,0,2.15),(.065,.065,.065),'Glow_Amber',sides=8,rings=4)
static('Hab_Sensor_Mast',g,'roof.gear')
g=Geo().box((0,0,.1),(.5,.5,.2),'Steel').rod((0,0,.15),(0,0,3.05),.075,.045,'Steel',sides=8)
g.rod((0,0,3.02),(0,-.70,3.18),.06,.04,'Steel',sides=8).box((0,-.70,3.17),(.42,.38,.10),'Steel').box((0,-.70,3.11),(.32,.28,.035),'Glow_Amber')
static('Street_Lamp_3p2m',g,'street.lamp')
static('Street_Bollard',Geo().rod((0,0,0),(0,0,.85),.12,.09,'Steel',sides=8).rod((0,0,.68),(0,0,.76),.102,.102,'Glow_Amber',sides=8),'street.bollard')
static('Street_Bulkhead_2m',Geo().box((0,0,1.6),(2,.35,3.2),'Habitat_Concrete').box((0,-.20,1.7),(1.72,.08,1.5),'Steel').box((0,-.25,2.75),(1.76,.02,.055),'Warning'),'street.barrier')
g=Geo().box((-2.20,0,1.9),(.4,.5,3.8),'Steel').box((2.20,0,1.9),(.4,.5,3.8),'Steel').box((0,0,3.6),(4,.5,.4),'Steel')
g.box((0,-.28,3.55),(3.8,.03,.08),'Glow_Amber')
static('Street_Gate_Frame_4m',g,'street.gate')
static('Interior_Bed',Geo().box((0,0,.28),(1.0,2.1,.56),'Steel').box((0,0,.64),(.94,2.02,.18),'Cloth_Medical').box((0,-.75,.77),(.70,.42,.16),'Cloth_Teal').box((0,.15,.76),(.96,1.3,.10),'Cloth_Teal'),'interior.bed')
static('Interior_Table',Geo().box((0,0,.76),(1.35,.75,.10),'Wood_Composite').box((-.5,0,.36),(.09,.55,.72),'Steel').box((.5,0,.36),(.09,.55,.72),'Steel'),'interior.table')
static('Interior_Chair',Geo().box((0,0,.46),(.48,.50,.09),'Rubber').box((0,.21,.76),(.48,.075,.56),'Rubber').box((-.18,0,.21),(.065,.40,.42),'Steel').box((.18,0,.21),(.065,.40,.42),'Steel'),'interior.chair')
static('Interior_Storage',Geo().box((0,0,1.0),(1.2,.55,2.0),'Habitat_Panels').box((-.3,-.29,1.0),(.55,.04,1.9),'Steel').box((.3,-.29,1.0),(.55,.04,1.9),'Steel').box((0,-.32,1.15),(.13,.04,.25),'Glow_Cyan'),'interior.storage')
g=Geo().box((0,0,.49),(1.7,.70,.98),'Steel').box((0,0,1.03),(1.8,.80,.10),'Alloy_Light')
g.box((0,.25,1.42),(1.6,.08,.65),'Steel').box((-.35,-.1,1.13),(.45,.30,.12),'Warning').box((.45,.18,1.40),(.4,.02,.22),'Glow_Cyan')
static('Interior_Workbench',g,'interior.workbench')
static('Interior_Terminal',Geo().box((0,0,.50),(.50,.45,1.0),'Steel').box((0,0,1.22),(.75,.18,.55),'Steel').box((0,-.10,1.24),(.64,.025,.40),'Glass_Opaque').box((0,-.117,1.28),(.52,.01,.023),'Glow_Cyan').box((-.16,-.117,1.15),(.20,.01,.04),'Glow_Cyan'),'interior.terminal')
static('Interior_Wall_Light',Geo().box((0,0,0),(.50,.10,.22),'Steel').box((0,-.065,0),(.42,.04,.15),'Glow_Amber'),'interior.light')
static('Interior_Crate',Geo().box((0,0,.38),(.80,.60,.76),'Steel').box((0,-.31,.38),(.62,.03,.45),'Cloth_Ochre').box((0,0,.80),(.86,.66,.08),'Rubber'),'interior.crate')
static('Interior_Planter',Geo().rings([(0,0,0),(0,0,.50)],[.24,.35],'Ceramic',sides=8).ellipsoid((0,0,.62),(.36,.28,.25),'Alien_Leaf',sides=8,rings=4),'interior.planter')
static('Interior_Med_Kit',Geo().box((0,0,.16),(.42,.27,.32),'Cloth_Medical').box((0,-.145,.16),(.20,.02,.055),'Cloth_Burgundy').box((0,-.146,.16),(.055,.02,.20),'Cloth_Burgundy'),'interior.medkit')
static('Field_Generator',Geo().box((0,0,.40),(1.4,.9,.8),'Steel').rod((0,0,.90),(0,0,1.16),.44,.44,'Copper',sides=10).rod((0,0,1.0),(0,0,1.05),.46,.46,'Glow_Cyan',sides=10).box((.50,-.5,.55),(.3,.12,.3),'Warning'),'field.generator')
static('Field_Breaker',Geo().box((0,0,.75),(.65,.35,1.5),'Habitat_Panels').box((0,-.20,1.1),(.47,.07,.5),'Steel').box((0,-.25,1.15),(.3,.03,.05),'Glow_Amber').box((0,-.26,.92),(.06,.08,.18),'Warning'),'field.breaker')
static('Field_Survey_Beacon',Geo().rod((0,0,0),(0,0,1.25),.025,.02,'Steel',sides=6).box((0,0,1.15),(.32,.07,.25),'Warning').ellipsoid((0,0,1.4),(.09,.09,.1),'Glow_Cyan',sides=6,rings=4),'field.beacon')
static('Mine_Support',Geo().box((-1.2,0,1.4),(.25,.32,2.8),'Steel').box((1.2,0,1.4),(.25,.32,2.8),'Steel').box((0,0,2.75),(2.5,.32,.25),'Steel').box((0,-.18,2.75),(.8,.03,.06),'Glow_Amber'),'mine.support')
static('Mine_Lantern',Geo().box((0,0,.07),(.23,.23,.14),'Steel').rod((0,0,.14),(0,0,.44),.075,.075,'Glow_Amber',sides=8).box((0,0,.47),(.22,.22,.08),'Steel').rod((-.07,0,.50),(-.07,0,.62),.012,.012,'Steel',sides=6).rod((.07,0,.50),(.07,0,.62),.012,.012,'Steel',sides=6).rod((-.07,0,.62),(.07,0,.62),.012,.012,'Steel',sides=6),'mine.lantern')
static('Ruin_Pillar',Geo().rings([(0,0,0),(0,0,.4),(0,0,2.9),(0,0,3.4)],[.55,.39,.31,.44],'Ruin_Obsidian',sides=6).box((0,-.35,1.65),(.035,.03,1.8),'Glow_Violet'),'ruin.pillar')
g=Geo()
for i in range(6):
    a=i*math.tau/6; b=(i+.88)*math.tau/6
    g.rod((2.8*math.cos(a),0,3+2.8*math.sin(a)),(2.8*math.cos(b),0,3+2.8*math.sin(b)),.32,.32,'Ruin_Obsidian',sides=6)
    g.rod((2.5*math.cos(a),-.28,3+2.5*math.sin(a)),(2.5*math.cos(b),-.28,3+2.5*math.sin(b)),.035,.035,'Glow_Violet',sides=5)
static('Ruin_Gate_Segment_Ring',g,'ruin.gate')
g=Geo().box((0,0,0),(1.2,.15,1.8),'Ruin_Obsidian')
for s in [-1,1]:
    g.rod((s*.40,-.1,-.2),(0,-.1,.45),.025,.025,'Glow_Cyan',sides=5)
    g.rod((0,-.1,.45),(s*.40,-.1,.10),.025,.025,'Glow_Cyan',sides=5)
g.ellipsoid((0,-.12,.16),(.07,.025,.07),'Glow_Violet',sides=6,rings=4)
static('Ruin_Observer_Relief',g,'ruin.relief')

for name,mat in [('Soil','Terrain_Regolith'),('Sand','Terrain_Dunes'),('Grass','Terrain_AlienGrass'),('Road','Road_Basalt'),('Mine','Rock_Mine')]:
    static('Ground_'+name+'_2m',Geo().box((0,0,-.08),(2,2,.16),mat),'ground',True)
static('Road_Straight_2m',Geo().box((0,0,-.09),(2,2,.18),'Road_Basalt').box((-.85,0,.006),(.04,2,.012),'Ceramic').box((.85,0,.006),(.04,2,.012),'Ceramic').box((0,0,.007),(.045,.7,.014),'Warning'),'road',True)
g=Geo().box((0,0,-.09),(2,2,.18),'Road_Basalt')
for i in range(6):
    a=i*math.pi/12; b=(i+1)*math.pi/12
    g.rod((-.85+1.70*math.cos(a),-.85+1.70*math.sin(a),.012),(-.85+1.70*math.cos(b),-.85+1.70*math.sin(b),.012),.025,.025,'Ceramic',sides=4)
static('Road_Corner_2m',g,'road',True)
g=Geo().box((0,0,-.09),(2,2,.18),'Road_Basalt')
for i in range(5):g.box((0,-.75+i*.38,.012),(1.65,.16,.024),'Ceramic')
static('Road_Crossing_2m',g,'road',True)
static('Road_Curb_2m',Geo().box((0,0,.08),(2,.22,.16),'Habitat_Concrete').box((0,-.12,.13),(2,.02,.035),'Warning'),'road.curb',True)
for i in range(5):
    g=Geo().ellipsoid((0,0,.24+i*.08),(.42+i*.14,.35+i*.09,.29+i*.09),'Rock_Mine',sides=7+i%3,rings=4)
    # Deform deterministically for visibly different rock silhouettes.
    g.v=[(x*(1+.13*math.sin(k*2.1+i)),y*(1+.16*math.cos(k*1.7)),max(0,z)) for k,(x,y,z) in enumerate(g.v)]
    static(f'Nature_Rock_{i+1:02}',g,'nature.rock',True)
for i in range(3):
    g=Geo(); height=1.8+i*.6
    g.rod((0,0,0),(0,0,height),.11,.045,'Wood_Composite',sides=7)
    for j in range(4):
        a=j*2.4+i; origin=(0,0,height*.45+j*.18)
        end=(math.cos(a)*(.5+i*.1),math.sin(a)*(.5+i*.1),height*.7+j*.2)
        g.rod(origin,end,.045,.015,'Wood_Composite',sides=6)
        g.ellipsoid(end,(.34,.29,.17),'Alien_Leaf',sides=7,rings=3)
    static(f'Nature_Alien_Tree_{i+1:02}',g,'nature.tree',True)
for i in range(3):
    g=Geo()
    for j in range(5):
        a=j*math.tau/5; r=.15+.03*(j%2); h=.3+i*.15+j*.035
        g.rod((math.cos(a)*r,math.sin(a)*r,0),(math.cos(a)*r*1.4,math.sin(a)*r*1.4,h),.018,.008,'Alien_Leaf',sides=5)
        g.ellipsoid((math.cos(a)*r*1.4,math.sin(a)*r*1.4,h),(.07,.065,.095),'Glow_Cyan' if i==0 else 'Glow_Violet' if i==1 else 'Glow_Amber',sides=6,rings=3)
    static(f'Nature_Glow_Reeds_{i+1:02}',g,'nature.plant',True)
g=Geo()
for j in range(7):
    a=j*2.4; x=math.cos(a)*.3; y=math.sin(a)*.3; h=.25+j*.055
    g.rod((x,y,0),(x,y,h),.025,.013,'Alien_Leaf',sides=5)
    g.rings([(x,y,h-.05),(x,y,h),(x,y,h+.05)],[.04,.14,.03],'Alien_Petal',sides=6)
static('Nature_Fungal_Cluster',g,'nature.plant',True)
g=Geo()
for i in range(5):
    x=(i-2)*.17; y=.12*math.sin(i*2)
    g.rings([(x,y,0),(x,y,.15),(x+.04,y,.4+i*.13)],[(.07,.09),(.08,.085),(.002,.002)],'Glow_Violet' if i%2 else 'Ruin_Obsidian',sides=5)
static('Nature_Crystal_Cluster',g,'nature.crystal',True)
g=Geo()
for j in range(12):
    a=j*2.4; r=.09+.025*j
    g.rod((math.cos(a)*r,math.sin(a)*r,0),(math.cos(a+.25)*r*1.1,math.sin(a+.25)*r*1.1,.16+.05*(j%3)),.018,.002,'Alien_Leaf',sides=3)
static('Nature_Grass_Clump',g,'nature.grass',True)

# Dome sectors, reusable at a 6.5 m envelope; 12 sectors form a whole dome.
g=Geo(); v=[]; f=[]
for j in range(7):
    a=(math.pi/2)*j/6
    for k in range(3):
        az=math.tau*(k/2)/12
        rr=max(.005,4*math.cos(a)); v.append((rr*math.cos(az),rr*math.sin(az),2.5+4*math.sin(a)))
for j in range(6):
    for k in range(2):
        n=j*3+k;f.append((n,n+1,n+4,n+3))
g.add(v,f,'Habitat_Panels')
for j in range(6):g.rod(v[j*3],v[(j+1)*3],.035,.035,'Steel',sides=6)
static('Hab_Dome_Sector_30deg',g,'dome.sector')
print('STATIC_MODULES',len(STATIC),flush=True)

DC=collection('05_Animated_Architecture')
door=armature('PressureDoor_Rig',[('root',(0,0,0),(0,0,.2),None),('leaf',(0,0,1),(0,0,1.2),'root')],DC)
door_obj=PROTOTYPES['Hab_Pressure_Door'].copy();door_obj.data=door_obj.data.copy();DC.objects.link(door_obj)
door_obj.name='PressureDoor_AnimatedLeaf';door_obj.parent=door
vg=door_obj.vertex_groups.new(name='leaf');vg.add(list(range(len(door_obj.data.vertices))),1,'REPLACE')
mod=door_obj.modifiers.new('Skin','ARMATURE');mod.object=door
door.animation_data_create(); CLIPS[door.name]=[]
for clip,reverse in [('Door_Open',False),('Door_Close',True)]:
    action=bpy.data.actions.new('PressureDoor__'+clip);action.use_fake_user=True;door.animation_data.action=action
    for f in range(31):
        u=ease(f/30);world_offset(door,'leaf',((1-u if reverse else u)*1.25,0,0))
        door.pose.bones['leaf'].keyframe_insert('location',frame=f)
    for fc in action.fcurves:
        for kp in fc.keyframe_points:kp.interpolation='LINEAR'
    track=door.animation_data.nla_tracks.new();track.name=clip;strip=track.strips.new(clip,0,action);strip.name=clip;track.mute=True
    CLIPS[door.name].append({'clip':clip,'start':0,'end':30,'duration_seconds':1,'loop':False})
door.animation_data.action=None;reset_pose(door)
exec(compile((ROOT/'Tools/vehicle_design.py').read_text(encoding='utf-8'),'vehicle_design.py','exec'),globals())

CHARACTERS={}
def character_recipe(name,role,build,head,hair,face='Neutral',hat=None,skin=None):
    build={'Standard':'Male_Normal','Slender':'Female_Normal','Broad':'Male_Normal'}.get(build,build)
    ids=[f'Body_{build}_Pelvis','Body_Neck',f'Head_{head}',f'Face_{face}']
    if role not in ['Civilian','Researcher','Medic']:ids.append('Weapon_CoilPistol_35cm')
    accessories=['Accessory_'+n for n in ROLE_ACCESSORIES[role]]
    if any('Helmet' in n for n in accessories):hair=None;hat=None
    elif 'Accessory_Mining_Hardhat' in accessories:hair='HatClearance_Buzz';hat=None
    ids.extend(accessories)
    if hair:ids.append('Hair_'+hair)
    if hat:ids.append(hat)
    ids.append(f'Outfit_{role}_{build}_Torso')
    for suf in ['L','R']:
        ids += [f'Body_Hand_{suf}',f'Boot_{suf}']
        for part in ['Sleeve','Cuff','Trouser','Greave']:ids.append(f'Outfit_{role}_{build}_{part}_{suf}')
    CHARACTERS[name]={'rig':human,'parts':ids,'body':build,'role':role,'skin':skin,'offsets':{n:list(accessory_offset(n,build,role)) for n in accessories}}

character_recipe('Player_Explorer','Explorer','Standard','Oval','SidePart')
character_recipe('Ria_Engineer','Engineer','Slender','Round','Ponytail',skin='Skin_Light')
character_recipe('Kain_Guard','Guard','Broad','Angular','Buzz','Stern',skin='Skin_Deep')
character_recipe('Mara_Medic','Medic','Slender','Narrow','Bun',skin='Skin_Cool')
character_recipe('NPC_Researcher','Researcher','Standard','Oval','Bob','Alert')
character_recipe('NPC_Miner','Miner','Male_Heavy','Round','HatClearance_Ponytail','Neutral','Hat_Engineer','Skin_Light')
character_recipe('NPC_Guard_Helmet','Guard','Standard','Angular',None,'Stern','Helmet_Guard','Skin_Deep')
character_recipe('NPC_HeavyCombat','HeavyCombat','Male_Heavy','Angular',None,'Stern',skin='Skin_Deep')
character_recipe('NPC_Pilot','Pilot','Female_Lean','Oval',None,'Alert',skin='Skin_Light')
character_recipe('NPC_Civilian','Civilian','Female_Heavy','Round','Curls','Neutral',skin='Skin_Warm')

BUILDINGS={}
def building(name):
    col=collection('Prefab_'+name);root=bpy.data.objects.new(name,None);col.objects.link(root)
    root['units']='meters';root['snap_m']=2;root['assembly_type']='linked modular meshes'
    BUILDINGS[name]={'collection':col,'root':root,'objects':[],'recipe':[]}
    return BUILDINGS[name]

def place(prefab,part,xyz=(0,0,0),yaw=0,scale=(1,1,1)):
    obj=PROTOTYPES[part].copy();obj.data=PROTOTYPES[part].data;prefab['collection'].objects.link(obj)
    obj.asset_clear();obj.parent=prefab['root'];obj.location=xyz;obj.rotation_euler[2]=yaw;obj.scale=scale
    prefab['objects'].append(obj);prefab['recipe'].append({'part':part,'position_m':list(xyz),'yaw_degrees':round(math.degrees(yaw),3),'scale':list(scale)})
    return obj

def rectangular_shell(p,width,depth,height,stories=1,terraced=False,workshop=False):
    for level in range(stories):
        w=width-(2*level if terraced else 0);d=depth
        z=height/stories*level;story_h=height/stories
        for ix in range(int(w/2)):
            for iy in range(int(d/2)):
                place(p,'Hab_Floor_2m',(-w/2+1+ix*2,-d/2+1+iy*2,z))
                if level==stories-1:place(p,'Hab_Roof_2m',(-w/2+1+ix*2,-d/2+1+iy*2,height))
        for ix in range(int(w/2)):
            x=-w/2+1+ix*2
            for side in [-1,1]:
                door_here=level==0 and ix==int(w/2)//2 and side==-1
                part='Hab_Wall_Door_2m' if door_here else 'Hab_Wall_Window_2m' if ix%2==0 else 'Hab_Wall_Solid_2m'
                # Door frames retain a 2.15 m door; fill the extra workshop height above.
                if door_here and story_h!=3.6:
                    place(p,part,(x,side*d/2,z))
                    if story_h>3.6:place(p,'Hab_Wall_Solid_2m',(x,side*d/2,z+3.6),0,(1,1,(story_h-3.6)/3.6))
                else:place(p,part,(x,side*d/2,z),0,(1,1,story_h/3.6))
                if door_here:place(p,'Hab_Pressure_Door',(x,side*d/2-.03,z))
        for iy in range(int(d/2)):
            y=-d/2+1+iy*2
            for side in [-1,1]:place(p,'Hab_Wall_Window_2m',(side*w/2,y,z),math.pi/2,(1,1,story_h/3.6))
        for x in [-w/2,w/2]:
            for y in [-d/2,d/2]:place(p,'Hab_Corner_3p6m',(x,y,z),0,(1,1,story_h/3.6))
        if terraced and level>0:
            for side in [-1,1]:
                for iy in range(int(d/2)):place(p,'Hab_Balcony_2m',(side*(w/2+1),-d/2+1+iy*2,z),side*math.pi/2)

p=building('Habitat_7p2m');rectangular_shell(p,6,4,7.2,2)
place(p,'Hab_Heat_Exchanger',(-1,0,7.2));place(p,'Hab_Solar_Array',(1,0,7.7));place(p,'Hab_Sensor_Mast',(2,1,7.2))
p=building('TerracedPods_10p8m');rectangular_shell(p,10,4,10.8,3,True)
place(p,'Hab_Solar_Array',(0,0,11.3));place(p,'Hab_Heat_Exchanger',(1,0,10.8))
p=building('Workshop_5p4m');rectangular_shell(p,8,6,5.4,1,False,True)
place(p,'Hab_Heat_Exchanger',(-2,1,5.4));place(p,'Hab_Sensor_Mast',(2,1,5.4))
for name,height in [('RibDome_6p5m',6.5),('Observatory_11p5m',11.5)]:
    p=building(name);base=height-6.5
    if base:
        # An octagonal observation tower supports the reusable dome.
        for i in range(8):
            a=i*math.tau/8
            place(p,'Hab_Wall_Window_2m',(3.4*math.cos(a),3.4*math.sin(a),0),a+math.pi/2,(1.41,1,base/3.6))
    for i in range(12):
        a=i*math.tau/12
        place(p,'Hab_Dome_Sector_30deg',(0,0,base),a)
        part='Hab_Wall_Door_Low_2p5m' if i==8 else 'Hab_Wall_Window_2m'
        position=(3.86*math.cos(a+math.pi/12),3.86*math.sin(a+math.pi/12),base)
        place(p,part,position,a+math.pi/12+math.pi/2,(1.035,1,1 if i==8 else 2.5/3.6))
        if i==8:place(p,'Hab_Pressure_Door',position,a+math.pi/12+math.pi/2)
    for ix in [-2,0,2]:
        for iy in [-2,0,2]:place(p,'Hab_Floor_2m',(ix,iy,base))
    place(p,'Hab_Sensor_Mast',(0,0,height))
print('PREFABS',len(BUILDINGS),'CHARACTERS',len(CHARACTERS),flush=True)

def clone_actor(name,rig,parts,col,offset=(0,0,0),skin=None,part_offsets=None):
    newrig=rig.copy();newrig.data=rig.data.copy();newrig.name=name+'_Rig';col.objects.link(newrig)
    newrig.location=offset
    newrig.animation_data_clear();newrig.animation_data_create()
    objects=[]
    for part in parts:
        original=PROTOTYPES[part];obj=original.copy();obj.data=original.data.copy();col.objects.link(obj)
        obj.name=name+'__'+part;obj.asset_clear();obj.parent=newrig
        if part_offsets and part in part_offsets:obj.location=part_offsets[part]
        for mod in obj.modifiers:
            if mod.type=='ARMATURE':mod.object=newrig
        if skin:
            for idx,mat in enumerate(obj.data.materials):
                if mat.name.startswith('Skin_'):obj.data.materials[idx]=MATS[skin]
        objects.append(obj)
    return newrig,objects

def export_glb(path,objects,rig=None,animated=False):
    for obj in bpy.context.selected_objects:obj.select_set(False)
    for obj in objects:obj.select_set(True)
    if rig:
        rig.select_set(True);bpy.context.view_layer.objects.active=rig
        rig.animation_data.action=None;reset_pose(rig)
        for track in rig.animation_data.nla_tracks:track.mute=not animated
    scene.frame_set(0)
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=animated,export_animation_mode='NLA_TRACKS',export_nla_strips=True,
        export_force_sampling=True,export_frame_range=False,export_frame_step=1,
        export_skins=bool(rig),export_def_bones=False,export_yup=True,
        export_extras=True,export_cameras=False,export_lights=False,
        export_materials='EXPORT',export_apply=False,export_texcoords=True,
        export_normals=True,export_image_format='AUTO',export_keep_originals=False,
        export_anim_slide_to_zero=True,export_optimize_animation_size=True)
    if rig:
        for track in rig.animation_data.nla_tracks:track.mute=True
        reset_pose(rig)

if '--geometry-check' in sys.argv:
    print('GEOMETRY_CHECK',len(ASSETS),'PARTS',max(a['triangles'] for a in ASSETS),'MAX_TRIANGLES',len(CHARACTERS),'CHARACTERS',flush=True)
    raise SystemExit(0)
EXPORTS=[]
for name,info in CHARACTERS.items():
    # Character GLBs carry material-matched examples; modules remain re-combinable.
    temp=collection('EXPORT_'+name)
    rig,objs=clone_actor(name,human,info['parts'],temp,skin=info['skin'],part_offsets=info.get('offsets'))
    # Clone NLA strips by referencing the common skeleton's actions.
    for tr in human.animation_data.nla_tracks:
        track=rig.animation_data.nla_tracks.new();track.name=tr.name
        strip=track.strips.new(tr.name,0,tr.strips[0].action);strip.name=tr.name;track.mute=True
    path=ROOT/'Export/Characters'/f'{name}.glb';export_glb(path,objs,rig,True)
    EXPORTS.append({'file':str(path.relative_to(ROOT)),'type':'character','mesh_parts':info['parts'],'clips':len(CLIP_SPECS)})
    for obj in objs+[rig]:bpy.data.objects.remove(obj,do_unlink=True)
    bpy.data.collections.remove(temp)
for kind,info in CREATURES.items():
    parts=[p for p in info['parts'] if not ('Head_' in p and p!=kind+'_Head_'+('Antler' if kind=='RiftGrazer' else 'Crown' if kind=='VeilWalker' else 'Scanner'))]
    info['default_parts']=parts
    path=ROOT/'Export/Creatures'/f'{kind}.glb';export_glb(path,[PROTOTYPES[n] for n in parts],info['rig'],True)
    EXPORTS.append({'file':str(path.relative_to(ROOT)),'type':'creature','mesh_parts':parts,'clips':len(CLIP_SPECS)})
    alternate=[p for p in info['parts'] if 'Head_' in p and p not in parts][0]
    altparts=[p for p in parts if 'Head_' not in p]+[alternate]
    path=ROOT/'Export/Creatures'/f'{kind}_Variant.glb';export_glb(path,[PROTOTYPES[n] for n in altparts],info['rig'],True)
    EXPORTS.append({'file':str(path.relative_to(ROOT)),'type':'creature_variant','mesh_parts':altparts,'clips':len(CLIP_SPECS)})
for kind,info in VEHICLES.items():
    for suffix,key in [('', 'defaults'),('_Variant','alternate')]:
        path=ROOT/'Export/Vehicles'/f'{kind}{suffix}.glb'
        export_glb(path,[PROTOTYPES[n] for n in info[key]],info['rig'],True)
        EXPORTS.append({'file':str(path.relative_to(ROOT)),'type':'vehicle','vehicle_type':kind,'mesh_parts':info[key],'clips':len(CLIPS[info['rig'].name])})
for name,obj in PROTOTYPES.items():
    if obj.parent and obj.parent.type=='ARMATURE':rig=obj.parent
    else:rig=None
    category='Vehicles/Parts' if obj['family'].startswith('vehicle.') else 'Environment' if obj['family']=='environment' else 'Modules'
    (ROOT/'Export'/category).mkdir(parents=True,exist_ok=True)
    path=ROOT/'Export'/category/f'{name}.glb';export_glb(path,[obj],rig,False)
    EXPORTS.append({'file':str(path.relative_to(ROOT)),'type':'module','mesh_parts':[name],'clips':0})
for name,p in BUILDINGS.items():
    path=ROOT/'Export/Buildings'/f'{name}.glb';export_glb(path,p['objects']+[p['root']])
    EXPORTS.append({'file':str(path.relative_to(ROOT)),'type':'building','mesh_parts':[r['part'] for r in p['recipe']],'clips':0})
path=ROOT/'Export/Modules/PressureDoor_Animated.glb';export_glb(path,[door_obj],door,True)
EXPORTS.append({'file':str(path.relative_to(ROOT)),'type':'skeletal_door','mesh_parts':['Hab_Pressure_Door'],'clips':2})
print('EXPORTS_COMPLETE',len(EXPORTS),flush=True)

def studio(name,cam_position,target,ortho,res=(1500,1000)):
    s=bpy.data.scenes.new(name);s.unit_settings.system='METRIC';s.render.fps=30
    s.render.engine='CYCLES';s.cycles.samples=20;s.cycles.use_denoising=True
    # CPU rendering is deterministic and independent of the user's GPU configuration.
    s.cycles.device='CPU';s.render.resolution_x=res[0];s.render.resolution_y=res[1];s.render.resolution_percentage=100
    s.render.image_settings.file_format='PNG';s.render.film_transparent=False
    s.view_settings.view_transform='AgX'
    world=bpy.data.worlds.new(name+'_World');world.use_nodes=True
    world.node_tree.nodes['Background'].inputs[0].default_value=(.13,.18,.24,1)
    world.node_tree.nodes['Background'].inputs[1].default_value=.35;s.world=world
    c=bpy.data.collections.new(name+'_Display');s.collection.children.link(c)
    camera_data=bpy.data.cameras.new(name+'_Camera');cam=bpy.data.objects.new(name+'_Camera',camera_data);c.objects.link(cam)
    cam.location=cam_position;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler()
    camera_data.type='ORTHO';camera_data.ortho_scale=ortho;s.camera=cam
    for suffix,pos,power,color,size in [('Key',(2,-5,8),1500,(1,.79,.57),7),('Fill',(-5,-1,4),1100,(.43,.72,1),6),('Rim',(3,6,6),1800,(.42,1,.88),5)]:
        light=bpy.data.lights.new(name+'_'+suffix,'AREA');light.energy=power;light.color=color;light.shape='DISK';light.size=size
        obj=bpy.data.objects.new(name+'_'+suffix,light);c.objects.link(obj);obj.location=pos
        obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    return s,c

def display_static(source,col,xyz=(0,0,0),yaw=0,scale=(1,1,1)):
    obj=PROTOTYPES[source].copy();obj.data=PROTOTYPES[source].data;obj.parent=None;obj.modifiers.clear();obj.vertex_groups.clear();obj.asset_clear()
    col.objects.link(obj);obj.location=xyz;obj.rotation_euler=(0,0,yaw);obj.scale=scale
    return obj

def text_label(col,text,location,size=.18,color='Ceramic'):
    data=bpy.data.curves.new('Label_'+text,'FONT');data.body=text;data.size=size;data.align_x='CENTER';data.extrude=0
    obj=bpy.data.objects.new('Label_'+text,data);col.objects.link(obj);obj.location=location;obj.rotation_euler[0]=math.pi/2
    data.materials.append(MATS[color]);return obj

def ground(col,size=30,z=-.04):
    mesh=bpy.data.meshes.new('StudioGround');mesh.from_pydata([(-size,-size,z),(size,-size,z),(size,size,z),(-size,size,z)],[],[(0,1,2,3)])
    obj=bpy.data.objects.new('StudioGround',mesh);col.objects.link(obj);mesh.materials.append(MATS['Rubber']);return obj

GALLERY,gcol=studio('01_Characters',(5,-16,6),(0,0,1),16.2,(2200,1000));ground(gcol)
GALLERY_ACTORS={}
for i,(name,info) in enumerate(CHARACTERS.items()):
    x=(i-(len(CHARACTERS)-1)/2)*1.45
    rg,objs=clone_actor(name,human,info['parts'],gcol,(x,0,0),info['skin'],info.get('offsets'));GALLERY_ACTORS[name]=(rg,objs)
    # Slight elbow bends improve the reading of the modular silhouette.
    set_rotation(rg,'forearm.L',(1,0,0),-.10);set_rotation(rg,'forearm.R',(1,0,0),-.10)
    text_label(gcol,name.replace('_',' '),(x,-.45,.025),.09)
text_label(gcol,'ERMA / HALO   -   HUMAN MODULES',(0,.35,2.50),.23)
text_label(gcol,'1.80 m  /  seven-head proportions  /  shared 23-bone skeleton',(0,.35,2.18),.115,'Glow_Cyan')

MONSTERS,mcol=studio('02_Creatures',(4.4,-10,4.7),(0,0,.85),8.0,(1500,950));ground(mcol)
MONSTER_ACTORS={}
for i,(kind,info) in enumerate(CREATURES.items()):
    x=(i-1)*2.5
    rg,objs=clone_actor(kind,info['rig'],info['default_parts'],mcol,(x,0,0));MONSTER_ACTORS[kind]=(rg,objs)
    text_label(mcol,kind,(x,-.92,.02),.16)
    text_label(mcol,f'{info["legs"]} LEGS / INTERCHANGEABLE PARTS',(x,-.92,-.18),.085,'Glow_Cyan')
text_label(mcol,'ERMA / N-0   -   FORMS AT THE VEIL',(0,.7,2.36),.22)

CITY,ccol=studio('03_Halo_Settlement',(29,-38,34),(0,4,4),48,(1700,1100))
CITY.cycles.samples=24;ground(ccol,50,-.18)
CITY.world.node_tree.nodes['Background'].inputs[1].default_value=.55
for obj in ccol.objects:
    if obj.type=='LIGHT':obj.data.energy*=10;obj.data.size*=2.5
for ix in range(-8,9):
    for iy in range(-5,8):
        part='Road_Straight_2m' if ix in [-1,0] else 'Ground_Grass_2m' if iy>=3 else 'Ground_Soil_2m'
        display_static(part,ccol,(ix*2,iy*2,0))
layout={'Habitat_7p2m':(-9,-3,0),'TerracedPods_10p8m':(8,7,0),'Workshop_5p4m':(8,-5,0),'RibDome_6p5m':(-9,7,0),'Observatory_11p5m':(0,18,0)}
for name,offset in layout.items():
    p=BUILDINGS[name]
    for obj in p['objects']:
        copy=obj.copy();copy.data=obj.data;copy.parent=None;ccol.objects.link(copy);copy.location=Vector(obj.location)+Vector(offset)
for y in [-8,-1,6,13]:display_static('Street_Lamp_3p2m',ccol,(-3.6,y,0))
for x,y in [(-14,4),(-14,9),(-14,-5),(14,2),(14,12)]:display_static('Nature_Alien_Tree_03',ccol,(x,y,0))
for x,y in [(-5,4),(-4,9),(3,2),(4,10)]:display_static('Nature_Glow_Reeds_01',ccol,(x,y,0))
for name,offset in [('Ria_Engineer',(-4,-3,0)),('Kain_Guard',(1,-3,0)),('Mara_Medic',(-4,3,0))]:
    info=CHARACTERS[name];clone_actor(name,human,info['parts'],ccol,offset,info['skin'],info.get('offsets'))

INTERIOR,icol=studio('04_Interior_Kit',(8,-12,10),(0,0,1),12,(1500,1000));ground(icol,15)
for ix in [-2,0,2]:
    for iy in [-2,0,2]:display_static('Hab_Floor_2m',icol,(ix,iy,0))
for x in [-2,0,2]:display_static('Hab_Wall_Window_2m',icol,(x,3,0))
for y in [-2,0,2]:display_static('Hab_Wall_Solid_2m',icol,(-3,y,0),math.pi/2)
for part,loc,yaw in [('Interior_Bed',(-1.6,1.4,0),0),('Interior_Storage',(1.8,2.5,0),0),('Interior_Workbench',(-2.4,-1.4,0),-math.pi/2),('Interior_Table',(1,-.5,0),0),('Interior_Chair',(1,.2,0),math.pi),('Interior_Terminal',(2.4,1.3,0),-.3),('Interior_Crate',(-.4,2.3,0),0),('Interior_Planter',(2.5,-2.4,0),0),('Interior_Med_Kit',(1,-.5,.81),0)]:display_static(part,icol,loc,yaw)
display_static('Interior_Wall_Light',icol,(0,2.85,2.5))
info=CHARACTERS['Ria_Engineer'];clone_actor('Interior_Ria',human,info['parts'],icol,(-.6,-1.8,0),info['skin'],info.get('offsets'))

NATURE,ncol=studio('05_Environment',(7,-11,10),(0,1,.7),12,(1500,1000));ground(ncol,15)
for i,name in enumerate(['Ground_Soil_2m','Ground_Sand_2m','Ground_Grass_2m','Road_Straight_2m','Ground_Mine_2m']):
    display_static(name,ncol,((i-2)*2,0,0))
    text_label(ncol,name.replace('Ground_','').replace('_2m',''),((i-2)*2,-1.2,-.03),.15)
for i in range(5):display_static(f'Nature_Rock_{i+1:02}',ncol,((i-2)*1.7,3,0))
for i in range(3):display_static(f'Nature_Alien_Tree_{i+1:02}',ncol,((i-1)*2.6,5,0))
for i,part in enumerate(['Nature_Glow_Reeds_01','Nature_Glow_Reeds_02','Nature_Glow_Reeds_03','Nature_Fungal_Cluster','Nature_Crystal_Cluster']):display_static(part,ncol,((i-2)*1.7,1.5,0))

ANIM,acol=studio('06_Animation_Review',(6,-13,6),(0,0,1),9.4,(1400,850));ground(acol)
ANIM_ACTORS=[]
info=CHARACTERS['Ria_Engineer'];rg,objs=clone_actor('Review_Human',human,info['parts'],acol,(-3,0,0),info['skin'],info.get('offsets'));ANIM_ACTORS.append((human,rg,objs))
for i,(kind,info) in enumerate(CREATURES.items()):
    rg,objs=clone_actor('Review_'+kind,info['rig'],info['default_parts'],acol,(-.9+i*2.2,0,0));ANIM_ACTORS.append((info['rig'],rg,objs))
for original,rig,objs in ANIM_ACTORS:
    action=bpy.data.actions[original.name+'__Walk'];rig.animation_data.action=action
    rig.animation_data.action_slot=action.slots[0];rig.animation_data.use_nla=False;rig.update_tag()
ANIM.frame_start=0;ANIM.frame_end=30
exec(compile((ROOT/'Tools/design_review_scenes.py').read_text(encoding='utf-8'),'design_review_scenes.py','exec'),globals())

def render(s,name):
    bpy.context.window.scene=s;s.render.filepath=str(ROOT/'Preview'/name)
    saved_device=s.cycles.device;s.cycles.device=RUNTIME_RENDER_DEVICE
    try:bpy.ops.render.render(write_still=True,scene=s.name)
    finally:s.cycles.device=saved_device

# Source validation at the end, after no more geometry mutations.
source_report={'mesh_parts':len(ASSETS),'max_triangles':max(x['triangles'] for x in ASSETS),'all_parts_under_500':all(x['triangles']<=500 for x in ASSETS),'rigs':{},'issues':[]}
for name,rig in RIGS.items():
    source_report['rigs'][name]={'bone_count':len(rig.data.bones),'clips':len(CLIPS[name])}
for obj in PROTOTYPES.values():
    if obj.parent and obj.parent.type=='ARMATURE':
        for v in obj.data.vertices:
            if abs(sum(g.weight for g in v.groups)-1)>1e-5:source_report['issues'].append(f'{obj.name}: non-unit weight {v.index}')
    if any(not math.isfinite(x) for v in obj.data.vertices for x in v.co):source_report['issues'].append(obj.name+': non-finite coordinate')
    obj.data.calc_loop_triangles()
    for tri in obj.data.loop_triangles:
        a,b,c=[obj.data.vertices[i].co for i in tri.vertices]
        if (b-a).cross(c-a).length<1e-10:source_report['issues'].append(obj.name+': degenerate triangle')

SKELETONS={}
for name,rig in RIGS.items():
    isvehicle=name.startswith('VEHICLE_')
    SKELETONS[name]={'units':'meters','authoring_up':'+Z','authoring_forward':'+X' if isvehicle else '-Y',
      'gltf_up':'+Y','gltf_forward':'+X' if isvehicle else '+Z','bones':[{'name':b.name,'parent':b.parent.name if b.parent else None,'head_m':list(b.head_local),'tail_m':list(b.tail_local),'inverse_bind_matrix_column_major':[x for col in b.matrix_local.inverted().transposed() for x in col]} for b in rig.data.bones]}
    SKELETONS[name]['signature']=hashlib.sha256(json.dumps(SKELETONS[name]['bones'],sort_keys=True).encode()).hexdigest()

RECIPES={name:{k:v for k,v in info.items() if k!='rig'} for name,info in CHARACTERS.items()}
RECIPES['creatures']={kind:{'parts':info['default_parts'],'legs':info['legs'],'alternate_heads':[p for p in info['parts'] if 'Head_' in p]} for kind,info in CREATURES.items()}
RECIPES['buildings']={name:{'parts':p['recipe']} for name,p in BUILDINGS.items()}
RECIPES['vehicles']={kind:{'parts':info['defaults'],'alternate_parts':info['alternate'],'slots':info['slots']} for kind,info in VEHICLES.items()}
for name,data in [('asset_catalog.json',{'schema':'erma_resources_v1','units':'meters','triangles_per_part_limit':500,'assets':ASSETS,'exports':EXPORTS,'textures':TEXTURES}),('skeletons.json',SKELETONS),('assembly_recipes.json',RECIPES),('animation_clips.json',CLIPS)]:
    (ROOT/'Documentation'/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
(ROOT/'Validation/source_report.json').write_text(json.dumps(source_report,indent=2),encoding='utf-8')
assert not source_report['issues'],source_report['issues'][:10]
for rig in RIGS.values():
    if rig.animation_data:rig.animation_data.action=None
    reset_pose(rig)
for s in [scene,GALLERY,MONSTERS,CITY,INTERIOR,NATURE,ANIM]+[s for s,n in DESIGN_RENDERS]:s.frame_set(0)
bpy.context.window.scene=GALLERY
# The saved scene opens in material preview with an intelligible overview.
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.shading.type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source/Erma_Resource_Library.blend'),compress=True)
print('SOURCE_SAVED',flush=True)
if '--no-render' in sys.argv:
    print('RESOURCE_BUILD_COMPLETE_NO_RENDER',flush=True)
    raise SystemExit(0)
for s,name in [(GALLERY,'01_characters.png'),(MONSTERS,'02_creatures.png'),(CITY,'03_halo_settlement.png'),(INTERIOR,'04_interior.png'),(NATURE,'05_environment.png')]:render(s,name)
for s,name in DESIGN_RENDERS:render(s,name)
# Eight pose frames per clip; these PNGs also serve as animation visual QA.
for clip in ['Walk','Run','Turn_Left_90','Attack_Melee','Attack_Fire','Jump_Full']:
    for original,rig,objs in ANIM_ACTORS:
        action=bpy.data.actions[original.name+'__'+clip];rig.animation_data.action=action
        rig.animation_data.action_slot=action.slots[0];rig.animation_data.use_nla=False;rig.update_tag()
    end=next(c['end'] for c in CLIPS['HUMAN_7H'] if c['clip']==clip)
    for i in range(8):
        ANIM.frame_set(round(end*i/7));ANIM.cycles.samples=10;ANIM.render.resolution_percentage=60
        render(ANIM,f'animation_{clip}_{i:02}.png')
print('RESOURCE_BUILD_COMPLETE',flush=True)
