"""Editable motion design. Pure FK/analytic IK is baked to the original deform bones.

Coordinates are HUMAN_7H metres: forward -Y, left +X, up +Z. No controls,
constraints or runtime solvers are required by the exported clips.
"""
import math
from mathutils import Matrix, Quaternion, Vector

PI=math.pi
TAU=math.tau
def clamp(x):return max(0.,min(1.,x))
def smooth(x):
    x=clamp(x);return x*x*(3-2*x)
def smoother(x):
    x=clamp(x);return x*x*x*(x*(x*6-15)+10)
def pulse(t,a,b,c):
    return smooth((t-a)/(b-a)) if t<b else 1-smooth((t-b)/(c-b))
def Q(axis,angle):return Quaternion(Vector(axis),angle)
def rx(a):return Q((1,0,0),a)
def ry(a):return Q((0,1,0),a)
def rz(a):return Q((0,0,1),a)

class Pose:
    """Parent-aware FK: arm/leg IK remains exact under pelvis and spine rotation."""
    def __init__(self,rig):
        self.rig=rig;self.rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
        self.parent={b.name:b.parent.name if b.parent else None for b in rig.data.bones}
        self.basis={n:Matrix.Identity(4) for n in self.rest};self.cache={}
    def fk(self,n):
        if n not in self.cache:
            p=self.parent[n]
            base=self.fk(p)@self.rest[p].inverted()@self.rest[n] if p else self.rest[n]
            self.cache[n]=base@self.basis[n]
        return self.cache[n]
    def base(self,n):
        p=self.parent[n]
        return self.fk(p)@self.rest[p].inverted()@self.rest[n] if p else self.rest[n].copy()
    def set_matrix(self,n,m):
        self.basis[n]=self.base(n).inverted()@m;self.cache.clear()
    def absolute(self,n,delta,position=None):
        m=(delta@self.rest[n].to_quaternion()).to_matrix().to_4x4()
        m.translation=Vector(position) if position is not None else self.base(n).translation
        self.set_matrix(n,m)
    def relative(self,n,delta):
        r=self.rest[n].to_quaternion()
        location=self.basis[n].translation.copy()
        self.basis[n]=(r.inverted()@delta@r).to_matrix().to_4x4()
        self.basis[n].translation=location;self.cache.clear()
    def translate(self,n,offset):
        m=self.fk(n).copy();m.translation+=Vector(offset);self.set_matrix(n,m)
    def deformation(self,n):return self.fk(n)@self.rest[n].inverted()
    def point(self,n,point):return self.deformation(n)@Vector(point)
    def chain(self,u,l,e,target,pole,end_delta=None):
        hip=self.base(u).translation;target=Vector(target)
        direction=target-hip;distance=direction.length;direction.normalize()
        a=self.rig.data.bones[u].length;b=self.rig.data.bones[l].length
        distance=max(abs(a-b)+.00001,min(a+b-.00001,distance))
        side=Vector(pole)-direction*Vector(pole).dot(direction)
        if side.length<1e-6:side=direction.cross(Vector((1,0,0)))
        side.normalize();along=(a*a-b*b+distance*distance)/(2*distance)
        knee=hip+direction*along+side*math.sqrt(max(0,a*a-along*along))
        ankle=hip+direction*distance
        hinge_axis=side.cross(direction).normalized()
        for n,start,end in [(u,hip,knee),(l,knee,ankle)]:
            bone=self.rig.data.bones[n]
            if u.startswith('thigh.'):
                # A stable knee-plane frame avoids arbitrary 180-degree axial
                # flips when a shin points opposite its bind direction in a fall.
                y=(end-start).normalized();x=hinge_axis;z=x.cross(y).normalized()
                orientation=Matrix((x,y,z)).transposed().to_quaternion()
                delta=orientation@self.rest[n].to_quaternion().inverted()
            else:delta=(bone.tail_local-bone.head_local).normalized().rotation_difference((end-start).normalized())
            self.absolute(n,delta)
        self.absolute(e,end_delta or Quaternion())
    def apply(self):
        for p in self.rig.pose.bones:
            p.rotation_mode='QUATERNION';p.matrix_basis=self.basis[p.name]

