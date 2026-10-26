"""Reuse a common HUMAN_7H animation bank without re-exporting character meshes.

Only animation accessors/channels are copied. Destination geometry, material,
fitting-offset nodes, skins and inverse binds remain byte-for-byte unchanged.
"""
import copy,hashlib,json,struct
from pathlib import Path
from verify_resource_pack import read_glb,accessor
ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'Export/Characters/Player_Explorer.glb'
src,source_binary=read_glb(source)
bank_hash=hashlib.sha256(source.read_bytes()).hexdigest()
joint_names={src['nodes'][i]['name'] for skin in src['skins'] for i in skin['joints']}
assert len(joint_names)==23
def binds(doc,binary):
    result={}
    for skin in doc['skins']:
        for node,m in zip(skin['joints'],accessor(doc,binary,skin['inverseBindMatrices'])):result[doc['nodes'][node]['name']]=m
    return result
source_binds=binds(src,source_binary)
report={'source':str(source.relative_to(ROOT)).replace('\\','/'),'characters':{}}
for path in sorted((ROOT/'Export/Characters').glob('*.glb')):
    if path==source:continue
    doc,binary=read_glb(path);destination_binds=binds(doc,binary)
    assert set(destination_binds)==joint_names,(path.name,'different skeleton')
    assert max(abs(x-y) for n in joint_names for x,y in zip(source_binds[n],destination_binds[n]))<1e-6
    if doc.get('asset',{}).get('extras',{}).get('human_animation_bank_sha256')==bank_hash:
        report['characters'][path.stem]={'clips':len(doc['animations']),'geometry_and_skin_unchanged':True,'reused_identical_bank':True}
        continue
    byname={n['name']:i for i,n in enumerate(doc['nodes']) if n.get('name') in joint_names}
    protected=json.dumps({k:doc.get(k) for k in ['nodes','meshes','skins','materials','textures','images','samplers']},sort_keys=True)
    original_prefix=bytes(binary);payload=bytearray(binary)
    accessors={};views={}
    def view(index):
        if index not in views:
            old=src['bufferViews'][index];assert old.get('buffer',0)==0
            while len(payload)%4:payload.append(0)
            start=old.get('byteOffset',0);size=old['byteLength']
            new=copy.deepcopy(old);new['buffer']=0;new['byteOffset']=len(payload)
            payload.extend(source_binary[start:start+size]);views[index]=len(doc['bufferViews']);doc['bufferViews'].append(new)
        return views[index]
    def acc(index):
        if index not in accessors:
            new=copy.deepcopy(src['accessors'][index]);assert not new.get('sparse')
            new['bufferView']=view(new['bufferView']);accessors[index]=len(doc['accessors']);doc['accessors'].append(new)
        return accessors[index]
    animations=[]
    for animation in src['animations']:
        new=copy.deepcopy(animation)
        for sampler in new['samplers']:
            sampler['input']=acc(sampler['input']);sampler['output']=acc(sampler['output'])
        for channel in new['channels']:
            node=src['nodes'][channel['target']['node']]['name']
            assert node in joint_names,(path.name,'unexpected non-bone animation target',node)
            channel['target']['node']=byname[node]
        animations.append(new)
    doc['animations']=animations
    doc.setdefault('asset',{}).setdefault('extras',{})['human_animation_bank_sha256']=bank_hash
    assert protected==json.dumps({k:doc.get(k) for k in ['nodes','meshes','skins','materials','textures','images','samplers']},sort_keys=True)
    assert bytes(payload[:len(original_prefix)])==original_prefix
    # The untouched old data stays in the binary buffer; the new accessors point
    # to the shared bank appended after it. Avoid copying again on unchanged runs.
    doc['buffers'][0]['byteLength']=len(payload)
    raw=json.dumps(doc,separators=(',',':'),ensure_ascii=False).encode('utf-8');raw+=b' '*((-len(raw))%4)
    payload.extend(b'\0'*((-len(payload))%4))
    total=12+8+len(raw)+8+len(payload)
    output=struct.pack('<4sII',b'glTF',2,total)+struct.pack('<II',len(raw),0x4E4F534A)+raw+struct.pack('<II',len(payload),0x004E4942)+payload
    staged=path.with_suffix('.staging.glb');staged.write_bytes(output);staged.replace(path)
    report['characters'][path.stem]={'clips':len(animations),'geometry_and_skin_unchanged':True,'previous_binary_prefix_sha256':hashlib.sha256(original_prefix).hexdigest(),'animation_accessors_copied':len(accessors)}
    print('SHARED_HUMAN_ANIMATIONS',path.stem,len(animations),flush=True)
assert len(report['characters'])==9
(ROOT/'Validation/civic_shared_animation_bank.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
