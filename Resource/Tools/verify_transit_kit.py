"""Validate actual exported mesh sockets, joint placement and sparse-guide geometry."""
import json
import math
from pathlib import Path
from verify_resource_pack import read_glb, accessor

ROOT=Path(__file__).resolve().parents[1]
KIT=json.loads((ROOT/'Documentation/transit_modules.json').read_text(encoding='utf-8'))
MODULES=KIT['modules']
REPORT=dict(modules_checked=0,physical_ports_checked=0,example_connections_checked=0,
            companion_edges_checked=0,airguide_modules_checked=0,max_port_error_m=0,
            max_example_connection_error_m=0,max_airguide_plan_coverage_ratio=0,errors=[])

def distance(a,b):return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def transform(p,placement,direction=False):
    a=math.radians(placement['yaw_degrees']);c,s=math.cos(a),math.sin(a)
    q=[c*p[0]-s*p[1],s*p[0]+c*p[1],p[2]]
    return q if direction else [x+y for x,y in zip(q,placement['translation_m'])]

GEOMETRY={}
for name,info in MODULES.items():
    try:
        doc,bin=read_glb(ROOT/'Export/Transit'/f'{name}.glb')
        vertices=[];top_vertices=[];up_area=0;faces=[]
        for mesh in doc['meshes']:
            for prim in mesh['primitives']:
                ps=[(v[0],-v[2],v[1]) for v in accessor(doc,bin,prim['attributes']['POSITION'])]
                vertices+=ps
                inds=[v[0] for v in accessor(doc,bin,prim['indices'])]
                for k in range(0,len(inds),3):
                    p,q,r=[ps[i] for i in inds[k:k+3]]
                    normal=cross([q[i]-p[i] for i in range(3)],[r[i]-p[i] for i in range(3)])
                    area=math.sqrt(sum(x*x for x in normal))/2
                    assert area>1e-10,(mesh['name'],'degenerate exported triangle')
                    if normal[2]>0:
                        up_area+=normal[2]/2
                        top_vertices.extend([p,q,r])
                    faces.append((p,q,r))
        assert len(doc['meshes'])==len(info['parts']), 'missing assembled meshes'
        GEOMETRY[name]=(vertices,top_vertices)
        for port in info['ports']:
            assert abs(sum(x*x for x in port['outward'])-1)<1e-5
            if info['category']=='airguide':continue
            nx,ny,_=port['outward'];p=port['position_m'];w=port['width_m']
            for s in [-1,1]:
                edge=(p[0]-ny*w/2*s,p[1]+nx*w/2*s,p[2])
                err=min(distance(v,edge) for v in top_vertices)
                REPORT['max_port_error_m']=max(REPORT['max_port_error_m'],err)
                assert err<2e-5,(port['id'],'missing actual surface edge',err)
            REPORT['physical_ports_checked']+=1
        if info['category']=='airguide':
            for v in vertices:
                if 'radius_m' in info:
                    radius=info['radius_m'];lateral=abs(math.hypot(v[0],v[1]-radius)-radius)
                elif 'Junction' in name:lateral=min(abs(v[0]),abs(v[1]))
                else:lateral=abs(v[1])
                assert lateral>2.5,(name,'mesh obstructs central 5m clear corridor',v)
                assert abs(v[2])<=.03,(name,'unexpected overhead panel')
            area=info.get('path_length_m',16)*6
            ratio=up_area/area
            REPORT['max_airguide_plan_coverage_ratio']=max(REPORT['max_airguide_plan_coverage_ratio'],ratio)
            assert ratio<.005,(name,'excessive plan coverage',ratio)
            REPORT['airguide_modules_checked']+=1
        REPORT['modules_checked']+=1
    except Exception as e:REPORT['errors'].append(f'{name}: {e}')

# Matching road/curb boundaries use identical subdivision, so no slivers appear in corners.
for name,info in MODULES.items():
    companion=info.get('walk_companion')
    if not companion:continue
    try:
        road_vertices,road_top=GEOMETRY[name];walk_vertices,walk_top=GEOMETRY[companion]
        # Every curb-bottom point on the roadway border is shared with the road top outline.
        # Test road border vertices at Z=0 against pavement footprint at any vertical level.
        if 'Curve' in name:
            r=info['radius_m'];h=info['lanes']*1.5
            edgepoints=[v for v in road_top if abs(v[2])<1e-6 and
                        min(abs(math.hypot(v[0],v[1]-r)-(r-h)),abs(math.hypot(v[0],v[1]-r)-(r+h)))<1e-5]
        elif 'Junction' in name:
            L=info['footprint_m'][0]/2;r=info['corner_radius_m']
            centers=[(sx*L,sy*L) for sx in [-1,1] for sy in [-1,1] if not ('TJunction' in name and sy==1)]
            edgepoints=[v for v in road_top if abs(v[2])<1e-6 and
                        any(abs(math.hypot(v[0]-x,v[1]-y)-r)<1e-5 for x,y in centers)]
        else:continue
        assert edgepoints,'no road edge samples'
        for v in edgepoints:
            err=min(math.hypot(v[0]-p[0],v[1]-p[1]) for p in walk_top)
            assert err<2e-5,(name,'companion outline subdivision mismatch',err)
            REPORT['companion_edges_checked']+=1
    except Exception as e:REPORT['errors'].append(f'{name}: {e}')

layout=json.loads((ROOT/'Documentation/transit_layout_example.json').read_text(encoding='utf-8'))
for link in layout['connections']:
    try:
        aa=layout['placements'][link['a']];bb=layout['placements'][link['b']]
        pa=next(p for p in MODULES[aa['module']]['ports'] if p['id']==link['port_a'])
        pb=next(p for p in MODULES[bb['module']]['ports'] if p['id']==link['port_b'])
        assert pa['kind']==pb['kind'] and pa['width_m']==pb['width_m']
        err=distance(transform(pa['position_m'],aa),transform(pb['position_m'],bb))
        REPORT['max_example_connection_error_m']=max(REPORT['max_example_connection_error_m'],err)
        assert err<1e-5
        va=transform(pa['outward'],aa,True);vb=transform(pb['outward'],bb,True)
        assert distance(va,[-v for v in vb])<1e-6
        REPORT['example_connections_checked']+=1
    except Exception as e:REPORT['errors'].append(f'example {link}: {e}')

REPORT['all_checks_passed']=not REPORT['errors']
(ROOT/'Validation/transit_connection_report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(REPORT,ensure_ascii=False,indent=2))
assert REPORT['all_checks_passed'],REPORT['errors']
