"""Independent checks against delivered GLBs, not just the construction parameters."""
import json
import math
import zipfile
from collections import Counter,defaultdict
from pathlib import Path
from verify_resource_pack import read_glb,accessor

ROOT=Path(__file__).resolve().parents[1]
KIT=json.loads((ROOT/'Documentation/road_plan_modules.json').read_text(encoding='utf-8'))
MODS=KIT['modules']
CAT=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
PARTS={a['id']:a for a in CAT['assets'] if a.get('expansion')=='road_plan_v1'}
BASE=json.loads(zipfile.ZipFile(ROOT/'Source/Archives/Pre200Delivery_20261026.zip').read('Documentation/road_plan_modules.json'))['modules']
for name,info in MODS.items():
    assert info['collision_envelope']==BASE[name]['collision_envelope'],(name,'physical envelope changed')
    assert info['ports']==BASE[name]['ports'],(name,'connection contract changed')
REPORT=dict(modules_checked=0,standalone_parts_checked=0,physical_ports_checked=0,connections_checked=0,
            max_triangles=0,max_port_position_error_m=0,max_port_tangent_error_degrees=0,
            max_normal_tangent_dot=0,min_triangle_area_m2=999,min_piecewise_linear_walk_width_m=2,ramps={},clearance_checks=[],errors=[])
GEOMETRY={}

def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def mul(a,s):return tuple(x*s for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def length(a):return math.sqrt(dot(a,a))
def distance(a,b):return length(sub(a,b))
def unit(a):return mul(a,1/length(a))
def author(p):return (p[0],-p[2],p[1])
def key(p):return tuple(round(v,7) for v in p)
def angle(a,b):return math.degrees(math.acos(max(-1,min(1,dot(unit(a),unit(b))))))
def area(poly):return abs(sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(poly,poly[1:]+poly[:1])))/2
def inside(p,polygon):
    x,y=p[:2];result=False
    for a,b in zip(polygon,polygon[1:]+polygon[:1]):
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:result=not result
    return result

