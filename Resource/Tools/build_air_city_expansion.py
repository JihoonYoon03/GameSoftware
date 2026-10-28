"""Blender 4.5: extend the saved library; never rebuild or alter game/runtime assets.

blender --background --python Resource/Tools/build_air_city_expansion.py
Reuses the original pack's geometry implementation and materials. Idempotent.
"""
import ast
import bpy
import bmesh
import math
import json
import sys
from pathlib import Path
from mathutils import Vector, Quaternion, Matrix

ROOT = Path(__file__).resolve().parents[1]
TAG = 'air_city_v1'
DESIGN_REVISION = 'sf_detail_first_v3'
SOURCE = ROOT / 'Source/Erma_Resource_Library.blend'
old_catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
previous_exports={e['file'] for e in old_catalog['exports'] if e.get('expansion')==TAG}
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
entry_scene_name=bpy.context.scene.name
comparison_ids=['Aircraft_Warden_Survey_LiftField_Front_L','Aircraft_Warden_Survey_Engine_Boost_L',
                'Utility_Transformer_Cabinet']
before_detail_meshes={n:(bpy.data.objects.get('Comparison_'+n+'_0') or bpy.data.objects[n]).data.copy()
                      for n in comparison_ids if bpy.data.objects.get(n)}
bpy.context.preferences.filepaths.save_version = 0
# Remove only this expansion's previous authoring and display data.
for s in list(bpy.data.scenes):
    if s.get('expansion') == TAG:
        bpy.data.scenes.remove(s)
for o in list(bpy.data.objects):
    if o.get('expansion') == TAG:
        bpy.data.objects.remove(o, do_unlink=True)
for c in list(bpy.data.collections):
    if c.get('expansion') == TAG:
        bpy.data.collections.remove(c)
for a in list(bpy.data.actions):
    if a.get('expansion') == TAG:
        bpy.data.actions.remove(a)
scene = bpy.data.scenes['00_Library']
bpy.context.window.scene = scene
scene.frame_set(0)
MATS = {m.name: m for m in bpy.data.materials}
for name,color,strength in [('Signal_Stop',(.95,.025,.055),2.5),('Field_Core',(.025,.72,1.0),2.7)]:
    m=MATS.get(name) or bpy.data.materials.new(name)
    m.diffuse_color=(*color,1)
    m.use_nodes=True
    shader=m.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value=(*color,1)
    shader.inputs['Roughness'].default_value=.25
    shader.inputs['Metallic'].default_value=.25
    shader.inputs['Emission Color'].default_value=(*color,1)
    shader.inputs['Emission Strength'].default_value=strength
    MATS[name]=m
ASSETS, PROTOTYPES, EXPORTS, CRAFT, PROPS, CLIPS = [], {}, [], {}, {}, {}
# Import the existing mesh/UV/skin/500-triangle implementation without executing
# the builder's scene reset, texture regeneration or legacy exports.
tree = ast.parse((ROOT / 'Tools/build_resource_pack.py').read_text(encoding='utf-8'))
geo_node = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'Geo')
exec(compile(ast.Module(body=[geo_node], type_ignores=[]), '<existing Geo>', 'exec'))
LegacyGeo=Geo
sys.path.insert(0,str(ROOT/'Tools'))
from detail_mesh_pipeline import DetailGeometry, DetailPipeline


class Geo(DetailGeometry,LegacyGeo):
    pass


def col(name, parent=None):
    c = bpy.data.collections.new(name)
    c['expansion'] = TAG
    (parent or scene.collection).children.link(c)
    return c


LIB = col('10_Air_City_Expansion', bpy.data.collections.get('RESOURCE_LIBRARY'))
SOURCE_SCENE=bpy.data.scenes.new('17_Detail_Sources')
SOURCE_SCENE['expansion']=TAG
SOURCE_SCENE.unit_settings.system='METRIC'
SOURCE_COLLECTION=bpy.data.collections.new('Detail_Sources_Air_City')
SOURCE_COLLECTION['expansion']=TAG
SOURCE_SCENE.collection.children.link(SOURCE_COLLECTION)
PIPELINE=DetailPipeline(SOURCE_COLLECTION,MATS,TAG,allow_draft_over_budget=True)


def make(g, name, c, slot, family, rig=None):
    if family.startswith('scenery.'):
        add_prop_detail(g,name)
    obj,asset=PIPELINE.build(g,name,c,rig,slot,family)
    ASSETS.append(asset)
    PROTOTYPES[name]=obj
    obj['expansion'] = TAG
    obj['authoring_up'] = '+Z'
    obj['forward'] = '+X'
    obj['design_revision'] = DESIGN_REVISION
    ASSETS[-1]['expansion'] = TAG
    ASSETS[-1]['design_revision'] = DESIGN_REVISION
    return obj


def loft(g, sections, mat, bone='hull'):
    # X, Y half-width, bottom Z, top Z; axis conversion matches original vehicles.
    return g.rings([(x, 0, (lo + hi) / 2) for x, w, lo, hi in sections],
                   [((hi - lo) / 2, w) for x, w, lo, hi in sections],
                   mat, bone, 8, True, (1, 0, 0))


def wing(g, points, thick, mat, bone):
    n = len(points)
    v = [(x, y, z - thick / 2) for x, y, z in points]
    v += [(x, y, z + thick / 2) for x, y, z in points]
    return g.add(v, [tuple(reversed(range(n))), tuple(range(n, 2*n))] +
                 [(i, (i+1) % n, (i+1) % n+n, i+n) for i in range(n)], mat, bone)


def chamfer(g,c,size,mat,bone=None):
    """Chamfered XZ silhouette, extruded along Y; no modern rectangular casing."""
    x,y,z=c
    w,d,h=[v/2 for v in size]
    cut=min(w,h)*.35
    outline=[(-w+cut,-h),(w-cut,-h),(w,-h+cut),(w,h-cut),
             (w-cut,h),(-w+cut,h),(-w,h-cut),(-w,-h+cut)]
    v=[(x+px,y+sy*d,z+pz) for sy in [-1,1] for px,pz in outline]
    return g.add(v,[tuple(reversed(range(8))),tuple(range(8,16))]+
                 [(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)],mat,bone)


def arc(g,c,r,inner,depth,start,end,mat,bone=None,steps=3):
    """Solid open arc: gaps between coil sectors remain visible at game scale."""
    steps=max(steps,6) if r>.30 else steps
    v=[]
    for i in range(steps+1):
        a=start+(end-start)*i/steps
        v.extend((c[0]+rr*math.cos(a),c[1]+rr*math.sin(a),c[2]+zz)
                 for rr,zz in [(r,-depth/2),(r,depth/2),(inner,depth/2),(inner,-depth/2)])
    faces=[(3,2,1,0),(steps*4,steps*4+1,steps*4+2,steps*4+3)]
    for i in range(steps):
        for k in range(4):
            faces.append((i*4+k,i*4+(k+1)%4,(i+1)*4+(k+1)%4,(i+1)*4+k))
    return g.add(v,faces,mat,bone)


def field(g,c,radius,depth,bone,paint):
    # Three disconnected induction shoes, interleaved luminous arcs and a
    # levitated central crystal: no fan blades, axle, grille or support crossbar.
    for i in range(3):
        a=i*math.tau/3
        arc(g,c,radius,radius*.77,depth,a+.10,a+1.75,paint,bone)
        arc(g,(c[0],c[1],c[2]+depth*.45),radius*.72,radius*.66,.045,
            a+.28,a+1.95,'Field_Core',bone,2)
        # Coil service fastener and a recessed coolant/inspection slot on every
        # induction shoe. These are physical details, not extra tessellation.
        angle=a+.85
        p=(c[0]+radius*.88*math.cos(angle),c[1]+radius*.88*math.sin(angle),c[2]+depth*.51)
        g.rod(p,(p[0],p[1],p[2]+.018),min(.05,radius*.065),min(.05,radius*.065),'Alloy_Light',bone,6)
        angle=a+1.25
        p=(c[0]+radius*.89*math.cos(angle),c[1]+radius*.89*math.sin(angle),c[2]+depth*.52)
        g.box(p,(radius*.18,radius*.055,.015),'Rubber',bone,Quaternion((0,0,1),angle+math.pi/2))
    g.rings([(c[0],c[1],c[2]+.04),(c[0],c[1],c[2]+.23),(c[0],c[1],c[2]+.48)],
            [radius*.22,radius*.34,radius*.08],'Field_Core',bone,sides=6)
    return g


def vents(g,c,length,width,count,mat='Rubber',bone=None):
    for i in range(count):
        g.box((c[0]+(i-(count-1)/2)*length/count,c[1],c[2]),
              (length/count*.45,width,.018),mat,bone)
    return g


