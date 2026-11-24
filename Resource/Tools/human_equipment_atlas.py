"""UV layout and surface artwork for character armor."""
import numpy as np

from human_sheet_maps import SIZE, write_png


TILES = {
    "chest": (0, 0, 2), "abdomen": (2, 0, 1), "heavy": (3, 0, 1),
    "shoulder": (2, 1, 1), "leg": (3, 1, 1),
    "pack": (0, 2, 1), "wrist": (1, 2, 1), "miner": (2, 2, 1), "visor": (3, 2, 1),
    "explorer": (0, 3, 1), "medic": (1, 3, 1), "shin": (2, 3, 1),
    "research": (3, 3, .5), "resident": (3.5, 3, .5),
}
PADDING = 8


def tile_uv(tile, u, v):
    col, row, span = TILES[tile]
    tile_size = SIZE // 4
    inner = span * tile_size - 2 * PADDING
    return ((col * tile_size + PADDING + u * inner) / SIZE,
            1 - (row * tile_size + PADDING + (1 - v) * inner) / SIZE)


def tile_art(tile, side):
    rng = np.random.default_rng(9714 + list(TILES).index(tile))
    axis = (np.arange(side, dtype=np.float32) + .5 - PADDING) / (side - 2 * PADDING)
    u, v = np.meshgrid(axis, 1 - axis)
    grain = rng.normal(0, .005, u.shape)
    patch = np.cos(u * 23 + v * 37) * np.sin(v * 43 - u * 15)
    cloud = .011 * np.cos(u * 13 - v * 9) + .007 * patch + grain
    tint = (.344, .407, .503)
    if tile in {"wrist", "visor"}:
        tint = (.225, .252, .281)
    elif tile in {"explorer", "medic", "shin"}:
        tint = (.78, .743, .670)
    elif tile in {"heavy", "miner", "research", "resident"}:
        tint = {"heavy":(.38,.37,.41),"miner":(.29,.28,.27),
                "research":(.26,.36,.34),"resident":(.62,.43,.25)}[tile]
    color = np.array(tint) + cloud[..., None]
    height = grain * .42 + patch * .002
    ao = np.full_like(u, .99)
    rough = np.clip(.53 + 2 * cloud, 0, 1)
    metal = np.full_like(u, .28)

    def rectangle(center, half, radius=.012, angle=0):
        du, dv = u - center[0], v - center[1]
        a = np.cos(angle)
        b = np.sin(angle)
        dx = np.abs(du*a + dv*b) - half[0] + radius
        dy = np.abs(-du*b + dv*a) - half[1] + radius
        return np.hypot(np.maximum(dx, 0), np.maximum(dy, 0)) + np.minimum(np.maximum(dx, dy), 0) - radius

    def cover(distance, width=.004):
        return np.clip(.5 - distance / width, 0, 1)

    def paint(distance, rgb, lift=0., gloss=.52, metallic=.14):
        nonlocal color, height, rough, metal
        mask = cover(distance)
        color = color * (1 - mask[..., None]) + (np.asarray(rgb) + cloud[..., None] * .65) * mask[..., None]
        height += lift * cover(distance, .017)
        rough = rough * (1 - mask) + gloss * mask
        metal = metal * (1 - mask) + metallic * mask

    def groove(distance, width=.006):
        nonlocal color, height, ao
        mask = np.exp(-(distance / width) ** 2)
        color *= 1 - .31 * mask[..., None]
        ao -= .16 * mask
        height -= .040 * mask

    def fitting(x, y, r=.013):
        distance = np.hypot(u - x, v - y) - r
        groove(distance, .004)
        paint(distance + .004, (.34, .37, .40), -.016, .39, .53)
        paint(rectangle((x, y), (r * .58, .003), .002, -.55), (.19, .23, .25), -.018)

    # Fine weathering follows the panel perimeter and leaves broad painted areas readable.
    rim = np.minimum.reduce((u, 1 - u, v, 1 - v))
    chips = np.clip((patch + rng.normal(0, .34, u.shape) - .62) * .41, 0, .32)
    chips *= np.exp(-((rim - .040) / .034) ** 2)
    color += chips[..., None] * np.array((.31, .25, .17))
    rough += chips * .5
    height -= chips * .041
    cyan = (.48, .78, .90)
    if tile == "chest":
        paint(rectangle((.729, .705), (.090, .057), .013, -.065), (.16, .20, .24), -.041)
        paint(rectangle((.729, .705), (.073, .039), .009, -.065), cyan, -.018, .34, .18)
        paint(rectangle((.726, .720), (.058, .005), .002, -.065), (.76, .93, .96), 0, .34)
        paint(rectangle((.704, .695), (.034, .004), .002, -.065), (.24, .54, .66), 0, .36)
        groove(rectangle((.50, .955), (.087, .058), .020), .005)
        for x in (.11, .89):
            fitting(x, .14)
            fitting(x, .84)
    elif tile.startswith("abdomen"):
        groove(rectangle((.50, .83), (.145, .057), .015))
        paint(rectangle((.5, .83), (.119, .030), .010), (.24, .27, .31), -.029, .39)
        for x in (.12, .88):
            fitting(x, .50)
    elif tile == "shoulder":
        for x in (.31, .49):
            paint(rectangle((x, .36), (.033, .20), .005, -.37), (.73, .65, .50), .003)
        paint(rectangle((.68, .71), (.054, .025), .008, -.20), cyan, -.022, .44)
        fitting(.83, .75, .025)
    elif tile == "leg":
        for y in (.22, .29):
            paint(rectangle((.51, y), (.165, .017), .006, .37), (.70, .63, .50), .003)
        groove(rectangle((.50, .64), (.24, .15), .050), .004)
        for x in (.18, .82):
            fitting(x, .85, .021)
    elif tile == "pack":
        groove(rectangle((.50, .55), (.26, .30), .09), .007)
        for x in (.28, .72):
            paint(rectangle((x, .64), (.026, .13), .015, .45 if x < .5 else -.45), cyan, -.027, .37)
            fitting(x, .24, .024)
        for y in (.30, .38, .46):
            groove(rectangle((.5, y), (.10, .020), .018), .006)
    elif tile == "wrist":
        groove(rectangle((.5, .5), (.36, .38), .08))
        for y in (.48, .64):
            paint(rectangle((.50, y), (.17, .028), .014), cyan, -.026, .35)
        fitting(.5, .22, .032)
    elif tile == "visor":
        paint(rectangle((.50, .65), (.29, .095), .053), (.081, .116, .157), -.035, .22, .42)
        paint(rectangle((.50, .67), (.22, .023), .015), (.35, .63, .78), -.004, .29)
        for y in (.24, .32, .40):
            groove(rectangle((.50, y), (.105, .019), .012))
    elif tile == "heavy":
        paint(rectangle((.50, .207), (.146, .063), .017), (.16, .16, .18), -.035)
        paint(rectangle((.50, .207), (.108, .029), .010), (.92, .65, .32), -.012, .40)
        for x in (.12, .88):
            fitting(x, .47, .017)
    elif tile == "miner":
        for y in (.19, .36, .53, .70):
            groove(rectangle((.50, y), (.32, .022), .012))
        paint(rectangle((.50,.53), (.25,.010), .008), (.69,.62,.45))
    elif tile == "research":
        for x in (.65,.73):
            paint(rectangle((x,.55), (.023,.095), .012, .10), (.71,.57,.80), .002)
        groove(rectangle((.57,.54), (.25,.23), .045), .009)
    elif tile == "resident":
        paint(rectangle((.71,.68), (.087,.033), .020), (.71,.56,.38), .012)
        paint(rectangle((.71,.68), (.069,.018), .012), (.80,.70,.55), .007)
        rough[:] = .80 + cloud
        metal[:] = 0
    else:
        tone = (.72, .34, .11) if tile in {"explorer", "shin"} else (.68, .26, .22)
        angle = {"explorer": .56, "medic": .60, "shin": 1.28}[tile]
        paint(rectangle((.50, .52), (.33, .071), .009, angle), tone, .003)
        if tile == "medic":
            paint(rectangle((.47, .62), (.080, .067), .004, angle + np.pi/2), tone, .003)

    dy, dx = np.gradient(height)
    z = np.ones_like(height)
    normal = np.stack((-dx * 5, dy * 5, z), axis=2)
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    return {"BaseColor": color, "Normal": normal * .5 + .5, "ORM": np.stack((ao, rough, metal), axis=2)}


def write_equipment_atlas(folder):
    maps = {name: np.zeros((SIZE, SIZE, 3), dtype=np.float32) for name in ("BaseColor", "Normal", "ORM")}
    for tile, (col, row, span) in TILES.items():
        step = SIZE // 4
        x, y, size = int(col * step), int(row * step), int(span * step)
        for channel, pixels in tile_art(tile, size).items():
            maps[channel][y:y+size, x:x+size] = pixels
    paths = {}
    for channel, pixels in maps.items():
        path = folder / f"HumanSheet_Equipment_{channel}.png"
        write_png(path, pixels)
        paths[channel] = path
    return paths
