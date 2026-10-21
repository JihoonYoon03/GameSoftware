"""Executed inside build_resource_pack.py's Blender authoring namespace."""
from mathutils import Matrix

# The original three silhouettes remain available as normal-build references.
# New primary library: sex presentation x body mass, all sharing HUMAN_7H.
HC.name='01a_Human_Reference_v1'
for obj in list(HC.objects):
    if obj.type=='MESH' and obj.name.startswith(('Body_Standard_','Body_Slender_','Body_Broad_','Outfit_','Gear_')):
        obj['legacy_reference']=True
        if obj.asset_data:obj.asset_data.description='v1 normal-build reference; see v2 body/outfit library'
        for record in ASSETS:
            if record['id']==obj.name:record['legacy_reference']=True
HC2=collection('01b_Human_Bodies_v2')
OC2=collection('01c_Purpose_Outfits_v2')
AC2=collection('01d_Attachable_Accessories')

BODY_PROFILES={
 'Male_Lean':dict(sex='male',mass='lean',shoulder=.193,rib=.174,waist=.108,hip=.143,front=.093,back=.077,belly=.091,limb=.84,bust=0),
 'Male_Normal':dict(sex='male',mass='normal',shoulder=.226,rib=.204,waist=.139,hip=.165,front=.119,back=.100,belly=.111,limb=1.07,bust=0),
 'Male_Heavy':dict(sex='male',mass='heavy',shoulder=.249,rib=.241,waist=.213,hip=.208,front=.156,back=.122,belly=.209,limb=1.32,bust=0),
 'Female_Lean':dict(sex='female',mass='lean',shoulder=.164,rib=.147,waist=.083,hip=.165,front=.094,back=.073,belly=.083,limb=.77,bust=.037),
 'Female_Normal':dict(sex='female',mass='normal',shoulder=.183,rib=.164,waist=.105,hip=.191,front=.112,back=.087,belly=.100,limb=.91,bust=.052),
 'Female_Heavy':dict(sex='female',mass='heavy',shoulder=.209,rib=.200,waist=.184,hip=.238,front=.148,back=.113,belly=.193,limb=1.22,bust=.064),
}
ROLE_META={
 'Explorer':dict(cloth='Cloth_Teal',shell='Ceramic',design='pressure suit / split pectoral shells / high seal collar / navigation harness'),
 'Engineer':dict(cloth='Cloth_Ochre',shell='Copper',design='asymmetric tool bib / rolled cuffs / diagonal repair harness / utility pockets'),
 'Guard':dict(cloth='Cloth_Indigo',shell='Steel',design='tactical segmented vest / independent shoulder armor / plated forearms and shins'),
 'Medic':dict(cloth='Cloth_Medical',shell='Ceramic',design='flared clinical vest / split apron / rounded purge ports / diagnostic forearm'),
 'Researcher':dict(cloth='Cloth_Burgundy',shell='Alloy_Light',design='long split coat / asymmetric placket / sensor sleeve / sample containers'),
 'Miner':dict(cloth='Cloth_Ochre',shell='Copper',design='external rib cage / impact shoulder caps / heavy work gauntlets / piston braces'),
 'HeavyCombat':dict(cloth='Rubber',shell='Steel',design='bulky powered cuirass / oversized pauldrons / layered leg armor / exoskeleton rails'),
 'Pilot':dict(cloth='Cloth_Teal',shell='Alloy_Light',design='close-fitting flight suit / compact rebreather / hip restraints / flexible limbs'),
 'Civilian':dict(cloth='Cloth_Indigo',shell='Wood_Composite',design='open utility jacket / folded collar / fabric pockets / soft rolled trousers'),
}
ACCESSORY_META={}
OUTFIT_EXTRAS={}

def asset2(g,name,col,slot,profile=None,role=None):
    obj=g.make(name,col,human,slot,'human')
    obj['revision']=2
    record=ASSETS[-1];record['revision']=2
    if profile:
        obj['body_profile']=profile;record['body_profile']=profile
        record.update({k:BODY_PROFILES[profile][k] for k in ['sex','mass']})
    if role:obj['purpose']=role;record['purpose']=role
    return obj