def add_prop_detail(g,name):
    """Purposeful manufacturing detail; ground/road connection surfaces stay exact."""
    if name=='City_TrafficSignal_HoverHousing':
        g.box((0,-.215,5.64),(1.8,.018,.035),'Glow_Cyan')
        for x in [-.93,.93]:
            g.rod((x,-.22,5.45),(x,-.25,5.45),.038,.038,'Alloy_Light',sides=6)
    elif name=='City_ChargePost_Pedestal':
        g.box((0,-.283,.45),(.40,.018,.38),'Glass_Opaque')
        for x in [-.12,0,.12]:
            g.box((x,-.30,.45),(.045,.015,.09),'Alloy_Light')
    elif name=='Utility_Transformer_Cabinet':
        g.box((0,-.565,.80),(1.2,.025,.55),'Steel')
        for x in [-.44,-.22,0,.22,.44]:
            g.box((x,-.585,.8),(.09,.025,.34),'Rubber')
    elif name=='Utility_CargoContainer_6m_Shell':
        for s in [-1,1]:
            for x in [-1.8,0,1.8]:
                g.box((x,s*1.21,1.25),(1.45,.035,1.55),'Steel')
            for x in [-2.3,2.3]:
                g.box((x,s*1.24,1.6),(.055,.02,.35),'Glow_Cyan')
    elif name in ['Utility_CargoContainer_6m_Door_L','Utility_CargoContainer_6m_Door_R']:
        s=-1 if name.endswith('_L') else 1
        g.box((3.065,s*.60,1.25),(.018,.82,1.7),'Vehicle_Utility')
        for z in [.42,2.08]:
            g.box((3.08,s*.60,z),(.025,.70,.04),'Alloy_Light')
    elif name=='Utility_SolarArray_Panel':
        for s in [-1,1]:
            for x in [-.92,.92]:
                for offset in [-.38,0,.38]:
                    g.box((x+offset,s*.62,1.70),(.012,.85,.012),'Alloy_Light',rot=Quaternion((1,0,0),s*.23))
    elif name=='Nature_SailTree_Canopy':
        for i in range(6):
            a=i*math.tau/6
            g.rod((.2,0,2.91),(.2+1.7*math.cos(a),math.sin(a),3.2),.025,.012,'Alien_Petal',sides=6)
    elif name=='City_TransitShelter_Frame':
        for x in [-1.7,1.7]:
            g.box((x,-.073,1.75),(.045,.025,1.5),'Glow_Cyan')
    elif name=='Frontier_SurveyTripod_Scanner':
        g.box((0,-.19,1.40),(.30,.025,.22),'Glass_Opaque')
        for x in [-.15,.15]:
            g.rod((x,-.21,1.40),(x,-.24,1.40),.035,.035,'Glow_Cyan',sides=6)
    return g


def rig_create(name, bones, c):
    bpy.ops.object.select_all(action='DESELECT')
    data = bpy.data.armatures.new(name+'_Skeleton')
    rig = bpy.data.objects.new(name, data)
    c.objects.link(rig)
    rig['expansion'] = TAG
    rig['forward'] = '+X'
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for name_, head, parent in bones:
        b = data.edit_bones.new(name_)
        b.head = head
        b.tail = Vector(head) + Vector((.18,0,0))
        if parent:
            b.parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    rig.select_set(False)
    rig.animation_data_create()
    return rig


