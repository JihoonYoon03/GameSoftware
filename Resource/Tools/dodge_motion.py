"""HUMAN_7H short dodge steps, authored in place in Blender metres."""
import math

from mathutils import Vector

from civic_motion import Pose, blend, feet, rx, ry, rz, smooth
from pistol_layer_motion import LOWER

REVISION = "dodge_step_v1"
FPS = 30
FRAMES = 12
DURATION = FRAMES / FPS
MOVE_SECONDS = .22
REFERENCE_SPEED = 15.0
PUSH_END = 2 / 60
LAND = 11 / 60
LAND_END = 13 / 60
DIRECTIONS = {
    "Forward": (0, -1), "ForwardLeft": (1, -1), "Left": (1, 0),
    "BackLeft": (1, 1), "Back": (0, 1), "BackRight": (-1, 1),
    "Right": (-1, 0), "ForwardRight": (-1, -1),
}
SPECS = {
    f"Dodge_{name}_{push}": (name, push)
    for name in DIRECTIONS for push in ("L", "R")
}


def direction(name):
    return Vector((*DIRECTIONS[name], 0)).normalized()


def reference(name, push):
    base = "Run" if name == "Forward" else f"Aim_Run_{name}"
    phase = 0.0 if push == "L" else .5
    return base, phase, (phase + .5) % 1


def travel(seconds):
    return min(seconds, MOVE_SECONDS) * REFERENCE_SPEED


def pose_for(name, push, seconds, sample):
    """The push and landing soles have short, independently anchored contacts."""
    land = "R" if push == "L" else "L"
    heading = direction(name)
    base, start_phase, end_phase = reference(name, push)
    start = sample(base, start_phase)
    end = sample(base, end_phase)

    # Directional aim references supply only lower-body keys. The upper pose
    # uses the corresponding running phase so the standalone clip is unarmed.
    def endpoint(lower, phase):
        result = Pose(lower.rig)
        upper = sample("Run", phase)
        for bone in result.basis:
            result.basis[bone] = (lower if bone in LOWER else upper).basis[bone].copy()
        return result

    start, end = endpoint(start, start_phase), endpoint(end, end_phase)
    if seconds <= 0:
        return start
    if seconds >= DURATION:
        return end

    pose = blend(start, end, seconds / DURATION)
    load = smooth(seconds / .065) * (1 - smooth((seconds - .25) / .15))
    brace = smooth(seconds / PUSH_END) * (1 - smooth((seconds - PUSH_END) / .07))
    side = 1 if push == "L" else -1
    lean = rx(-heading.y * .26 * load) @ ry(heading.x * .23 * load)
    pelvis_shift = heading * (.055 * load - .10 * brace)
    pelvis_shift.z = -.095 * load - .035 * brace
    pose.translate("pelvis", pelvis_shift)
    pose.relative("pelvis", rx(-heading.y * .10 * load) @ ry(heading.x * .09 * load))
    pose.relative("spine", lean)
    pose.relative("chest", rz(side * .12 * load) @ rx(.06 * load))
    pose.relative("head", ry(-heading.x * .18 * load) @ rx(heading.y * .19 * load))

    def ankle(p, foot):
        return p.fk(f"foot.{foot}").translation.copy()

    def ankle_rotation(p, foot):
        bone = f"foot.{foot}"
        return p.deformation(bone).to_quaternion()

    def step(foot, distance, height, width=.145):
        return Vector((width if foot == "L" else -width, 0, .141 + height)) + heading * distance

    start_plant = ankle(start, push)
    landing = step(land, .30, 0, .17)
    landing_exit = landing - heading * (travel(LAND_END) - travel(LAND))
    lift_off = start_plant - heading * travel(PUSH_END)
    toe = ankle_rotation(start, push)
    tracks = {
        push: [
            (0, start_plant, toe),
            (PUSH_END, lift_off, toe),
            (.105, step(push, -.30, .18), rx(.26)),
            (LAND, step(push, -.27, .16, .17), rx(.15)),
            (LAND_END, step(push, -.21, .12, .17), rx(.10)),
            (.307, step(push, .04, .14), rx(-.06)),
            (DURATION, ankle(end, push), ankle_rotation(end, push)),
        ],
        land: [
            (0, ankle(start, land), ankle_rotation(start, land)),
            (PUSH_END, step(land, -.15, .11), rx(.12)),
            (.105, step(land, .17, .23), rx(-.18)),
            (LAND, landing, rx(0)),
            (LAND_END, landing_exit, rx(0)),
            (.295, step(land, -.22, .025), rx(.10)),
            (DURATION, ankle(end, land), ankle_rotation(end, land)),
        ],
    }
    targets, deltas = {}, {}
    for foot, keys in tracks.items():
        for (a, point_a, rot_a), (b, point_b, rot_b) in zip(keys, keys[1:]):
            if seconds <= b:
                weight = (seconds - a) / (b - a)
                anchored = (foot == push and b == PUSH_END) or (foot == land and a == LAND)
                weight = weight if anchored else smooth(weight)
                targets[foot] = point_a.lerp(point_b, weight)
                deltas[foot] = rot_a.slerp(rot_b, weight)
                break

    # When the inside foot leads a side dodge, keep the boots in their lanes.
    # Apply the correction to the free foot so the load-bearing sole stays put.
    gap = targets["L"].x - targets["R"].x
    if gap < .22:
        fixed = land if LAND <= seconds <= LAND_END else push
        free = "R" if fixed == "L" else "L"
        targets[free].x += (.22 - gap) * (1 if free == "L" else -1)
    poles = {foot: (heading.x * .25 + (.12 if foot == "L" else -.12), -1, 0)
             for foot in targets}
    feet(pose, targets, deltas, poles)
    return pose