def blend(a,b,t):
    if t<=0:return a
    if t>=1:return b
    result=Pose(a.rig);w=smooth(t)
    for n in result.basis:
        la,qa,sa=a.basis[n].decompose();lb,qb,sb=b.basis[n].decompose()
        result.basis[n]=Matrix.LocRotScale(la.lerp(lb,w),qa.slerp(qb,w),sa.lerp(sb,w))
    return result

def feet(p,targets=None,deltas=None,poles=None):
    for suffix,s in [('L',1),('R',-1)]:
        target=targets[suffix] if targets else (s*.115,0,.14)
        p.chain('thigh.'+suffix,'shin.'+suffix,'foot.'+suffix,target,
                poles[suffix] if poles else (0,-1,0),deltas[suffix] if deltas else Quaternion())

def arms(p,targets=None,deltas=None,poles=None):
    for suffix,s in [('L',1),('R',-1)]:
        target=targets[suffix] if targets else (s*.27,.005,.94)
        p.chain('upper_arm.'+suffix,'forearm.'+suffix,'hand.'+suffix,target,
                poles[suffix] if poles else (s*.06,1,-.15),
                deltas[suffix] if deltas else rz(-s*.15))

def standing(rig,t=0,breath=True):
    p=Pose(rig);wave=math.sin(TAU*t) if breath else 0
    p.translate('pelvis',(.006*wave,0,-.020))
    p.relative('spine',rx(.018+.005*wave));p.relative('chest',rx(-.012+.008*wave))
    p.relative('neck',rz(.018*wave));p.relative('head',ry(.012*wave))
    arms(p);feet(p)
    return p

def aim(p,recoil=0,torso=0,offset=(0,0,0)):
    p.relative('spine',rx(.025+torso));p.relative('chest',rx(-.018-.035*recoil))
    # Both wrist targets follow a single rigid weapon transform, maintaining grip.
    weapon=rx(-PI/2-.10*recoil)
    right=Vector((-.075,-.36+.027*recoil,1.40+.014*recoil))+Vector(offset)
    left=right+weapon@Vector((.030,-.020,.065))
    arms(p,{'R':right,'L':left},{'R':weapon,'L':weapon@rz(-.32)},
         {'R':(-.15,.1,-1),'L':(.15,.1,-1)})