TYPES = {
    'Kite_Courier': (6.4, 7.8, 1.65, 'Vehicle_Teal', 'swept-wing one-seat frontier courier'),
    'Manta_AirTaxi': (7.2, 8.6, 2.65, 'Vehicle_Ivory', 'four-field enclosed settlement air taxi'),
    'Mule_Skycrane': (9.2, 9.6, 3.6, 'Vehicle_Utility', 'twin-boom suspended freight VTOL'),
    'Warden_Survey': (7.8, 9.0, 2.25, 'Vehicle_Patrol', 'gimbaled-field long-range survey aircraft'),
}
for kind, (L,W,H,paint,purpose) in TYPES.items():
    c = col('AircraftParts_'+kind, LIB)
    z = 1.05 if kind == 'Mule_Skycrane' else .48
    bz = z + H*.26
    bones = [('root',(0,0,0),None),('hull',(0,0,bz),'root'),
             ('canopy',(-L*.02,0,z+H*.5),'hull'),('cargo',(-L*.34,0,z+.3),'hull'),
             ('sensor',(L*.34,0,z+.06),'hull'),('driver_socket',(L*.14,0,z+H*.51),'hull'),
             ('passenger_socket',(-L*.10,0,z+H*.51),'hull')]
    for suf,s in [('L',1),('R',-1)]:
        bones += [('wing.'+suf,(-L*.08,s*W*.12,bz),'hull'),
                  ('engine.'+suf,(-L*.29,s*W*.18,bz),'hull'),
                  ('lift_front.'+suf,(L*.17,s*W*.34,bz),'hull'),
                  ('lift_rear.'+suf,(-L*.26,s*W*.34,bz),'hull'),
                  ('gear.'+suf,(0,s*W*.12,z+.14),'hull')]
    rig = rig_create('AIRCRAFT_'+kind,bones,c)
    info = dict(rig=rig,slots={},default_parts=[],alternate_parts=[],parts=[],
                parameters=dict(length=L,span=W,body_height=H,paint=paint,purpose=purpose))
    CRAFT[kind] = info

    def part(slot, label, g, default=True):
        obj = make(g,'Aircraft_'+kind+'_'+label,c,slot,'aircraft.'+kind,rig)
        obj['attachment_slot'] = slot
        info['slots'].setdefault(slot,[]).append(obj.name)
        info['parts'].append(obj.name)
        if default:
            info['default_parts'].append(obj.name)
        return obj

    narrow = kind in ['Kite_Courier','Warden_Survey']
    hw = W*(.075 if narrow else .12)
    g = loft(Geo(),[(-L*.47,hw*.38,z+.18,z+H*.43),(-L*.28,hw,z,z+H*.66),
                    (L*.19,hw,z+.02,z+H*.62),(L*.45,hw*.25,z+.23,z+H*.34)],paint)
    g.box((0,0,z+.12),(L*.62,hw*1.3,.12),'Steel','hull')
    for s in [-1,1]:
        g.box((L*.29,s*hw*.60,z+H*.46),(.30,.065,.08),'Glow_Cyan','hull')
        g.box((-.05*L,s*hw*.985,z+H*.40),(L*.42,.035,.07),'Field_Core','hull')
        g.box((-.03*L,s*hw*.99,z+H*.28),(L*.30,.035,H*.14),'Alloy_Light','hull')
    vents(g,(L*.35,0,z+H*.45),L*.09,hw*.70,4,'Rubber','hull')
    part('hull','PressureHull',g)
    for label,armored in [('Canopy_Glass',False),('Canopy_Reinforced',True)]:
        g = loft(Geo(),[(-L*.07,hw*.89,z+H*.5,z+H*.73),
                       (L*.03,hw*.90,z+H*.54,z+H),
                       (L*.22,hw*.68,z+H*.50,z+H*.87),
                       (L*.30,hw*.31,z+H*.44,z+H*.54)],paint if armored else 'Glass_Opaque','canopy')
        if armored:
            for s in [-1,1]:
                g.box((L*.10,s*hw*.89,z+H*.77),(L*.19,.025,H*.17),'Glass_Opaque','canopy')
        else:
            g.rod((-L*.04,0,z+H*.90),(L*.23,0,z+H*.86),.035,.035,'Alloy_Light','canopy',6)
        for s in [-1,1]:
            g.rod((-L*.06,s*hw*.78,z+H*.65),(L*.22,s*hw*.64,z+H*.66),
                  .026,.026,'Steel','canopy',8)
        part('canopy',label,g,not armored)
    g = Geo()
    for x in ([L*.12] if narrow else [L*.12,-L*.06]):
        for y in ([0] if narrow else [-hw*.45,hw*.45]):
            g.box((x,y,z+H*.49),(.42,.38,.12),'Rubber','hull')
            g.box((x-.20,y,z+H*.61),(.10,.38,.36),'Rubber','hull')
            g.box((x,y,z+H*.557),(.28,.018,.012),'Cloth_Teal','hull')
    g=chamfer(g,(L*.22,0,z+H*.63),(.22,hw*.85,.15),'Steel','hull')
    for s in [-1,1]:
        g.box((L*.225,s*hw*.22,z+H*.71),(.10,.12,.018),'Glow_Cyan','hull')
    part('seats','CabinSeats',g)
    for suf,s in [('L',1),('R',-1)]:
        bone = 'wing.'+suf
        if kind == 'Kite_Courier':
            points = [(L*.12,s*hw,bz+.24),(-L*.27,s*W*.50,bz+.08),
                      (-L*.42,s*W*.48,bz),(-L*.24,s*hw,bz)]
        elif kind == 'Mule_Skycrane':
            points = [(L*.30,s*hw,bz+.1),(L*.28,s*W*.36,bz+.1),
                      (-L*.38,s*W*.36,bz+.1),(-L*.35,s*hw,bz+.1)]
        else:
            points = [(L*.24,s*hw,bz+.18),(L*.04,s*W*.43,bz+.12),
                      (-L*.26,s*W*.46,bz+.04),(-L*.30,s*hw,bz+.04)]
        g = wing(Geo(),points,.16,paint,bone)
        # Faceted separated armor vane and luminous field bus replace exposed
        # aircraft spars; the profile stays easy to read in the quarter view.
        wing(g,[(L*.02,s*W*.17,bz+.32),(-L*.12,s*W*.32,bz+.36),
                (-L*.29,s*W*.34,bz+.27),(-L*.19,s*W*.16,bz+.27)],.075,'Alloy_Light',bone)
        g.rod((-L*.09,s*hw,bz+.25),(-L*.14,s*W*.30,bz+.30),.032,.032,'Field_Core',bone,6)
        for u in [.20,.29]:
            g.rod((-L*.20,s*W*u,bz+.33),(-L*.20,s*W*u,bz+.36),.045,.045,'Steel',bone,6)
        part('wing_'+suf,'Wing_'+suf,g)
        # Swept fields, quad-field taxi, industrial lifting arms and survey array.
        liftareas = [('Front',.17),('Rear',-.26)] if kind in ['Manta_AirTaxi','Mule_Skycrane'] else [('Front',-.08)]
        for area,x in liftareas:
            r = W*(.118 if len(liftareas)==2 else .13)
            cy = s*W*(.35 if len(liftareas)==2 else .37)
            bone = 'lift_'+area.lower()+'.'+suf
            g = field(Geo(),(x*L,cy,bz+.36),r,.26,bone,paint)
            part('lift_'+area+'_'+suf,'LiftField_'+area+'_'+suf,g)
        for boost in [False,True]:
            bone = 'engine.'+suf
            x,y = -L*.33,s*hw*1.03
            r = .26 if not boost else .36
            g = chamfer(Geo(),(x,y,bz),(1.45,r*2.1,r*2.2),paint,bone)
            for offset in [-.36,.02,.39]:
                g.rod((x+offset-.045,y,bz),(x+offset+.045,y,bz),r*1.12,r*1.12,
                      'Field_Core' if boost else 'Glow_Cyan',bone,8)
            g.rod((x-.77,y,bz),(x-.84,y,bz),r*.72,r*.40,'Field_Core',bone,8)
            vents(g,(x+.15,y,bz+r*1.12),.55,r*.7,4,'Rubber',bone)
            part('engine_'+suf,('Engine_Efficient_' if not boost else 'Engine_Boost_')+suf,g,not boost)
        bone='gear.'+suf
        g=Geo().rod((L*.17,s*hw*.85,z+.25),(L*.10,s*hw*1.55,.15),.06,.075,'Steel',bone,6)
        g.rod((-L*.23,s*hw*.85,z+.25),(-L*.24,s*hw*1.55,.15),.06,.075,'Steel',bone,6)
        g.box((-.06*L,s*hw*1.55,.09),(L*.57,.18,.18),'Rubber',bone)
        part('gear_'+suf,'LandingSkid_'+suf,g)
        # Split canted field vanes replace conventional upright airplane fins.
        g=wing(Geo(),[(-L*.31,s*hw*.6,z+H*.42),(-L*.43,s*hw*.8,z+H*.42),
                      (-L*.47,s*hw*2.25,z+H*.82),(-L*.36,s*hw*1.9,z+H*.79)],.10,paint,'hull')
        g.rod((-L*.40,s*hw*1.05,z+H*.56),(-L*.43,s*hw*2.02,z+H*.80),.026,.026,'Field_Core','hull',6)
        part('tail_'+suf,'TailFin_'+suf,g)
    if kind=='Mule_Skycrane':
        for suf,s in [('L',1),('R',-1)]:
            g=loft(Geo(),[(-L*.48,.18,bz-.1,bz+.23),(L*.30,.18,bz-.1,bz+.23)],'Steel')
            g.v=[(x,y+s*W*.32,zz) for x,y,zz in g.v]
            part('boom_'+suf,'OutboardBoom_'+suf,g)
    for alt in [False,True]:
        g=Geo()
        if kind=='Kite_Courier':
            g.box((-L*.23,0,z+H*.60),(L*.24,hw*1.7,.38),'Steel' if alt else paint,'hull')
            g.box((-L*.23,0,z+H*.80),(.44,.15,.05),'Glow_Amber','hull')
        elif kind=='Manta_AirTaxi':
            g.box((-L*.22,0,z+H*.56),(L*.31,hw*1.75,H*.59),'Ceramic' if alt else paint,'hull')
            for s in [-1,1]:
                g.box((-L*.22,s*hw*.884,z+H*.64),(L*.22,.035,H*.23),'Glass_Opaque','hull')
                if alt:
                    g.box((-L*.22,s*hw*.91,z+H*.43),(.34,.04,.12),'Cloth_Burgundy','hull')
                    g.box((-L*.22,s*hw*.91,z+H*.43),(.12,.04,.34),'Cloth_Burgundy','hull')
        elif kind=='Mule_Skycrane':
            g.box((-L*.08,0,.67),(L*.49,hw*1.8,1.30 if not alt else .35),paint,'hull')
            for x in [-L*.27,L*.10]:
                g.rod((x,-hw*.73,.85),(x,-hw*.73,bz),.07,.07,'Steel','hull',6)
                g.rod((x,hw*.73,.85),(x,hw*.73,bz),.07,.07,'Steel','hull',6)
            if not alt:
                for x in [-.24,-.12,0,.12]:
                    g.box((x*L,0,1.34),(.075,hw*1.85,.08),'Alloy_Light','hull')
        else:
            g.rod((L*.31,0,z+.09),(L*.31,0,z-.24),.21,.25,'Steel','sensor',10)
            g.rod((L*.32,-.26,z-.16),(L*.32,-.29,z-.16),.12,.12,'Glow_Violet' if alt else 'Glow_Cyan','sensor',10)
            if alt:
                for s in [-1,1]:
                    g.box((L*.32,s*.36,z-.10),(.55,.16,.15),'Warning','sensor')
        part('payload','Payload_'+('Alternate' if alt else 'Standard'),g,not alt)
    # The service hatch is independent from payload, with a real hinge bone.
    g=Geo().box((-L*.345,0,z+H*.49),(.10,hw*1.45,H*.28),paint,'cargo')
    g.box((-L*.405,0,z+H*.51),(.05,.30,.06),'Alloy_Light','cargo')
    for s in [-1,1]:
        g.rod((-L*.407,s*hw*.52,z+H*.48),(-L*.43,s*hw*.52,z+H*.48),.045,.045,'Steel','cargo',6)
    part('hatch','ServiceHatch',g)
    info['alternate_parts']=[choices[-1] for choices in info['slots'].values()]


def reset(rig):
    for p in rig.pose.bones:
        p.rotation_mode='QUATERNION'
        p.rotation_quaternion=(1,0,0,0)
        p.location=(0,0,0)
        p.scale=(1,1,1)


def rotate(rig,bone,axis,angle):
    p=rig.pose.bones[bone]
    p.rotation_quaternion=Quaternion(p.bone.matrix_local.to_quaternion().inverted()@Vector(axis),angle)


for kind,info in CRAFT.items():
    rig=info['rig']
    clips=[('Aircraft_Reference',1,False),('Flight_Idle',60,True),('Cruise',60,True),('Boost',30,True),
           ('Bank_Left',30,False),('Bank_Right',30,False),('LandingGear_Deploy',30,False),
           ('LandingGear_Retract',30,False),('Canopy_Open',30,False),('Canopy_Close',30,False),
           ('Cargo_Open',30,False),('Cargo_Close',30,False)]
    CLIPS[rig.name]=[]
    for clip,frames,loop in clips:
        action=bpy.data.actions.new(rig.name+'__'+clip)
        action['expansion']=TAG
        action.use_fake_user=True
        rig.animation_data.action=action
        for f in range(frames+1):
            t=f/frames
            reset(rig)
            for suf,s in [('L',1),('R',-1)]:
                if clip in ['Flight_Idle','Cruise','Boost']:
                    amp=.06 if clip=='Boost' else .025
                    rotate(rig,'engine.'+suf,(0,1,0),amp*math.sin(math.tau*t))
                    rotate(rig,'lift_front.'+suf,(0,1,0),amp*math.sin(math.tau*t))
                    rotate(rig,'lift_rear.'+suf,(0,1,0),amp*math.sin(math.tau*t))
                if clip.startswith('Bank_'):
                    rotate(rig,'hull',(1,0,0),(.18 if clip.endswith('Left') else -.18)*math.sin(math.pi*t))
                if clip.startswith('LandingGear_'):
                    amount=t if clip.endswith('Retract') else 1-t
                    rotate(rig,'gear.'+suf,(1,0,0),s*1.05*amount)
            if clip.startswith('Canopy_'):
                rotate(rig,'canopy',(0,1,0),-.80*(t if clip.endswith('Open') else 1-t))
            if clip.startswith('Cargo_'):
                rotate(rig,'cargo',(0,1,0),1.15*(t if clip.endswith('Open') else 1-t))
            for p in rig.pose.bones:
                p.keyframe_insert('rotation_quaternion',frame=f)
                p.keyframe_insert('location',frame=f)
        for fc in action.fcurves:
            for key in fc.keyframe_points:
                key.interpolation='LINEAR'
        track=rig.animation_data.nla_tracks.new()
        track.name=clip
        track.strips.new(clip,0,action).name=clip
        track.mute=True
        CLIPS[rig.name].append(dict(clip=clip,start=0,end=frames,fps=30,duration_seconds=frames/30,
                                  loop=loop,root_translation='in_place'))
    rig.animation_data.action=None
    reset(rig)