def torso_weights(obj):
    # Cloth below the ribcage blends onto the spine. Rigid add-on plates stay separate.
    chest=obj.vertex_groups.get('chest');spine=obj.vertex_groups.get('spine') or obj.vertex_groups.new(name='spine')
    if not chest:return
    for v in obj.data.vertices:
        if not any(g.group==chest.index for g in v.groups):continue
        z=v.co.z
        if z>=1.28:continue
        w=max(0,min(1,(z-1.08)/.20))
        chest.remove([v.index])
        if w>0:chest.add([v.index],w,'REPLACE')
        if w<1:spine.add([v.index],1-w,'REPLACE')

def body_contour(g,p,mat,pad=0,bone='chest',bottom=1.02):
    # Separate front/back extents produce an abdomen silhouette in side view.
    levels=[(bottom,p['hip']*.89,p['belly']*.92,p['back']*.95),
            (1.12,p['waist'],p['belly'],p['back']),
            (1.25,p['rib']*.90,p['front'],p['back']),
            (1.34,p['rib'],p['front']+p['bust']*.38,p['back']),
            (1.425,p['shoulder'],p['front']*.79,p['back']*.92),
            (1.47,.082,.070,.065)]
    centers=[];radii=[]
    for z,w,front,back in levels:
        centers.append((0,(back-front)/2,z));radii.append((w+pad,(front+back)/2+pad))
    g.rings(centers,radii,mat,bone,10)
    return g

def limb_segment(g,a,b,r1,r2,mat,bone,sides=8):
    # A middle cross section gives a natural bicep/calf rather than a straight rod.
    a=Vector(a);b=Vector(b);axis=b-a
    g.rings([a,a+axis*.34,a+axis*.73,b],[r1,r1*1.04,(r1+r2)*.50,r2],mat,bone,sides,True,axis)
    return g

for build,p in BODY_PROFILES.items():
    sex=p['sex'];heavy=p['mass']=='heavy'
    g=Geo().rings([(0,0,.91),(0,-.005,1.005),(0,(p['back']-p['belly'])/2,1.075)],[(p['hip'],p['back']+.018),(p['hip']*.98,p['belly']*.95),(p['waist']*1.04,p['belly']*.94)],'Cloth_Indigo','pelvis',10)
    asset2(g,f'Body_{build}_Pelvis',HC2,'pelvis',build)
    g=Geo().rings([(0,(p['back']-p['belly'])/2,1.045),(0,(p['back']-p['belly'])/2,1.13),(0,0,1.275)],[(p['hip']*.88,(p['belly']+p['back'])/2),(p['waist'],(p['belly']+p['back'])/2),(p['rib']*.90,(p['front']+p['back'])/2)],'Skin_Warm','spine',10)
    asset2(g,f'Body_{build}_Abdomen',HC2,'abdomen',build)
    g=Geo().rings([(0,0,1.25),(0,-p['bust']*.15,1.335),(0,0,1.425),(0,0,1.47)],[(p['rib']*.89,(p['front']+p['back'])/2),(p['rib'],(p['front']+p['back'])/2),(p['shoulder'],p['back']),(.075,.066)],'Skin_Warm','chest',10)
    if sex=='female':
        for s in [-1,1]:g.ellipsoid((s*p['rib']*.47,-p['front']*.66,1.333),(.070*(p['rib']/.164),p['bust'],.062),'Skin_Warm','chest',8,4)
    asset2(g,f'Body_{build}_Chest',HC2,'chest',build)
    for suf,s in [('L',1),('R',-1)]:
        for label,bone,a,b,r1,r2 in [
          ('UpperArm','upper_arm',(s*.21,0,1.43),(s*.34,0,1.17),.066*p['limb'],.048*p['limb']),
          ('Forearm','forearm',(s*.34,0,1.17),(s*.43,-.015,.94),.050*p['limb'],.032*p['limb']),
          ('Thigh','thigh',(s*.105,0,.96),(s*.115,-.01,.55),.087*p['limb']*(1.08 if sex=='female' else 1),.057*p['limb']),
          ('Shin','shin',(s*.115,-.01,.55),(s*.115,0,.14),.058*p['limb'],.037*p['limb'])]:
            asset2(limb_segment(Geo(),a,b,r1,r2,'Skin_Warm',bone+'.'+suf),f'Body_{build}_{label}_{suf}',HC2,label.lower()+'.'+suf,build)

# Ground is the bottom of the soles; the canonical bare-head height is exactly 1.8m.
for suf in ['L','R']:
    for v in PROTOTYPES['Boot_'+suf].data.vertices:
        if v.index>=8:v.co.z-=.015
    PROTOTYPES['Boot_'+suf].data.update()