def locomotion(rig,cycle,fast=0,flee=False):
    p=Pose(rig);q=TAU*cycle
    stance=.60-.20*fast;speed=1.08+1.38*fast;period=1/(1+.5*fast)
    stride=speed*period;half=stride*stance*.5
    # Low at load acceptance, tall at midstance; running adds a short flight.
    bob=-.070-.065*fast-(.020+.025*fast)*math.sin(2*q)
    p.translate('pelvis',(.018*math.sin(q),0,bob))
    p.relative('pelvis',rz(-(.04+.025*fast)*math.cos(q))@ry(.018*math.sin(q)))
    p.relative('spine',rx(.025+.10*fast))
    p.relative('chest',rz((.065+.035*fast)*math.cos(q))@rx(.020))
    p.relative('head',rz(-.025*math.cos(q))@rx(-.015-.045*fast))
    targets={};deltas={}
    for suffix,phase,s in [('L',0,1),('R',.5,-1)]:
        f=(cycle+phase)%1
        if f<stance:
            y=-half+stride*f;z=0
            heel=-.12*(1-smooth(f/.11));toe=.26*smooth((f-(stance-.16))/.16)
            angle=heel+toe
            # Rotate about heel/toe contact rather than sinking a flat foot.
            pivot=Vector((s*.115,.065 if angle<=0 else -.195,0))
            ankle=pivot+rx(angle)@(Vector((s*.115,0,.14))-pivot)
            ankle.y+=y
        else:
            u=(f-stance)/(1-stance)
            # Hermite curve keeps the stance velocity at toe off and heel strike.
            m=stride*(1-stance)
            y=(2*u**3-3*u*u+1)*half+(u**3-2*u*u+u)*m+(-2*u**3+3*u*u)*(-half)+(u**3-u*u)*m
            angle=.26*(1-smooth(u/.35))-.12*smooth((u-.62)/.38)
            lift=(.105+.15*fast)*math.sin(PI*u)**1.35
            pivot=Vector((s*.115,.065 if angle<=0 else -.195,0))
            ankle=pivot+rx(angle)@(Vector((s*.115,0,.14))-pivot)
            ankle.y+=y;ankle.z+=lift
            # The lowest rigid boot point remains above the swing clearance.
            boot=[Vector((s*.115,yy,0)) for yy in [-.195,.065]]
            zmin=min((rx(angle)@(v-Vector((s*.115,0,.14)))).z for v in boot)
            ankle.z=max(ankle.z,lift-zmin)
        targets[suffix]=ankle;deltas[suffix]=rx(angle)
    feet(p,targets,deltas)
    for suffix,phase,s in [('L',0,1),('R',PI,-1)]:
        a=q+phase;swing=math.cos(a)
        # Relaxed shoulders; elbows fold more during fast recovery, not locked A-pose.
        wrist=p.point('chest',(s*.27,0,.965+.17*fast))
        wrist.y+=(.13+.12*fast)*swing;wrist.z+=.018*math.sin(a-.3)
        p.chain('upper_arm.'+suffix,'forearm.'+suffix,'hand.'+suffix,wrist,(s*.08,1,-.12),rx(-.10-.35*fast)@rz(-s*.08))
    return p

DIRECTION_ANGLES={'Forward':0,'ForwardLeft':45,'Left':90,'BackLeft':135,'Back':180,
                  'BackRight':-135,'Right':-90,'ForwardRight':-45}

def aim_speed(direction,fast=False):
    a=abs(DIRECTION_ANGLES[direction])
    return ({0:1.8,45:1.5,90:1.25,135:1.2,180:1.2} if fast else
            {0:.90,45:.75,90:.65,135:.65,180:.65})[a]

def aim_locomotion(rig,cycle,direction,fast=False):
    """Fixed facing, synchronized feet, uncrossed sidesteps, common rigid grip."""
    p=Pose(rig);q=TAU*cycle;angle=math.radians(DIRECTION_ANGLES[direction])
    travel=Vector((math.sin(angle),-math.cos(angle),0));side=abs(travel.x)
    stance=.4 if fast else .6;period=2/3 if fast else 1.;stride=aim_speed(direction,fast)*period
    half=stride*stance*.5;width=.16+.10*side
    bob=-.07-(.012 if fast else .008)*math.sin(2*q)
    p.translate('pelvis',(.006*math.sin(q),0,bob))
    p.relative('pelvis',ry(.008*math.sin(q)))
    targets={};deltas={}
    for suffix,phase,s in [('L',0,1),('R',.5,-1)]:
        f=(cycle+phase)%1
        if f<stance:
            along=half-stride*f;lift=0
            roll=(-.09*(1-smooth(f/.10))+.18*smooth((f-(stance-.12))/.12))*math.cos(angle)
        else:
            u=(f-stance)/(1-stance);m=-stride*(1-stance)
            along=(2*u**3-3*u*u+1)*(-half)+(u**3-2*u*u+u)*m+(-2*u**3+3*u*u)*half+(u**3-u*u)*m
            lift=(.14 if fast else .085)*math.sin(PI*u)**1.4
            roll=(.18*(1-smooth(u/.35))-.09*smooth((u-.62)/.38))*math.cos(angle)
        rotation=rx(roll);center=Vector((s*width,0,.14))+travel*along
        # For strafing the soles stay facing the target; no whole-clip yaw trick.
        low=min((rotation@Vector((0,yy,-.14))).z for yy in [-.195,.065])
        center.z=max(.14,lift-low)
        targets[suffix]=center;deltas[suffix]=rotation
    feet(p,targets,deltas)
    aim(p,offset=(.0015*math.sin(q),0,.0015*math.sin(2*q)))
    p.relative('neck',rx(.025));p.relative('head',ry(-.006*math.sin(q)))
    return p

