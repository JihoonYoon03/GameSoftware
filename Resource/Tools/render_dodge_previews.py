"""Render the two staged review scenes to frames for delivery GIFs."""
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_dodge_animations import STAGE
from stage_dodge_delivery import BRANCHES, SHOWCASE

for name, tag in [(SHOWCASE, "directions"), (BRANCHES, "interrupts")]:
    scene = bpy.data.scenes[name]
    bpy.context.window.scene = scene
    scene.render.resolution_percentage = 68
    scene.cycles.samples = 6
    for i in range(13):
        frame = scene.frame_end * i / 12
        scene.frame_set(int(frame), subframe=frame % 1)
        scene.render.filepath = str(STAGE / f"{tag}_{i:02}.png")
        bpy.ops.render.render(write_still=True)
    print("DODGE_RENDERED", tag, flush=True)
