"""Authored CQ-07C2 poses on the existing HUMAN_7H skeleton."""
from math import pi, sin, tau

from mathutils import Vector

from civic_motion import Pose, arms, blend, recovery_state, rx, ry, rz
from pistol_layer_motion import LOWER

REVISION = "support_work_v1"
SPECS = {
    "Support_Treat_Enter": (24, False, "full_body"),
    "Support_Treat_Loop": (36, True, "full_body"),
    "Support_Treat_Exit": (21, False, "full_body"),
    "Support_Inspect_Enter": (15, False, "human_upper_body"),
    "Support_Inspect_Loop": (42, True, "human_upper_body"),
    "Support_Inspect_Exit": (15, False, "human_upper_body"),
    "Support_Carry_Idle": (60, True, "human_upper_body"),
    "Support_Carry_Walk": (30, True, "human_upper_body"),
}
CASE_CENTER = Vector((0,-.47,1.04))
CASE_BOTTOM = CASE_CENTER - Vector((0,0,.16))


def copy_pose(source):
    pose = Pose(source.rig)
    pose.basis = {name: matrix.copy() for name, matrix in source.basis.items()}
    return pose


def treatment(rig, phase):
    pose = recovery_state(rig, "kneel")
    shifting = sin(tau*phase)
    touch = .5 - .5*sin(tau*phase + pi/2)
    pose.relative("chest", rx(.25 + .075*touch) @ rz(.05*shifting))
    pose.relative("neck", rx(.16))
    pose.relative("head", rx(.23) @ ry(.055*shifting))
    wrists = {"L": (.10,-.623,.386),
              "R": (-.092+.027*shifting,-.592-.066*touch,.377+.035*(1-touch))}
    arms(pose, wrists, {"L":rx(-.84), "R":rx(-.92) @ rz(.12*shifting)},
         {"L":(.55,.18,-1), "R":(-.5,.1,-1)})
    return pose


def inspect(idle, phase):
    pose = copy_pose(idle)
    press = sin(pi*phase)**8
    scanning = sin(tau*phase)
    pose.relative("spine", rx(.09))
    pose.relative("chest", rz(.045) @ rx(.03))
    pose.relative("neck", rx(.10))
    pose.relative("head", rx(.27 + .06*press) @ ry(.09*scanning))
    arms(pose, {"R":(-.14+.045*scanning,-.539+.078*(1-press),.946+.134*(1-press)),
                "L":(.24,-.32,1.04)},
         {"R":rx(-1.2) @ rz(.12), "L":rx(-.6)},
         {"R":(-.38,.35,-1), "L":(.58,.42,-1)})
    return pose


def carry(base, phase):
    pose = copy_pose(base)
    motion = sin(tau*phase)
    pose.relative("chest", rx(.055) @ rz(.035*motion))
    pose.relative("head", rx(-.035))
    targets = {foot: pose.point("chest", (.222*sign,-.44,1.025))
               for foot, sign in (("L",1),("R",-1))}
    basis = pose.deformation("chest").to_quaternion()
    arms(pose, targets, {"L":basis @ rx(-1.06) @ rz(-.22), "R":basis @ rx(-1.06) @ rz(.22)},
         {"L":(.48,.60,-1), "R":(-.48,.60,-1)})
    return pose


def pose_for(clip, phase, sample):
    idle = sample("Idle", 0)
    if clip.startswith("Support_Carry"):
        return carry(sample("Walk" if clip.endswith("Walk") else "Idle", phase), phase)
    work = treatment(idle.rig, phase if clip.endswith("Loop") else 0) if clip.startswith(
        "Support_Treat") else inspect(idle, phase if clip.endswith("Loop") else 0)
    if clip.endswith("Loop"):
        return work
    if clip.endswith("Enter"):
        return blend(idle, work, phase)
    return blend(work, idle, phase)


def upper_body_error(base, posed):
    return max(abs(posed.basis[name][i][j] - base.basis[name][i][j])
               for name in LOWER for i in range(4) for j in range(4))
