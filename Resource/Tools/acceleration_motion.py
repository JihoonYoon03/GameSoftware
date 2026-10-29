"""Additional in-place acceleration/braking poses. Original motions are sampled, not edited."""
import math
from mathutils import Vector, Matrix, Quaternion
from civic_motion import Pose, smooth, blend, feet, rx, ry, rz
from pistol_layer_motion import pistol_pose

REVISION='acceleration_v1'
SPEED={'Idle':0.,'Walk':1.08,'Run':2.46,'Sprint':4.2}
PERIOD={'Idle':2.,'Walk':1.,'Run':2/3,'Sprint':.5}
SPECS={}
for stem,a,b,frames in [('Walk_Start','Idle','Walk',18),('Walk_Stop','Walk','Idle',24),
                        ('Run_Start','Idle','Run',18),('Run_Stop','Run','Idle',24),
                        ('Sprint_Start','Idle','Sprint',18),('Sprint_Stop','Sprint','Idle',30),
                        ('Run_To_Walk','Run','Walk',18),('Run_To_Sprint','Run','Sprint',15),
                        ('Sprint_To_Run','Sprint','Run',18)]:
    for foot,phase in [('L',0.),('R',.5)]:
        T=frames/30
        fa=0 if a=='Idle' else 1/PERIOD[a];fb=0 if b=='Idle' else 1/PERIOD[b]
        total=T*(fa+fb)/2
        phase0=phase-total if a=='Idle' else phase
        SPECS[stem+'_'+foot]=dict(source=a,target=b,frames=frames,loop=False,duration_seconds=T,
            entry_phase=0 if a=='Idle' else phase0%1,exit_phase=0 if b=='Idle' else (phase0+total)%1,
            phase_origin=phase0,entry_contact=foot,phase_advance=total,
            start_speed_mps=SPEED[a],end_speed_mps=SPEED[b])
SPECS['Sprint']=dict(source='Sprint',target='Sprint',frames=15,loop=True,duration_seconds=.5,
    entry_phase=0.,exit_phase=0.,phase_origin=0.,entry_contact='L',phase_advance=1.,
    start_speed_mps=4.2,end_speed_mps=4.2)

def phase_at(info,t):
    if info['loop']:return t
    a,b=info['source'],info['target'];T=info['duration_seconds']
    fa=0 if a=='Idle' else 1/PERIOD[a];fb=0 if b=='Idle' else 1/PERIOD[b]
    # Exact integral of smoothstep frequency; position and speed stay continuous.
    return info['phase_origin']+T*(fa*t+(fb-fa)*(t**3-.5*t**4))

def distance_at(info,t):
    T=info['duration_seconds'];a=info['start_speed_mps'];b=info['end_speed_mps']
    return T*(a*t+(b-a)*(t**3-.5*t**4))

def sprint(rig,cycle):
    p=Pose(rig);q=math.tau*cycle
    p.translate('pelvis',(.012*math.sin(q),-.025,-.155-.035*math.sin(2*q)))
    p.relative('pelvis',rx(.08)@rz(-.055*math.cos(q)))
    p.relative('spine',rx(.12));p.relative('chest',rz(.08*math.cos(q))@rx(.025))
    p.relative('head',rx(-.11)@rz(-.025*math.cos(q)))
    stance=.32;stride=4.2*.5;half=stride*stance/2;targets={};rotations={}
    for suffix,off,s in [('L',0,1),('R',.5,-1)]:
        f=(cycle+off)%1
        if f<stance:
            y=-half+stride*f
            angle=-.10*(1-smooth(f/.08))+.24*smooth((f-(stance-.12))/.12)
            lift=0
        else:
            u=(f-stance)/(1-stance);m=stride*(1-stance)
            y=(2*u**3-3*u*u+1)*half+(u**3-2*u*u+u)*m+(-2*u**3+3*u*u)*(-half)+(u**3-u*u)*m
            angle=.24*(1-smooth(u/.34))-.10*smooth((u-.64)/.36)
            lift=.28*math.sin(math.pi*u)**1.35
        pivot=Vector((s*.115,.065 if angle<=0 else -.195,0))
        ankle=pivot+rx(angle)@(Vector((s*.115,0,.14))-pivot)
        ankle.y+=y;ankle.z+=lift
        if lift:
            low=min((rx(angle)@Vector((0,yy,-.14))).z for yy in [-.195,.065])
            ankle.z=max(ankle.z,lift-low)
        targets[suffix]=ankle;rotations[suffix]=rx(angle)
    feet(p,targets,rotations)
    for suffix,s in [('L',1),('R',-1)]:
        swing=math.cos(q+(0 if suffix=='L' else math.pi))
        wrist=p.point('chest',(s*.27,0,1.17));wrist.y+=.26*swing
        wrist.z+=.025*math.sin(q)
        p.chain('upper_arm.'+suffix,'forearm.'+suffix,'hand.'+suffix,wrist,(s*.08,1,-.20),rx(-.50)@rz(-s*.08))
    # Existing rigid pistol and socket stay safely lowered; upper-body layers may override this.
    return pistol_pose(p,'Pistol_Carry_Run',cycle)

def pose_for(rig,name,t,sample):
    info=SPECS[name]
    if name=='Sprint':return sprint(rig,t)
    a,b=info['source'],info['target'];ph=phase_at(info,t)
    pa=sample(a,0 if a=='Idle' else ph%1);pb=sample(b,0 if b=='Idle' else ph%1)
    p=blend(pa,pb,t)
    if t==0 or t==1:return p
    accel=info['end_speed_mps']-info['start_speed_mps']
    wave=math.sin(math.pi*t)**2
    # Counterbalance is articulated through pelvis/spine, with foot targets held below.
    foot_targets={s:p.fk('foot.'+s).translation.copy() for s in ['L','R']}
    foot_rots={s:p.fk('foot.'+s).to_quaternion()@p.rest['foot.'+s].to_quaternion().inverted() for s in ['L','R']}
    lean=max(-.12,min(.15,accel*.035))*wave
    def add_rotation(n,q):
        rest=p.rest[n].to_quaternion()
        p.basis[n]=p.basis[n]@(rest.inverted()@q@rest).to_matrix().to_4x4();p.cache.clear()
    add_rotation('spine',rx(lean))
    add_rotation('chest',rx(-lean*.35)@rz(.018*wave*(1 if info['entry_contact']=='L' else -1)))
    add_rotation('head',rx(-lean*.55))
    p.translate('pelvis',(0,-lean*.10,-abs(lean)*.12))
    feet(p,foot_targets,foot_rots)
    if b=='Idle':
        # A final settling step releases the first support foot before returning to Idle.
        lead=info['entry_contact'];other='R' if lead=='L' else 'L'
        for suffix,begin,end,height in [(lead,.44,.92,.10),(other,.14,.64,.08)]:
            if begin<t<end:
                u=(t-begin)/(end-begin);target=p.fk('foot.'+suffix).translation.copy()
                target.z+=height*math.sin(math.pi*u)**2
                delta=p.fk('foot.'+suffix).to_quaternion()@p.rest['foot.'+suffix].to_quaternion().inverted()
                p.chain('thigh.'+suffix,'shin.'+suffix,'foot.'+suffix,target,(0,-1,0),delta)
    return p