def side_down(rig,breath=0,dead=False):
    p=Pose(rig);d=ry(-PI/2+.025*breath);hip=Vector((0,0,.335))
    p.absolute('pelvis',d,hip)
    p.relative('spine',rx(.12));p.relative('chest',rx(-.06+.012*breath))
    p.relative('head',rz(-.10)@rx(.10))
    targets={};angles={};poles={}
    for suffix,s in [('L',1),('R',-1)]:
        targets[suffix]=hip+d@Vector((s*.14,-.22 if s==1 else -.10,-.59))
        angles[suffix]=d@rx(-.12);poles[suffix]=d@Vector((0,-1,0))
    feet(p,targets,angles,poles)
    targets={'R':hip+d@Vector((-.08,-.42,.30)), 'L':hip+d@Vector((.18,-.35,.43))}
    # The weapon stays on its socket, horizontal and away from the head/ground.
    arms(p,targets,{'R':rx(-PI/2)@ry(.12),'L':d@rx(-.75)},
         {'R':(0,-1,.1),'L':(0,-1,.4)})
    return p

def orient_down(p,direction):
    yaw={'Front':-PI/2,'Back':PI/2,'Left':0,'Right':PI}[direction]
    d=rz(yaw);result=Pose(p.rig)
    for bone in p.rig.data.bones:
        n=bone.name
        if n=='root':continue
        m=p.fk(n);delta=m.to_quaternion()@p.rest[n].to_quaternion().inverted()
        result.absolute(n,d@delta,d@m.translation)
    return result

def recovery_state(rig,label,back=False):
    if label=='down':return side_down(rig)
    if label=='stand':return standing(rig,0,False)
    p=Pose(rig)
    if label=='support':
        p.absolute('pelvis',rx(1.20)@ry(-.08),(.02,0,.45))
        p.relative('spine',rx(.30));p.relative('chest',rx(-.05));p.relative('head',rx(-.20))
        # Left palm supports; armed right hand remains clear of the ground.
        arms(p,{'L':(.28,-.52,.025),'R':(-.40,-.22,.47)},
             {'L':rx(-PI/2),'R':rx(-PI/4)}, {'L':(.8,0,0),'R':(-1,.1,0)})
        feet(p,{'L':(.15,.36,.21),'R':(-.16,.31,.21)},
             {'L':rx(-.9),'R':rx(-.9)})
    elif label=='kneel':
        p.absolute('pelvis',rx(.26),(0,.02,.56));p.relative('spine',rx(.18));p.relative('head',rx(-.22))
        feet(p,{'L':(.17,-.30,.14),'R':(-.16,.38,.20)},
             {'L':Quaternion(),'R':rx(-.85)}, {'L':(0,-1,0),'R':(0,-1,0)})
        arms(p,{'L':(.20,-.36,.57),'R':(-.33,-.17,.84)},
             {'L':rx(-.8),'R':rx(-.3)})
    elif label=='rise':
        p.absolute('pelvis',rx(.15),(0,0,.76));p.relative('spine',rx(.10));p.relative('head',rx(-.17))
        feet(p,{'L':(.15,-.18,.14),'R':(-.14,.12,.14)})
        arms(p,{'L':(.29,-.17,.86),'R':(-.31,-.03,.93)})
    return p

def keyposes(rig,t,keys):
    if t<=keys[0][0]:return keys[0][1]
    for (a,pa),(b,pb) in zip(keys,keys[1:]):
        if t<=b:return blend(pa,pb,(t-a)/(b-a))
    return keys[-1][1]

