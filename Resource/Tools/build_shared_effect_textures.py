"""Build the tileable linear textures requested by GFX-06."""

import hashlib
import json
from pathlib import Path

import numpy as np

from human_sheet_maps import write_png


ROOT = Path(__file__).resolve().parents[1]
SIZE = 512


def noise(rng, u, v, columns, rows):
    grid = rng.random((rows, columns))
    x, y = u * columns, v * rows
    ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = grid[iy % rows, ix % columns]
    b = grid[iy % rows, (ix + 1) % columns]
    c = grid[(iy + 1) % rows, ix % columns]
    d = grid[(iy + 1) % rows, (ix + 1) % columns]
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def contrast(values):
    lo, hi = np.percentile(values, (2, 98))
    return np.clip((values - lo) / (hi - lo), 0, 1)


def mask_textures(u, v):
    rng = np.random.default_rng(11606)
    water_shape = noise(rng, u, v, 7, 7)
    ripple = .5 + .5 * np.cos(2 * np.pi * (8 * u + 5 * v + 1.8 * water_shape))
    water = contrast(.61 * ripple**2 + .25 * water_shape + .14 * noise(rng, u, v, 34, 31))

    plume = noise(rng, u, v, 13, 6)
    billows = noise(rng, u + .11 * plume, v, 23, 11)
    exhaust = contrast(.52 * plume + .31 * billows + .17 * noise(rng, u, v, 43, 24))

    line_noise = noise(rng, u, v, 2, 64)
    scan = .5 + .5 * np.cos(2 * np.pi * (48 * v + .12 * line_noise))
    holo = contrast(.47 * line_noise + .43 * scan**4 + .10 * noise(rng, u, v, 32, 96))

    dust_base = noise(rng, u, v, 11, 10)
    granules = noise(rng, u, v, 77, 69)
    dust = contrast((.28 + .72 * dust_base) * (.45 * dust_base + .55 * granules**3))
    return np.stack((water, exhaust, holo, dust), axis=-1), water_shape


def flow_texture(water_shape):
    # A bounded curl field from a periodic stream function. Keep vector magnitude.
    dx = (np.roll(water_shape, -1, 1) - np.roll(water_shape, 1, 1)) * .5
    dy = (np.roll(water_shape, -1, 0) - np.roll(water_shape, 1, 0)) * .5
    scale = np.hypot(dx, dy).max()
    return np.stack((.5 + .48 * dy / scale, .5 - .48 * dx / scale,
                     np.zeros_like(water_shape)), axis=-1)


def verify_texture(pixels, channels):
    assert pixels.shape == (SIZE, SIZE, channels)
    assert np.isfinite(pixels).all() and pixels.min() >= 0 and pixels.max() <= 1
    for channel in pixels.transpose(2, 0, 1):
        if np.ptp(channel) < 1e-8:
            continue
        for axis in (0, 1):
            rows = np.moveaxis(channel, axis, 0)
            seam = np.mean(np.abs(rows[-1] - rows[0]))
            differences = np.mean(np.abs(np.diff(rows, axis=0)), axis=1)
            assert seam <= np.percentile(differences, 99) * 1.10, (axis, seam)


def main():
    relative = Path("Export/Textures/SharedEffects")
    destination = ROOT / relative
    destination.mkdir(parents=True, exist_ok=True)
    coordinates = (np.arange(SIZE, dtype=float) + .5) / SIZE
    u, v = np.meshgrid(coordinates, coordinates)
    masks, water_shape = mask_textures(u, v)
    shifted, shifted_shape = mask_textures(u + 1, v - 1)
    assert np.allclose(masks, shifted, atol=1e-10)
    assert np.allclose(water_shape, shifted_shape, atol=1e-10)
    flow = flow_texture(water_shape)
    textures = {}
    for name, pixels, channels in (("Frontier_Effects_Masks", masks, 4),
                                    ("Frontier_Water_Flow", flow, 3)):
        verify_texture(pixels, channels)
        image = destination / (name + ".png")
        write_png(image, pixels)
        textures[name] = {
            "file": (relative / image.name).as_posix(),
            "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        }
    textures["Frontier_Effects_Masks"]["channels"] = {
        "R": "water_ripples", "G": "exhaust_and_flame_breakup",
        "B": "signal_hologram_scan", "A": "mine_dust_density",
    }
    textures["Frontier_Water_Flow"].update({
        "channels": {"R": "U", "G": "V in PNG row direction", "B": "unused"},
        "decode": "sample.rg * 2 - 1",
    })
    manifest = {
        "request": "GFX-06",
        "size": [SIZE, SIZE],
        "color_space": "linear",
        "wrap": "repeat",
        "textures": textures,
        "material_defaults": {
            "water": {"mask": "R", "tile_uv": [1, 1], "scroll_uv_per_second": [.021, -.013],
                      "flow_strength_uv_per_second": .035},
            "exhaust": {"mask": "G", "tile_uv": [1, 2], "scroll_uv_per_second": [0, -.67],
                        "opacity_multiplier": .70},
            "signal_hologram": {"mask": "B", "tile_uv": [1, 1], "scroll_uv_per_second": [0, -.17],
                                "modulation_strength": .23},
            "mine_dust": {"mask": "A", "tile_uv": [1, 1], "scroll_uv_per_second": [.042, -.033],
                          "opacity_multiplier": .44},
        },
    }
    path = ROOT / "Documentation/shared_effect_texture_bank.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("GFX-06: verified and wrote two tileable linear 512 x 512 textures")


if __name__ == "__main__":
    main()