def outfit(build,p,role):
    meta=ROLE_META[role];cloth=meta['cloth'];shell=meta['shell'];limb=p['limb']
    bulk={'HeavyCombat':.043,'Miner':.026,'Guard':.025,'Explorer':.017,'Medic':.021,'Researcher':.023,'Engineer':.015,'Pilot':.009,'Civilian':.016}[role]
    g=body_contour(Geo(),p,cloth,bulk)
    front=p['front']+p['bust']*.38+bulk
    if role=='Explorer':
        for s in [-1,1]:
            g.box((s*p['rib']*.48,-front-.012,1.34),(p['rib']*.84,.032,.16),shell,'chest')
            g.rod((s*.11,-front-.038,1.42),(s*.09,-p['belly']-.04,1.08),.016,.016,'Rubber','chest',5)
        g.rod((0,0,1.46),(0,0,1.51),.088,.086,'Steel','chest',8)
    elif role=='Engineer':
        g.box((-.035,-front-.02,1.29),(p['rib']*1.50,.032,.24),shell,'chest')
        g.rod((-.15,-front-.032,1.43),(.12,-p['belly']-.032,1.06),.025,.025,'Rubber','chest',5)
        for x in [-.075,.025,.095]:g.box((x,-p['belly']-.052,1.13),(.06,.065,.08),'Wood_Composite','spine')
    elif role=='Guard':
        g.rings([(0,-front+.013,1.23),(0,-front+.013,1.39)],[(p['rib']*.88,.065),(p['rib']*1.02,.065)],shell,'chest',6)
        for j in range(3):g.box((0,-p['belly']-.025,1.08+j*.048),(p['waist']*1.7,.035,.032),shell,'spine')
    elif role=='Medic':
        g.box((0,-front-.018,1.315),(.13,.045,.18),'Ceramic','chest')
        for s in [-1,1]:g.rod((s*.13,-front-.005,1.38),(s*.13,-front-.035,1.38),.040,.040,'Steel','chest',8)
        g.box((0,-front-.049,1.32),(.075,.012,.023),'Cloth_Burgundy','chest').box((0,-front-.05,1.32),(.023,.012,.075),'Cloth_Burgundy','chest')
        for s in [-1,1]:g.box((s*.10,-p['belly']-.015,.975),(.13,.022,.23),cloth,'thigh.'+('L' if s>0 else 'R'))
    elif role=='Researcher':
        g.box((-.06,-front-.015,1.28),(.025,.025,.34),'Alloy_Light','chest')
        g.rod((0,0,1.455),(0,0,1.525),.079,.069,cloth,'chest',8)
        for suf,s in [('L',1),('R',-1)]:
            g.box((s*.13,-p['belly']+.01,.86),(.17,.028,.40),cloth,'thigh.'+suf)
            g.box((s*.13,p['back']+.014,.86),(.17,.028,.40),cloth,'thigh.'+suf)
    elif role=='Miner':
        for j in range(5):g.box((0,-front-.03,1.18+j*.054),(p['rib']*1.86,.065,.031),shell,'chest')
        g.rod((-.14,-front-.04,1.43),(-.14,-p['belly']-.04,1.04),.021,.021,'Steel','chest',6)
        g.rod((.14,-front-.04,1.43),(.14,-p['belly']-.04,1.04),.021,.021,'Steel','chest',6)
    elif role=='HeavyCombat':
        g.ellipsoid((0,-.045,1.36),(p['rib']+bulk,.17,.18),shell,'chest',8,4)
        for s in [-1,1]:g.box((s*.11,-front-.061,1.31),(.14,.08,.20),'Alloy_Light','chest')
        g.box((0,-front-.10,1.38),(.085,.025,.045),'Glow_Cyan','chest')
        for j in range(3):g.box((0,-p['belly']-.032,1.065+j*.045),(p['waist']*1.65,.06,.029),'Steel','spine')
    elif role=='Pilot':
        g.box((0,-front-.015,1.35),(.12,.035,.12),'Alloy_Light','chest')
        for s in [-1,1]:
            g.rod((s*.085,-front-.027,1.40),(s*.10,-p['belly']-.03,1.12),.013,.013,'Rubber','chest',6)
        g.rod((0,0,1.46),(0,0,1.50),.08,.08,'Rubber','chest',8)
    else:
        for s in [-1,1]:
            g.box((s*.11,-front-.006,1.22),(.095,.018,.12),cloth,'chest')
            g.rod((s*.07,-front-.005,1.45),(s*.13,-front-.005,1.36),.027,.020,cloth,'chest',4)
        g.box((0,-front-.014,1.30),(.016,.012,.28),'Steel','chest')
    torso=asset2(g,f'Outfit_{role}_{build}_Torso',OC2,'outfit.torso',build,role);torso_weights(torso)
    for suf,s in [('L',1),('R',-1)]:
        upper='upper_arm.'+suf;fore='forearm.'+suf;thigh='thigh.'+suf;shin='shin.'+suf
        a=(s*.21,0,1.43);b=(s*.34,0,1.17);c=(s*.43,-.015,.94)
        r=.066*limb+bulk*.65
        g=limb_segment(Geo(),a,b,r,.048*limb+.008,cloth,upper)
        if role in ['HeavyCombat','Miner','Guard']:
            size=.128 if role=='HeavyCombat' else .103 if role=='Miner' else .091
            g.ellipsoid((s*.245,0,1.405),(size,size*.86,size*.85),shell,upper,8,4)
            if role=='HeavyCombat':g.box((s*.29,-.05,1.40),(.17,.15,.11),'Alloy_Light',upper)
        elif role=='Medic':g.ellipsoid((s*.225,0,1.41),(.080,.074,.070),'Ceramic',upper,8,3)
        elif role=='Explorer':g.box((s*.24,-.07,1.36),(.10,.032,.13),shell,upper)
        elif role=='Civilian':g.rod((s*.31,0,1.23),b,.062*limb,.061*limb,cloth,upper,8)
        elif role=='Engineer':g.box((s*.27,-.075,1.31),(.075,.035,.13),'Rubber',upper)
        elif role=='Researcher':g.box((s*.245,-.07,1.34),(.075,.02,.16),'Ceramic',upper)
        asset2(g,f'Outfit_{role}_{build}_Sleeve_{suf}',OC2,'outfit.upperarm.'+suf,build,role)
        g=limb_segment(Geo(),b,c,.051*limb+.008,.034*limb+.006,cloth,fore)
        if role in ['HeavyCombat','Miner']:
            g.rod((s*.36,0,1.12),c,.081*limb,.067*limb,shell,fore,8)
            g.box((s*.399,-.077,1.035),(.12,.075,.16),'Steel',fore)
            if role=='Miner':g.box((s*.40,-.122,1.06),(.10,.02,.028),'Warning',fore)
        elif role=='Guard':g.box((s*.389,-.063,1.05),(.095,.055,.16),'Steel',fore)
        elif role=='Medic':
            g.box((s*.389,-.067,1.08),(.11,.045,.14),'Ceramic',fore)
            g.box((s*.389,-.095,1.09),(.075,.016,.065),'Glow_Cyan',fore)
        elif role=='Researcher':g.rod((s*.39,-.005,1.03),c,.055*limb,.05*limb,'Alloy_Light',fore,8)
        elif role=='Engineer':
            g.rod((s*.39,-.01,1.035),c,.066*limb,.064*limb,'Rubber',fore,8)
            g.box((s*.39,-.073,1.055),(.075,.025,.07),'Warning',fore)
        elif role=='Explorer':g.box((s*.388,-.065,1.08),(.08,.035,.10),'Steel',fore)
        elif role=='Pilot':g.rod((s*.412,-.012,.99),c,.044*limb,.043*limb,'Rubber',fore,8)
        else:g.rod((s*.407,-.011,1.01),c,.052*limb,.051*limb,cloth,fore,8)
        asset2(g,f'Outfit_{role}_{build}_Cuff_{suf}',OC2,'outfit.forearm.'+suf,build,role)
        r1=.087*limb*(1.08 if p['sex']=='female' else 1)+.010;r2=.060*limb+.008
        g=limb_segment(Geo(),(s*.105,0,.96),(s*.115,-.01,.55),r1,r2,cloth,thigh)
        if role=='HeavyCombat':
            g.box((s*.15,-r1*.75,.77),(.17*limb,.075,.26),'Steel',thigh)
            g.rod((s*(.105+r1+.02),0,.93),(s*(.115+r2+.02),0,.57),.019,.019,'Alloy_Light',thigh,6)
        elif role in ['Guard','Miner']:g.box((s*.16,-r1*.55,.78),(.12*limb,.062,.20),shell,thigh)
        elif role=='Engineer':g.box((s*.18,-.01,.79),(.075,.14,.15),'Wood_Composite',thigh)
        elif role=='Pilot':g.rod((s*.105,-r1*.6,.91),(s*.115,-r2*.7,.64),.014,.014,'Steel',thigh,5)
        elif role=='Medic':g.box((s*.16,-.06,.77),(.09,.045,.17),'Cloth_Medical',thigh)
        elif role=='Explorer':g.box((s*.17,0,.80),(.07,.12,.13),'Cloth_Teal',thigh)
        elif role=='Civilian':g.box((s*.15,-.06,.83),(.09,.02,.12),cloth,thigh)
        elif role=='Researcher':g.box((s*.16,-.04,.81),(.085,.03,.12),'Ceramic',thigh)
        asset2(g,f'Outfit_{role}_{build}_Trouser_{suf}',OC2,'outfit.thigh.'+suf,build,role)
        g=limb_segment(Geo(),(s*.115,-.01,.55),(s*.115,0,.14),.060*limb+.008,.038*limb+.006,cloth if role in ['Civilian','Researcher','Medic'] else 'Rubber',shin)
        if role in ['HeavyCombat','Miner','Guard']:
            g.box((s*.115,-.067,.46),(.125*limb,.075,.12),shell,shin)
            g.box((s*.115,-.06,.29),(.11*limb,.055,.20),shell,shin)
            if role in ['HeavyCombat','Miner']:
                for side in [-1,1]:g.rod((s*.115+side*.07*limb,0,.49),(s*.115+side*.07*limb,0,.19),.013,.013,'Alloy_Light',shin,6)
        elif role=='Explorer':g.box((s*.115,-.055,.44),(.1*limb,.04,.13),'Ceramic',shin)
        elif role=='Engineer':g.box((s*.115,-.056,.45),(.10*limb,.042,.09),'Rubber',shin)
        elif role=='Pilot':g.rod((s*.115,0,.23),(s*.115,0,.15),.055*limb,.05*limb,'Alloy_Light',shin,8)
        elif role=='Medic':g.box((s*.115,-.052,.42),(.08*limb,.025,.09),'Ceramic',shin)
        elif role=='Researcher':g.box((s*.115,-.043,.22),(.075*limb,.018,.12),'Cloth_Medical',shin)
        else:g.rod((s*.115,0,.22),(s*.115,0,.15),.053*limb,.052*limb,cloth,shin,8)
        asset2(g,f'Outfit_{role}_{build}_Greave_{suf}',OC2,'outfit.shin.'+suf,build,role)