def prop(name, specs, purpose, grid=None):
    c=col('PropParts_'+name,LIB)
    parts=[]
    for slot,g in specs:
        parts.append(make(g,name+'_'+slot,c,slot,'scenery.'+name).name)
    PROPS[name]=dict(parts=parts,purpose=purpose,grid_m=grid)


# Lane modules are 3 m wide, 4 m long, +X travel; 2/4/6-lane strips
# share end sockets and are assembled from independent lane surfaces/markings.
for lanes in [2,4,6]:
    w=lanes*3
    specs=[]
    for i in range(lanes):
        y=(i-(lanes-1)/2)*3
        specs.append((f'Lane_{i+1:02}',Geo().box((0,y,-.10),(4,3,.20),'Road_Basalt')))
    g=Geo()
    for i in range(lanes-1):
        y=(i-(lanes-2)/2)*3
        if i==lanes//2-1:
            for d in [-.10,.10]:
                g.box((0,y+d,.012),(4,.065,.018),'Warning')
        else:
            g.box((0,y,.012),(1.60,.075,.018),'Ceramic')
    specs.append(('LaneMarkings',g))
    g=Geo()
    for s in [-1,1]:
        g.box((0,s*(w/2-.13),.012),(4,.07,.018),'Ceramic')
    specs.append(('EdgeMarkings',g))
    prop(f'Road_{lanes}Lane_4m',specs,f'{lanes}-lane road; endpoints X +/-2 m',dict(x=4,y=3))
for name in ['Crossroad','TJunction']:
    specs=[('Surface',Geo().box((0,0,-.10),(12,12,.20),'Road_Basalt'))]
    for axis in [0,1]:
        for s in [-1,1]:
            if name=='TJunction' and axis==1 and s==1:
                continue
            g=Geo()
            for i in range(8):
                p=[-3.85+i*1.1,s*5.15,.014]
                size=(.60,1.2,.018)
                if axis==0:
                    p[0],p[1]=p[1],p[0]
                    size=(1.2,.60,.018)
                g.box(p,size,'Glow_Cyan')
            # Thin emissive tiles are ground holograms, not a pedestrian pole.
            specs.append((f'Crosswalk_{axis}_{s}',g))
    if name=='TJunction':
        specs.append(('ClosedEdge',Geo().box((0,5.78,.012),(12,.1,.018),'Ceramic')))
    prop('Road_'+name+'_12m',specs,'12m intersection; 4-lane connections at +/-6m',dict(x=12,y=12))
