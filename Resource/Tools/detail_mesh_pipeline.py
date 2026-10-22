"""Blender authoring: retain a detailed source, then perform lossless mesh cleanup.

The 500-triangle budget is a delivery ceiling, never a shape-generation target.
No collapse decimation or ratio-driven simplification is performed. Over-budget
parts fail for an explicit functional split or authored topology correction.
"""
import math
import bpy
import bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree


class DetailGeometry:
    """Mixin for the pack's Geo: manufactured edges and finer curved profiles."""

    def add(self,verts,faces,mat,bone=None):
        if not hasattr(self,'component_ids'):
            self.component_ids=[]
            self.component_bones=[]
            self.component_materials=[]
        component=len(self.component_bones)
        self.component_bones.append(bone or 'root')
        self.component_materials.append(mat)
        self.component_ids.extend([component]*len(faces))
        return super().add(verts,faces,mat,bone)

    def box(self,c,s,mat,bone=None,rot=None):
        mechanical=mat in {'Steel','Alloy_Light','Ceramic','Rubber'} or mat.startswith('Vehicle_')
        bevel=min(min(s)*.10,.025)
        if not mechanical or min(s)<.075 or max(s)<.3:
            return super().box(c,s,mat,bone,rot)
        bm=bmesh.new()
        bmesh.ops.create_cube(bm,size=1)
        for v in bm.verts:
            v.co=Vector((v.co.x*s[0],v.co.y*s[1],v.co.z*s[2]))
        bmesh.ops.bevel(bm,geom=list(bm.edges),offset=bevel,segments=2 if min(s)>.25 else 1,
                        affect='EDGES',clamp_overlap=True)
        bm.verts.ensure_lookup_table()
        bm.verts.index_update()
        verts=[(rot@v.co if rot else v.co)+Vector(c) for v in bm.verts]
        faces=[tuple(v.index for v in f.verts) for f in bm.faces]
        self.add(verts,faces,mat,bone)
        bm.free()
        return self

    def rings(self,centers,radii,mat,bone=None,sides=8,caps=True,axis=None):
        # Organic/faceted precursor crystals keep their intentional sections.
        faceted=mat in {'Ruin_Obsidian','Alien_Petal','Glow_Violet','Field_Core','Rock_Mine'}
        if not faceted:
            radius=max(max(r) if isinstance(r,(tuple,list)) else r for r in radii)
            sides=max(sides,16 if radius>.30 else 12 if radius>.065 else 8)
        return super().rings(centers,radii,mat,bone,sides,caps,axis)


def _uv(mesh):
    layer=mesh.uv_layers.get('UVMap') or mesh.uv_layers.new(name='UVMap')
    for f in mesh.polygons:
        dominant=max(range(3),key=lambda k:abs(f.normal[k]))
        axes=[i for i in range(3) if i!=dominant]
        for li in f.loop_indices:
            p=mesh.vertices[mesh.loops[li].vertex_index].co
            layer.data[li].uv=(p[axes[0]]*.5,p[axes[1]]*.5)


def _weights(obj,names,indices):
    for i,name in enumerate(names):
        group=obj.vertex_groups.new(name=name)
        vertices=[j for j,index in enumerate(indices) if index==i]
        if vertices:
            group.add(vertices,1,'REPLACE')


