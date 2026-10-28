"""Repair invalid Blender/MikkTSpace outputs using actual triangle UV derivatives.

Some thin planar strips export zero tangent vectors despite valid UV triangles.
Positions, indices, normals, UVs and materials are never changed here.
"""
import json
import math
import struct
from verify_resource_pack import read_glb,accessor

def repair(path):
    doc,source=read_glb(path);binary=bytearray(source[:doc['buffers'][0]['byteLength']]);fixed=0
    def sub(a,b):return tuple(x-y for x,y in zip(a,b))
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
    def norm(a):return math.sqrt(dot(a,a))
    for mesh in doc['meshes']:
        for prim in mesh['primitives']:
            aa=prim['attributes'];points=accessor(doc,binary,aa['POSITION']);uv=accessor(doc,binary,aa['TEXCOORD_0']);nn=accessor(doc,binary,aa['NORMAL'])
            current=accessor(doc,binary,aa['TANGENT']) if 'TANGENT' in aa else [(0,0,0,1)]*len(points)
            bad=[i for i,(n,t) in enumerate(zip(nn,current)) if abs(norm(t[:3])-1)>.001 or abs(dot(n,t[:3]))>.001]
            if not bad:continue
            sums=[[0.0]*6 for _ in points];best=[None]*len(points)
            indices=[i[0] for i in accessor(doc,binary,prim['indices'])]
            for k in range(0,len(indices),3):
                ids=indices[k:k+3];a,b,c=[points[i] for i in ids];ta,tb,tc=[uv[i] for i in ids]
                e1,e2=sub(b,a),sub(c,a);u1,v1=sub(tb,ta);u2,v2=sub(tc,ta);den=u1*v2-u2*v1
                assert abs(den)>1e-20,('UV-degenerate triangle cannot define a tangent',path,mesh['name'],k)
                tangent=tuple((e1[j]*v2-e2[j]*v1)/den for j in range(3))
                bitangent=tuple((e2[j]*u1-e1[j]*u2)/den for j in range(3));weight=norm(cross(e1,e2))
                for i in ids:
                    for j in range(3):sums[i][j]+=tangent[j]*weight;sums[i][j+3]+=bitangent[j]*weight
                    if best[i] is None or weight>best[i][0]:best[i]=(weight,tangent,bitangent)
            if 'TANGENT' not in aa:
                binary+=b'\0'*((-len(binary))%4);offset=len(binary);binary+=b'\0'*(len(points)*16)
                view=len(doc['bufferViews']);doc['bufferViews'].append(dict(buffer=0,byteOffset=offset,byteLength=len(points)*16,target=34962))
                aa['TANGENT']=len(doc['accessors']);doc['accessors'].append(dict(bufferView=view,componentType=5126,count=len(points),type='VEC4'))
            attr=doc['accessors'][aa['TANGENT']];view=doc['bufferViews'][attr['bufferView']]
            assert attr['componentType']==5126
            start=view.get('byteOffset',0)+attr.get('byteOffset',0);stride=view.get('byteStride',16)
            for i in bad:
                n=nn[i];t=sums[i][:3];b=sums[i][3:];projection=dot(n,t);t=tuple(t[j]-n[j]*projection for j in range(3))
                if norm(t)<1e-15:
                    assert best[i] is not None
                    t,b=best[i][1:];projection=dot(n,t);t=tuple(t[j]-n[j]*projection for j in range(3))
                magnitude=norm(t);assert magnitude>1e-15
                t=tuple(x/magnitude for x in t);handed=-1 if dot(cross(n,t),b)<0 else 1
                struct.pack_into('<4f',binary,start+i*stride,*t,handed);fixed+=1
    if fixed:
        doc['buffers'][0]['byteLength']=len(binary)
        js=json.dumps(doc,separators=(',',':'),ensure_ascii=False).encode();js+=b' '*((-len(js))%4)
        binary+=b'\0'*((-len(binary))%4)
        raw=struct.pack('<III',0x46546C67,2,12+8+len(js)+8+len(binary))+struct.pack('<II',len(js),0x4E4F534A)+js+struct.pack('<II',len(binary),0x004E4942)+binary
        path.write_bytes(raw)
    return fixed
