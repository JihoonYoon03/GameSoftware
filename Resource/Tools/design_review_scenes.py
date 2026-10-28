"""Additional Blender scenes for sex/mass, purpose silhouettes and vehicles."""
DESIGN_RENDERS=[]

BODY_STUDY,bcol=studio('07_Body_Profiles',(3,-15,7),(0,1.4,1),12.5,(2100,1250));ground(bcol)
for i,(build,p) in enumerate(BODY_PROFILES.items()):
    parts=[f'Body_{build}_{n}' for n in ['Pelvis','Abdomen','Chest']]+['Body_Neck','Head_Oval','Face_Neutral']
    for suf in ['L','R']:
        parts += [f'Body_{build}_{n}_{suf}' for n in ['UpperArm','Forearm','Thigh','Shin']]+[f'Body_Hand_{suf}',f'Boot_{suf}']
    for row in [0,1]:
        x=(i-2.5)*1.7;y=row*3.0
        rig,objs=clone_actor('BodyReview_'+build+str(row),human,parts,bcol,(x,y,0))
        if row:rig.rotation_euler[2]=math.pi/2
        for obj in objs:
            for k,mat in enumerate(obj.data.materials):
                if mat.name.startswith('Skin_'):obj.data.materials[k]=MATS['Ceramic']
        if row==0:text_label(bcol,build.replace('_',' '),(x,-.50,.02),.12)
text_label(bcol,'BODY PROFILES / SAME HEIGHT / SHARED SKELETON',(0,4.1,2.55),.23)
text_label(bcol,'male & female / lean - normal - heavy / front and side',(0,4.1,2.22),.14,'Glow_Cyan')
DESIGN_RENDERS.append((BODY_STUDY,'06_body_profiles.png'))

def review_outfit_parts(role,build):
    accessories=['Accessory_'+n for n in ROLE_ACCESSORIES[role]]
    parts=[f'Body_{build}_Pelvis','Body_Neck','Head_Oval','Face_Neutral',f'Outfit_{role}_{build}_Torso']
    if not any('Helmet' in n for n in accessories):parts.append('Hair_HatClearance_Buzz' if role=='Miner' else 'Hair_Buzz')
    for suf in ['L','R']:
        parts += [f'Body_Hand_{suf}',f'Boot_{suf}']+[f'Outfit_{role}_{build}_{n}_{suf}' for n in ['Sleeve','Cuff','Trouser','Greave']]
    return parts+accessories,{n:accessory_offset(n,build,role) for n in accessories}

OUTFIT_STUDY,ocol=studio('08_Outfit_Purposes',(3,-15,9),(0,2.2,1),11.3,(2100,1400));ground(ocol)
for i,role in enumerate(ROLE_META):
    row=0 if i<5 else 1;column=i if row==0 else i-5
    x=(column-(2 if row==0 else 1.5))*1.85;y=row*4.5
    parts,offsets=review_outfit_parts(role,'Male_Normal')
    rig,objs=clone_actor('Purpose_'+role,human,parts,ocol,(x,y,0),part_offsets=offsets)
    text_label(ocol,role,(x,y-.48,.02),.14)
text_label(ocol,'NINE PURPOSES / DISTINCT GEOMETRY / DETACHABLE GEAR',(0,5.4,2.5),.21)
DESIGN_RENDERS.append((OUTFIT_STUDY,'07_outfit_purposes.png'))
gray=OUTFIT_STUDY.copy();gray.name='08b_Outfit_Silhouette_Check';gray.use_fake_user=True
neutral=material('QA_Neutral_Gray',(.36,.36,.36),.85)
gray.view_layers[0].material_override=neutral
DESIGN_RENDERS.append((gray,'08_outfits_grayscale.png'))

VLOC={'Coupe':(-5,-3,0),'Shuttle':(2,-3,0),'Hauler':(-5,3,0),'Patrol':(2,3,0),'JetBike':(8,0,0)}
for alt in [False,True]:
    s,col=studio('09b_Vehicle_Variants' if alt else '09_Vehicles',(18,-24,22),(1,1,.8),25,(1900,1250));ground(col,45)
    for kind,info in VEHICLES.items():
        pos=VLOC[kind];parts=info['alternate' if alt else 'defaults']
        rig,objs=clone_actor('VehicleReview_'+kind+str(alt),info['rig'],parts,col,pos)
        text_label(col,kind+(' / B' if alt else ' / A'),(pos[0],pos[1]-1.5,.04),.30)
    text_label(col,'HALO / MODULAR HOVER VEHICLES'+(' / ALTERNATE PARTS' if alt else ''),(1,8,3.3),.40)
    for obj in col.objects:
        if obj.type=='LIGHT':obj.data.energy*=3;obj.data.size*=1.7
    DESIGN_RENDERS.append((s,'10_vehicle_variants.png' if alt else '09_vehicles.png'))