def _surface_error(a,b):
    """Independent surface sampling in both directions, including face centers."""
    def dot(u,v): return sum(x*y for x,y in zip(u,v))
    def sub(u,v): return tuple(x-y for x,y in zip(u,v))
    def distance(p,a,b,c):
        # Double precision recheck: Blender's float BVH nearest-point position
        # loses precision on long, thin vent/bevel triangles.
        ab,ac,ap=sub(b,a),sub(c,a),sub(p,a)
        n=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
        nn=dot(n,n)
        if nn>1e-28:
            t=dot(ap,n)/nn
            q=tuple(p[i]-t*n[i] for i in range(3))
            aq=sub(q,a)
            d00,d01,d11=dot(ab,ab),dot(ab,ac),dot(ac,ac)
            den=d00*d11-d01*d01
            if den>1e-28:
                u=(dot(aq,ab)*d11-dot(aq,ac)*d01)/den
                v=(dot(aq,ac)*d00-dot(aq,ab)*d01)/den
                if u>=-1e-10 and v>=-1e-10 and u+v<=1+1e-10:
                    return abs(t)*math.sqrt(nn)
        best=float('inf')
        for x,y in [(a,b),(b,c),(c,a)]:
            xy=sub(y,x)
            t=max(0,min(1,dot(sub(p,x),xy)/max(dot(xy,xy),1e-30)))
            best=min(best,math.sqrt(sum((p[i]-x[i]-t*xy[i])**2 for i in range(3))))
        return best
    surfaces=[]
    for source in [a,b]:
        mesh=source.copy()
        bmesh.ops.triangulate(mesh,faces=list(mesh.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
        mesh.verts.index_update()
        vertices=[tuple(v.co) for v in mesh.verts]
        triangles=[tuple(v.index for v in f.verts) for f in mesh.faces]
        mesh.free()
        tree=BVHTree.FromPolygons(vertices,triangles,all_triangles=True)
        surfaces.append((vertices,triangles,tree))
    error=0
    for i in [0,1]:
        vertices,triangles,_=surfaces[i]
        target_vertices,target_triangles,target=surfaces[1-i]
        samples=vertices+[tuple(sum(vertices[j][k] for j in tri)/3 for k in range(3)) for tri in triangles]
        for p in samples:
            hit=target.find_nearest(p)
            assert hit and hit[0] is not None,'empty optimized surface'
            candidates={hit[2]}
            if hit[3]>1e-5:
                candidates.update(h[2] for h in target.find_nearest_range(p,hit[3]+1e-5))
            exact=min(distance(p,*(target_vertices[j] for j in target_triangles[index])) for index in candidates)
            error=max(error,exact)
    return error


def _occluded_faces(bm,component_bones,opaque_components=None):
    """Cull only triangles wholly inside another closed, same-bone solid.

    Component provenance is kept through triangulation. AABB rejection, strict
    interior tests at vertices/edge midpoints/center and a convex target requirement
    make the deletion conservative. Material boundaries outside solids are retained.
    """
    layer=bm.faces.layers.int['authoring_component']
    groups={}
    for f in bm.faces:
        groups.setdefault(f[layer],[]).append(f)
    solids={}
    for comp,faces in groups.items():
        if opaque_components is not None and comp not in opaque_components:
            continue
        verts=list({v for f in faces for v in f.verts})
        # Every edge of a closed primitive must be used by two of its faces.
        edges={e for f in faces for e in f.edges}
        if not all(sum(f[layer]==comp for f in e.link_faces)==2 for e in edges):
            continue
        # For convex solids, containment of a triangle's vertices guarantees its
        # entire area is inside. Concave wings/arcs are intentionally excluded.
        if any(any(f.normal.dot(v.co-f.verts[0].co)>2e-6 for v in verts) for f in faces):
            continue
        planes=[(f.normal.copy(),f.verts[0].co.copy()) for f in faces]
        lo=tuple(min(v.co[i] for v in verts) for i in range(3))
        hi=tuple(max(v.co[i] for v in verts) for i in range(3))
        solids[comp]=(planes,lo,hi)
    hidden=[]
    for comp,faces in groups.items():
        for f in faces:
            coords=[v.co for v in f.verts]
            lo=tuple(min(p[i] for p in coords) for i in range(3))
            hi=tuple(max(p[i] for p in coords) for i in range(3))
            for target,(planes,tlo,thi) in solids.items():
                if target==comp or component_bones[comp]!=component_bones[target]:
                    continue
                if any(lo[i]<=tlo[i]+1e-6 or hi[i]>=thi[i]-1e-6 for i in range(3)):
                    continue
                if all(all(n.dot(p-origin)<-1e-6 for n,origin in planes) for p in coords):
                    hidden.append(f)
                    break
    return hidden


class DetailPipeline:
    def __init__(self,source_collection,materials,tag,allow_draft_over_budget=False):
        self.collection=source_collection
        self.materials=materials
        self.tag=tag
        self.records=[]
        self.allow_draft_over_budget=allow_draft_over_budget

    def build(self,g,name,collection,rig,slot,family):
        mats=list(dict.fromkeys(g.m))
        names=list(dict.fromkeys(w or 'root' for w in g.w))
        raw=bpy.data.meshes.new('DetailMesh__'+name)
        raw.from_pydata(g.v,[],g.f)
        for mat in mats:
            raw.materials.append(self.materials[mat])
        for face,mat in zip(raw.polygons,g.m):
            face.material_index=mats.index(mat)
        raw.update()
        bm=bmesh.new()
        bm.from_mesh(raw)
        comp_layer=bm.faces.layers.int.new('authoring_component')
        for face,component in zip(bm.faces,g.component_ids):
            face[comp_layer]=component
        layer=bm.verts.layers.int.new('authoring_bone')
        bm.verts.ensure_lookup_table()
        for i,v in enumerate(bm.verts):
            v[layer]=names.index(g.w[i] or 'root')
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        # A triangle source removes ambiguity for nonplanar loft faces. Planar
        # tessellation is dissolved only after the detailed source is retained.
        bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
        original=bm.copy()
        original.to_mesh(raw)
        raw.update()
        _uv(raw)
        raw.calc_loop_triangles()
        source=bpy.data.objects.new('Detail__'+name,raw)
        self.collection.objects.link(source)
        source['expansion']=self.tag
        source['role']='editable_detail_source_not_exported'
        source['export_part']=name
        source['source_rig']=rig.name if rig else ''
        if rig:
            source_indices=[v[original.verts.layers.int['authoring_bone']] for v in original.verts]
            _weights(source,names,source_indices)
        opaque=set()
        for component,mat_name in enumerate(g.component_materials):
            mat=self.materials[mat_name]
            shader=mat.node_tree.nodes.get('Principled BSDF') if mat.use_nodes else None
            if shader:
                if shader.inputs['Alpha'].default_value>=.99999 and shader.inputs['Transmission Weight'].default_value<1e-6:
                    opaque.add(component)
            elif not mat.use_nodes and mat.diffuse_color[3]>=.99999:
                opaque.add(component)
        hidden=_occluded_faces(bm,g.component_bones,opaque)
        hidden_count=len(hidden)
        if hidden:
            bmesh.ops.delete(bm,geom=hidden,context='FACES_ONLY')
            # Unreferenced interior vertices carry no visible shape or skin.
            loose=[v for v in bm.verts if not v.link_faces]
            if loose:
                bmesh.ops.delete(bm,geom=loose,context='VERTS')
        visible_reference=bm.copy()
        # Do not merge rigidly weighted joints or cross material seams.
        for bone in range(len(names)):
            verts=[v for v in bm.verts if v[layer]==bone]
            if verts:
                bmesh.ops.remove_doubles(bm,verts=verts,dist=1e-7)
        for edge in bm.edges:
            if len({v[layer] for v in edge.verts})>1:
                edge.seam=True
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-9)
        bmesh.ops.dissolve_limit(bm,angle_limit=math.radians(.002),use_dissolve_boundaries=True,
                                 verts=list(bm.verts),edges=list(bm.edges),
                                 delimit={'MATERIAL','SEAM','SHARP'})
        # Interior culling may leave a primitive open. Preserve its authored
        # outward winding instead of reorienting an open connected component.
        bm.normal_update()
        error=_surface_error(visible_reference,bm)
        reverted=False
        rejected_error=error
        if error>=5e-5:
            # A nonplanar loft can be re-tessellated differently after a dissolve.
            # Keep the detailed topology rather than accepting visible loss.
            bm.free()
            bm=visible_reference.copy()
            layer=bm.verts.layers.int['authoring_bone']
            reverted=True
            error=_surface_error(visible_reference,bm)
        assert error<5e-5,(name,'surface preservation failed',error)
        mesh=bpy.data.meshes.new(name+'_Mesh')
        for mat in mats:
            mesh.materials.append(self.materials[mat])
        bone_indices=[v[layer] for v in bm.verts]
        bm.to_mesh(mesh)
        mesh.update()
        _uv(mesh)
        mesh.calc_loop_triangles()
        count=len(mesh.loop_triangles)
        record=dict(id=name,source_triangles=len(raw.loop_triangles),optimized_triangles=count,
                    removed_triangles=len(raw.loop_triangles)-count,max_surface_error_m=error,
                    occluded_internal_triangles_removed=hidden_count,
                    source_object=source.name,operations=['weld_same_bone_1e-7m','degenerate_cleanup',
                    'coplanar_dissolve_0.002deg_preserve_material_and_joint_seams'])
        record['kept_detail_topology']=reverted
        if reverted:
            record['operations']=['retained_source_topology_after_surface_comparison']
            record['rejected_cleanup_error_m']=rejected_error
        self.records.append(record)
        original.free()
        visible_reference.free()
        bm.free()
        assert count>0,name
        if not self.allow_draft_over_budget:
            assert count<=500,(name,'detail-preserving result exceeds part limit; split a functional module',count)
        obj=bpy.data.objects.new(name,mesh)
        collection.objects.link(obj)
        if rig:
            obj.parent=rig
            _weights(obj,names,bone_indices)
            mod=obj.modifiers.new('Skin','ARMATURE')
            mod.object=rig
        obj['asset_id']=name
        obj['slot']=slot
        obj['family']=family
        obj['triangle_count']=count
        obj['max_triangles']=500
        obj['units']='meters'
        obj['detail_source']=source.name
        obj['pipeline']='detail_first_lossless_cleanup_v1'
        obj.asset_mark()
        obj.asset_data.description=f'{family} / {slot} / detailed source preserved / {count} triangles'
        asset=dict(id=name,family=family,slot=slot,vertices=len(mesh.vertices),polygons=len(mesh.polygons),
                   triangles=count,materials=mats,skeleton=rig.name if rig else None,
                   source_triangles=record['source_triangles'],detail_source=source.name,
                   max_surface_error_m=error)
        return obj,asset
