"""Retopologize within original parts; never add parts or export files.

The user explicitly permits limited cosmetic simplification instead of splitting.
Nails, stitches, decals and thin gasket relief become double-sided surface detail;
silhouettes, principal material features, skin domains and ports remain protected.
"""
import ast,bpy,json,sys,math,collections
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from feature_mesh_reduction import FeatureReducer
from part_budget_policy import triangle_budget,REVISION
from mesh_surface_checks import errors
from silhouette_preservation import field_geometry,remove_cosmetic_faces,outline_locks
audit=json.loads((ROOT/'Validation/part_budget_audit.json').read_text())
reference=audit['largest_reference']['diagonal_m'];plan={};failures=[];quality=[]
roads=json.loads((ROOT/'Documentation/road_plan_modules.json').read_text(encoding='utf-8'))['modules']
portmap={pid:m['ports'] for m in roads.values() for pid in m['parts']}

def compact(points,faces,mats,weights,ids,features):
    vv=sorted({v for i in ids for v in faces[i]});mp={v:i for i,v in enumerate(vv)}
    return dict(points=[points[i] for i in vv],triangles=[[mp[v] for v in faces[i]] for i in ids],materials=[mats[i] for i in ids],
                weights=[weights[i] for i in vv],features=[features[i] for i in ids],source_vertex_ids=vv,source_triangles=ids)

def broad_face(me,ids,points,faces,direction):
    planes=collections.defaultdict(list)
    for i in ids:
        t=me.loop_triangles[i];n=t.normal
        key=tuple(round(x,4) for x in n)+(round(n.dot(Vector(points[faces[i][0]])),6),)
        planes[key].append(i)
    maximum=max(sum(me.loop_triangles[i].area for i in vv) for vv in planes.values())
    total=sum(me.loop_triangles[i].area for i in ids)
    if maximum<total*.20:return None
    choices=[(k,vv) for k,vv in planes.items() if sum(me.loop_triangles[i].area for i in vv)>=maximum*.8]
    return max(choices,key=lambda item:Vector(item[0][:3]).dot(direction))[1]

def rebuild_field(ob,points,faces,mats,ws,features):
    return field_geometry(ob,points,faces,mats,ws,features)