EXPLODED,ecol=studio('09c_Vehicle_Exploded',(18,-23,20),(0,0,1),25,(1800,1250));ground(ecol,40)
for kind,pos in [('Hauler',(-6,0,1.4)),('JetBike',(6,0,1.4))]:
    info=VEHICLES[kind];L=info['parameters']['length'];W=info['parameters']['width'];H=info['parameters']['height']
    offsets={}
    for slot,choices in info['slots'].items():
        name=next((n for n in choices if n in info['defaults']),None)
        if not name:continue
        off=(0,0,0)
        if slot=='nose':off=(L*.36,0,.18)
        elif slot=='canopy':off=(0,0,H*.75)
        elif slot=='payload':off=(-L*.25,0,H*.7)
        elif slot=='service_hatch':off=(-L*.5,0,H*.45)
        elif slot=='underbody':off=(0,0,-H*.45)
        elif slot.startswith('pod.') or slot.startswith('rail.') or slot.startswith('footrest.'):
            off=(L*.08 if '.front.' in slot else -L*.08 if '.rear.' in slot else 0,(1 if slot.endswith('L') else -1)*W*.65,0)
        elif slot.startswith('engine.'):
            off=(-L*.46,(1 if slot.endswith('L') else -1)*W*.25,.10)
        offsets[name]=off
    rig,objs=clone_actor('Exploded_'+kind,info['rig'],info['defaults'],ecol,pos,part_offsets=offsets)
    for obj in objs:
        part=obj.name.split('__',1)[1];off=Vector(offsets[part])
        if off.length<.05:continue
        center=sum((v.co for v in obj.data.vertices),Vector())/len(obj.data.vertices)+Vector(pos)
        curve=bpy.data.curves.new('AssemblyGuide','CURVE');curve.dimensions='3D';curve.bevel_depth=.007;curve.bevel_resolution=0
        spline=curve.splines.new('POLY');spline.points.add(1)
        spline.points[0].co=(*center,1);spline.points[1].co=(*(center+off),1)
        line=bpy.data.objects.new('AssemblyGuide',curve);ecol.objects.link(line);curve.materials.append(MATS['Glow_Cyan'])
    text_label(ecol,kind+' / SLOT ASSEMBLY',(pos[0],-4,.04),.33)
for obj in ecol.objects:
    if obj.type=='LIGHT':obj.data.energy*=3;obj.data.size*=1.7
DESIGN_RENDERS.append((EXPLODED,'11_vehicle_exploded.png'))

METRICS={}
for build,p in BODY_PROFILES.items():
    parts=[PROTOTYPES[f'Body_{build}_{n}'] for n in ['Pelvis','Abdomen','Chest']]
    metric={}
    for name,obj in zip(['hip','waist','chest'],parts):
        coords=[v.co for v in obj.data.vertices]
        if name=='waist':coords=[v for v in coords if abs(v.z-1.13)<.005]
        metric[name+'_width_m']=max(v.x for v in coords)-min(v.x for v in coords)
        metric[name+'_depth_m']=max(v.y for v in coords)-min(v.y for v in coords)
    METRICS[build]=metric
purpose_geometry={role:hashlib.sha256(json.dumps([tuple(v.co) for v in PROTOTYPES[f'Outfit_{role}_Male_Normal_Torso'].data.vertices]).encode()).hexdigest() for role in ROLE_META}
assert len(set(purpose_geometry.values()))==len(ROLE_META),'outfit purpose geometry must differ beyond materials'
(ROOT/'Validation/design_report.json').write_text(json.dumps({'body_profiles':METRICS,'outfit_torso_geometry_hashes':purpose_geometry,'distinct_outfit_geometries':len(set(purpose_geometry.values())),'accessory_meshes':len(ACCESSORY_META),'vehicle_types':len(VEHICLES),'vehicle_parts':sum(len(i['parts']) for i in VEHICLES.values()),'all_checks_passed':True},indent=2),encoding='utf-8')
print('DESIGN_REVIEW_SCENES_READY',len(DESIGN_RENDERS),flush=True)