for name,info in MODS.items():
    try:
        doc,buf=read_glb(ROOT/f'Export/RoadPlan/{name}.glb')
        nodes=[n for n in doc['nodes'] if 'mesh' in n]
        assert len(nodes)==len(doc['meshes'])==len(info['parts']),(name,'assembly nodes changed')
        assert {n.get('extras',{}).get('asset_id') for n in nodes}==set(info['parts'])
        geo=dict(vertices=[],tops=defaultdict(list),triangles=defaultdict(list),boundary=defaultdict(Counter),part_vertices={})
        GEOMETRY[name]=geo
        for node in nodes:
            pid=node['extras']['asset_id'];role=node['extras']['collision_role'];vertices=[];count=0
            assert node.get('scale',[1,1,1])==[1,1,1] and node.get('translation',[0,0,0])==[0,0,0]
            assert not node.get('skin') and not doc.get('animations')
            for prim in doc['meshes'][node['mesh']]['primitives']:
                attrs=prim['attributes'];ps=[author(p) for p in accessor(doc,buf,attrs['POSITION'])]
                normals=[author(p) for p in accessor(doc,buf,attrs['NORMAL'])]
                assert 'TEXCOORD_0' in attrs and 'TANGENT' in attrs,(pid,'UV/tangent missing')
                uv=accessor(doc,buf,attrs['TEXCOORD_0']);tangents=accessor(doc,buf,attrs['TANGENT'])
                for n,t,tex in zip(normals,tangents,uv):
                    assert abs(length(n)-1)<.002 and abs(length(t[:3])-1)<.002,(pid,'non-unit normal/tangent')
                    ortho=abs(dot(n,author(t[:3])));REPORT['max_normal_tangent_dot']=max(REPORT['max_normal_tangent_dot'],ortho)
                    assert ortho<.002,(pid,'normal/tangent basis not orthogonal',ortho)
                    assert all(math.isfinite(x) for x in tex)
                indices=[i[0] for i in accessor(doc,buf,prim['indices'])];count+=len(indices)//3;vertices+=ps
                assert doc['materials'][prim['material']]['name'] in KIT['materials'],(pid,'unshared material')
                for i in range(0,len(indices),3):
                    p,q,r=[ps[j] for j in indices[i:i+3]];n=cross(sub(q,p),sub(r,p));a=length(n)/2
                    REPORT['min_triangle_area_m2']=min(REPORT['min_triangle_area_m2'],a)
                    assert a>1e-12,(pid,'degenerate triangle',a)
                    if n[2]>0 and n[2]/length(n)>.65:
                        geo['tops'][role].extend([p,q,r]);geo['triangles'][role].append((p,q,r,n))
                        for x,y in [(p,q),(q,r),(r,p)]:geo['boundary'][role][tuple(sorted([key(x),key(y)]))]+=1
            assert count==PARTS[pid]['triangles']<=PARTS[pid]['triangle_budget']<=200,(pid,'part budget/count changed',count)
            REPORT['max_triangles']=max(REPORT['max_triangles'],count)
            geo['vertices']+=vertices;geo['part_vertices'][pid]=vertices
            expected=info['render_bounds'][pid]
            if expected:
                for k in range(3):assert abs(min(v[k] for v in vertices)-expected['min'][k])<1e-4 and abs(max(v[k] for v in vertices)-expected['max'][k])<1e-4
        assert len({p['id'] for p in info['ports']})==len(info['ports'])
        for port in info['ports']:
            assert port['lanes'] in [0,2,4] and port['level'] in [0,1]
            assert abs(length(port['outward'])-1)<1e-5
            role='drive_surface' if port['kind']=='road' else 'walk_surface'
            points=geo['tops'][role];p=port['position_m'];d=port['outward'];horizontal=unit((d[0],d[1],0));normal=(-horizontal[1],horizontal[0],0)
            graph=defaultdict(list)
            for edge,n in geo['boundary'][role].items():
                if n==1:graph[edge[0]].append(edge[1]);graph[edge[1]].append(edge[0])
            for side in [-1,1]:
                endpoint=add(p,mul(normal,side*port['width_m']/2));actual=min(points,key=lambda q:distance(endpoint,q))
                err=distance(endpoint,actual);REPORT['max_port_position_error_m']=max(REPORT['max_port_position_error_m'],err)
                assert err<=.001,(name,port['id'],'missing physical port edge',err)
                neighbors=[q for q in graph[key(actual)] if dot(unit(sub(q,actual)),mul(d,-1))>.95]
                assert neighbors,(name,port['id'],'no longitudinal surface edge')
                deviation=min(angle(sub(q,actual),mul(d,-1)) for q in neighbors)
                REPORT['max_port_tangent_error_degrees']=max(REPORT['max_port_tangent_error_degrees'],deviation)
                assert deviation<=.1+1e-4,(name,port['id'],'physical chord tangent mismatch',deviation)
            REPORT['physical_ports_checked']+=1
        if info.get('road_outline'):
            polygon=info['road_outline'];expected=area(polygon)
            actual=sum(abs(n[2])/2 for p,q,r,n in geo['triangles']['drive_surface'])
            assert abs(actual-expected)<max(.002,expected*1e-5),(name,'outline area / overlapped surface',expected,actual)
        if info.get('paint_only'):
            assert not info['collision_envelope'] and not info['ports']
            assert all(v[2]>.0019 for v in geo['vertices']),(name,'paint contains a roadway slab')
        if info['category']=='junction':
            assert info['total_approach_lanes']<=10 and len(info['ports'])<=4
            assert not ('X_' in name and info['lanes']>2)
        if info['category']=='ramp':
            grades=[math.hypot(n[0],n[1])/abs(n[2]) for p,q,r,n in geo['triangles']['drive_surface']]
            actual=max(grades);assert actual<=.09+1e-5,(name,'actual mesh grade',actual)
            assert abs(max(v[0] for v in geo['vertices'])-110)<1e-4 and abs(max(v[2] for v in geo['vertices'])-info['rise_m'])<1e-4
            assert abs(info['ports'][0]['slope'])<1e-8 and abs(info['ports'][1]['slope'])<1e-8
            for clearance in info['clearance_envelope']:
                if clearance['vehicle_8m_allowed']:assert clearance['underside_z_m']>=8-1e-6
                if clearance['pedestrian_2p2m_allowed']:assert clearance['underside_z_m']>=2.2-1e-6
            REPORT['ramps'][name]=dict(max_actual_grade=actual,upper_surface_z_m=info['rise_m'],upper_underclearance_m=info['rise_m']-.4)
        REPORT['modules_checked']+=1
    except Exception as exc:REPORT['errors'].append(f'{name}: {exc}')