for build,p in BODY_PROFILES.items():
    for role in ROLE_META:outfit(build,p,role)

SOCKET_DEFINITIONS={
 'chest':dict(bone='chest',position=(0,-.16,1.33),normal=(0,-1,0)),
 'back':dict(bone='back_socket',position=(0,.18,1.30),normal=(0,1,0)),
 'waist_front':dict(bone='pelvis',position=(0,-.15,1.01),normal=(0,-1,0)),
 'hip_L':dict(bone='pelvis',position=(.20,0,.98),normal=(1,0,0)),
 'hip_R':dict(bone='pelvis',position=(-.20,0,.98),normal=(-1,0,0)),
 'shoulder_L':dict(bone='upper_arm.L',position=(.24,0,1.42),normal=(1,0,0)),
 'shoulder_R':dict(bone='upper_arm.R',position=(-.24,0,1.42),normal=(-1,0,0)),
 'forearm_L':dict(bone='forearm.L',position=(.39,-.075,1.07),normal=(0,-1,0)),
 'forearm_R':dict(bone='forearm.R',position=(-.39,-.075,1.07),normal=(0,-1,0)),
 'head':dict(bone='head',position=(0,0,1.74),normal=(0,-1,0)),
 'hand_R':dict(bone='hand.R',position=(-.465,-.03,.87),normal=(0,0,-1)),
}
for name,data in SOCKET_DEFINITIONS.items():
    empty=bpy.data.objects.new('ATTACH_'+name,None);AC2.objects.link(empty)
    empty.empty_display_type='ARROWS';empty.empty_display_size=.075;empty.parent=human
    empty.parent_type='BONE';empty.parent_bone=data['bone'];bpy.context.view_layer.update()
    empty.matrix_world=Matrix.Translation(Vector(data['position']))
    empty['socket']=name;empty['compatible_skeleton']='HUMAN_7H';empty.hide_render=True

