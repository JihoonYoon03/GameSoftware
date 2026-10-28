"""Right-handed lowered carry and a magazine/slide reload on the original rig."""
import math
from mathutils import Matrix,Vector
from civic_motion import Pose,rx,rz,smooth

SPECS={'Pistol_Carry_Idle':('Idle',60,True),'Pistol_Carry_Walk':('Walk',30,True),
       'Pistol_Carry_Run':('Run',20,True),'Reload_Pistol':('Idle',42,False)}
LOWER=['root','pelvis','thigh.L','shin.L','foot.L','thigh.R','shin.R','foot.R']
LEFT=['upper_arm.L','forearm.L','hand.L']

def carry_right(p,t,fast=False):
    phase=math.tau*t
    target=p.point('chest',(-.37,-.205,1.10 if not fast else 1.18))
    target.y+=(.018 if not fast else .028)*math.cos(phase)
    target.z+=.007*math.sin(phase)
    rotation=rz(-.06)@rx(-.45 if not fast else -.55)
    p.chain('upper_arm.R','forearm.R','hand.R',target,(-.12,.65,-.7),rotation)
    return target,rotation

def pistol_pose(base,clip,t):
    p=Pose(base.rig);p.basis={n:m.copy() for n,m in base.basis.items()}
    target,rotation=carry_right(p,t if clip!='Reload_Pistol' else 0,clip=='Pistol_Carry_Run')
    if clip!='Reload_Pistol':return p
    original_left={n:p.basis[n].copy() for n in LEFT}
    raised=smooth(t/.18)*(1-smooth((t-.82)/.18))
    work=Vector((0,-.30,1.31))
    target=target.lerp(work,raised)
    # Move away from the chest before crossing inward; bulky female armor has
    # a deeper front shell than the unarmored reference character.
    target.y-=.12*math.sin(math.pi*raised)
    rotation=rotation.slerp(rx(-1.30),raised)
    pole=Vector((-.12,.65,-.7)).lerp(Vector((-.12,.12,-1)),raised)
    p.chain('upper_arm.R','forearm.R','hand.R',target,pole,rotation)
    # Coordinates relative to the right hand's authored head. The magazine
    # points toward source +Y; the slide recoils toward source +Z.
    mag=Vector((-.035,0,-.113));slide=Vector((-.035,-.064,-.140))
    if t<.30:contact=mag
    elif t<.46:contact=mag+Vector((0,.18*smooth((t-.30)/.16),0))
    elif t<.67:contact=mag+Vector((.055*math.sin(math.pi*(t-.46)/.21),.18*(1-smooth((t-.46)/.21)),0))
    elif t<.73:contact=mag.lerp(slide,smooth((t-.67)/.06))
    elif t<.80:contact=slide+Vector((0,0,.055*smooth((t-.73)/.07)))
    elif t<.85:contact=slide+Vector((0,0,.055*(1-smooth((t-.80)/.05))))
    else:contact=slide
    hand_rotation=rotation@rz(.20)
    palm_offset=Vector((.448,-.030,.865))-p.rest['hand.L'].translation
    wrist=target+rotation@contact-hand_rotation@palm_offset
    p.chain('upper_arm.L','forearm.L','hand.L',wrist,(.10,.12,-1),hand_rotation)
    weight=smooth((t-.045)/.155)*(1-smooth((t-.85)/.15))
    for n in LEFT:
        la,qa,sa=original_left[n].decompose();lb,qb,sb=p.basis[n].decompose()
        p.basis[n]=Matrix.LocRotScale(la.lerp(lb,weight),qa.slerp(qb,weight),sa.lerp(sb,weight))
    p.cache.clear();return p