for pid,a in PARTS.items():
    try:
        doc,buf=read_glb(ROOT/f'Export/RoadPlan/Parts/{pid}.glb')
        assert len(doc['meshes'])==1
        assert sum(len(accessor(doc,buf,p['indices']))//3 for p in doc['meshes'][0]['primitives'])==a['triangles']<=a['triangle_budget']<=200
        REPORT['standalone_parts_checked']+=1
    except Exception as exc:REPORT['errors'].append(f'{pid}: {exc}')

for name,info in MODS.items():
    if info['category']!='walk' or 'road_companion' not in info:continue
    try:
        road=MODS[info['road_companion']];geo=GEOMETRY[name]
        if 'Curve' in name:
            r=road['radius_m'];center=(0,r)
            for pid,points in geo['part_vertices'].items():
                if 'Paving' not in pid:continue
                radii=[math.hypot(v[0],v[1]-r) for v in points if abs(v[2]-.15)<1e-5]
                assert abs(max(radii)-min(radii)-2)<1e-4,(pid,'usable walk width shrank')
        for pid,points in geo['part_vertices'].items():
            center=None
            if 'Curve' in name and 'Paving' in pid:center=(0,road['radius_m'])
            elif 'CornerPaving_' in pid:
                h=road['lanes']*1.5;r=road['corner_radius_m'];i=int(pid.rsplit('_',1)[1])
                if len(road['ports'])==3:
                    b=road['approach_lanes'][-1]*1.5;center=[(b+r,h+r),(-b-r,h+r)][i]
                else:center=[(-h-r,-h-r),(h+r,-h-r),(h+r,h+r),(-h-r,h+r)][i]
            if center is None:continue
            top={key(p) for p in points if abs(p[2]-.15)<1e-5}
            radii=[math.hypot(p[0]-center[0],p[1]-center[1]) for p in top]
            angles=sorted(set(round(math.atan2(p[1]-center[1],p[0]-center[0])%(2*math.pi),6) for p in top))
            gaps=sorted([(b-a)%(2*math.pi) for a,b in zip(angles,angles[1:]+angles[:1])])
            max_step=max(gaps[:-1]);minimum=(max(radii)-min(radii))*math.cos(max_step/2)
            REPORT['min_piecewise_linear_walk_width_m']=min(REPORT['min_piecewise_linear_walk_width_m'],minimum)
            assert minimum>=1.999-2e-5,(pid,'polygonal walk loses over 1mm width',minimum)
        if road.get('road_outline'):
            # Triangle centers must be outside the actual drive polygon.
            for p,q,r,n in geo['triangles']['walk_surface']:
                center=mul(add(add(p,q),r),1/3)
                assert not inside(center,road['road_outline']),(name,'walk overlaps carriageway',center)
    except Exception as exc:REPORT['errors'].append(str(exc))

def transform(p,placement,direction=False):
    t=math.radians(placement['yaw_degrees']);c,s=math.cos(t),math.sin(t)
    q=(c*p[0]-s*p[1],s*p[0]+c*p[1],p[2])
    return q if direction else add(q,placement['position_m'])

examples=json.loads((ROOT/'Documentation/road_plan_examples.json').read_text(encoding='utf-8'))['examples']
for name,example in examples.items():
    for link in example['connections']:
        try:
            a,b=[example['placements'][link[k]] for k in ['a','b']]
            pa,pb=[next(p for p in MODS[item['module']]['ports'] if p['id']==link[key]) for item,key in [(a,'port_a'),(b,'port_b')]]
            assert pa['kind']==pb['kind'] and pa['lanes']==pb['lanes'] and pa['level']==pb['level']
            assert abs(pa['width_m']-pb['width_m'])<1e-6
            assert distance(transform(pa['position_m'],a),transform(pb['position_m'],b))<=.001
            assert angle(transform(pa['outward'],a,True),mul(transform(pb['outward'],b,True),-1))<=.1
            REPORT['connections_checked']+=1
        except Exception as exc:REPORT['errors'].append(f'{name} {link}: {exc}')
    for check in example['checks']:
        try:
            if check.get('flat_reach_m'):
                assert check['flat_reach_m']==180
                for x in check['pier_stations_m']:
                    assert abs(x-check['ground_crossing_station_m'])-.8>check['ground_walk_outer_edge_m'],('support enters ground road/walk',x)
            if check.get('kind')=='grade_separated_crossing':
                deckbottom=min(v[2] for v in GEOMETRY['RoadPlan_DeckStructure_L16']['vertices'])+check['deck_surface_z_m']
                assert deckbottom>=8-.001
                for x in check['pier_stations_m']:assert abs(x)-.8>check['ground_walk_outer_edge_m']
                assert check['no_graph_connection_between_levels']
                REPORT['clearance_checks'].append(dict(example=name,actual_structural_clearance_m=deckbottom,pier_clear_of_ground_walk=True))
        except Exception as exc:REPORT['errors'].append(f'{name}: {exc}')

required=[f'RoadPlan_Surface_{lanes}L_L{n}' for lanes in [2,4] for n in [4,8,16]]+[
    'RoadPlan_T_4L_2L','RoadPlan_T_2L','RoadPlan_X_2L','RoadPlan_Transition_4L_2L',
    'RoadPlan_Curve_2L_A90_R7','RoadPlan_Ramp_2L_Rise8_L110','RoadPlan_Deck_2L_L8','RoadPlan_Deck_2L_L16',
    'RoadPlan_CrosswalkPaint_2L','RoadPlan_CrosswalkPaint_4L']+[f'RoadPlan_Walk_W2_L{n}' for n in [2,4,8]]
assert all(n in MODS for n in required)
assert all('6L' not in n and 'Tunnel' not in n for n in MODS)
REPORT['all_checks_passed']=not REPORT['errors']
(ROOT/'Validation/road_plan_export_report.json').write_text(json.dumps(REPORT,indent=2)+'\n',encoding='utf-8')
print(json.dumps(REPORT,indent=2))
assert REPORT['all_checks_passed'],REPORT['errors'][:12]
