"""Write the support animation integration contract from validated clips."""
import json
import math

from texture_pilot import ROOT, write_json


def publish():
    source = json.loads((ROOT/"Validation/support_animation_authoring.json").read_text(encoding="utf-8"))
    check = json.loads((ROOT/"Validation/support_animation_delivery.json").read_text(encoding="utf-8"))
    assert source["all_checks_passed"] and check["all_checks_passed"]
    review = check["review"]
    patient_offset = source["treatment_patient_actor_offset_m"]
    rows = {}
    for name, row in source["clips"].items():
        state = name.rsplit("_",1)[-1]
        if name.startswith("Support_Treat"):
            events = {"Enter":{"work_ready":1}, "Loop":{"left_support":.50,"right_scan":.42},
                      "Exit":{"hands_released":.32,"locomotion_ready":1}}[state]
        elif name.startswith("Support_Inspect"):
            events = {"Enter":{"work_ready":1},"Loop":{"terminal_input_hover":.50},
                      "Exit":{"hands_clear":.45,"aim_blend_allowed":1}}[state]
        else:
            events = {"case_attached":0}
        rows[name] = {
            **row, "rate_hz":60, "root_motion":False, "root_yaw_deg":0,
            "events":[{"id":event,"phase":phase,"seconds":phase*row["duration_seconds"],"visual_only":True}
                      for event,phase in events.items()],
        }
    write_json(ROOT/"Documentation/support_animation_bank.json", {
        "schema":"erma.support_animation_bank.v1", "request":"CQ-07C2 / GFX-04",
        "rig_id":"HUMAN_7H", "bones":23, "base_clips":98, "shared_extension_clips":len(rows),
        "bank_glb":"Export/AnimationBanks/HUMAN_7H_Support.glb", "bank_sha256":source["bank_sha256"],
        "bank_bind_reference":"SupportBank_BindReference (ignore for rendering)",
        "clip_contract":rows,
        "duration_authority":"game; use loop sampling during the current treatment/inspection action clock",
        "interruption":{"from":"currently evaluated pose at any phase","blend_seconds":[.12,.20],
                        "to":"game-selected idle, aim, locomotion or hit response",
                        "release_visual_props_immediately":True,"resume_phase_for_walk":"retain current gait phase"},
        "actor_wrist_space":"Blender authoring metres; HUMAN_7H +X left, -Y forward, +Z up",
        "support_targets":{
            "treatment":{"reference_patient_action":"Downed_Back_Loop",
                         "patient_actor_offset_m":patient_offset,"patient_actor_yaw_deg":source["treatment_patient_actor_yaw_deg"],
                         "reference_actor_distance_m":math.hypot(*patient_offset[:2]),
                         "game_target_acquisition_max_m":1.8,
                         "contact":"left hand on torso; right hand performs short hover gesture",
                         "measured_left_min_gap_m":min(row[0] for row in review["47_Support_Treatment"]["closest_samples_m"]),
                         "kit":"Interior_Med_Kit, placed next to the pair"},
            "terminal":{"asset":"Interior_Terminal","reference_actor_distance_m":.84,"game_target_acquisition_max_m":.9,
                        "terminal_actor_offset_m":[0,-.84,0],"terminal_yaw_deg":180,
                        "keyboard_center_local_m":[0,-.28,.977],"contact":"right hand hover over control shelf",
                        "min_vertex_gap_m":min(review["48_Support_Terminal"]["closest_samples_m"])},
            "carry":{"asset":"Interior_Med_Kit","use":"shared sealed case for the supply pickup",
                     "attach_bone":"hand.R","attachment_matrix_authoring_row_major":source["carry_case_hand_R_local_matrix"],
                     "grip_check_m":review["49_Support_Carry"]["case_socket_max_error_m"],
                     "mask":"human_upper_body","walk_reference":"Walk",
                     "walk_phase":{"left_contact":0,"right_contact":.5},
                     "simultaneous_weapon_in_hands":False},
        },
        "references":{"reviewed":"2026-11-23","method":"public Mixamo previews; hand-authored on HUMAN_7H",
                      "motions":[
                          {"name":"Kneeling Inspecting","query":"kneel","observed":"one-knee load and lowered gaze"},
                          {"name":"Button Pushing","query":"button","observed":"reach, brief hover and withdrawal"},
                          {"name":"Box Idle","query":"carry","observed":"hold the load near the body"},
                          {"name":"Box Walk Arc","query":"carry","observed":"stable forearms during a step"},
                      ]},
    })


if __name__ == "__main__":
    publish()
