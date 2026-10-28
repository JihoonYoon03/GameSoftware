"""Modular hover vehicle authoring, executed in the pack builder's namespace."""
VEHICLES={}
VC=collection('07_Modular_Vehicles')
for args in [('Vehicle_Teal',(.035,.25,.29),.36,.50),('Vehicle_Ivory',(.69,.70,.63),.43,.35),('Vehicle_Utility',(.58,.28,.07),.48,.45),('Vehicle_Patrol',(.045,.085,.18),.33,.60),('Vehicle_Burgundy',(.30,.055,.08),.38,.55)]:
    material(*args)
VEHICLE_TYPES={
 'Coupe':dict(length=4.3,width=1.70,height=1.35,paint='Vehicle_Teal',purpose='civilian two-seat hover coupe'),
 'Shuttle':dict(length=5.6,width=1.85,height=2.10,paint='Vehicle_Ivory',purpose='enclosed passenger / medical shuttle'),
 'Hauler':dict(length=5.2,width=1.90,height=2.00,paint='Vehicle_Utility',purpose='industrial freight / field service hauler'),
 'Patrol':dict(length=4.6,width=1.75,height=1.55,paint='Vehicle_Patrol',purpose='armored district security / survey patrol'),
 'JetBike':dict(length=2.5,width=.95,height=1.05,paint='Vehicle_Burgundy',purpose='open tandem-seat hover bike'),
}

def loft_x(g,sections,mat,bone,sides=8):
    # section = (X, half width Y, lower Z, upper Z)
    return g.rings([(x,0,(lo+hi)/2) for x,w,lo,hi in sections],[((hi-lo)/2,w) for x,w,lo,hi in sections],mat,bone,sides,True,(1,0,0))