def accessory(name,g,slot,description):
    obj=asset2(g,'Accessory_'+name,AC2,'accessory.'+slot)
    obj['attachment_socket']=slot
    ACCESSORY_META[obj.name]={'slot':slot,'bone':SOCKET_DEFINITIONS[slot]['bone'],'description':description}
    return obj

# Independent packs, tools, protection and sensors. These are separate mesh objects.
g=Geo().box((0,.205,1.30),(.24,.13,.30),'Steel','back_socket')
for s in [-1,1]:g.rod((s*.082,.24,1.18),(s*.082,.24,1.46),.053,.053,'Ceramic','back_socket',8)
g.box((0,.285,1.31),(.035,.02,.20),'Glow_Cyan','back_socket')
accessory('LifeSupport_TwinTank',g,'back','paired breathable-gas cylinders / EVA and mining')
g=Geo().box((0,.23,1.29),(.33,.16,.34),'Steel','back_socket')
for s in [-1,1]:
    g.rod((s*.17,.25,1.15),(s*.17,.25,1.45),.065,.065,'Copper','back_socket',8)
    g.rod((s*.17,.25,1.13),(s*.17,.25,1.18),.07,.07,'Glow_Amber','back_socket',8)
g.box((0,.325,1.30),(.15,.04,.13),'Warning','back_socket')
accessory('Mining_PowerRig',g,'back','external load-bearing industrial power rig')
g=Geo().box((0,.23,1.30),(.36,.19,.35),'Steel','back_socket')
for j in range(5):g.box((0,.34,1.17+j*.055),(.28,.035,.025),'Alloy_Light','back_socket')
for s in [-1,1]:g.rod((s*.13,.23,1.46),(s*.13,.23,1.54),.055,.055,'Copper','back_socket',8)
accessory('Combat_Reactor',g,'back','armored reactor and heat-exchange fins')
g=Geo().box((0,.215,1.28),(.28,.14,.30),'Cloth_Medical','back_socket')
for s in [-1,1]:g.rod((s*.075,.30,1.18),(s*.075,.30,1.39),.032,.032,'Glow_Cyan','back_socket',8)
g.box((0,.302,1.44),(.16,.035,.045),'Cloth_Burgundy','back_socket')
accessory('Medic_RescuePack',g,'back','oxygen / sterile supply / diagnostic cells')
g=Geo().box((0,.205,1.28),(.27,.13,.27),'Cloth_Burgundy','back_socket')
for i in range(3):g.rod((-.085+i*.085,.285,1.18),(-.085+i*.085,.285,1.39),.027,.027,'Ceramic','back_socket',6)
g.rod((.11,.21,1.40),(.11,.21,1.66),.008,.005,'Steel','back_socket',6)
accessory('Research_SampleRack',g,'back','sealed sample cylinders and relay aerial')
g=Geo().box((0,.19,1.27),(.22,.10,.23),'Cloth_Teal','back_socket').box((0,.25,1.28),(.14,.02,.06),'Alloy_Light','back_socket')
accessory('Pilot_CompactPack',g,'back','low-profile flight life-support module')
g=Geo().box((0,.20,1.26),(.26,.14,.28),'Cloth_Indigo','back_socket').box((0,.28,1.25),(.19,.025,.13),'Wood_Composite','back_socket')
accessory('Civilian_Rucksack',g,'back','soft utility backpack')
g=Geo().box((0,-.173,1.33),(.15,.045,.16),'Steel','chest').box((0,-.203,1.35),(.11,.017,.075),'Glow_Cyan','chest')
accessory('Navigation_Console',g,'chest','detachable chest navigation display')
g=Geo().box((.055,-.173,1.32),(.12,.045,.20),'Ceramic','chest')
for j in range(3):g.rod((.032+j*.027,-.205,1.26),(.032+j*.027,-.205,1.39),.009,.009,'Glow_Cyan','chest',6)
accessory('Medic_CartridgeRack',g,'chest','independent sterile injection cartridge tray')
g=Geo().rod((0,-.185,1.26),(0,-.245,1.26),.069,.065,'Steel','chest',8).rod((0,-.247,1.26),(0,-.263,1.26),.05,.05,'Glow_Amber','chest',8)
accessory('Mining_Respirator',g,'chest','external industrial breathing filter')
g=Geo().box((0,-.178,1.32),(.26,.06,.21),'Steel','chest')
for j in range(4):g.box((0,-.215,1.25+j*.042),(.23,.025,.024),'Alloy_Light','chest')
accessory('Ablative_RibPlate',g,'chest','detachable layered impact breastplate')
for suf,s in [('L',1),('R',-1)]:
    slot='shoulder_'+suf;bone='upper_arm.'+suf
    g=Geo().ellipsoid((s*.25,0,1.43),(.105,.09,.085),'Steel',bone,8,4)
    g.box((s*.29,-.05,1.43),(.10,.08,.07),'Warning',bone)
    accessory('Shoulder_Tactical_'+suf,g,slot,'independent tactical shoulder cap')
    g=Geo().ellipsoid((s*.28,0,1.43),(.145,.13,.13),'Steel',bone,8,4)
    for j in range(3):g.box((s*.31,-.07,1.37+j*.06),(.16,.12,.035),'Alloy_Light',bone)
    accessory('Shoulder_Heavy_'+suf,g,slot,'oversized layered powered-armor shoulder cap')
    g=Geo().rod((s*.255,-.02,1.44),(s*.255,-.17,1.44),.045,.04,'Steel',bone,8).rod((s*.255,-.17,1.44),(s*.255,-.19,1.44),.032,.032,'Glow_Amber',bone,8)
    accessory('Work_Lamp_'+suf,g,slot,'independent shoulder lamp')
    bone='forearm.'+suf;slot='forearm_'+suf
    accessory('Wrist_Scanner_'+suf,Geo().box((s*.39,-.085,1.07),(.10,.055,.13),'Steel',bone).box((s*.39,-.118,1.08),(.078,.016,.075),'Glow_Cyan',bone),slot,'detachable navigation and material scanner')
    g=Geo().box((s*.39,-.082,1.06),(.095,.052,.14),'Ceramic',bone).rod((s*.39,-.115,1.06),(s*.39,-.115,.98),.014,.008,'Alloy_Light',bone,6)
    accessory('Medical_Injector_'+suf,g,slot,'forearm treatment tool')
    bone='pelvis';slot='hip_'+suf
    accessory('Utility_Pouch_'+suf,Geo().box((s*.21,-.01,.98),(.10,.15,.14),'Cloth_Ochre',bone).box((s*.21,-.094,1.00),(.08,.02,.045),'Steel',bone),slot,'belt utility pouch')
    g=Geo().box((s*.215,-.01,.96),(.09,.15,.15),'Cloth_Medical',bone).box((s*.215,-.093,.97),(.05,.015,.015),'Cloth_Burgundy',bone).box((s*.215,-.094,.97),(.015,.015,.05),'Cloth_Burgundy',bone)
    accessory('Medic_Pouch_'+suf,g,slot,'sterile emergency pouch')
    g=Geo().box((s*.22,.005,.97),(.075,.14,.17),'Rubber',bone)
    for j in range(3):g.rod((s*.22,-.05+j*.045,.96),(s*.22,-.05+j*.045,1.12),.009,.009,'Alloy_Light',bone,6)
    accessory('Tool_Roll_'+suf,g,slot,'visible spanners and repair tool roll')
