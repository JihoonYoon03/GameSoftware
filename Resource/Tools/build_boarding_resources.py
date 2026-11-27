"""CQ-06B/C cockpit-compatible vehicles and visual-only rider action bank."""

import json
import math
from pathlib import Path
import sys

import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_facility_animations import (LEGACY_REFERENCE, LIBRARY, ROOT, check_bank, export_bank,
                                       legacy_source, register_action)
from civic_motion import Pose, pulse, rx, ry, rz, smoother
from verify_human_reference_v4 import skeleton
from verify_resource_pack import accessor, read_glb


CONFIG = {
    "Coupe": {
        "glb": "Vehicles/Coupe", "body": "Vehicle_Coupe_Chassis",
        "seat_part": "Vehicle_Coupe_Seat_Cockpit", "body_bone": "chassis",
        "well": [[-.31, -.525, .35], [.91, .525, 2.4]],
        "entries": [[[-.01, -.73, .50], [.38, .73, 2.4]]],
        "seat": [-.08, .20, .446], "recline": .28, "pedal_x": .61,
        "foot_z": .49, "control_x": .43, "control_z": .91,
        "entry_x": .22, "stand_y": 1.10, "step_y": .70, "step_z": .50,
        "grip": [.39, .54, .97], "sides": [1,-1],
    },
    "Courier": {
        "glb": "Aircraft/Kite_Courier", "body": "Aircraft_Kite_Courier_PressureHull",
        "seat_part": "Aircraft_Kite_Courier_CabinSeats", "body_bone": "hull",
        "well": [[.47, -.405, .72], [1.60, .405, 2.7]],
        "entries": [[[.87, .32, .88], [1.29, .76, 2.7]]],
        "seat": [.71, 0, .95], "recline": .20, "pedal_x": 1.38,
        "foot_z": .86, "control_x": 1.23, "control_z": 1.43,
        "entry_x": 1.12, "stand_y": 1.12, "step_y": .74, "step_z": .67,
        "grip": [1.30, .56, 1.18], "sides": [1],
    },
}
SEATED = {
    "Coupe_Driver_Idle_Loop": ("Coupe", 84, True, ""),
    "Courier_Pilot_Idle_Loop": ("Courier", 84, True, ""),
    "Courier_Pilot_Ascent": ("Courier", 32, False, "ascent"),
    "Courier_Pilot_Bank_Left": ("Courier", 37, False, "left"),
    "Courier_Pilot_Bank_Right": ("Courier", 37, False, "right"),
    "Courier_Pilot_Touchdown": ("Courier", 29, False, "touchdown"),
}
ENTER_EVENTS = {"canopy_open_request": .0, "grab_rail": .24, "step_contact": .37,
                "seat_contact": .82, "canopy_close_request": 1.0}
EXIT_EVENTS = {"canopy_open_request": .0, "seat_leave": .18, "step_contact": .53,
               "ground_contact": .84, "canopy_close_request": 1.0}


def point(p):
    """Vehicle +X forward to humanoid -Y forward."""
    return Vector((p[1], -p[0], p[2]))


def smooth_track(t, stops):
    for (start, a), (end, b) in zip(stops, stops[1:]):
        if t <= end:
            return Vector(a).lerp(Vector(b), smoother((t - start) / (end - start)))
    return Vector(stops[-1][1])


def add_blocks(obj, boxes, material):
    """Add chamfer-friendly small cockpit details within the existing part."""
    if material.name in obj.data.materials:
        index = obj.data.materials.find(material.name)
    else:
        index = len(obj.data.materials)
        obj.data.materials.append(material)
    topology = bmesh.new()
    topology.from_mesh(obj.data)
    obj_inverse = obj.matrix_world.inverted()
    uv = topology.loops.layers.uv.verify()
    for center, size in boxes:
        box = bmesh.ops.create_cube(topology, size=1)
        vertices = set(box["verts"])
        for vertex in vertices:
            vertex.co = obj_inverse @ (Vector(center) + Vector(tuple(a*b for a,b in zip(vertex.co,size))))
        for face in {face for vertex in vertices for face in vertex.link_faces}:
            face.material_index = index
            for loop in face.loops:
                co = obj.matrix_world @ loop.vert.co
                loop[uv].uv = (co.x+co.y*.38, co.z+co.y*.28)
    topology.normal_update()
    topology.to_mesh(obj.data)
    topology.free()