for kind,p in VEHICLE_TYPES.items():
    L=p['length'];W=p['width'];H=p['height'];z=.18;paint=p['paint'];bike=kind=='JetBike'
    col=collection('VehicleParts_'+kind)
    body_z=z+H*.33
    specs=[('root',(0,0,0),(0,0,.12),None),('chassis',(0,0,body_z),(.15,0,body_z),'root'),
      ('nose',(.20*L,0,body_z),(.40*L,0,body_z),'chassis'),
      ('canopy',(-.15*L,0,z+H*.56),(.10*L,0,z+H*.56),'chassis'),
      ('cargo',(-.41*L,0,z+H*({'JetBike':.72,'Coupe':.62,'Patrol':.65}.get(kind,.86))),(-.41*L,.12,z+H*({'JetBike':.72,'Coupe':.62,'Patrol':.65}.get(kind,.86))),'chassis'),
      ('turret_yaw',(-.12*L,0,z+H*.82),(-.12*L,0,z+H*.95),'chassis'),
      ('turret_pitch',(-.12*L,0,z+H*.99),(.08*L,0,z+H*.99),'turret_yaw'),
      ('muzzle',(.15*L,0,z+H*.99),(.23*L,0,z+H*.99),'turret_pitch'),
      ('driver_socket',(.03*L,0,z+H*.45),(.03*L,0,z+H*.58),'chassis'),
      ('passenger_socket',(-.19*L,0,z+H*.45),(-.19*L,0,z+H*.58),'chassis')]
    for suf,s in [('L',1),('R',-1)]:
        specs += [(f'frontpod.{suf}',(.27*L,s*.38*W,z+H*.19),(.40*L,s*.38*W,z+H*.19),'chassis'),
                  (f'rearpod.{suf}',(-.27*L,s*.38*W,z+H*.19),(-.40*L,s*.38*W,z+H*.19),'chassis'),
                  (f'engine.{suf}',(-.37*L,s*.29*W,z+H*.33),(-.47*L,s*.29*W,z+H*.33),'chassis')]
    rig=armature('VEHICLE_'+kind,specs,col)
    rig['forward']='+X';rig['vehicle_type']=kind
    info={'rig':rig,'parameters':p,'parts':[],'slots':{},'defaults':[],'alternate':[]}
    def part(slot,label,g,default=True):
        if slot=='payload':g.w=['chassis' if w=='cargo' else w for w in g.w]
        obj=g.make('Vehicle_'+kind+'_'+label,col,rig,slot,'vehicle.'+kind)
        obj['vehicle_type']=kind;obj['forward']='+X';obj['attachment_slot']=slot
        ASSETS[-1].update(vehicle_type=kind,forward='+X',attachment_slot=slot)
        info['parts'].append(obj.name);info['slots'].setdefault(slot,[]).append(obj.name)
        if default:info['defaults'].append(obj.name)
        return obj
    halfwidth=W*(.22 if bike else .35)
    g=loft_x(Geo(),[(-.43*L,halfwidth*.75,z,z+H*.36),(-.27*L,halfwidth,z,z+H*.49),(.20*L,halfwidth,z+.04,z+H*.53),(.35*L,halfwidth*.67,z+.09,z+H*.38)],paint,'chassis')
    g.box((-.05*L,0,z+.055),(.57*L,halfwidth*1.85,.11),'Steel','chassis')
    part('chassis','Chassis',g)
    g=loft_x(Geo(),[(.17*L,halfwidth*.91,z+H*.22,z+H*.54),(.38*L,halfwidth*.74,z+H*.26,z+H*.43),(.50*L,halfwidth*.32,z+H*.29,z+H*.36)],paint,'nose')
    for s in [-1,1]:g.box((.405*L,s*halfwidth*.56,z+H*.39),(.14*L,.065,.045),'Glow_Cyan','nose')
    part('nose','Nose_Sport',g)
    g=loft_x(Geo(),[(.17*L,halfwidth*.98,z+H*.20,z+H*.54),(.40*L,halfwidth*.93,z+H*.18,z+H*.45),(.50*L,halfwidth*.75,z+H*.19,z+H*.37)],'Steel' if kind=='Patrol' else paint,'nose')
    g.box((.472*L,0,z+H*.25),(.045*L,halfwidth*1.85,.12),'Rubber','nose')
    for s in [-1,1]:
        g.rod((.48*L,s*halfwidth*.60,z+H*.39),(.50*L,s*halfwidth*.60,z+H*.39),.047,.047,'Glow_Amber','nose',8)
        g.box((.45*L,s*halfwidth*.84,z+H*.30),(.08*L,.065,.08),'Warning','nose')
    part('nose','Nose_Utility',g,False)

    if bike:
        g=loft_x(Geo(),[(.12*L,.10*W,z+H*.54,z+H*.60),(.26*L,.14*W,z+H*.62,z+H*.96),(.30*L,.08*W,z+H*.66,z+H*.83)],'Glass_Opaque','canopy')
        g.rod((.12*L,-.22*W,z+H*.68),(.12*L,.22*W,z+H*.68),.027,.027,'Steel','canopy',8)
        part('canopy','Canopy_Windscreen',g)
        g=loft_x(Geo(),[(.12*L,.17*W,z+H*.58,z+H*.65),(.23*L,.20*W,z+H*.65,z+H*.99),(.30*L,.11*W,z+H*.70,z+H*.88)],'Glass_Opaque','canopy')
        g.rod((.12*L,-.25*W,z+H*.68),(.12*L,.25*W,z+H*.68),.027,.027,'Alloy_Light','canopy',8)
        part('canopy','Canopy_RallyScreen',g,False)
        part('seats','Seat_Tandem',Geo().box((-.10*L,0,z+H*.56),(.44*L,.31*W,.10),'Rubber','chassis').box((-.31*L,0,z+H*.64),(.07*L,.31*W,.16),'Rubber','chassis'))
    else:
        rear=-.22*L if kind!='Hauler' else .05*L
        front=.22*L if kind!='Hauler' else .35*L
        g=loft_x(Geo(),[(rear,halfwidth*.85,z+H*.50,z+H*.64),(rear+.08*L,halfwidth*.88,z+H*.56,z+H*.96),(front-.07*L,halfwidth*.75,z+H*.56,z+H),(front,halfwidth*.57,z+H*.54,z+H*.63)],'Glass_Opaque','canopy',10)
        g.box(((front+rear)/2,0,z+H*.96),((front-rear)*.55,.055,.03),'Alloy_Light','canopy')
        part('canopy','Canopy_Panoramic',g)
        g=loft_x(Geo(),[(rear,halfwidth*.9,z+H*.49,z+H*.67),(rear+.045*L,halfwidth*.88,z+H*.52,z+H*.93),(front-.06*L,halfwidth*.79,z+H*.52,z+H*.93),(front,halfwidth*.72,z+H*.50,z+H*.64)],paint,'canopy')
        g.box((front-.045*L,0,z+H*.79),(.032*L,halfwidth*1.42,H*.20),'Glass_Opaque','canopy')
        for s in [-1,1]:g.box(((rear+front)/2,s*halfwidth*.88,z+H*.78),((front-rear)*.62,.024,H*.18),'Glass_Opaque','canopy')
        part('canopy','Canopy_Armored',g,False)
        g=Geo()
        for s in [-1,1]:
            g.box((.015*L,s*W*.16,z+H*.48),(.17*L,W*.22,H*.12),'Rubber','chassis')
            g.box((-.065*L,s*W*.16,z+H*.60),(.045*L,W*.22,H*.26),'Rubber','chassis')
        part('seats','Seat_Cockpit',g)

    for suf,s in [('L',1),('R',-1)]:
        for area,xsign,bone in [('Front',1,'frontpod.'+suf),('Rear',-1,'rearpod.'+suf)]:
            x=xsign*.27*L;y=s*.38*W
            for version in ['Standard','Rugged']:
                rr=W*(.092 if version=='Standard' else .105)
                g=Geo().rings([(x-.12*L,y,z+H*.19),(x+.12*L,y,z+H*.19)],[(H*.105,rr),(H*.085,rr*.78)],paint if version=='Standard' else 'Steel',bone,8,True,(1,0,0))
                g.rod((x-.09*L,y,z+H*.075),(x+.09*L,y,z+H*.075),rr*.70,rr*.70,'Glow_Cyan',bone,8)
                if version=='Rugged':
                    for d in [-.055,.045]:g.box((x+d*L,y,z+H*.28),(.032*L,rr*1.9,.055),'Warning',bone)
                part(f'pod.{area.lower()}.{suf}',f'Pod_{area}_{version}_{suf}',g,version=='Standard')
        bone='engine.'+suf;x=-.40*L;y=s*.29*W
        for version in ['Efficient','Boost']:
            g=Geo().rod((x+.05*L,y,z+H*.34),(-.50*L,y,z+H*.34),W*.082,W*(.095 if version=='Boost' else .075),'Steel',bone,10)
            g.rod((-.485*L,y,z+H*.34),(-.497*L,y,z+H*.34),W*.065,W*.062,'Glow_Amber' if version=='Boost' else 'Glow_Cyan',bone,10)
            if version=='Boost':
                for j in range(3):g.box((x+.028*L,y,z+H*.27+j*H*.07),(.10*L,W*.19,.025),'Copper',bone)
            part('engine.'+suf,'Engine_'+version+'_'+suf,g,version=='Efficient')

    if kind=='Hauler':
        g=Geo().box((-.20*L,0,z+H*.67),(.40*L,.67*W,H*.65),paint,'cargo')
        for j in range(4):g.box((-.36*L+j*.10*L,0,z+H*.99),(.025*L,.70*W,.04),'Steel','cargo')
        for s in [-1,1]:g.box((-.20*L,s*.342*W,z+H*.65),(.32*L,.025,H*.28),'Alloy_Light','cargo')
        part('payload','Payload_Container',g)
        g=Geo().box((-.20*L,0,z+H*.48),(.43*L,.70*W,.12),'Steel','cargo')
        for s in [-1,1]:g.box((-.20*L,s*.34*W,z+H*.63),(.43*L,.045,H*.28),'Warning','cargo')
        g.box((-.20*L,0,z+H*.62),(.26*L,.43*W,.25),'Habitat_Panels','cargo')
        part('payload','Payload_Flatbed',g,False)
    elif kind=='Shuttle':
        g=Geo().box((-.29*L,0,z+H*.63),(.22*L,.69*W,H*.64),paint,'cargo')
        for s in [-1,1]:g.box((-.29*L,s*.352*W,z+H*.74),(.17*L,.022,H*.19),'Glass_Opaque','cargo')
        part('payload','Payload_Passenger',g)
        g=Geo().box((-.29*L,0,z+H*.63),(.22*L,.69*W,H*.64),'Ceramic','cargo')
        for s in [-1,1]:
            g.box((-.29*L,s*.355*W,z+H*.74),(.095*L,.022,H*.04),'Cloth_Burgundy','cargo')
            g.box((-.29*L,s*.356*W,z+H*.74),(.025*L,.022,H*.16),'Cloth_Burgundy','cargo')
        part('payload','Payload_Ambulance',g,False)
    elif kind=='Patrol':
        g=Geo().box((-.12*L,0,z+H*.97),(.25*L,.11*W,.10),'Steel','turret_yaw')
        for s in [-1,1]:g.box((-.12*L,s*.16*W,z+H*.97),(.10*L,.17*W,.08),'Glow_Amber' if s>0 else 'Glow_Cyan','turret_yaw')
        part('payload','Payload_Lightbar',g)
        g=Geo().rod((-.12*L,0,z+H*.86),(-.12*L,0,z+H*.96),.18,.16,'Steel','turret_yaw',8)
        g.box((-.12*L,0,z+H*.99),(.18*L,.26*W,.14),'Steel','turret_yaw')
        g.rod((-.08*L,0,z+H*.99),(.15*L,0,z+H*.99),.06,.035,'Steel','turret_pitch',8)
        for x in [-.015,.035,.085]:g.rod((x*L,0,z+H*.99),((x+.018)*L,0,z+H*.99),.069,.069,'Copper','turret_pitch',8)
        part('payload','Payload_CoilTurret',g,False)
    elif bike:
        g=Geo().box((-.30*L,0,z+H*.57),(.19*L,.39*W,H*.23),paint,'cargo').box((-.30*L,0,z+H*.72),(.20*L,.42*W,.04),'Rubber','cargo')
        part('payload','Payload_StowBox',g)
        g=Geo()
        for s in [-1,1]:g.box((-.25*L,s*.25*W,z+H*.57),(.23*L,.19*W,H*.31),'Cloth_Indigo','cargo')
        part('payload','Payload_Panniers',g,False)
        for suf,s in [('L',1),('R',-1)]:
            part('footrest.'+suf,'Footrest_'+suf,Geo().rod((-.04*L,s*.12*W,z+H*.25),(-.04*L,s*.40*W,z+H*.25),.025,.025,'Steel','chassis',6))
    else:
        g=Geo().box((-.31*L,0,z+H*.51),(.22*L,.61*W,H*.18),paint,'cargo').box((-.31*L,0,z+H*.62),(.18*L,.55*W,.025),'Alloy_Light','cargo')
        part('payload','Payload_RearDeck',g)
        g=Geo().box((-.31*L,0,z+H*.57),(.22*L,.61*W,H*.31),'Steel','cargo')
        g.box((-.31*L,0,z+H*.75),(.18*L,.52*W,.04),'Warning','cargo')
        part('payload','Payload_UtilityBox',g,False)
    if kind in ['Hauler','Shuttle']:
        hatch=Geo().box((-.413*L,0,z+H*.64),(.025*L,.55*W,.42*H),'Steel','cargo')
        hatch.box((-.429*L,0,z+H*.65),(.014*L,.10*W,.045),'Glow_Amber','cargo')
    else:
        hz={'JetBike':.72,'Coupe':.62,'Patrol':.65}[kind]
        hatch=Geo().box((-.31*L,0,z+H*hz),(.19*L,W*(.38 if bike else .50),.035),'Steel','cargo')
        hatch.box((-.30*L,0,z+H*hz+.022),(.06*L,.055,.018),'Glow_Cyan','cargo')
    part('service_hatch','Service_Hatch',hatch)
    # Reusable within-type side rails and underside stabilizer.
    for suf,s in [('L',1),('R',-1)]:
        g=Geo().rod((-.37*L,s*.32*W,z+H*.40),(.30*L,s*.32*W,z+H*.40),.025,.025,'Alloy_Light','chassis',6)
        part('rail.'+suf,'Rail_'+suf,g)
    part('underbody','Underbody_FieldCoil',Geo().rod((0,0,z+.025),(0,0,z+.06),W*.21,W*.21,'Glow_Cyan','chassis',10))
    # An alternate assembly swaps one choice in each meaningful slot.
    for slot,choices in info['slots'].items():
        info['alternate'].append(choices[-1] if len(choices)>1 else choices[0])
    VEHICLES[kind]=info

