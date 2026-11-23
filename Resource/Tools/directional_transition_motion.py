"""In-place turning and aim locomotion transitions on HUMAN_7H."""
from math import pi, sin, tau

from mathutils import Vector

from civic_motion import blend, feet, rx, ry, rz, smooth

SPECS = {f"Run_Turn180_{side}": (16, "turn", side, pi) for side in ("Left","Right")}
SPECS.update({f"Turn90_{side}": (15, "turn", side, pi/2) for side in ("Left","Right")})
SPECS.update({f"Aim_Strafe_{stage}_{heading}": (12 if stage=="Start" else 14, stage, heading, 0)
              for heading in ("Forward","Back","Left","Right") for stage in ("Start","Stop")})
REVISION = "directional_transition_v1"
BRACE = (.20,.42)
DRIVE = (.58,.76)


def turn_parameters(side):
    sign = 1 if side == "Left" else -1
    feet_order = ("L","R") if sign == 1 else ("R","L")
    first = Vector((.175*sign,-.245,.141))
    second = Vector((-.18*sign,-.105,.141))
    return sign, feet_order, first, second


def path_at(side, angle, phase):
    """Review path for a controller that rotates around successive support points."""
    sign, _, first, second = turn_parameters(side)

    def yaw(t):
        return angle * sign * smooth((t-.18)/.64)

    def pivot(first_xy, q):
        return first_xy - rz(q) @ first_xy

    current = yaw(phase)
    start_rotation = yaw(DRIVE[0])
    second_anchor = pivot(first, start_rotation) + rz(start_rotation) @ second
    first_offset = pivot(first, current)
    second_offset = second_anchor - rz(current) @ second
    offset = first_offset.lerp(second_offset, smooth((phase-BRACE[1])/(DRIVE[0]-BRACE[1])))
    return offset, current


def ankle(pose, name):
    matrix = pose.fk("foot."+name)
    return matrix.translation.copy(), pose.deformation("foot."+name).to_quaternion()


def track_target(keys, t):
    for (a, position_a, rot_a),(b, position_b, rot_b) in zip(keys,keys[1:]):
        if t <= b:
            weight = smooth((t-a)/(b-a))
            return position_a.lerp(position_b, weight), rot_a.slerp(rot_b,weight)
    return keys[-1][1:]


def turn(side, angle, phase, sample):
    sign, (brake, drive), front, second = turn_parameters(side)
    gait = "Run" if angle > 2 else "Walk"
    entry = 0 if brake=="L" else .5
    start, end = sample(gait, entry), sample(gait, (entry+.5)%1)
    if phase <= 0:
        return start
    if phase >= 1:
        return end
    p = blend(start,end,phase)
    sink = sin(pi*phase)**2
    yaw_mid = sign*sin(pi*phase)
    # Hips carry weight onto the brace, then shoulders catch up as the exit foot loads.
    p.translate("pelvis", (sign*.055*sin(tau*phase), -.018*sink, -.085*sink))
    p.relative("pelvis", rz(.24*yaw_mid) @ ry(.08*yaw_mid))
    p.relative("spine", rx(-.115*sin(tau*phase)) @ rz(-.34*yaw_mid))
    p.relative("chest", rz(.18*yaw_mid) @ rx(.08*sink))
    p.relative("head", rz(.28*yaw_mid*(1-phase)))
    tracks = {
        brake: [
            (0,*ankle(start,brake)), (BRACE[0],front,rx(0)), (BRACE[1],front,rx(0)),
            (.64,Vector((sign*.19,.12,.26)),rx(.13)), (.84,Vector((sign*.14,-.16,.26)),rx(-.12)),
            (1,*ankle(end,brake)),
        ],
        drive: [
            (0,*ankle(start,drive)), (.34,Vector((-sign*.23,.13,.22)),rx(-.17)),
            (DRIVE[0],second,rx(0)), (DRIVE[1],second,rx(0)), (1,*ankle(end,drive)),
        ],
    }
    targets, rotations = {}, {}
    for name, keys in tracks.items():
        target, rotation = track_target(keys,phase)
        contact = BRACE if name==brake else DRIVE
        if contact[0] <= phase < contact[1] + .12:
            _, now = path_at(side,angle,min(phase,contact[1]))
            _, planted = path_at(side,angle,contact[0])
            rotation = rz(planted-now).slerp(rotation, smooth((phase-contact[1])/.12))
        targets[name], rotations[name] = target, rotation
    feet(p,targets,rotations)
    return p


def strafe(stage, heading, t, sample):
    idle, gait = sample("Aim_Base",0), sample("Aim_Walk_"+heading,.5 if heading=="Right" else 0)
    start,end = (idle,gait) if stage=="Start" else (gait,idle)
    p = blend(start,end,t)
    if t in (0,1):
        return p
    vector = {"Forward":(0,-1),"Back":(0,1),"Left":(1,0),"Right":(-1,0)}[heading]
    amount = sin(pi*t)**2
    direction = 1 if stage=="Start" else -1
    targets, deltas = {}, {}
    for name in ("L","R"):
        targets[name], deltas[name] = ankle(p,name)
    p.translate("pelvis", (vector[0]*.042*direction*amount,vector[1]*.048*direction*amount,-.048*amount))
    swing = "R" if heading=="Right" else "L"
    targets[swing].z += .12*amount
    targets[swing].x += vector[0]*.075*direction*sin(pi*t)**2
    targets[swing].y += vector[1]*.095*direction*sin(pi*t)**2
    feet(p,targets,deltas)
    return p


def pose_for(name, t, sample):
    _, stage, heading, angle = SPECS[name]
    return turn(heading,angle,t,sample) if stage=="turn" else strafe(stage,heading,t,sample)