accessory('Belt_LifeSupport',Geo().box((0,-.17,1.01),(.24,.075,.09),'Steel','pelvis').box((0,-.215,1.01),(.10,.02,.04),'Glow_Cyan','pelvis'),'waist_front','independent pressure-suit belt console')
accessory('Goggles',Geo().box((0,-.105,1.695),(.20,.04,.055),'Steel','head').box((-.052,-.129,1.697),(.075,.015,.032),'Glass_Opaque','head').box((.052,-.129,1.697),(.075,.015,.032),'Glass_Opaque','head'),'head','separate protective goggles')
g=Geo().rings([(0,0,1.74),(0,0,1.80),(0,0,1.84)],[(.125,.116),(.115,.109),(.04,.04)],'Warning','head',10).box((0,-.11,1.748),(.235,.10,.025),'Steel','head')
g.rod((0,-.128,1.79),(0,-.174,1.79),.033,.033,'Glow_Amber','head',8)
accessory('Mining_Hardhat',g,'head','reinforced mining helmet with work light')
g=Geo().rings([(0,0,1.565),(0,0,1.70),(0,0,1.80),(0,0,1.84)],[(.098,.11),(.128,.124),(.12,.112),(.04,.04)],'Ceramic','head',10)
g.box((0,-.121,1.70),(.20,.028,.085),'Glass_Opaque','head').box((0,-.14,1.72),(.17,.012,.012),'Glow_Cyan','head')
g.rod((0,0,1.55),(0,0,1.58),.11,.108,'Steel','head',10)
accessory('EVA_Helmet',g,'head','sealed rounded pressure helmet / hides hair')
g=Geo().box((0,0,1.72),(.24,.23,.23),'Steel','head').box((0,-.13,1.73),(.18,.025,.055),'Glass_Opaque','head').box((0,-.11,1.625),(.16,.07,.06),'Steel','head')
accessory('Combat_Helmet',g,'head','compact tactical visor and armored jaw')
g=Geo().rings([(0,0,1.57),(0,0,1.71),(0,0,1.84)],[(.12,.12),(.145,.14),(.07,.08)],'Steel','head',8)
g.box((0,-.139,1.72),(.19,.03,.052),'Glow_Amber','head').box((0,-.11,1.61),(.19,.09,.07),'Alloy_Light','head')
accessory('Heavy_Helmet',g,'head','wide armored helmet for the powered suit')
accessory('Research_Visor',Geo().box((0,-.125,1.70),(.22,.025,.10),'Glass_Opaque','head').box((-.12,-.02,1.70),(.035,.16,.05),'Ceramic','head').box((.12,-.02,1.70),(.035,.16,.05),'Ceramic','head'),'head','detachable panoramic research face shield')

