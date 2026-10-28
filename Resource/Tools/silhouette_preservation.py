"""Spend triangles on the original outline before small manufactured decoration."""
import collections
from mathutils import Vector

def field_geometry(ob,points,faces,mats,weights,features):
    # Keep every original shoe station and the complete central crystal. The
    # three small bolts/slots disappear; emission moves to existing inner walls.
    by=collections.defaultdict(list)
    for i,c in enumerate(features):by[c].append(i)
    if not all(i in by for i in range(13)):return None
    center=sum((Vector(points[v]) for i in by[12] for v in faces[i]),Vector())/sum(len(faces[i]) for i in by[12])
    selected=[i for c in [0,4,8,12] for i in by[c]];glow=mats[by[1][0]];outm=[]
    for i in selected:
        c=features[i];t=ob.data.loop_triangles[i];mid=sum((Vector(points[v]) for v in faces[i]),Vector())/3
        radial=mid-center;radial.z=0
        inner=c!=12 and radial.length>1e-6 and t.normal.dot(radial.normalized())<-.65
        outm.append(glow if inner else mats[i])
    ids=sorted({v for i in selected for v in faces[i]});mp={v:j for j,v in enumerate(ids)}
    return dict(points=[points[v] for v in ids],triangles=[[mp[v] for v in faces[i]] for i in selected],materials=outm,
                weights=[weights[v] for v in ids],source_vertex_ids=ids,source_triangles=selected,
                preserved_outline_components=[0,4,8,12])

def remove_cosmetic_faces(ob,ids,features,mats,points,faces,budget,diagonal):
    by=collections.defaultdict(list)
    for i in ids:by[features[i]].append(i)
    remove=set();details=[]
    for c,ff in by.items():
        names={ob.data.materials[mats[i]].name for i in ff}
        if names and all(n.startswith('Fastener_') or n=='Fabric_Seam' for n in names):
            remove.update(ff);details.append(c)
    # Remove complete small added trims rather than leaving paper-thin fragments.
    if len(ids)-len(remove)>budget and 'Glazing' not in ob.get('asset_id',ob.name):
        total=sum(ob.data.loop_triangles[i].area for i in ids)
        candidates=[]
        for c,ff in by.items():
            if c==0 or c in details:continue
            names={ob.data.materials[mats[i]].name for i in ff}
            if any(n.startswith(('Skin_','Eye_','Hair_','Chitin','Alien_','Glazing','Field_','Glow_')) for n in names):continue
            vv={v for i in ff for v in faces[i]};dims=[max(points[v][k] for v in vv)-min(points[v][k] for v in vv) for k in range(3)]
            area=sum(ob.data.loop_triangles[i].area for i in ff)
            if min(dims)<.016 and max(dims)<diagonal*.55 and area<total*.045:
                candidates.append((area,c,ff))
        for area,c,ff in sorted(candidates):
            if len(ids)-len(remove)<=budget:break
            remove.update(ff);details.append(c)
    return [i for i in ids if i not in remove],details

def outline_locks(prepared,ob,budget):
    # The principal authored curved shell remains exactly as drawn. Secondary
    # panels can lose bevels before its curve stations or silhouette are moved.
    if ob.get('asset_id',ob.name).startswith('RoadPlan_'):return set()
    ff=[i for i,c in enumerate(prepared['features']) if c==0]
    if not ff:return set()
    normals=set()
    for i in ff:
        a,b,c=[Vector(prepared['points'][v]) for v in prepared['triangles'][i]]
        n=(b-a).cross(c-a).normalized();normals.add(tuple(round(x,3) for x in n))
    if len(normals)<=8:return set()
    vertices={v for i in ff for v in prepared['triangles'][i]}
    if len(ff)<=budget*.92:return vertices
    # Large clothing shells have internal rings as well as their outline. Keep
    # front/side profiles at every authored height instead of freezing hidden
    # surface vertices and making the 200 hard ceiling impossible to reach.
    levels=collections.defaultdict(list)
    for v in vertices:levels[round(prepared['points'][v][2],5)].append(v)
    locks=set()
    for ids in levels.values():
        if len(ids)<4:continue
        for k in [0,1]:
            for fn in [min,max]:
                value=fn(prepared['points'][v][k] for v in ids)
                locks.update(v for v in ids if abs(prepared['points'][v][k]-value)<1e-6)
    return locks