for number,row in enumerate(audit['parts_data']):
    name=row['id'];ob=bpy.data.objects[row['object']];me=ob.data;me.calc_loop_triangles();budget=triangle_budget(row['diagonal_m'],reference)
    points=[tuple(v.co) for v in me.vertices];faces=[tuple(t.vertices) for t in me.loop_triangles];mats=[me.polygons[t.polygon_index].material_index for t in me.loop_triangles]
    ws=[tuple(sorted((ob.vertex_groups[g.group].name,round(g.weight,7)) for g in v.groups if g.weight>1e-7)) for v in me.vertices]
    attr=me.attributes.get('authoring_component');features=[attr.data[t.polygon_index].value if attr else -1 for t in me.loop_triangles]
    if not attr:
        vf=collections.defaultdict(set)
        for i,f in enumerate(faces):
            for v in f:vf[v].add(i)
        pending=set(range(len(faces)));cid=0
        while pending:
            todo=[min(pending)]
            while todo:
                i=todo.pop()
                if i not in pending:continue
                pending.remove(i);features[i]=cid
                for v in faces[i]:todo.extend(vf[v]&pending)
            cid+=1
    ids=list(range(len(faces)));operations=[];role=ob.get('collision_role','');family=row['family'];center=(Vector(row['bounds'][0])+Vector(row['bounds'][1]))*.5
    if len(ids)>budget:
        painted=('marking' in family or (family.startswith('transit.') and any(t in name for t in ['Mark','DirectionTicks'])))
        if painted:
            visible=[i for i in ids if me.loop_triangles[i].normal.z>.65]
            if visible:ids=visible;operations.append('paint_or_hologram_surface_without_artificial_solid_backing')
        elif (role=='drive_surface' or family=='transit.road' or family.startswith('scenery.Road_')) and row['bounds'][1][2]<=.10:
            visible=[i for i in ids if me.loop_triangles[i].normal.z>.65]
            if visible:ids=visible;operations.append('ground_supported_road_hidden_below_grade_faces')
        elif role in ['walk_surface','curb'] or family in ['transit.walk','road_plan.walk']:
            low=row['bounds'][0][2]
            hidden={i for i,t in enumerate(me.loop_triangles) if t.normal.z<-.65 and all(abs(points[v][2]-low)<1e-5 for v in t.vertices)}
            if hidden:ids=[i for i in ids if i not in hidden];operations.append('support_contact_bottom_faces')
    visible_before=list(ids)
    if len(ids)>budget:
        ids,omitted=remove_cosmetic_faces(ob,ids,features,mats,points,faces,budget,row['diagonal_m'])
        if omitted:operations.append('remove_complete_small_fasteners_stitches_and_trims_before_curve_changes')
    if len(ids)>budget:
        primitive=collections.defaultdict(list)
        for i in ids:primitive[features[i]].append(i)
        for feature,ff in primitive.items():
            material_names={me.materials[mats[i]].name for i in ff};verts={v for i in ff for v in faces[i]}
            lo=[min(points[v][k] for v in verts) for k in range(3)];hi=[max(points[v][k] for v in verts) for k in range(3)]
            dims=[hi[k]-lo[k] for k in range(3)];mid=(Vector(lo)+Vector(hi))*.5
            retained=None;kind=None
            if name.startswith('Face_'):
                if feature in [0,1,8,9]:
                    retained=[i for i in ff if me.loop_triangles[i].normal.y<-.05];kind='eye_front_surface_keep_iris_and_white'
                elif feature!=16:
                    retained=broad_face(me,ff,points,faces,Vector((0,-1,0)));kind='facial_surface_detail_reduce_hidden_relief'
            elif 'Glazing' in name and material_names=={'Rubber'}:
                retained=broad_face(me,ff,points,faces,mid-center);kind='thin_gasket_surface_keep_outline'
            elif (any(m.startswith('Fastener_') for m in material_names) and max(dims)<row['diagonal_m']*.22) or material_names=={'Fabric_Seam'}:
                retained=broad_face(me,ff,points,faces,mid-center);kind='small_fastener_or_stitch_surface'
            elif name.startswith('Body_Hand_') and material_names=={'Skin_Light'}:
                retained=broad_face(me,ff,points,faces,Vector((0,-1,0)));kind='nail_surface'
            elif feature!=0 and min(dims)<max(dims)*.15 and max(dims)<row['diagonal_m']*.5 and not any(n.startswith(('Skin_','Eye_','Hair_','Chitin','Alien_')) for n in material_names):
                retained=broad_face(me,ff,points,faces,mid-center);kind='minor_panel_relief_as_surface_keep_visible_design'
            if retained and len(retained)<len(ff):
                removed=set(ff)-set(retained);ids=[i for i in ids if i not in removed]
                operations.append(kind)
    prepared=compact(points,faces,mats,ws,ids,features)
    locked=outline_locks(prepared,ob,budget)
    if name in portmap and role in ['drive_surface','walk_surface']:
        for i,p in enumerate(prepared['points']):
            if role=='walk_surface' and abs(p[2]-row['bounds'][1][2])<1e-5:locked.add(i)
            for port in portmap[name]:
                d=Vector(port['outward']);d.z=0;d.normalize();rel=Vector(p)-Vector(port['position_m'])
                side=abs(rel.dot(Vector((-d.y,d.x,0))));along=rel.dot(d)
                if -1.5<=along<=1e-4 and abs(side-port['width_m']/2)<1e-4:locked.add(i)
    stations=[None for p in prepared['points']]
    rig=ob.parent if ob.parent and ob.parent.type=='ARMATURE' else next((m.object for m in ob.modifiers if m.type=='ARMATURE'),None)
    if rig:
        votes=collections.Counter(n for w in ws for n,v in w)
        if votes:
            bone=rig.data.bones[votes.most_common(1)[0][0]];axis=(bone.tail_local-bone.head_local).normalized()
            levels=collections.Counter(round(Vector(p).dot(axis),5) for p in prepared['points'])
            if len([v for v in levels.values() if v>=6])>=2:
                stations=[round(Vector(p).dot(axis),5) if levels[round(Vector(p).dot(axis),5)]>=6 else None for p in prepared['points']]
    field=rebuild_field(ob,points,faces,mats,ws,features) if ('LiftField_' in name or name in ['City_ChargePost_InductionHalo','Utility_Transformer_FieldCapacitors','City_TrafficSignal_FieldStabilizer_L','City_TrafficSignal_FieldStabilizer_R']) and len(faces)>budget else None
    if field is not None and len(field['triangles'])<=budget:
        reduced=field;operations.append('original_full_curve_stations_and_crystal_bolts_slots_removed_emission_on_inner_wall')
    elif len(prepared['triangles'])>budget:
        reduced=FeatureReducer(prepared['points'],prepared['triangles'],prepared['materials'],prepared['weights'],locked=locked,features=prepared['features'],stations=stations).reduce(budget)
        if len(reduced['triangles'])>budget:
            # Keep the original mesh identity; relax primitive seam restrictions
            # only after the constrained attempt cannot reach the requested cap.
            reduced=FeatureReducer(prepared['points'],prepared['triangles'],prepared['materials'],prepared['weights'],locked=locked).reduce(budget)
            operations.append('relaxed_internal_primitive_seams')
        operations.append('feature_ranked_edge_retopology')
    else:reduced=prepared
    err=errors(points,faces,reduced['points'],reduced['triangles']) if len(reduced['triangles'])!=len(faces) else 0
    # Cosmetic relief removal is recorded separately from silhouette retopology.
    retopo_error=errors(prepared['points'],prepared['triangles'],reduced['points'],reduced['triangles']) if len(reduced['triangles'])!=len(prepared['triangles']) else 0
    if len(reduced['triangles'])>budget:failures.append([name,len(reduced['triangles']),budget])
    for f in reduced['triangles']:
        a,b,c=[Vector(reduced['points'][v]) for v in f]
        if (b-a).cross(c-a).length_squared<1e-24:failures.append([name,'degenerate surface']);break
    if retopo_error>max(.003,row['diagonal_m']*.045):quality.append([name,retopo_error,row['diagonal_m']])
    reduced.update(id=name,label='Main',budget=budget,diagonal_m=row['diagonal_m'],bounds=row['bounds'],method='no_split_surface_and_feature_retopology',
                   source_triangles=ids,surface_error_m=err,retopology_surface_error_m=retopo_error,operations=list(dict.fromkeys(operations)))
    plan[name]=dict(source_object=ob.name,source_triangles=len(faces),source_bounds=row['bounds'],family=family,slot=row['slot'],
                    culled_triangles=sorted(set(range(len(faces)))-set(ids)),culling_reason=';'.join(dict.fromkeys(operations)),groups=[reduced])
    if number%200==0:print('NO_SPLIT_BUDGET_PLAN',number,'COUNT FAILURES',len(failures),'REVIEW',len(quality),flush=True)
result={'revision':REVISION,'reference':audit['largest_reference'],'parts':plan,'failures':failures,'quality_review':quality,
        'original_parts':len(plan),'final_parts':len(plan),'extra_part_files':0}
(ROOT/'Validation/size_budget_plan.json').write_text(json.dumps(result,separators=(',',':'))+'\n',encoding='utf-8')
print('NO_SPLIT_PLAN_COMPLETE','PARTS',len(plan),'TRIANGLES',sum(len(p['groups'][0]['triangles']) for p in plan.values()),'FAILURES',json.dumps(failures),'REVIEW',json.dumps(quality),flush=True)