ROLE_ACCESSORIES={
 'Explorer':['LifeSupport_TwinTank','Navigation_Console','Belt_LifeSupport','Wrist_Scanner_L','Utility_Pouch_R'],
 'Engineer':['Mining_PowerRig','Tool_Roll_L','Utility_Pouch_R','Work_Lamp_L','Goggles'],
 'Guard':['Combat_Reactor','Ablative_RibPlate','Shoulder_Tactical_L','Shoulder_Tactical_R','Utility_Pouch_R'],
 'Medic':['Medic_RescuePack','Medic_CartridgeRack','Medical_Injector_L','Medic_Pouch_L','Medic_Pouch_R'],
 'Researcher':['Research_SampleRack','Research_Visor','Wrist_Scanner_R','Utility_Pouch_L'],
 'Miner':['Mining_PowerRig','Mining_Respirator','Mining_Hardhat','Work_Lamp_R','Tool_Roll_L'],
 'HeavyCombat':['Combat_Reactor','Shoulder_Heavy_L','Shoulder_Heavy_R','Heavy_Helmet','Utility_Pouch_L'],
 'Pilot':['Pilot_CompactPack','EVA_Helmet','Belt_LifeSupport','Wrist_Scanner_L'],
 'Civilian':['Civilian_Rucksack','Utility_Pouch_L'],
}