def jump(rig,clip,t):
    def make(crouch,lift,fold,arm):
        p=standing(rig,0,False)
        p.translate('pelvis',(0,.025*crouch/.2,lift-crouch))
        p.relative('spine',rx(crouch*.8));p.relative('head',rx(-crouch*.5))
        feet(p,{s:(x,-fold*.12,.14+lift+fold*.20) for s,x in [('L',.115),('R',-.115)]})
        arms(p,{s:(sign*.30,arm,.99+max(0,-arm)*.45) for s,sign in [('L',1),('R',-1)]},
             {s:rx(-max(0,-arm)*.8) for s in ['L','R']})
        return p
    air=make(0,.09,.34,-.15)
    if clip=='Jump_Air':
        p=air;p.relative('chest',rx(-.012+.015*math.sin(TAU*t)));return p
    if clip=='Jump_Start':return keyposes(rig,t,[(0,make(0,0,0,0)),(.55,make(.20,0,0,.18)),(1,air)])
    if clip=='Jump_Land':return keyposes(rig,t,[(0,air),(.32,make(.20,0,0,-.08)),(1,make(0,0,0,0))])
    if t<.2:return keyposes(rig,t/.2,[(0,make(0,0,0,0)),(.65,make(.20,0,0,.17)),(1,make(0,0,.05,-.15))])
    if t>.80:return keyposes(rig,(t-.8)/.2,[(0,make(0,0,.05,-.12)),(.30,make(.20,0,0,-.06)),(1,make(0,0,0,0))])
    u=(t-.2)/.6;lift=.46*4*u*(1-u)
    return make(0,lift,.05+.30*math.sin(PI*u),-.15-.08*math.sin(PI*u))

def turn(rig,t,sign):
    p=standing(rig,0,False);yaw=sign*PI*.5*smoother(t)
    p.relative('root',rz(yaw));p.relative('chest',rz(sign*.10*math.sin(PI*t)))
    p.relative('head',rz(sign*.13*math.sin(PI*t)))
    targets={};deltas={}
    # Two alternating steps: each planted foot remains at its world position.
    for suffix,s,start,end in [('L',1,.06,.46),('R',-1,.50,.94)]:
        u=smoother((t-start)/(end-start));angle=sign*PI*.5*u
        targets[suffix]=rz(angle)@Vector((s*.115,0,.14))
        if start<t<end:targets[suffix].z+=.10*math.sin(PI*(t-start)/(end-start))**1.3
        deltas[suffix]=rz(angle)
    feet(p,targets,deltas,{s:rz(yaw)@Vector((0,-1,0)) for s in ['L','R']})
    return p

def hit(rig,t,direction):
    p=standing(rig,0,False);aim(p)
    amount=pulse(t,0,.23,1)
    x,y={'Front':(0,1),'Back':(0,-1),'Left':(-1,0),'Right':(1,0)}[direction]
    p.relative('spine',rx(.025-y*.11*amount)@ry(x*.11*amount))
    p.relative('chest',rx(-.018-y*.10*amount)@ry(x*.09*amount))
    p.relative('neck',rx(y*.06*amount)@ry(-x*.05*amount))
    # Arms follow chest as a rigid grip assembly; do not re-solve them independently.
    return p

def knock(rig,t,direction,death=False):
    start=standing(rig,0,False)
    x,y={'Front':(0,1),'Back':(0,-1),'Left':(-1,0),'Right':(1,0)}[direction]
    catch=standing(rig,0,False)
    catch.translate('pelvis',(x*.08,y*.08,-.18))
    catch.relative('pelvis',rx(-y*.32)@ry(x*.34))
    catch.relative('spine',rx(y*.12)@ry(-x*.10))
    feet(catch,{'L':(.19,-.08,.14),'R':(-.19,.07,.14)})
    arms(catch,{'L':(.35,-.31,1.02),'R':(-.35,-.30,1.02)},
         {'L':rx(-.7),'R':rx(-.7)})
    fall=orient_down(side_down(rig),direction)
    # Catch weight, collapse, contact and small settling before the stable hold.
    near=blend(catch,fall,.80)
    near.relative('head',rz(-.12)@rx(.12))
    return keyposes(rig,t,[(0,start),(.18,catch),(.66,near),(.85,fall),(1,fall)])