def open_well(obj, bounds, lining):
    # Reconnect the split GLB UV vertices before cutting the solid exterior.
    topology = bmesh.new()
    topology.from_mesh(obj.data)
    bmesh.ops.remove_doubles(topology, verts=list(topology.verts), dist=.000012)
    bmesh.ops.recalc_face_normals(topology, faces=list(topology.faces))
    topology.to_mesh(obj.data)
    topology.free()
    lo, hi = map(Vector, bounds)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(lo+hi)*.5)
    cutter = bpy.context.object
    for vertex in cutter.data.vertices:
        vertex.co = Vector(tuple(x*y for x,y in zip(vertex.co,hi-lo)))
    cutter.data.materials.append(lining)
    bpy.context.view_layer.objects.active = obj
    boolean = obj.modifiers.new("Recessed cockpit opening", "BOOLEAN")
    boolean.operation, boolean.solver = "DIFFERENCE", "EXACT"
    boolean.object, boolean.material_mode = cutter, "TRANSFER"
    bpy.ops.object.modifier_move_up(modifier=boolean.name)
    bpy.ops.object.modifier_apply(modifier=boolean.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def cockpit(scene, config):
    bpy.context.window.scene = scene
    bpy.ops.import_scene.gltf(filepath=str(ROOT/"Export"/(config["glb"]+".glb")))
    rig = next(obj for obj in scene.objects if obj.type == "ARMATURE")
    rig.data.pose_position = "REST"
    bpy.context.view_layer.update()
    body = bpy.data.objects[config["body"]]
    seat = bpy.data.objects[config["seat_part"]]
    liner = bpy.data.materials.new(scene.name+" interior liner")
    liner.diffuse_color = (.055,.064,.067,1)
    liner.use_nodes = True
    bsdf = liner.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = liner.diffuse_color
    bsdf.inputs["Roughness"].default_value = .76
    open_well(body,config["well"],liner)
    for bounds in config["entries"]:
        open_well(body,bounds,liner)
    # Reuse the existing assembly node, material/skin slot and part identity.
    seat.data.clear_geometry()
    sx,sy,sz = config["seat"]
    controls = config["control_x"]
    control_z = config["control_z"]
    cush = [((sx+.08,sy,sz-.030),(.40,.38,.060)),
            ((sx-.16,sy,sz+.116),(.076,.38,.290))]
    if scene.name=="Coupe":
        cush += [((x,-y,z),dims) for (x,y,z),dims in cush]
    add_blocks(seat,cush,liner)
    controls_blocks = [((controls+.05,sy,control_z-.13),(.145,.15,.25)),
                      ((controls,sy,control_z),(.076,.37,.048)),
                      ((config["pedal_x"]+.03,sy,config["foot_z"]-.1),(.10,.33,.09))]
    seat_base = config["well"][0][2]
    for seat_y in (sy,-sy) if scene.name=="Coupe" else (sy,):
        controls_blocks.append(((sx+.005,seat_y,(sz+seat_base-.05)/2),
                                (.28,.30,sz-seat_base-.03)))
    for side in config["sides"]:
        gx,gy,gz = config["grip"]
        controls_blocks.append(((gx,side*gy,gz),(.24,.049,.054)))
        # Keep the grab rail attached to the uncut front edge of the sill.
        controls_blocks.append(((gx+.093,side*(gy-.039),gz-.098),(.045,.138,.211)))
        controls_blocks.append(((config["entry_x"],side*config["step_y"],config["step_z"]-.162),
                                (.32,.22,.044)))
        controls_blocks.append(((config["entry_x"],side*(config["step_y"]-.17),config["step_z"]-.173),
                                (.22,.31,.08)))
    machinery = next((m for m in body.data.materials if m and "steel" in m.name.lower()),liner)
    add_blocks(seat,controls_blocks,machinery)
    bpy.context.view_layer.objects.active = seat
    bevel = seat.modifiers.new("Cushion and instrument edge chamfers","BEVEL")
    bevel.width,bevel.segments = .015,1
    bevel.limit_method = "ANGLE"
    bpy.ops.object.modifier_move_up(modifier=bevel.name)
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    for obj in (body,seat):
        obj.vertex_groups.clear()
        obj.vertex_groups.new(name=config["body_bone"]).add(list(range(len(obj.data.vertices))),1,"REPLACE")
        obj.data.calc_loop_triangles()
        assert 0 < len(obj.data.loop_triangles) < 800
    rig.data.pose_position = "POSE"
    body_polygons = [tuple(poly.vertices) for poly in body.data.polygons]
    body_points = [body.matrix_world @ vertex.co for vertex in body.data.vertices]
    tree = BVHTree.FromPolygons(body_points,body_polygons)
    foot = Vector((config["pedal_x"]-.13,config["seat"][1],2.5))
    hit,*_ = tree.ray_cast(foot,(0,0,-1))
    assert hit is not None and abs(hit.z-config["well"][0][2]) < .002,(scene.name, hit)
    return rig


def rider_pose(rig, config, t, side=1, seated=False, response=""):
    seat_x,seat_y,seat_z = config["seat"]
    hips = (seat_x,seat_y,seat_z+.09)
    approach = config["entry_x"]
    lean = -config["recline"]
    stand = config["stand_y"]
    step = config["step_y"]
    step_height = config["step_z"]
    amount = pulse(t,.06,.41,.95) if response else 0.
    if seated:
        hip = point(hips)
        gaze = .023 * math.sin(math.tau*t) if not response else 0.
        fore = 0.
        roll = amount * {"left": -.10, "right": .10}.get(response,0.)
        pitch = amount * {"ascent": -.062, "touchdown": .095}.get(response,0.)
    else:
        hip = point(smooth_track(t,[(0,(approach,side*stand,.91)),
            (.25,(approach,side*(stand-.10),.84)),
            (.37,(approach,side*(step+.20),.855)),
            (.53,(approach,side*(step-.015),step_height+.57)),
            (.64,(approach-.025,side*(step-.24),step_height+.61)),
            (.82,hips),(1,hips)]))
        gaze = side*.18*pulse(t,.06,.34,.71)
        fore = side*-.84*pulse(t,.06,.30,.83)
        roll = side*.12*pulse(t,.17,.57,.85)
        pitch = .27*pulse(t,.17,.50,.83)
        lean *= smoother((t-.59)/.26)

    pose=Pose(rig)
    pose.absolute("pelvis", rz(fore)@rx(lean*.39),hip)
    pose.relative("spine",rx(lean*.61+pitch+gaze*.16)@ry(roll))
    pose.relative("chest",rx(-pitch*.28)@ry(roll*.45))
    pose.relative("head",rx(-lean*.40-pitch*.22)@rz(gaze))
    for tag, sign in (("L",1),("R",-1)):
        pedal = (config["pedal_x"]-.13,seat_y+sign*.12,config["foot_z"])
        control = (config["control_x"]-.149,seat_y+sign*.151,config["control_z"]+.015)
        if seated:
            ankle,wrist = point(pedal),point(control)
            palm = rx(-1.18)
        else:
            start = (approach+sign*.095,side*stand+sign*.095,.14)
            if sign==side:
                planted = (approach-.11,side*(step+.045),step_height)
                footpath = [(0,start),(.38,start),(.49,(approach-.11,side*(step+.15),step_height+.17)),
                            (.56,planted),(.64,planted),(.74,(approach-.08,side*(step-.36),step_height+.22)),
                            (.88,pedal),(1,pedal)]
            else:
                footprint = (approach+.07,side*(step-.025),step_height)
                footpath = [(0,start),(.16,start),(.28,(approach+sign*.08,side*(step+.05),step_height+.18)),
                            (.37,footprint),(.55,footprint),(.68,(approach+.12,side*(step-.30),step_height+.22)),
                            (.82,pedal),(1,pedal)]
            ankle = point(smooth_track(t,footpath))
            idle_wrist = (approach+.08,side*stand+sign*.25,1.025)
            grip = (config["grip"][0]-.052,side*(config["grip"][1]+.025),config["grip"][2]+.035)
            if sign==side:
                grip = (grip[0]+.095,grip[1]+side*.12,grip[2]+.16)
            wrist = point(smooth_track(t,[(0,idle_wrist),(.10,idle_wrist),(.24,grip),(.46,grip),
                                         (.94,control),(1,control)]))
            palm = rx(-1.18*smoother((t-.1)/.6))@rz(fore*.22)
        pose.chain("thigh."+tag,"shin."+tag,"foot."+tag,ankle,(sign*.12,-1,.11))
        pose.chain("upper_arm."+tag,"forearm."+tag,"hand."+tag,wrist,(sign*.65,.50,-.58),palm)
    return pose


def bind_action(rig, action):
    animation = rig.animation_data
    animation.use_nla = False
    animation.action = action
    animation.action_slot = action.slots[0]


def preview_rider(source, scene, actions, kind, rig, output):
    parts = json.loads((ROOT/"Documentation/assembly_recipes.json").read_text("utf-8"))["Player_Explorer"]
    names = [name for name in parts["parts"] if not name.startswith(("Accessory_","Weapon_"))]
    with bpy.data.libraries.load(str(LIBRARY),link=False) as (_,target):
        target.objects = names
    rider = source.copy()
    rider.name = kind+" | HUMAN_7H rider"
    scene.collection.objects.link(rider)
    rider.rotation_euler.z = math.pi/2
    for part in target.objects:
        scene.collection.objects.link(part)
        part.parent = rider
        for modifier in part.modifiers:
            if modifier.type == "ARMATURE":
                modifier.object = rider
    rig.animation_data.use_nla = False
    canopy = next(track.strips[0].action for track in rig.animation_data.nla_tracks
                  if track.name == "Canopy_Open")
    bind_action(rig,canopy)
    # Sample the stock canopy's open endpoint in the review project.
    open_frame = round(canopy.frame_range[1])
    scene.frame_set(open_frame)
    evaluated = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    opened = {bone.name:bone.matrix_basis.copy() for bone in evaluated.pose.bones}
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = opened[bone.name]

    camera_data = bpy.data.cameras.new(kind+" review camera")
    camera = bpy.data.objects.new(camera_data.name,camera_data)
    scene.collection.objects.link(camera)
    camera.location = (4.6,5.0,3.25) if kind=="Coupe" else (4.9,5.7,3.9)
    focus = Vector((.24,.05,.99) if kind=="Coupe" else (.88,.10,1.28))
    camera.rotation_euler = (focus-camera.location).to_track_quat("-Z","Y").to_euler()
    camera_data.type,camera_data.ortho_scale = "ORTHO", 3.75 if kind=="Coupe" else 4.35
    scene.camera = camera
    scene.world = bpy.data.worlds.new(kind+" studio")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes["Background"].inputs[0].default_value=(.22,.27,.32,1)
    scene.world.node_tree.nodes["Background"].inputs[1].default_value=.75
    for label,position,power in (("Key",(2,1,5),680),("Side",(-3,2,3),400),("Fill",(-1,-4,3),300)):
        data = bpy.data.lights.new(kind+" "+label,"AREA")
        data.energy,data.size=power,4
        light=bpy.data.objects.new(data.name,data)
        scene.collection.objects.link(light)
        light.location=position
        light.rotation_euler=(focus-light.location).to_track_quat("-Z","Y").to_euler()
    scene.render.engine,scene.cycles.samples = "CYCLES",20
    scene.cycles.use_denoising = True
    scene.render.resolution_x,scene.render.resolution_y,scene.render.resolution_percentage = 1160,800,100
    ground = bpy.data.meshes.new(kind+" review floor")
    ground.from_pydata([(-200,-200,-.015),(200,-200,-.015),(200,200,-.015),(-200,200,-.015)],[],[(0,1,2,3)])
    scene.collection.objects.link(bpy.data.objects.new(ground.name,ground))
    reviews = [(kind+"_Enter_Left",.44),
               ("Coupe_Driver_Idle_Loop" if kind=="Coupe" else "Courier_Pilot_Idle_Loop", .31)]
    bind_action(rider,actions[reviews[1][0]])
    check_closed_canopy(scene,rig,rider)
    for action_name,phase in reviews:
        bind_action(rider,actions[action_name])
        scene.frame_start,scene.frame_end=0,round(actions[action_name].frame_range[1])
        scene.frame_set(round(phase*scene.frame_end))
        scene.render.filepath=str(output/(action_name+".png"))
        bpy.ops.render.render(write_still=True)
    bind_action(rider,actions[reviews[0][0]])
    scene.frame_end=round(actions[reviews[0][0]].frame_range[1])
    scene.frame_set(round(reviews[0][1]*scene.frame_end))
    scene.render.filepath="//../Preview/Boarding/"+reviews[0][0]+".png"
    return rider


def evaluated_geometry(obj,depsgraph):
    evaluated=obj.evaluated_get(depsgraph)
    mesh=evaluated.to_mesh()
    geometry=([evaluated.matrix_world@vertex.co for vertex in mesh.vertices],
              [tuple(face.vertices) for face in mesh.polygons])
    evaluated.to_mesh_clear()
    return geometry


def check_closed_canopy(scene, rig, rider):
    rig.data.pose_position="REST"
    scene.frame_set(0)
    depsgraph=bpy.context.evaluated_depsgraph_get()
    canopy=[BVHTree.FromPolygons(*evaluated_geometry(obj,depsgraph)) for obj in scene.objects
            if obj.type=="MESH" and "_Canopy_" in obj.name]
    head=[obj for obj in scene.objects if obj.parent==rider
          and obj.type=="MESH" and obj.name.startswith(("Head_","Hair_","Body_Neck"))]
    assert canopy and head
    for frame in (0,21,42,63,84):
        scene.frame_set(frame)
        depsgraph=bpy.context.evaluated_depsgraph_get()
        for obj in head:
            tree=BVHTree.FromPolygons(*evaluated_geometry(obj,depsgraph))
            assert not any(glazing.overlap(tree) for glazing in canopy), (scene.name,obj.name,frame)
    rig.data.pose_position="POSE"


def check_boarding_actions(path, details, actor):
    document, binary = read_glb(path)
    contact_report = {}
    for clip in document["animations"]:
        contract = details[clip["name"]]
        duration = 0
        for channel in clip["channels"]:
            sampler = clip["samplers"][channel["sampler"]]
            duration = max(duration,accessor(document,binary,sampler["input"])[-1][0])
            target = channel["target"]
            node = document["nodes"][target["node"]]["name"]
            values = accessor(document,binary,sampler["output"])
            if node == "root":
                assert all(all(abs(a-b)<1e-6 for a,b in zip(value,values[0])) for value in values)
            if contract["loop"]:
                assert max(abs(a-b) for a,b in zip(values[0],values[-1])) < 2e-5
        assert abs(duration-contract["duration_s"]) < 2e-5
    for kind, config in CONFIG.items():
        actor_points = []
        for sign in config["sides"]:
            touch = rider_pose(actor,config,ENTER_EVENTS["step_contact"],side=sign)
            raised_leg = "foot.R" if sign>0 else "foot.L"
            expected = point((config["entry_x"]+.07,sign*(config["step_y"]-.025),config["step_z"]))
            miss = (touch.fk(raised_leg).translation-expected).length
            assert miss<.018, (kind, sign, miss)
            actor_points.append(round(miss,6))
        contact_report[kind] = {"step_contact_error_m":actor_points,
                                "explorer_normal_closed_canopy_frames":[0,21,42,63,84]}
    return contact_report


def write_glb(path, scene):
    bpy.context.window.scene = scene
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_active_scene=True,
                             export_yup=True, export_animations=True, export_animation_mode="NLA_TRACKS",
                             export_skins=True, export_def_bones=False, export_frame_range=False,
                             export_extras=True, export_image_format="AUTO")