def accessory_offset(name,build,role):
    p=BODY_PROFILES[build];slot=ACCESSORY_META[name]['slot']
    bulk={'HeavyCombat':.043,'Miner':.026,'Guard':.025,'Explorer':.017,'Medic':.021,'Researcher':.023,'Engineer':.015,'Pilot':.009,'Civilian':.016}[role]
    if slot=='chest':return (0,-(p['front']+p['bust']*.38+bulk-.125),0)
    if slot=='back':return (0,p['back']-.10+bulk,0)
    if slot=='waist_front':return (0,-max(0,p['belly']-.11),0)
    if slot.startswith('hip_'):return ((1 if slot.endswith('L') else -1)*max(0,p['hip']-.165),0,0)
    if slot.startswith('shoulder_'):return ((1 if slot.endswith('L') else -1)*max(0,.066*(p['limb']-1)),0,0)
    if slot.startswith('forearm_'):return (0,-max(0,.05*(p['limb']-1)),0)
    return (0,0,0)

(ROOT/'Documentation/body_profiles.json').write_text(json.dumps(BODY_PROFILES,indent=2),encoding='utf-8')
(ROOT/'Documentation/accessory_sockets.json').write_text(json.dumps({'schema':'human_accessories_v2','skeleton':'HUMAN_7H','mesh_space':'shared bind pose; full-rig coordinates','sockets':SOCKET_DEFINITIONS,'accessories':ACCESSORY_META,'default_sets':ROLE_ACCESSORIES,'profiles':BODY_PROFILES},indent=2),encoding='utf-8')
(ROOT/'Documentation/accessory_fitting_offsets.json').write_text(json.dumps({build:{role:{name:accessory_offset(name,build,role) for name in ACCESSORY_META} for role in ROLE_META} for build in BODY_PROFILES},indent=2),encoding='utf-8')
print('HUMAN_V2',len(BODY_PROFILES),'BODY TYPES',len(ROLE_META),'OUTFIT PURPOSES',len(ACCESSORY_META),'ACCESSORIES',flush=True)