g=Geo()
for i in range(12):
    a=i*math.pi/24
    b=(i+1)*math.pi/24
    g.add([(r*math.cos(t),r*math.sin(t),zz) for zz in [-.2,0] for r,t in [(6,a),(12,a),(12,b),(6,b)]],
          [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'Road_Basalt')
mark=Geo()
for i in range(12):
    a=(i+.5)*math.pi/24
    mark.box((9*math.cos(a),9*math.sin(a),.014),(.07,.7,.02),'Warning',rot=Quaternion((0,0,1),a))
prop('Road_Curve90_R9m',[('ArcSurface',g),('CenterMarkings',mark)],
     '2-lane quarter turn; center (0,0), radius9; sockets (9,0),(0,9); tangent +Y/-X')
prop('City_Sidewalk_2x4m',[
    ('Slab',Geo().box((0,0,.08),(4,2,.16),'Habitat_Concrete')),
    ('Curb',Geo().box((0,-.91,.17),(4,.18,.18),'Ceramic')),
    ('ServiceStrip',Geo().box((0,.63,.17),(4,.24,.035),'Steel'))], '2m sidewalk on 2m grid',2)
g=Geo()
for i in range(6):
    g.box((i*.62-1.55,0,.035),(.34,2,.07),'Warning' if i%2==0 else 'Rubber')
prop('Road_SpeedTable_4m',[('Ramp',Geo().box((0,0,.025),(4,2,.05),'Steel')),('WarningBars',g)],'raised low speed marking',2)
prop('City_Streetlight_4m',[
    ('Foundation',chamfer(Geo(),(0,0,.20),(.62,.62,.40),'Steel')),
    ('Mast',chamfer(Geo(),(0,0,1.9),(.22,.26,3.8),'Ceramic')
       .box((0,-.145,2),(.055,.025,3.1),'Field_Core')),
    ('Luminaire',arc(Geo(),(0,-.30,3.9),.70,.55,.14,.15,math.pi*1.50,'Ceramic',steps=6)
       .rod((0,-.30,3.88),(0,-.30,3.94),.22,.22,'Field_Core',sides=8))], 'split-halo light over an induction spine')
g=chamfer(Geo(),(0,0,5.25),(2.4,.28,.94),'Steel')
g=chamfer(g,(0,-.165,5.25),(2.16,.06,.68),'Glass_Opaque')
stabilizer_l=field(Geo(),(-1.28,0,5.12),.28,.10,None,'Ceramic')
stabilizer_r=field(Geo(),(1.28,0,5.12),.28,.10,None,'Ceramic')
stop=Geo().box((-.64,-.215,5.25),(.30,.045,.30),'Signal_Stop')
wait=Geo().box((0,-.215,5.25),(.28,.045,.28),'Glass_Opaque',rot=Quaternion((0,1,0),math.pi/4))
go=Geo().add([(.48,y,5.05) for y in [-.24,-.21]]+
            [(.80,y,5.25) for y in [-.24,-.21]]+
            [(.48,y,5.45) for y in [-.24,-.21]],
            [(0,2,4),(1,5,3),(0,1,3,2),(2,3,5,4),(4,5,1,0)],'Glass_Opaque')
prop('City_TrafficSignal',[
    ('HoverHousing',g),('FieldStabilizer_L',stabilizer_l),('FieldStabilizer_R',stabilizer_r),('StopGlyph',stop),
    ('WaitGlyph',wait),('GoGlyph',go)], 'vehicle-only signal floating at 5.25m; no pole, foundation or pedestrian head')
PROPS['City_TrafficSignal'].update(hover_height_m=5.25,facing='-Y',
    signal_states={'STOP':{'part':'City_TrafficSignal_StopGlyph','material':'Signal_Stop'},
                   'WAIT':{'part':'City_TrafficSignal_WaitGlyph','material':'Glow_Amber'},
                   'GO':{'part':'City_TrafficSignal_GoGlyph','material':'Glow_Cyan'}},
    default_state='STOP',inactive_material='Glass_Opaque',pedestrian_control='road surface holograms only')
projectors=Geo()
for x in [-2.8,2.8]:
    for y in [-1.15,1.15]:
        projectors.rod((x,y,.015),(x,y,.11),.16,.12,'Steel',sides=8)
        projectors.rod((x,y,.115),(x,y,.14),.07,.07,'Field_Core',sides=8)
walk=Geo()
for i in range(8):
    walk.box((-2.1+i*.6,0,.035),(.32,2.10,.016),'Field_Core')
guidance=Geo()
for y in [-1.38,1.38]:
    guidance.box((0,y,.04),(4.8,.035,.018),'Glow_Cyan')
prop('City_PedestrianHologram_6m',[('Projectors',projectors),('WalkGrid',walk),('Guidance',guidance)],
     'pedestrian crossing projected on road surface; no elevated pedestrian signal',dict(x=6,y=3))
prop('City_Bollard',[
    ('Base',Geo().rod((0,0,0),(0,0,.12),.20,.20,'Steel',sides=8)),
    ('Post',Geo().rod((0,0,.12),(0,0,.9),.105,.09,'Ceramic',sides=8).rod((0,0,.71),(0,0,.78),.108,.108,'Glow_Cyan',sides=8))], 'pedestrian edge marker')
g=Geo()
for x in [-1.6,1.6]:
    g.box((x,0,.17),(.40,.72,.34),'Steel')
prop('Road_CrashBarrier_4m',[('Feet',g),('Rail',chamfer(Geo(),(0,0,.64),(4,.30,.64),'Ceramic')),
    ('Reflectors',Geo().box((0,-.17,.68),(3.2,.035,.065),'Field_Core'))], '4m ceramic crash barrier with luminous guidance',4)
g=Geo()
for x in [-.85,.85]:
    g.box((x,0,.26),(.15,.62,.52),'Steel')
prop('City_Bench', [('Legs',g),('Seat',chamfer(Geo(),(0,0,.51),(2.1,.70,.18),'Ceramic')),
    ('Back',chamfer(Geo(),(0,.26,.85),(2.1,.14,.58),'Vehicle_Teal')
      .box((0,-.36,.51),(1.7,.025,.04),'Field_Core'))], 'cantilever composite settlement seating')
prop('City_Planter',[
    ('Vessel',Geo().rings([(0,0,.05),(0,0,.8)],[(.60,.60),(.75,.75)],'Ceramic',sides=8)),
    ('Soil',Geo().box((0,0,.81),(1.05,1.05,.08),'Terrain_Regolith')),
    ('AlienFoliage',Geo().rod((0,0,.82),(0,0,1.65),.12,.07,'Alien_Leaf',sides=6)
      .ellipsoid((0,0,1.62),(.85,.65,.45),'Alien_Leaf',sides=8,rings=3)
      .rod((0,0,1.45),(.35,-.20,1.9),.07,.03,'Glow_Cyan',sides=6))], 'contained local vegetation')
g=Geo()
for x in [-1.7,1.7]:
    g.box((x,.65,1.5),(.12,.12,3),'Steel')
roof=wing(Geo(),[(-2.2,-1.4,2.9),(-.12,-1.6,3.18),(-.12,1.1,3.45),(-2,1,3.1)],.12,'Ceramic',None)
wing(roof,[(.12,-1.6,3.18),(2.2,-1.4,2.9),(2,1,3.1),(.12,1.1,3.45)],.12,'Ceramic',None)
roof.box((0,0,3.18),(.08,2.1,.04),'Field_Core')
prop('City_TransitShelter', [('Frame',g),('Roof',roof),
    ('BackPanel',Geo().box((0,.70,1.65),(3.5,.055,2.3),'Glass_Opaque')),
    ('Seat',chamfer(Geo(),(0,.35,.52),(2.8,.55,.18),'Ceramic')),
    ('RouteDisplay',chamfer(Geo(),(1.1,.64,1.9),(.65,.10,1.0),'Steel')
       .box((1.1,.50,2.10),(.44,.025,.045),'Field_Core')
       .box((1.1,.50,1.90),(.35,.025,.045),'Field_Core')
       .box((1.1,.50,1.70),(.44,.025,.045),'Field_Core'))], 'split-shell transit canopy with projected route display',2)
prop('City_Wayfinding',[
    ('Mast',chamfer(Geo(),(0,0,.65),(.34,.32,1.30),'Ceramic').box((0,-.18,.65),(.07,.025,.7),'Field_Core')),
    ('SignBoards',chamfer(Geo(),(.38,0,2.55),(1.55,.12,.36),'Vehicle_Teal')
       .box((-.25,0,2.05),(1.30,.035,.26),'Field_Core')),
    ('Arrows',Geo().add([(.7,y,2.46) for y in [-.09,-.07]]+
                       [(.92,y,2.55) for y in [-.09,-.07]]+
                       [(.7,y,2.64) for y in [-.09,-.07]],
                       [(0,2,4),(1,5,3),(0,1,3,2),(2,3,5,4),(4,5,1,0)],'Glow_Cyan'))], 'direction boards; no baked text')
prop('City_ChargePost',[
    ('Pedestal',chamfer(Geo(),(0,0,.65),(.65,.55,1.3),'Ceramic')),
    ('Display',Geo().box((0,-.29,.94),(.42,.035,.42),'Glass_Opaque').box((0,-.31,1.03),(.28,.025,.04),'Glow_Cyan')),
    ('InductionHalo',field(Geo(),(.70,0,.78),.47,.13,None,'Vehicle_Teal'))], 'contactless vehicle induction dock; no cable')
g=Geo()
for i in range(7):
    g.box((-.62+i*.20,-.565,1.3),(.06,.05,.8),'Steel')
prop('Utility_Transformer',[
    ('Cabinet',chamfer(Geo(),(0,0,.95),(1.6,1.1,1.9),'Vehicle_Teal')),
    ('CoolingFins',g),('FieldCapacitors',field(Geo(),(0,0,2.13),.55,.16,None,'Ceramic')),
    ('Status',Geo().box((.48,-.57,1.45),(.13,.035,.3),'Glow_Amber'))], 'settlement power distribution')
g=Geo()
for i in range(8):
    g.box((-2.6+i*.74,0,2.50),(.10,2.46,.10),'Alloy_Light')
prop('Utility_CargoContainer_6m',[
    ('Shell',chamfer(Geo(),(0,0,1.25),(6,2.4,2.5),'Vehicle_Utility')),
    ('RoofRibs',g),('Door_L',Geo().box((3.02,-.60,1.25),(.07,1.16,2.30),'Steel')),
    ('Door_R',Geo().box((3.02,.60,1.25),(.07,1.16,2.30),'Steel')),
    ('SealEmitters',Geo().box((3.09,-.45,1.25),(.03,.06,1.7),'Field_Core')
       .box((3.09,.45,1.25),(.03,.06,1.7),'Field_Core'))], 'faceted pressure cargo pod with separate doors and luminous seals',2)
g=Geo()
for i in range(6):
    g.box((-.75+i*.30,-.42,.75),(.11,.05,.8),'Steel')
prop('Utility_HVAC', [('Housing',chamfer(Geo(),(0,0,.65),(2.1,.8,1.3),'Ceramic')),('Grille',g),
    ('Exhaust',field(Geo(),(.45,0,1.48),.34,.13,None,'Steel'))], 'closed-cycle habitat climate pod')
g=Geo()
for x in [-1.4,1.4]:
    g.box((x,0,.7),(.14,1.2,1.4),'Steel')
spec=[('Supports',g)]
for i in range(3):
    spec.append((f'Pipe_{i}',Geo().rod((-2,(i-1)*.38,1.35),(2,(i-1)*.38,1.35),.14,.14,'Copper' if i==1 else 'Alloy_Light',sides=10)))
prop('Utility_PipeRack_4m',spec,'stackable utility conduit bay',4)
g=chamfer(Geo(),(0,0,2.0),(.42,.40,4.0),'Ceramic')
g.box((0,-.22,2),(.07,.025,3.3),'Field_Core')
prop('Utility_CommRelay',[('Mast',g),('Panel_L',arc(Geo(),(0,0,4.5),1.25,1.05,.20,.16,math.pi-.16,'Ceramic',steps=6)),
    ('Panel_R',arc(Geo(),(0,0,4.5),1.25,1.05,.20,math.pi+.16,math.tau-.16,'Ceramic',steps=6)),
    ('Beacon',Geo().rings([(0,0,4.1),(0,0,4.5),(0,0,5.1)],[.12,.30,.08],'Field_Core',sides=6))], 'split induction-array communications relay')
g=Geo()
for x in [-1,1]:
    g.rod((x,0,0),(x,0,1.45),.08,.08,'Steel',sides=6)
petals=Geo()
for s in [-1,1]:
    for x in [-.92,.92]:
        wing(petals,[(x-.76,s*.14,1.54),(x+.76,s*.14,1.54),
                     (x+.68,s*1.10,1.78),(x-.58,s*1.26,1.82)],.065,'Solar',None)
prop('Utility_SolarArray', [('Stand',g),('Panel',petals),
    ('Rails',Geo().box((0,0,1.55),(3.8,.08,.07),'Field_Core'))], 'four-petal photonic energy collector')
g=Geo()
for x in [-.7,.7]:
    for y in [-.7,.7]:
        g.rod((x,y,0),(x,y,.7),.08,.08,'Steel',sides=6)
prop('Utility_WaterTank',[('Legs',g),('Tank',Geo().rings([(0,0,.6),(0,0,.8),(0,0,2.8),(0,0,3.05)],[.6,1.05,1.05,.6],'Ceramic',sides=12)),
    ('Pipe',Geo().rod((1.04,0,.9),(1.04,0,2.6),.085,.085,'Copper',sides=8))], 'pressurized settlement reservoir')
spec=[]
for ix in range(4):
    for iy in range(4):
        spec.append((f'Deck_{ix}_{iy}',Geo().box(((ix-1.5)*4,(iy-1.5)*4,-.1),(4,4,.2),'Habitat_Concrete')))
g=Geo()
for i in range(3):
    a=i*math.tau/3
    arc(g,(0,0,.025),3.2,3.12,.025,a+.15,a+1.70,'Field_Core',steps=5)
    g.box((4.2*math.cos(a),4.2*math.sin(a),.022),(.80,.12,.025),'Glow_Amber',rot=Quaternion((0,0,1),a))
spec.append(('GuidanceReticle',g))
g=Geo()
for x,y in [(-7,-7),(-7,7),(7,-7),(7,7)]:
    g.rod((x,y,0),(x,y,.2),.15,.15,'Glow_Cyan',sides=8)
spec.append(('CornerLights',g))
prop('Airfield_LandingPad_16m',spec,'16m modular landing deck; origin at ground center',4)
prop('Airfield_Beacon',[
    ('Base',Geo().box((0,0,.12),(.60,.60,.24),'Steel')),
    ('Light',Geo().rod((0,0,.24),(0,0,.66),.13,.13,'Glow_Amber',sides=8)),
    ('Guard',Geo().box((0,0,.72),(.38,.38,.08),'Steel'))], 'landing corridor light')
g=Geo()
for i in range(3):
    a=i*math.tau/3
    g.rod((0,0,1.2),(.6*math.cos(a),.6*math.sin(a),.06),.04,.055,'Steel',sides=6)
prop('Frontier_SurveyTripod',[('Tripod',g),('Scanner',Geo().box((0,0,1.4),(.55,.35,.4),'Vehicle_Utility')),
    ('Lens',field(Geo(),(0,0,1.95),.35,.10,None,'Vehicle_Teal'))], 'anchored survey station with levitated scan crystal')
g=Geo()
for x in [-1.4,1.4]:
    g.rod((x,0,0),(x,0,2.6),.08,.06,'Steel',sides=6)
prop('Frontier_FieldShelter', [('Frame',g),
    ('Canopy',wing(Geo(),[(-1.7,-1.3,2.35),(1.7,-1.3,2.35),(1.4,1.3,2.7),(-1.4,1.3,2.7)],.09,'Solar',None)
       .box((0,0,2.55),(.045,2.4,.035),'Field_Core')),
    ('SupplyBox',Geo().box((-.8,.6,.35),(1,.65,.7),'Vehicle_Utility'))], 'open survey camp shelter')
prop('Frontier_MiningConveyor',[
    ('Frame',Geo().box((0,0,.65),(4,1,.18),'Steel')),
    ('Belt',chamfer(Geo(),(0,0,.93),(3.8,.80,.12),'Vehicle_Teal')
       .box((0,-.43,.77),(3.8,.05,.04),'Field_Core').box((0,.43,.77),(3.8,.05,.04),'Field_Core')),
    ('Legs',Geo().box((-1.4,0,.3),(.16,.85,.6),'Steel').box((1.4,0,.3),(.16,.85,.6),'Steel')),
    ('Motor',Geo().rod((1.55,-.5,.62),(1.55,-.85,.62),.23,.23,'Vehicle_Utility',sides=10))], '4m modular mine service conveyor',4)
prop('Ruin_ObserverMonolith',[
    ('Foot',Geo().rings([(0,0,0),(0,0,.35)],[1.1,.9],'Ruin_Obsidian',sides=6)),
    ('Shaft',Geo().rings([(0,0,.3),(.12,0,3.3),(0,0,4.5)],[(.55,.40),(.45,.32),(.08,.10)],'Ruin_Obsidian',sides=6)),
    ('Inlay',Geo().box((.04,-.34,2.4),(.06,.035,1.8),'Glow_Violet').box((.04,-.36,3.1),(.55,.035,.07),'Glow_Violet'))], 'abstract precursor observation trace')
for suf,s in [('L',-1),('R',1)]:
    g=Geo().rod((s*1.7,0,.1),(s*1.5,0,3),.45,.34,'Ruin_Obsidian',sides=6)
    g.rod((s*1.5,0,3),(s*.42,0,4.1),.34,.23,'Ruin_Obsidian',sides=6)
    if suf=='L':
        arch=[('Pier_L',g)]
    else:
        arch.append(('Pier_R',g))
arch.append(('SuspendedShard',Geo().rod((-.13,0,4.18),(.12,0,4.24),.17,.12,'Glow_Violet',sides=6)))
prop('Ruin_FracturedArch',arch,'open broken arch with separated anomaly shard')
g=Geo()
for i in range(5):
    a=i*2.4
    x,y=math.cos(a)*.48,math.sin(a)*.48
    h=.7+i*.25
    g.rings([(x,y,0),(x,y,h*.75),(x+.1,y,h)],[.20,.20,.025],'Glow_Violet' if i%2 else 'Alien_Petal',sides=5)
prop('Nature_PrismBloom',[('RockBed',Geo().ellipsoid((0,0,.10),(.9,.8,.2),'Rock_Mine',sides=8,rings=3)),('Crystals',g)],'alien mineral growth')
prop('Nature_SailTree',[
    ('Trunk',Geo().rod((0,0,0),(.18,0,3.5),.28,.12,'Wood_Composite',sides=7)),
    ('Canopy',Geo().ellipsoid((.2,0,3.4),(2.1,1.3,.6),'Alien_Leaf',sides=10,rings=4)),
    ('GlowSeedpods',Geo().ellipsoid((-.7,-.5,2.8),(.16,.16,.35),'Glow_Cyan',sides=6,rings=3)
      .ellipsoid((.8,-.2,2.7),(.18,.18,.4),'Glow_Cyan',sides=6,rings=3))], 'alien umbrella canopy; frontier silhouette')

over_budget=[r for r in PIPELINE.records if r['optimized_triangles']>500]
draft_report=dict(parts=len(PIPELINE.records),over_budget=over_budget,
                  source_triangles=sum(r['source_triangles'] for r in PIPELINE.records),
                  optimized_triangles=sum(r['optimized_triangles'] for r in PIPELINE.records),
                  max_surface_error_m=max(r['max_surface_error_m'] for r in PIPELINE.records))
draft_report['max_source_triangles']=max(r['source_triangles'] for r in PIPELINE.records)
draft_report['max_optimized_triangles']=max(r['optimized_triangles'] for r in PIPELINE.records)
(ROOT/'Validation/detail_draft_check.json').write_text(json.dumps(draft_report,indent=2)+'\n',encoding='utf-8')
if over_budget:
    drafts=ROOT/'Source/Drafts'
    drafts.mkdir(parents=True,exist_ok=True)
    bpy.data.libraries.write(str(drafts/'Air_City_Detail_Overbudget.blend'),{SOURCE_SCENE},
                             path_remap='RELATIVE',fake_user=True,compress=True)
assert not over_budget,[(r['id'],r['optimized_triangles']) for r in over_budget]
if '--geometry-check' in sys.argv:
    print('DETAIL_GEOMETRY_CHECK',json.dumps(draft_report),flush=True)
    raise SystemExit(0)


def export(path,names,rig=None,animated=False,kind='scenery'):
    path=ROOT/path
    path.parent.mkdir(parents=True,exist_ok=True)
    staged=path.with_name(path.stem+'.staging.glb')
    bpy.context.window.scene=scene
    bpy.ops.object.select_all(action='DESELECT')
    for n in names:
        PROTOTYPES[n].select_set(True)
    if rig:
        rig.select_set(True)
        bpy.context.view_layer.objects.active=rig
        rig.animation_data.action=None
        for t in rig.animation_data.nla_tracks:
            t.mute=not animated
        reset(rig)
    scene.frame_set(0)
    bpy.ops.export_scene.gltf(filepath=str(staged),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=animated,export_animation_mode='NLA_TRACKS',export_nla_strips=True,
        export_force_sampling=True,export_frame_range=False,export_skins=bool(rig),export_def_bones=False,
        export_yup=True,export_extras=True,export_cameras=False,export_lights=False,
        export_materials='EXPORT',export_apply=False,export_texcoords=True,export_normals=True,
        export_anim_slide_to_zero=True)
    staged.replace(path)
    EXPORTS.append(dict(file=str(path.relative_to(ROOT)).replace('\\','/'),type=kind,
                        mesh_parts=names,clips=len(CLIPS[rig.name]) if animated else 0,expansion=TAG))
    if rig:
        for t in rig.animation_data.nla_tracks:
            t.mute=True
        reset(rig)


for kind,info in CRAFT.items():
    for suffix,key in [('', 'default_parts'),('_Variant','alternate_parts')]:
        export(f'Export/Aircraft/{kind}{suffix}.glb',info[key],info['rig'],True,'aircraft')
    for n in info['parts']:
        export(f'Export/Aircraft/Parts/{n}.glb',[n],info['rig'],False,'module')
for name,info in PROPS.items():
    export(f'Export/Scenery/{name}.glb',info['parts'])
    for n in info['parts']:
        export(f'Export/Scenery/Parts/{n}.glb',[n],kind='module')
current_exports={e['file'] for e in EXPORTS}
retired=[]
for old in sorted(previous_exports-current_exports):
    target=(ROOT/old).resolve()
    assert target.is_relative_to((ROOT/'Export').resolve()),target
    if target.is_file():
        target.unlink()
        retired.append(old)
print('EXPANSION_EXPORTS',len(EXPORTS),'PARTS',len(ASSETS),flush=True)


def tagged(obj):
    obj['expansion']=TAG
    return obj


def studio(name,center,span,res=(1800,1200)):
    s=bpy.data.scenes.new(name)
    s['expansion']=TAG
    s.unit_settings.system='METRIC'
    s.render.fps=30
    s.render.engine='CYCLES'
    s.cycles.device='CPU'
    s.cycles.samples=16
    s.cycles.use_denoising=True
    s.render.resolution_x,s.render.resolution_y=res
    s.render.resolution_percentage=100
    s.render.image_settings.file_format='PNG'
    s.view_settings.view_transform='AgX'
    c=bpy.data.collections.new(name+'_Display')
    c['expansion']=TAG
    s.collection.children.link(c)
    world=bpy.data.worlds.new(name+'_World')
    world.use_nodes=True
    world.node_tree.nodes['Background'].inputs[0].default_value=(.20,.26,.32,1)
    world.node_tree.nodes['Background'].inputs[1].default_value=.45
    s.world=world
    data=bpy.data.cameras.new(name+'_Camera')
    cam=tagged(bpy.data.objects.new(name+'_Camera',data))
    c.objects.link(cam)
    cam.location=Vector(center)+Vector((span*.60,-span*.85,span*.92))
    cam.rotation_euler=(Vector(center)-cam.location).to_track_quat('-Z','Y').to_euler()
    data.type='ORTHO'
    data.ortho_scale=span
    s.camera=cam
    for label,pos,power,color in [('Key',(0,-.3,1),190,(1,.83,.66)),('Fill',(-.5,.1,.65),130,(.53,.76,1)),('Rim',(.2,.6,1),220,(.58,1,.92))]:
        ld=bpy.data.lights.new(name+label,'AREA')
        ld.energy=power*span
        ld.color=color
        ld.shape='DISK'
        ld.size=span*.5
        ob=tagged(bpy.data.objects.new(name+label,ld))
        c.objects.link(ob)
        ob.location=Vector(center)+Vector(pos)*span
        ob.rotation_euler=(Vector(center)-ob.location).to_track_quat('-Z','Y').to_euler()
    mesh=bpy.data.meshes.new(name+'_Ground')
    mesh.from_pydata([(-150,-150,-.23),(150,-150,-.23),(150,150,-.23),(-150,150,-.23)],[],[(0,1,2,3)])
    mesh.materials.append(MATS['Rubber'])
    ob=tagged(bpy.data.objects.new(name+'_Ground',mesh))
    c.objects.link(ob)
    return s,c


def label(c,text,pos,size=.30):
    data=bpy.data.curves.new('Caption','FONT')
    data.body=text
    data.align_x='CENTER'
    data.size=size
    data.materials.append(MATS['Ceramic'])
    ob=tagged(bpy.data.objects.new('Caption_'+text,data))
    c.objects.link(ob)
    ob.location=pos
    # Flat on deck, readable from the gallery camera.
    return ob


def display(names,c,offset=(0,0,0),yaw=0,explode=False):
    rot=Matrix.Rotation(yaw,4,'Z')
    for i,n in enumerate(names):
        src=PROTOTYPES.get(n) or bpy.data.objects.get(n)
        ob=tagged(src.copy())
        ob.data=src.data
        ob.name='Display_'+n
        ob.parent=None
        ob.modifiers.clear()
        ob.asset_clear()
        c.objects.link(ob)
        shift=Vector((0,0,0))
        if explode:
            center=sum((v.co for v in ob.data.vertices),Vector())/len(ob.data.vertices)
            shift=Vector((center.x*.35,center.y*.55,max(0,center.z)*.65))
        ob.location=Vector(offset)+rot.to_3x3()@shift
        ob.rotation_euler=(0,0,yaw)


GALLERY,gcol=studio('10_Aircraft',(0,1,0),30,(2000,1500))
VARIANTS,vcol=studio('10b_Aircraft_Variants',(0,1,0),30,(2000,1500))
for i,(kind,info) in enumerate(CRAFT.items()):
    xy=((i%2-.5)*13,(i//2-.5)*14)
    for c,key in [(gcol,'default_parts'),(vcol,'alternate_parts')]:
        display(info[key],c,(*xy,0),-math.pi/2)
        label(c,kind.replace('_',' ').upper(),(xy[0],xy[1]-5.7,-.18),.36)
label(gcol,'HALO  /  FRONTIER AVIATION',(0,15,-.18),.60)
label(vcol,'HALO  /  ALTERNATE MODULES',(0,15,-.18),.60)
EXPLODED,ecol=studio('10c_Aircraft_Exploded',(0,0,1),20,(1800,1200))
display(CRAFT['Warden_Survey']['default_parts'],ecol,explode=True)
label(ecol,'WARDEN  /  INDEPENDENT ASSEMBLY PARTS',(0,-7,-.18),.45)
catalog_props=[(n,i) for n,i in PROPS.items() if not n.startswith('Road_') and n!='Airfield_LandingPad_16m']
CATALOG,pcol=studio('11_City_Frontier_Props',(0,0,0),45,(2400,1800))
for i,(name,info) in enumerate(catalog_props):
    xy=((i%5-2)*8,(i//5-2)*8)
    display(info['parts'],pcol,(*xy,0))
    label(pcol,name.replace('Utility_','').replace('City_','').replace('Frontier_','').replace('_',' '),
          (xy[0],xy[1]-3.2,-.18),.24)
label(pcol,'HALO  /  CITY & FRONTIER KIT',(0,23,-.18),.65)
ROADS,rcol=studio('11b_Road_Modules',(0,1,0),48,(2000,1500))
for lanes,y in [(6,-10),(4,-2),(2,6)]:
    display(PROPS[f'Road_{lanes}Lane_4m']['parts'],rcol,(-12,y,0),math.pi/2)
    label(rcol,f'{lanes} LANES / 4m SEGMENT',(-12,y-3,-.18),.34)
for name,pos in [('Road_Crossroad_12m',(9,-8,0)),('Road_TJunction_12m',(9,8,0)),
                 ('Road_Curve90_R9m',(-24,12,0))]:
    display(PROPS[name]['parts'],rcol,pos)
label(rcol,'HALO  /  MODULAR ROAD NETWORK',(0,25,-.18),.60)
OUTPOST,ocol=studio('12_Airfield_Outpost',(0,1,0),43,(2000,1500))
display(PROPS['Airfield_LandingPad_16m']['parts'],ocol,(-7,6,0))
display(CRAFT['Manta_AirTaxi']['default_parts'],ocol,(-7,6,.06),-math.pi/2)
for i in range(7):
    display(PROPS['Road_4Lane_4m']['parts'],ocol,(-12+i*4,-9,0))
    display(PROPS['City_Sidewalk_2x4m']['parts'],ocol,(-12+i*4,-2,.02))
for x in [-15,-3,9]:
    display(PROPS['City_Streetlight_4m']['parts'],ocol,(x,-2,0),math.pi)
display(PROPS['City_TrafficSignal']['parts'],ocol,(0,-9,0))
display(PROPS['City_PedestrianHologram_6m']['parts'],ocol,(0,-9,.01),math.pi/2)
for name,pos,yaw in [('City_TransitShelter',(5,0,0),0),('City_Bench',(9,0,0),0),
                     ('City_ChargePost',(2,2,0),0),('Utility_CargoContainer_6m',(6,10,0),0),
                     ('Utility_SolarArray',(7,6,0),0),('Utility_CommRelay',(12,10,0),0),
                     ('Utility_Transformer',(11,5,0),0),('City_Planter',(12,0,0),0),
                     ('Nature_SailTree',(-16,12,0),0),('Frontier_SurveyTripod',(-15,2,0),0)]:
    display(PROPS[name]['parts'],ocol,pos,yaw)
label(ocol,'HALO  /  AIRFIELD & TRANSIT DISTRICT',(0,-17,-.18),.62)
DETAILS,dcol=studio('13_Field_Tech_Details',(0,0,2),18,(2100,1400))
display(PROPS['City_TrafficSignal']['parts'],dcol,(-4,0,0))
display(PROPS['City_PedestrianHologram_6m']['parts'],dcol,(-4,0,0))
display(PROPS['City_ChargePost']['parts'],dcol,(2,-1,0))
field_name=CRAFT['Warden_Survey']['slots']['lift_Front_L'][0]
display([field_name],dcol,(5.7+7.8*.08,-9*.37,1.3))
label(dcol,'HOVER VEHICLE SIGNAL',(-4,-2,-.18),.28)
label(dcol,'GROUND CROSSING HOLOGRAM',(-4,2.1,-.18),.24)
label(dcol,'INDUCTION DOCK',(2,-2.4,-.18),.26)
label(dcol,'FIELD DRIVE / NO ROTOR',(6,1,-.18),.28)
label(dcol,'HALO / NON-CONTACT INFRASTRUCTURE',(0,4,-.18),.42)
COMPARISON,qcol=studio('18_Detail_Comparison',(0,0,.2),19,(2400,1800))
record_by_name={r['id']:r for r in PIPELINE.records}
for row,name in enumerate(comparison_ids):
    if name not in before_detail_meshes:
        continue
    old_mesh=before_detail_meshes[name]
    old_mesh.calc_loop_triangles()
    record=record_by_name[name]
    meshes=[old_mesh,bpy.data.objects[record['source_object']].data,PROTOTYPES[name].data]
    counts=[len(old_mesh.loop_triangles),record['source_triangles'],record['optimized_triangles']]
    for column,(mesh,count) in enumerate(zip(meshes,counts)):
        x=(column-1)*5.5
        y=(1-row)*4.4
        obj=tagged(bpy.data.objects.new('Comparison_'+name+'_'+str(column),mesh))
        qcol.objects.link(obj)
        lo=[min(v.co[i] for v in mesh.vertices) for i in range(3)]
        hi=[max(v.co[i] for v in mesh.vertices) for i in range(3)]
        obj.location=(x-(lo[0]+hi[0])/2,y-(lo[1]+hi[1])/2,.15-lo[2])
        label(qcol,f'{count} TRIANGLES',(x,y-1.7,-.18),.24)
for i,caption in enumerate(['PREVIOUS SIMPLE MODEL','DETAILED EDITABLE SOURCE','DETAIL-PRESERVING CLEANUP']):
    label(qcol,caption,((i-1)*5.5,7,-.18),.25)
label(qcol,'COIL / ENGINE / POWER CABINET',(0,-7,-.18),.35)


def write_json(path,value):
    target=ROOT/path
    temporary=target.with_suffix(target.suffix+'.new')
    temporary.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    temporary.replace(target)


def fit_camera(s):
    """Fit every display mesh and caption, with a measured 6% margin per side."""
    bpy.context.window.scene=s
    bpy.context.view_layer.update()
    camera=s.camera
    # All gallery objects are unparented. Compose authored transforms directly:
    # newly-created inactive scenes can still have unevaluated matrix_world and
    # bound_box values in a background Blender session.
    def matrix(o):
        return Matrix.LocRotScale(o.location,o.rotation_euler.to_quaternion(),o.scale)
    inv=matrix(camera).inverted()
    pts=[]
    for o in s.objects:
        if o.type=='MESH' and not o.name.endswith('_Ground'):
            local=[v.co for v in o.data.vertices]
        elif o.type=='FONT':
            half=len(o.data.body)*o.data.size*.72/2
            local=[Vector((x,y,0)) for x in [-half,half] for y in [-o.data.size*.2,o.data.size*1.2]]
        else:
            continue
        transform=inv@matrix(o)
        pts.extend(transform@p for p in local)
    lo=[min(p[i] for p in pts) for i in [0,1]]
    hi=[max(p[i] for p in pts) for i in [0,1]]
    center=Vector(((lo[0]+hi[0])/2,(lo[1]+hi[1])/2,0))
    camera.location+=camera.rotation_euler.to_quaternion()@center
    aspect=s.render.resolution_x/s.render.resolution_y
    camera.data.ortho_scale=max(hi[0]-lo[0],(hi[1]-lo[1])*aspect)*1.12
    s['framing_margin']=.06


for s in [GALLERY,VARIANTS,EXPLODED,CATALOG,ROADS,OUTPOST,DETAILS,COMPARISON]:
    fit_camera(s)


report=dict(expansion=TAG,design_revision=DESIGN_REVISION,retired_exports=retired,
            mesh_parts=len(ASSETS),max_triangles=max(a['triangles'] for a in ASSETS),
            all_parts_under_500=True,aircraft_types=len(CRAFT),scenery_assemblies=len(PROPS),issues=[],parts=[])
for a in ASSETS:
    o=PROTOTYPES[a['id']]
    o.data.calc_loop_triangles()
    for tri in o.data.loop_triangles:
        p,q,r=[o.data.vertices[i].co for i in tri.vertices]
        if (q-p).cross(r-p).length<1e-10:
            report['issues'].append(a['id']+': degenerate triangle')
    if any(not math.isfinite(x) for v in o.data.vertices for x in v.co):
        report['issues'].append(a['id']+': non-finite coordinate')
    assert o.data.uv_layers and 0<len(o.data.loop_triangles)<=500
    if o.parent:
        assert all(abs(sum(g.weight for g in v.groups)-1)<1e-5 for v in o.data.vertices)
    report['parts'].append(dict(id=a['id'],polygons=a['polygons'],triangles=a['triangles']))
assert not report['issues'],report['issues']
write_json('Validation/air_city_source_report.json',report)
write_json('Validation/detail_pipeline_report.json',dict(
    schema='detail_first_lossless_cleanup_v1',scope=TAG,triangle_limit=500,
    source_scene=SOURCE_SCENE.name,comparison_scene=COMPARISON.name,
    simplification_policy='preserve external geometry, materials and rigid joint assignments; no ratio decimation',
    summary=draft_report,parts=PIPELINE.records))
write_json('Documentation/aircraft_assemblies.json',dict(schema='erma_aircraft_v1',units='meters',
    design_revision=DESIGN_REVISION,propulsion='segmented induction fields; no mechanical rotors',
    authoring_forward='+X',authoring_up='+Z',gltf_forward='+X',gltf_up='+Y',
    origin='ground center in deployed landing-skid bind pose',types={k:{**{key:v for key,v in i.items() if key!='rig'},'skeleton':i['rig'].name} for k,i in CRAFT.items()}))
write_json('Documentation/scenery_assemblies.json',dict(schema='erma_scenery_v1',units='meters',
    design_revision=DESIGN_REVISION,
    authoring_up='+Z',gltf_up='+Y',origin='ground center unless explicit sockets below',
    assemblies=PROPS))
catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
catalog['assets']=[a for a in catalog['assets'] if a.get('expansion')!=TAG]+ASSETS
catalog['exports']=[a for a in catalog['exports'] if a.get('expansion')!=TAG]+EXPORTS
write_json('Documentation/asset_catalog.json',catalog)
for path,addition in [('Documentation/animation_clips.json',CLIPS),
    ('Documentation/assembly_recipes.json',{**{k:dict(parts=i['default_parts'],skeleton=i['rig'].name) for k,i in CRAFT.items()},
                                          **{k+'_Variant':dict(parts=i['alternate_parts'],skeleton=i['rig'].name) for k,i in CRAFT.items()},
                                          **{k:dict(parts=i['parts']) for k,i in PROPS.items()}})]:
    data=json.loads((ROOT/path).read_text(encoding='utf-8'))
    data.update(addition)
    write_json(path,data)
skels=json.loads((ROOT/'Documentation/skeletons.json').read_text(encoding='utf-8'))
for info in CRAFT.values():
    rig=info['rig']
    skels[rig.name]=dict(units='meters',authoring_up='+Z',authoring_forward='+X',gltf_up='+Y',gltf_forward='+X',
        bones=[dict(name=b.name,parent=b.parent.name if b.parent else None,head_m=list(b.head_local),tail_m=list(b.tail_local),
                    inverse_bind_matrix_column_major=[b.matrix_local.inverted()[r][c] for c in range(4) for r in range(4)]) for b in rig.data.bones])
write_json('Documentation/skeletons.json',skels)
overall=json.loads((ROOT/'Validation/source_report.json').read_text(encoding='utf-8'))
overall.update(mesh_parts=len(catalog['assets']),max_triangles=max(a['triangles'] for a in catalog['assets']),
               all_parts_under_500=all(a['triangles']<=500 for a in catalog['assets']))
for info in CRAFT.values():
    rig=info['rig']
    overall['rigs'][rig.name]=dict(bone_count=len(rig.data.bones),clips=len(CLIPS[rig.name]))
overall['expansion_report']='air_city_source_report.json'
write_json('Validation/source_report.json',overall)
# Preserve the current entry scene when extending a library edited by other tools.
bpy.context.window.scene=bpy.data.scenes.get(entry_scene_name) or GALLERY
for s in [GALLERY,VARIANTS,EXPLODED,CATALOG,ROADS,OUTPOST,DETAILS,COMPARISON]:
    s.frame_set(0)
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE),compress=True)
print('EXPANSION_SAVED',len(ASSETS),len(PROPS),flush=True)
if '--no-render' not in sys.argv:
    for s,file in [(GALLERY,'12_aircraft'),(VARIANTS,'13_aircraft_variants'),
                   (EXPLODED,'14_aircraft_exploded'),(CATALOG,'15_city_frontier_props'),
                   (ROADS,'15b_road_modules'),(OUTPOST,'16_airfield_outpost'),(DETAILS,'17_field_tech_details'),
                   (COMPARISON,'23_detail_pipeline_comparison')]:
        bpy.context.window.scene=s
        s.render.filepath=str(ROOT/'Preview'/f'{file}.png')
        bpy.ops.render.render(write_still=True)
        print('PREVIEW_SAVED',file,flush=True)
print('AIR_CITY_COMPLETE',flush=True)