def main():
    output = Path(sys.argv[sys.argv.index("--")+1]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    actor, review = legacy_source()
    bpy.context.window.scene=review
    actions={}
    details={}
    for kind,config in CONFIG.items():
        for sign in config["sides"]:
            side="Left" if sign>0 else "Right"
            for entering,phase in ((True,"Enter"),(False,"Exit")):
                frames = (56 if entering else 51)+(9 if kind=="Courier" else 0)
                name=f"{kind}_{phase}_{side}"
                poses=[rider_pose(actor,config,(i if entering else frames*2-i)/(frames*2),side=sign)
                       for i in range(frames*2+1)]
                events = ENTER_EVENTS if entering else EXIT_EVENTS
                actions[name]=register_action(actor,"HUMAN_7H",name,poses,False,events)
                details[name]={"duration_s":frames/30,"loop":False,"events":events}
    for name,(kind,frames,loop,response) in SEATED.items():
        poses=[rider_pose(actor,CONFIG[kind],i/(frames*2),seated=True,response=response)
               for i in range(frames*2+1)]
        poses[-1]=poses[0]
        actions[name]=register_action(actor,"HUMAN_7H",name,poses,loop,{})
        details[name]={"duration_s":frames/30,"loop":loop,"events":{}}
    path=output/"HUMAN_7H_Boarding.glb"
    stripped=export_bank(path,actor,actions)
    bpy.context.window.scene=review
    bpy.data.scenes.remove(stripped)
    check_bank(path,LEGACY_REFERENCE,actions)
    contact_report = check_boarding_actions(path,details,actor)

    for kind,config in CONFIG.items():
        scene=bpy.data.scenes.new(kind)
        scene.render.fps=30
        rig=cockpit(scene,config)
        path=output/(kind+"_Boarding.glb")
        write_glb(path,scene)
        before,_=read_glb(ROOT/"Export"/(config["glb"]+".glb"))
        after,raw=read_glb(path)
        first=skeleton(*read_glb(ROOT/"Export"/(config["glb"]+".glb")))
        updated=skeleton(after,raw)
        assert [item[:2] for item in first]==[item[:2] for item in updated]
        assert max(abs(a-b) for (_,_,x),(_,_,y) in zip(first,updated) for a,b in zip(x,y))<5e-5
        assert {a["name"] for a in before["animations"]}=={a["name"] for a in after["animations"]}
        assert len(before["meshes"])==len(after["meshes"])
        config["canopy_pivot_m"]=list(rig.data.bones["canopy"].head_local)
        config["original_driver_socket_m"]=list(rig.data.bones["driver_socket"].head_local)
        preview_rider(actor,scene,actions,kind,rig,output)
    (output/"vehicle_boarding_bank.json").write_text(json.dumps({
        "requests":["CQ-06B","CQ-06C"], "forward":"+X","up":"+Z","units":"meters",
        "actor_heading_from_human_to_vehicle_degrees":90,
        "actor_root_motion":"in_place", "pelvis_is_visual_only":True,
        "canopy_mechanism":["Canopy_Open","Canopy_Close"],
        "clips":details, "vehicle_contacts":CONFIG, "contact_checks":contact_report,
    },indent=2)+"\n",encoding="utf-8")
    bpy.context.window.scene=bpy.data.scenes["Coupe"]
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=="VIEW_3D":
                area.spaces.active.region_3d.view_perspective="CAMERA"
                area.spaces.active.overlay.show_overlays=False
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(output/"Vehicle_Boarding.blend"),compress=True)


if __name__ == "__main__":
    main()