def animate_vehicle(kind,info):
    rig=info['rig'];L=info['parameters']['length'];H=info['parameters']['height'];rig.animation_data_create()
    specs=[('Vehicle_Reference',1,False),('Hover_Idle',60,True),('Cruise',30,True),('Boost',30,True),
      ('Steer_Left',30,False),('Steer_Right',30,False),('Canopy_Open',30,False),('Canopy_Close',30,False),
      ('Cargo_Open',30,False),('Cargo_Close',30,False)]
    if kind=='Patrol':specs.append(('Vehicle_Fire',12,False))
    CLIPS[rig.name]=[]
    for clip,frames,loop in specs:
        action=bpy.data.actions.new(rig.name+'__'+clip);action.use_fake_user=True;rig.animation_data.action=action
        for frame in range(frames+1):
            t=frame/frames;q=math.tau*t;reset_pose(rig)
            if clip in ['Hover_Idle','Cruise','Boost']:
                world_offset(rig,'chassis',(0,0,(.018 if clip=='Hover_Idle' else .010)*math.sin(q)))
                for suf,s in [('L',1),('R',-1)]:
                    set_rotation(rig,'engine.'+suf,(0,1,0),(.045 if clip=='Boost' else .015)*math.sin(q+s*.3))
                    set_rotation(rig,'frontpod.'+suf,(1,0,0),.025*math.sin(q+s*.4))
            if clip.startswith('Steer_'):
                angle=(1 if clip=='Steer_Left' else -1)*.35*math.sin(math.pi*t)
                for suf in ['L','R']:
                    set_rotation(rig,'frontpod.'+suf,(0,0,1),angle)
                    set_rotation(rig,'engine.'+suf,(0,0,1),angle*.35)
            if clip.startswith('Canopy_'):
                amount=ease(t) if clip.endswith('Open') else 1-ease(t)
                set_rotation(rig,'canopy',(0,1,0),-.85*amount)
            if clip.startswith('Cargo_'):
                amount=ease(t) if clip.endswith('Open') else 1-ease(t)
                set_rotation(rig,'cargo',(0,1,0),(.75 if kind in ['Hauler','Shuttle'] else -.75)*amount)
            if clip=='Vehicle_Fire':
                recoil=max(0,1-t*4)*math.sin(min(1,t*8)*math.pi/2)
                world_offset(rig,'turret_pitch',(-.075*recoil,0,0))
                set_rotation(rig,'turret_yaw',(0,0,1),.05*math.sin(math.pi*t))
            for bone in rig.pose.bones:
                bone.keyframe_insert('rotation_quaternion',frame=frame);bone.keyframe_insert('location',frame=frame)
        for fc in action.fcurves:
            for key in fc.keyframe_points:key.interpolation='LINEAR'
        track=rig.animation_data.nla_tracks.new();track.name=clip;strip=track.strips.new(clip,0,action);strip.name=clip;track.mute=True
        CLIPS[rig.name].append({'clip':clip,'start':0,'end':frames,'fps':30,'duration_seconds':frames/30,'loop':loop,'root_translation':'in_place','purpose':'skeletal vehicle mechanism'})
    rig.animation_data.action=None;reset_pose(rig)

for kind,info in VEHICLES.items():animate_vehicle(kind,info)
(ROOT/'Export/Vehicles').mkdir(parents=True,exist_ok=True)
(ROOT/'Documentation/vehicle_assemblies.json').write_text(json.dumps({'schema':'erma_modular_vehicles_v1','units':'meters','authoring_forward':'+X','gltf_forward':'+X','authoring_up':'+Z','gltf_up':'+Y','hover_clearance_m':.18,'types':{kind:{'parameters':info['parameters'],'skeleton':info['rig'].name,'slots':info['slots'],'default_parts':info['defaults'],'alternate_parts':info['alternate']} for kind,info in VEHICLES.items()}},indent=2),encoding='utf-8')
print('VEHICLES',len(VEHICLES),'TYPES',sum(len(i['parts']) for i in VEHICLES.values()),'PARTS',flush=True)