def human_pose(rig,clip,t):
    if clip in ['Reference_A','Turn_Base']:return Pose(rig)
    if clip=='Idle':return standing(rig,t)
    if clip.startswith(('Aim_Walk_','Aim_Run_')):
        return aim_locomotion(rig,t,clip.split('_',2)[2],clip.startswith('Aim_Run_'))
    if clip in ['Walk','Run','Walk_To_Run','Flee_Run']:
        cycle=.8*t+.2*t*t if clip=='Walk_To_Run' else t
        fast=0 if clip=='Walk' else smooth(t) if clip=='Walk_To_Run' else 1
        return locomotion(rig,cycle,fast,clip=='Flee_Run')
    if clip.startswith('Turn_'):return turn(rig,t,1 if 'Left' in clip else -1)
    if clip.startswith('Jump_'):return jump(rig,clip,t)
    if clip in ['Aim_Base','Attack_Fire','Security_StunFire']:
        p=standing(rig,0,False)
        r=0 if clip=='Aim_Base' else pulse(t,.08,.24,.94)
        aim(p,r*(.6 if clip=='Security_StunFire' else 1));return p
    if clip=='Attack_Melee':
        p=standing(rig,0,False);wind=pulse(t,0,.25,.46);strike=pulse(t,.30,.51,.95)
        p.translate('pelvis',(0,-.045*strike,-.045*strike))
        p.relative('pelvis',rz(.10*wind-.15*strike));p.relative('spine',rz(.20*wind-.26*strike)@rx(.09*strike))
        p.relative('chest',rz(.18*wind-.20*strike));p.relative('head',rz(-.15*wind+.22*strike))
        arms(p,{'L':(.27,-.27,1.22),'R':(-.32+.19*strike,-.16+.13*wind-.39*strike,1.18+.10*strike)},
             {'L':rx(-.65),'R':rx(-.65-1.0*strike)})
        feet(p,{'L':(.14,-.08,.14),'R':(-.15,.06,.14)})
        if t<.1:p=blend(standing(rig,0,False),p,t/.1)
        if t>.85:p=blend(p,standing(rig,0,False),(t-.85)/.15)
        return p
    if clip.startswith('Hit_'):return hit(rig,t,clip.split('_')[1])
    if clip.startswith(('Knockdown_','Death_')) and not clip.endswith('Pose'):
        return knock(rig,t,clip.split('_')[1],clip.startswith('Death_'))
    if clip.startswith('Death_'):return orient_down(side_down(rig,0,True),clip.split('_')[1])
    if clip.startswith('Downed_') or clip=='Medical_Stabilized':
        direction=clip.split('_')[1] if clip.count('_')==2 else 'Front'
        return orient_down(side_down(rig,.5*math.sin(TAU*t) if clip.startswith('Downed_') else .3*math.sin(PI*t)**2),direction)
    if clip.startswith('GetUp_') or clip=='Medical_GetUp':
        direction=clip.split('_')[1] if clip.startswith('GetUp_') else 'Front'
        back=direction=='Back'
        down=orient_down(recovery_state(rig,'down'),direction)
        guard=orient_down(recovery_state(rig,'down'),direction)
        wrist=guard.fk('hand.R').translation.copy()
        wrist+=Vector({'Front':(0,-.25,.13),'Back':(0,.25,.13),'Left':(.20,0,.13),'Right':(-.20,0,.13)}[direction])
        hip=guard.fk('upper_arm.R').translation;elbow=guard.fk('forearm.R').translation
        pole=elbow-(hip+guard.fk('hand.R').translation)*.5
        guard.chain('upper_arm.R','forearm.R','hand.R',wrist,pole,rx(-PI/4))
        return keyposes(rig,t,[(0,down),(.10,guard),(.26,recovery_state(rig,'support',back)),
                              (.53,recovery_state(rig,'kneel',back)),(.78,recovery_state(rig,'rise',back)),
                              (1,recovery_state(rig,'stand'))])
    if clip=='Flee_Start':
        p=blend(standing(rig,0,False),locomotion(rig,0,1),t)
        rest=p.rest['head'].to_quaternion()
        extra=rest.inverted()@rz(.22*pulse(t,0,.25,.70))@rest
        p.basis['head']=p.basis['head']@extra.to_matrix().to_4x4();p.cache.clear()
        return p
    if clip=='Flee_Stop':return blend(locomotion(rig,0,1),standing(rig,0,False),t)
    if clip in ['Ride_Base','Rider_Brace']:
        # Seat origin is the driver's pelvis. HUMAN -Y is rotated to bike +X at assembly.
        p=Pose(rig);r=pulse(t,0,.23,1) if clip=='Rider_Brace' else 0
        p.absolute('pelvis',rx(.14),(0,0,.91))
        p.relative('spine',rx(.13+.10*r));p.relative('chest',rx(-.04-.05*r))
        p.relative('head',rx(-.14-.06*r))
        feet(p,{'L':(.34,.10,.6585),'R':(-.34,.10,.6585)})
        arms(p,{'L':(.191,-.238,1.015),'R':(-.191,-.238,1.015)},
             {'L':rx(-.75),'R':rx(-.75)})
        return p
    raise ValueError(clip)

