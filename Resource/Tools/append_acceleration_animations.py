"""Append only the new animation streams, protecting all original data and clips."""
import copy,json,struct,hashlib,time
from pathlib import Path
from verify_resource_pack import read_glb,accessor
ROOT=Path(__file__).resolve().parents[1]
NEW=set(json.loads((ROOT/'Validation/acceleration_authoring.json').read_text())['clips'])
src,bb=read_glb(ROOT/'Validation/Acceleration_Animation_Bank.glb');assert {a['name'] for a in src['animations']}==NEW
def signature(doc,buf,animation):
    data=[]
    for channel in animation['channels']:
        sampler=animation['samplers'][channel['sampler']]
        data.append((doc['nodes'][channel['target']['node']]['name'],channel['target']['path'],sampler.get('interpolation','LINEAR'),accessor(doc,buf,sampler['input']),accessor(doc,buf,sampler['output'])))
    return hashlib.sha256(json.dumps(sorted(data,key=lambda x:(x[0],x[1])),separators=(',',':')).encode()).hexdigest()
bank_sig={a['name']:signature(src,bb,a) for a in src['animations']};report={'files':{},'all_checks_passed':False}
catpath=ROOT/'Documentation/asset_catalog.json';cat=json.loads(catpath.read_text(encoding='utf-8'))
for e in cat['exports']:
    if e['type']!='character':continue
    path=ROOT/e['file'];doc,buf=read_glb(path);payload=bytearray(buf)
    old={a['name']:signature(doc,buf,a) for a in doc['animations'] if a['name'] not in NEW}
    protected={k:copy.deepcopy(doc.get(k)) for k in ['nodes','meshes','skins','materials','textures','images','samplers']}
    byname={doc['nodes'][i]['name']:i for skin in doc['skins'] for i in skin['joints']};assert len(byname)==23
    views={};accs={}
    def view(i):
        if i not in views:
            v=copy.deepcopy(src['bufferViews'][i]);start=v.get('byteOffset',0);payload.extend(b'\0'*((-len(payload))%4));v['byteOffset']=len(payload)
            payload.extend(bb[start:start+v['byteLength']]);views[i]=len(doc['bufferViews']);doc['bufferViews'].append(v)
        return views[i]
    def acc(i):
        if i not in accs:
            a=copy.deepcopy(src['accessors'][i]);a['bufferView']=view(a['bufferView']);accs[i]=len(doc['accessors']);doc['accessors'].append(a)
        return accs[i]
    existing_new={a['name']:signature(doc,buf,a) for a in doc['animations'] if a['name'] in NEW}
    changed={n for n in NEW if existing_new.get(n)!=bank_sig[n]}
    if existing_new!=bank_sig:
        doc['animations']=[a for a in doc['animations'] if a['name'] not in changed]
        for animation in src['animations']:
            if animation['name'] not in changed:continue
            a=copy.deepcopy(animation)
            for s in a['samplers']:s['input']=acc(s['input']);s['output']=acc(s['output'])
            for ch in a['channels']:ch['target']['node']=byname[src['nodes'][ch['target']['node']]['name']]
            doc['animations'].append(a)
        doc['buffers'][0]['byteLength']=len(payload);raw=json.dumps(doc,separators=(',',':'),ensure_ascii=False).encode();raw+=b' '*((-len(raw))%4);payload+=b'\0'*((-len(payload))%4)
        data=struct.pack('<4sII',b'glTF',2,12+8+len(raw)+8+len(payload))+struct.pack('<II',len(raw),0x4E4F534A)+raw+struct.pack('<II',len(payload),0x004E4942)+payload
        stage=path.with_suffix('.acceleration-staging.glb');stage.write_bytes(data)
        for attempt in range(10):
            try:stage.replace(path);break
            except PermissionError:
                if attempt==9:raise
                time.sleep(.2*(attempt+1))
    final,fb=read_glb(path)
    assert all(final.get(k)==v for k,v in protected.items())
    assert bytes(fb[:len(buf)])==bytes(buf),'original binary prefix changed'
    assert old=={a['name']:signature(final,fb,a) for a in final['animations'] if a['name'] not in NEW}
    assert bank_sig=={a['name']:signature(final,fb,a) for a in final['animations'] if a['name'] in NEW}
    e['clips']=len(final['animations']);assert e['clips']==len(old)+len(NEW)
    report['files'][e['file']]={'preserved_clips':len(old),'new_clips':len(NEW),'clips':e['clips'],'streams_written':len(changed),'geometry_skin_materials_and_bind_unchanged':True,'original_binary_prefix_sha256':hashlib.sha256(buf).hexdigest()}
assert len(report['files'])==10
catpath.write_text(json.dumps(cat,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
report['all_checks_passed']=True;(ROOT/'Validation/acceleration_glb_preservation.json').write_text(json.dumps(report,indent=2)+'\n')
print('ACCELERATION_ANIMATIONS_APPENDED',len(report['files']),'CHARACTERS; EXISTING CLIPS/GEOMETRY PRESERVED',flush=True)
