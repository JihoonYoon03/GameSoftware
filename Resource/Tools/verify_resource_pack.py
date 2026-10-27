"""Validate deliverable GLBs without requiring a game project or Blender."""
import json, struct, math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FORMATS={5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
WIDTHS={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}

def read_glb(path):
    raw=path.read_bytes()
    assert raw[:4]==b'glTF' and struct.unpack_from('<I',raw,4)[0]==2
    assert struct.unpack_from('<I',raw,8)[0]==len(raw)
    ptr=12;doc=None;binary=b''
    while ptr<len(raw):
        length,kind=struct.unpack_from('<II',raw,ptr);ptr+=8
        if kind==0x4E4F534A:doc=json.loads(raw[ptr:ptr+length].decode('utf-8'))
        if kind==0x004E4942:binary=raw[ptr:ptr+length]
        ptr+=length
    assert doc is not None
    return doc,binary

def accessor(doc,binary,index):
    a=doc['accessors'][index];view=doc['bufferViews'][a['bufferView']]
    fmt,size=FORMATS[a['componentType']];width=WIDTHS[a['type']]
    stride=view.get('byteStride',size*width)
    start=view.get('byteOffset',0)+a.get('byteOffset',0)
    return [struct.unpack_from('<'+fmt*width,binary,start+i*stride) for i in range(a['count'])]

def verify():
    catalog=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
    assets={a['id']:a for a in catalog['assets']}
    report={'files_checked':0,'meshes_checked':0,'skinned_mesh_nodes':0,'animated_exports':0,
      'max_triangles_per_mesh':0,'animation_counts':{},'errors':[],'warnings':[]}
    for entry in catalog['exports']:
        file=entry['file']
        try:
            doc,binary=read_glb(ROOT/file);report['files_checked']+=1
            assert not doc.get('extensionsRequired'), 'unexpected required extension'
            assert doc.get('scenes') and all(s.get('nodes') for s in doc['scenes']),'empty exported scene'
            assert 0<=doc.get('scene',0)<len(doc['scenes']),'invalid default scene'
            for mi,mesh in enumerate(doc.get('meshes',[])):
                report['meshes_checked']+=1;triangles=0
                for prim in mesh['primitives']:
                    assert prim.get('mode',4)==4,'non-triangle topology'
                    indices=accessor(doc,binary,prim['indices'])
                    assert len(indices)%3==0
                    triangles+=len(indices)//3
                    attrs=prim['attributes'];positions=accessor(doc,binary,attrs['POSITION'])
                    assert all(math.isfinite(x) for xyz in positions for x in xyz),'non-finite position'
                    assert all(i[0]<len(positions) for i in indices),'invalid vertex index'
                    assert 'NORMAL' in attrs and 'TEXCOORD_0' in attrs,'missing normal/UV'
                    if 'WEIGHTS_0' in attrs:
                        wa=doc['accessors'][attrs['WEIGHTS_0']]
                        weights=accessor(doc,binary,attrs['WEIGHTS_0'])
                        divisor=65535 if wa.get('normalized') and wa['componentType']==5123 else 255 if wa.get('normalized') and wa['componentType']==5121 else 1
                        assert all(abs(sum(w)/divisor-1)<.002 for w in weights),'invalid skin weights'
                ids={n.get('extras',{}).get('asset_id') for n in doc['nodes'] if n.get('mesh')==mi}
                assert ids and None not in ids, 'mesh identity missing'
                cap=min(assets[pid]['triangle_budget'] for pid in ids)
                assert 0<triangles<=cap<=200,f'{mesh.get("name")}: {triangles} triangles, cap {cap}'
                report['max_triangles_per_mesh']=max(report['max_triangles_per_mesh'],triangles)
            for node in doc.get('nodes',[]):
                if 'skin' in node:
                    report['skinned_mesh_nodes']+=1
                    assert node['skin']<len(doc['skins'])
                    skin=doc['skins'][node['skin']]
                    assert len(skin['joints'])>1,'single root is not an articulated skeleton'
                    if 'inverseBindMatrices' in skin:
                        matrices=accessor(doc,binary,skin['inverseBindMatrices'])
                        assert all(math.isfinite(x) for m in matrices for x in m)
            animations=doc.get('animations',[])
            assert len(animations)==entry['clips'],f'expected {entry["clips"]} clips, got {len(animations)}'
            if animations:
                report['animated_exports']+=1;report['animation_counts'][file]=[a.get('name') for a in animations]
                assert len({a.get('name') for a in animations})==len(animations),'duplicate clip names'
                for anim in animations:
                    assert anim.get('channels'),'empty animation'
                    for sampler in anim['samplers']:
                        times=[x[0] for x in accessor(doc,binary,sampler['input'])]
                        assert len(times)>=2 and all(a<b for a,b in zip(times,times[1:])),'invalid clip timeline'
                        assert all(math.isfinite(x) for row in accessor(doc,binary,sampler['output']) for x in row),'invalid animation sample'
            if entry['type'] in ['character','creature','creature_variant']:
                names={a.get('name') for a in animations}
                assert {'Walk','Run','Turn_Base','Reference_A','Attack_Melee','Attack_Fire','Jump_Start','Jump_Air','Jump_Land','Jump_Full','Walk_To_Run'}<=names,f'missing required clips: {names}'
                assert doc.get('skins'),'missing skeletal skin'
        except Exception as e:report['errors'].append(f'{file}: {e}')
    for path in catalog['textures']:
        assert (ROOT/path).is_file(),path
    report['all_checks_passed']=not report['errors']
    (ROOT/'Validation/export_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='animation_counts'},ensure_ascii=False,indent=2))
    assert report['all_checks_passed'],report['errors'][:10]

if __name__=='__main__':verify()
