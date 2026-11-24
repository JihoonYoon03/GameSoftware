"""Formed armor shells for the service guard, with texture-only markings."""
from math import sin, tau

from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import tessellate_polygon

from human_equipment_atlas import tile_uv
from human_sheet_forms import Geometry, LINER, Surface, choose, leg_lateral, shell


MARKED = Surface("Equipment", "strand")
EDGE = shell("darkblue")


def fit_surface(suit, gap):
    mesh = BVHTree.FromPolygons(suit.v, suit.faces)

    def fitted(x, z, y):
        surface = mesh.ray_cast(Vector((x, -1, z)), Vector((0, 1, 0)))[0]
        if surface is None:
            return y
        clearance = surface.y - gap
        width = .012
        blend = max(0, width - abs(y - clearance)) / width
        return min(y, clearance) - blend * blend * width * .25

    return fitted


def form_panel(g, contour, crown, stage, tile, bone, *, thickness=.016, bevel=.004, rear=False, layers=None):
    """Close a curved grid with a narrow bevel and a separate thickness face."""
    count = len(contour)
    layers = layers or choose(stage, (0, .17, .35, .54, .74, .90, 1), (0, .24, .51, .77, 1),
                              (0, .34, .64, 1), (0, .52, 1))
    center_z = sum((lo + hi) * .5 for _, lo, hi in contour) / count
    xmin, xmax = contour[0][0], contour[-1][0]
    midpoint = (xmin + xmax) * .5
    zmin = min(lo for _, lo, _ in contour)
    zspan = max(hi for _, _, hi in contour) - zmin
    inset_x = 1 - 2 * bevel / (xmax - xmin)
    inset_z = 1 - 2 * bevel / zspan
    direction = -1 if rear else 1

    def point(shape, depth):
        g.v.append(Vector(shape) + Vector((0, direction * depth, 0)))
        g.weights.append({bone: 1})
        return len(g.v) - 1

    front = []
    tex = {}
    for t in layers:
        for x, low, high in contour:
            z = low + (high - low) * t
            xx = midpoint + (x - midpoint) * inset_x
            zz = center_z + (z - center_z) * inset_z
            vertex = point((xx, crown(xx, zz), zz), 0)
            front.append(vertex)
            u = (x - xmin) / (xmax - xmin)
            v = (z - zmin) / zspan if tile == "chest" else t
            tex[vertex] = tile_uv(tile, 1 - u if rear else u, v)

    def face(vertices, material, smooth=False):
        ids = tuple(reversed(vertices)) if rear else vertices
        g.poly(ids, material, smooth, [tex[i] for i in ids] if material == MARKED else None)

    for j in range(len(layers) - 1):
        for i in range(count - 1):
            k = j * count + i
            quad = (k, k + 1, k + count + 1, k + count)
            face(tuple(front[n] for n in quad), MARKED, True)

    last = (len(layers) - 1) * count
    perimeter = [*range(count), *range(2 * count - 1, last + count, count),
                 *range(last + count - 2, last - 1, -1), *range(last - count, 0, -count)]
    rim = []
    back = []
    for k in perimeter:
        x, low, high = contour[k % count]
        z = low + (high - low) * layers[k // count]
        if stage<2:
            rim.append(point((x, crown(x, z), z), bevel))
        back.append(point((x, crown(x, z), z), thickness))
    for j, a in enumerate(perimeter):
        m = (j + 1) % len(perimeter)
        b = perimeter[m]
        if rim:
            face((front[b], front[a], rim[j], rim[m]), EDGE)
            face((rim[m], rim[j], back[j], back[m]), EDGE)
        else:
            face((front[b], front[a], back[j], back[m]), EDGE)
    coords = [Vector((g.v[i].x, g.v[i].z, 0)) for i in back]
    for triangle in tessellate_polygon([coords]):
        face(tuple(back[i] for i in reversed(triangle)), LINER)
    return g


def symmetric_edges(left_to_center):
    return [*left_to_center, *((-x, low, high) for x, low, high in reversed(left_to_center[:-1]))]


def chest(stage, fitted):
    g = Geometry()
    stages = (
        [(-.198,1.327,1.382),(-.183,1.283,1.414),(-.160,1.271,1.450),(-.131,1.286,1.467),
         (-.103,1.314,1.474),(-.078,1.327,1.460),(-.054,1.332,1.444),(-.027,1.333,1.442),
         (0,1.334,1.442)],
        [(-.198,1.327,1.382),(-.180,1.280,1.421),(-.156,1.273,1.453),(-.126,1.292,1.471),
         (-.093,1.319,1.470),(-.058,1.331,1.445),(-.028,1.333,1.442),(0,1.334,1.442)],
        [(-.198,1.327,1.382),(-.174,1.277,1.430),(-.145,1.280,1.461),(-.105,1.312,1.472),
         (-.055,1.331,1.445),(0,1.334,1.442)],
        [(-.197,1.320,1.388),(-.170,1.278,1.439),(-.129,1.290,1.471),(-.070,1.329,1.449),
         (0,1.334,1.442)],
    )
    def curve(x, z):
        y = -.152 + .082 * (x/.201)**2 + .019*((z-1.375)/.12)**2
        return fitted(x, z, y)

    form_panel(g, symmetric_edges(stages[stage + 1]), curve, stage, "chest", "chest", thickness=.014, bevel=.0034)
    # The two abdominal plates fit under the arch and pivot with the lower torso.
    for z, width in ((1.270, .153), (1.199, .135)):
        stations = choose(stage, (-1,-.86,-.61,-.27,0,.27,.61,.86,1),
                          (-1,-.82,-.48,0,.48,.82,1), (-1,-.79,0,.79,1), (-1,-.79,0,.79,1))
        section = [(u * width, z - .027 + .018 * abs(u)**5, z + .036 - .015 * abs(u)**4) for u in stations]
        form_panel(g, section, lambda x, height: fitted(x,height,-.145+.055*(x/width)**2+.08*(height-z)),
                   stage, "abdomen", "spine", thickness=.012, bevel=.0034,
                   layers=(0,.36,.72,1) if stage<0 else (0,.5,1))
    return g


def hard_profile(g, x, z, width, height, y, depth, stage, tile, bone, *, rear=False, skew=0,
                 bulge=.014, fitted=None):
    positions = choose(stage, (-1,-.89,-.62,-.30,0,.30,.62,.89,1),
                       (-1,-.79,0,.79,1),(-1,-.77,0,.77,1),(-1,-.77,0,.77,1))
    outline = [(x+width*t, z+height*(-1+.25*abs(t)**4)+skew*t,
                z+height*(1-.32*abs(t)**4)+skew*t) for t in positions]
    sign = -1 if rear else 1
    def crown(xx, zz):
        face_y = y+sign*bulge*((xx-x)/width)**2
        return fitted(xx, zz, face_y) if fitted else face_y

    return form_panel(g, outline, crown,
                      stage, tile, bone, thickness=depth, rear=rear, bevel=min(.004,width*.1,height*.16),
                      layers=(0,.5,1) if stage<0 else (0,1))


def shoulder(stage, side, fitted):
    sign = 1 if side == "L" else -1
    outline = [
        (.175,1.386,1.450),(.201,1.355,1.467),(.227,1.338,1.474),
        (.256,1.330,1.468),(.285,1.344,1.444),(.304,1.372,1.408),
    ]
    if stage >= 2:
        outline = [outline[i] for i in (0,1,3,4,5)]
    mirrored = sorted((x*sign, lo, hi) for x, lo, hi in outline)
    return form_panel(Geometry(), mirrored,
                      lambda x,z: fitted(x,z,-.073+.037*((abs(x)-.232)/.073)**2+.27*(z-1.393)),
                      stage, "shoulder", "upper_arm."+side, thickness=.019, bevel=.0043,
                      layers=(0,.28,.58,.84,1) if stage<0 else (0,.5,1))


def pack(stage):
    g = Geometry()
    hard_profile(g, 0, 1.298, .141, .163, .228, .066, stage, "pack", "chest",
                 rear=True, bulge=.023)
    for sign in (-1, 1):
        x = sign * .114
        g.loft([(x,.152,1.180),(x,.161,1.203),(x,.171,1.376),(x,.154,1.412)],
               [(.021,.021),(.030,.031),(.031,.029),(.018,.019)],
               choose(stage,16,12,8,6),EDGE,{"chest":1},power=.76)
    return g


def collar(stage):
    rows = [(1.453,.083,.068,.008),(1.462,.077,.066,.007),(1.485,.063,.062,.010),
            (1.529,.065,.065,.009),(1.536,.061,.062,.009)]
    g = Geometry()
    g.loft([(0,y,z) for z,w,d,y in rows],[(w,d) for _,w,d,_ in rows],
           choose(stage,28,20,14,10),shell("carbon"),{"chest":1},power=.91,caps=False)
    return g


def harness(stage):
    g = Geometry()
    # Structural helmet/mantle docking shoulders sit over the chest shell edge.
    for sign in (-1, 1):
        hard_profile(g, sign*.122, 1.455, .021, .030, -.130, .027,
                     stage, "wrist", "chest", skew=sign*.013, bulge=.004)
    return g


def belt(stage):
    g = Geometry()
    rows = [(1.042,.140,.105,.012),(1.047,.154,.109,.014),
            (1.079,.149,.106,.011),(1.085,.140,.100,.010)]
    g.loft([(0,y,z) for z,w,d,y in rows],[(w,d) for _,w,d,_ in rows],
           choose(stage,32,24,16,10),shell("carbon"),{"pelvis":1},power=.85,caps=False)
    return g


def leg(stage, rig, profile, side, fitted):
    sign = 1 if side == "L" else -1
    g = Geometry()
    thigh_x = sign * leg_lateral(profile, .123, .065, upper=True)
    hard_profile(g, thigh_x, .784, .062, .103, -.102, .019,
                 stage, "leg", "thigh."+side, skew=sign*.019, fitted=fitted)
    knee = rig.bones["shin."+side].head_local
    shin_x = sign * leg_lateral(profile, abs(knee.x), .050)
    if stage < 2:
        hard_profile(g, shin_x, .560, .054, .045, knee.y-.081, .020,
                     stage, "wrist", "shin."+side, fitted=fitted)
        hard_profile(g, shin_x+sign*.007, .369, .044, .099, -.074, .018,
                     stage, "leg", "shin."+side, skew=-sign*.013, fitted=fitted)
    return g


def wrist(stage, rig, profile, side):
    g = Geometry()
    arm = rig.bones["forearm."+side]
    weight = {arm.name:1}
    offset = min(.027, .211 * (1 - profile.shoulder / .232))
    terminal = arm.head_local.lerp(arm.tail_local,.79)
    terminal.x += -offset if side == "L" else offset
    axis = (arm.tail_local - arm.head_local).normalized()
    stack = [terminal + d*axis for d in (-.038,-.033,.027,.032)]
    g.loft(stack,[(.038,.040),(.046,.048),(.043,.047),(.038,.041)],
           choose(stage,22,16,12,8),EDGE,weight,axis=axis,power=.86)
    hard_profile(g, terminal.x, terminal.z, .036, .027,
                 terminal.y-.048, .017, stage, "wrist", arm.name, skew=-.006)
    return g


def helmet(stage):
    g = Geometry()
    rows = [(1.557,.072,.072,.017),(1.568,.093,.087,.013),(1.607,.107,.107,.007),
            (1.648,.110,.115,.007),(1.687,.112,.120,.008),(1.727,.109,.118,.015),
            (1.775,.089,.100,.027),(1.814,.063,.077,.039),(1.834,.025,.044,.042),
            (1.840,.009,.015,.043)]
    select=choose(stage,range(10),(0,1,2,3,4,5,7,9),(0,2,3,5,7,9),(0,2,4,6,9))
    profile=[rows[i] for i in select]
    n=choose(stage,32,20,12,10)
    g.loft([(0,y,z) for z,x,d,y in profile],[(x,d) for _,x,d,_ in profile],n,
           shell("blue"),{"head":1},power=.79)
    for row in range(len(profile)-1):
        z=(profile[row][0]+profile[row+1][0])*.5
        for i in range(n):
            phase=tau*(i+.5)/n
            if sin(phase)<-.54 and 1.624<z<1.738:
                g.materials[1+row*n+i]=shell("visor")
    hard_profile(g,0,1.623,.091,.076,-.124,.024,stage,"visor","head",bulge=.025)
    return g


def guard_parts(stage, rig, profile, sealed, suit):
    fitted = fit_surface(suit, .016)
    yield "NeckSeal", collar(stage)
    yield "ChestShell", chest(stage, fitted)
    yield "HarnessDock", harness(stage)
    yield "WaistArmor", belt(stage)
    yield "DorsalPack", pack(stage)
    if sealed:
        yield "Helmet", helmet(stage)
    for side in ("L", "R"):
        yield "ShoulderCap_"+side, shoulder(stage, side, fitted)
        yield "Cuff_"+side, wrist(stage, rig, profile, side)
        yield "LegPanels_"+side, leg(stage, rig, profile, side, fitted)