NEW_HUMAN=[*(('Hit_'+d,9,False) for d in ['Front','Back','Left','Right']),
           *(('Knockdown_'+d,24,False) for d in ['Front','Back','Left','Right']),
           ('Downed_Loop',60,True),('GetUp_Front',42,False),('GetUp_Back',42,False),
           ('Death_Front',30,False),('Death_Back',30,False),
           ('Death_Front_Pose',1,False),('Death_Back_Pose',1,False),
           ('Flee_Start',6,False),('Flee_Run',20,True),('Flee_Stop',6,False),
           ('Security_StunFire',12,False),('Medical_Stabilized',30,False),
           ('Medical_GetUp',42,False),('Ride_Base',1,False),('Rider_Brace',15,False)]
NEW_HUMAN += [('Downed_'+d+'_Loop',60,True) for d in ['Back','Left','Right']]
NEW_HUMAN += [('GetUp_'+d,42,False) for d in ['Left','Right']]
NEW_HUMAN += [('Aim_'+speed+'_'+d,30 if speed=='Walk' else 20,True)
              for speed in ['Walk','Run'] for d in DIRECTION_ANGLES]

def vehicle_pose(rig,clip,t):
    p=Pose(rig)
    if clip=='Vehicle_Disabled':
        settle=smoother(t)
        for s in ['L','R']:
            p.relative('frontpod.'+s,ry(-.055*settle));p.relative('rearpod.'+s,ry(.045*settle))
            p.relative('engine.'+s,ry(.04*settle))
        return p
    bump=math.sin(3*PI*t)*math.exp(-4*t)*math.sin(PI*t)
    for s,sign in [('L',1),('R',-1)]:
        factor=sign if clip.endswith('Side') or clip=='Bike_Impact' else 1
        direction=-1 if clip.endswith('Rear') else 1
        # The chassis, canopy and seat remain rigid; only field suspension responds.
        p.relative('frontpod.'+s,ry(.12*bump*factor*direction))
        p.relative('rearpod.'+s,ry(-.09*bump*factor*direction))
        p.relative('engine.'+s,ry(.045*bump*factor*direction))
    return p

NEW_VEHICLE=[('Vehicle_Impact_Front',15,False),('Vehicle_Impact_Rear',15,False),
             ('Vehicle_Impact_Side',15,False),('Vehicle_Disabled',24,False)]
