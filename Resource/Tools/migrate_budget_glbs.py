"""Replace mesh payloads from the Blender bank; preserve transforms/rigs/clips.

Part identities, export paths and assembly nodes remain unchanged.
"""
import copy,hashlib,json,struct,sys,collections,zipfile,time
from pathlib import Path
from verify_resource_pack import read_glb,accessor,FORMATS,WIDTHS
ROOT=Path(__file__).resolve().parents[1]
bank,bankbin=read_glb(ROOT/'Validation/Budget_Geometry.glb')
banknodes={n['extras']['asset_id']:n for n in bank['nodes'] if 'mesh' in n and n.get('extras',{}).get('asset_id')}
catalog=json.loads((ROOT/'Validation/size_budget_catalog_staged.json').read_text(encoding='utf-8'))
mapping=json.loads((ROOT/'Validation/size_budget_mapping.json').read_text(encoding='utf-8'))
assets={a['id']:a for a in catalog['assets']}
assert set(banknodes)==set(assets)
from part_budget_policy import REVISION
assert all(ids==[n] for n,ids in mapping.items()),'User forbids budget-driven part/file splits'

def animation_hash(doc,binary):
    h=hashlib.sha256()
    for a in doc.get('animations',[]):
        h.update(json.dumps({k:v for k,v in a.items() if k!='samplers'},sort_keys=True).encode())
        for s in a['samplers']:
            h.update(s.get('interpolation','LINEAR').encode())
            for key in ['input','output']:
                aa=doc['accessors'][s[key]];view=doc['bufferViews'][aa['bufferView']]
                width=WIDTHS[aa['type']];fmt,size=FORMATS[aa['componentType']]
                start=view.get('byteOffset',0)+aa.get('byteOffset',0);stride=view.get('byteStride',width*size)
                h.update(str((aa['type'],aa['componentType'],aa['count'])).encode())
                for i in range(aa['count']):h.update(binary[start+i*stride:start+i*stride+width*size])
    return h.hexdigest()

def compact(doc,binary):
    meshes=sorted({n['mesh'] for n in doc['nodes'] if 'mesh' in n});mm={old:i for i,old in enumerate(meshes)}
    doc['meshes']=[doc['meshes'][i] for i in meshes]
    for n in doc['nodes']:
        if 'mesh' in n:n['mesh']=mm[n['mesh']]
    used=set()
    for m in doc['meshes']:
        for p in m['primitives']:
            used.update(p['attributes'].values())
            if 'indices' in p:used.add(p['indices'])
            for t in p.get('targets',[]):used.update(t.values())
    for s in doc.get('skins',[]):
        if 'inverseBindMatrices' in s:used.add(s['inverseBindMatrices'])
    for a in doc.get('animations',[]):
        for s in a['samplers']:used.update([s['input'],s['output']])
    aa={old:i for i,old in enumerate(sorted(used))};doc['accessors']=[doc['accessors'][i] for i in sorted(used)]
    for m in doc['meshes']:
        for p in m['primitives']:
            p['attributes']={k:aa[v] for k,v in p['attributes'].items()}
            if 'indices' in p:p['indices']=aa[p['indices']]
            p['targets']=[{k:aa[v] for k,v in t.items()} for t in p.get('targets',[])] if p.get('targets') else p.get('targets',[])
            if not p.get('targets'):p.pop('targets',None)
    for s in doc.get('skins',[]):
        if 'inverseBindMatrices' in s:s['inverseBindMatrices']=aa[s['inverseBindMatrices']]
    for a in doc.get('animations',[]):
        for s in a['samplers']:s['input']=aa[s['input']];s['output']=aa[s['output']]
    views={a['bufferView'] for a in doc['accessors']}
    assert not any(a.get('sparse') for a in doc['accessors'])
    views.update(i['bufferView'] for i in doc.get('images',[]) if 'bufferView' in i)
    vm={old:i for i,old in enumerate(sorted(views))};payload=bytearray();newviews=[]
    for index in sorted(views):
        old=doc['bufferViews'][index];new=copy.deepcopy(old);assert old.get('buffer',0)==0
        payload.extend(b'\0'*((-len(payload))%4));start=old.get('byteOffset',0)
        new['byteOffset']=len(payload);payload.extend(binary[start:start+old['byteLength']]);newviews.append(new)
    for a in doc['accessors']:a['bufferView']=vm[a['bufferView']]
    for image in doc.get('images',[]):
        if 'bufferView' in image:image['bufferView']=vm[image['bufferView']]
    doc['bufferViews']=newviews;doc['buffers']=[{'byteLength':len(payload)}]
    return doc,payload

def save(doc,binary,path):
    doc,binary=compact(doc,binary)
    raw=json.dumps(doc,separators=(',',':'),ensure_ascii=False).encode('utf-8');raw+=b' '*((-len(raw))%4)
    binary.extend(b'\0'*((-len(binary))%4))
    data=struct.pack('<4sII',b'glTF',2,12+8+len(raw)+8+len(binary))+struct.pack('<II',len(raw),0x4E4F534A)+raw+struct.pack('<II',len(binary),0x004E4942)+binary
    stage=path.with_name(path.stem+'.budget-staging.glb');stage.write_bytes(data)
    for attempt in range(10):
        try:stage.replace(path);break
        except PermissionError:
            if attempt==9:raise
            time.sleep(.2*(attempt+1))
    return doc,binary

def rewrite(original,original_binary,assembly,force_part=None):
    doc=copy.deepcopy(original);binary=bytearray(original_binary)
    before_anim=animation_hash(doc,binary);before_skins=copy.deepcopy(doc.get('skins',[]))
    old_nodes=copy.deepcopy(doc['nodes']);parents={c:i for i,n in enumerate(old_nodes) for c in n.get('children',[])}
    viewcache={};accesscache={};meshcache={};bindchecks=set();added=[]
    def buffer_view(index):
        if index not in viewcache:
            old=bank['bufferViews'][index];new=copy.deepcopy(old);assert old.get('buffer',0)==0
            binary.extend(b'\0'*((-len(binary))%4));start=old.get('byteOffset',0)
            new['byteOffset']=len(binary);binary.extend(bankbin[start:start+old['byteLength']]);viewcache[index]=len(doc['bufferViews']);doc['bufferViews'].append(new)
        return viewcache[index]
    def import_accessor(index,jointmap=None):
        key=(index,tuple(jointmap) if jointmap is not None else None)
        if key in accesscache:return accesscache[key]
        a=copy.deepcopy(bank['accessors'][index]);assert not a.get('sparse')
        if jointmap is None:a['bufferView']=buffer_view(a['bufferView'])
        else:
            values=accessor(bank,bankbin,index);fmt,width=FORMATS[a['componentType']]
            packed=b''.join(struct.pack('<'+fmt*len(row),*(jointmap[int(v)] for v in row)) for row in values)
            binary.extend(b'\0'*((-len(binary))%4));offset=len(binary);binary.extend(packed)
            a['bufferView']=len(doc['bufferViews']);a.pop('byteOffset',None);a.pop('min',None);a.pop('max',None)
            doc['bufferViews'].append(dict(buffer=0,byteOffset=offset,byteLength=len(packed),target=34962))
        result=len(doc['accessors']);doc['accessors'].append(a);accesscache[key]=result;return result
    def mesh_for(pid,oldnode):
        node=banknodes[pid];bs=bank['skins'][node['skin']] if 'skin' in node else None
        skin=doc['skins'][oldnode['skin']] if 'skin' in oldnode else None
        jointmap=None;rigid=None
        if skin and not bs:
            # The pressure door reuses a static prototype as a rigid leaf skin.
            # Derive its exact existing binding instead of inventing a new rig.
            active=set()
            for prim in original['meshes'][oldnode['mesh']]['primitives']:
                attrs=prim['attributes']
                for joints,weights in zip(accessor(original,original_binary,attrs['JOINTS_0']),accessor(original,original_binary,attrs['WEIGHTS_0'])):
                    active.update(j for j,w in zip(joints,weights) if w>0)
            assert len(active)==1,(pid,'non-rigid static prototype requires authored skin',active)
            rigid=int(next(iter(active)))
        elif skin:
            source_names=[bank['nodes'][i]['name'] for i in bs['joints']]
            dest_names=[doc['nodes'][i]['name'] for i in skin['joints']]
            assert set(source_names)==set(dest_names),(pid,source_names,dest_names)
            jointmap=[dest_names.index(n) for n in source_names]
            key=(node['skin'],oldnode['skin'])
            if key not in bindchecks:
                src=accessor(bank,bankbin,bs['inverseBindMatrices']);dst=accessor(doc,binary,skin['inverseBindMatrices'])
                assert max(abs(x-y) for i,m in enumerate(src) for x,y in zip(m,dst[jointmap[i]]))<2e-5,(pid,'inverse binds differ')
                bindchecks.add(key)
        else:assert not bs,(pid,'lost destination skin')
        old_mats=[p['material'] for p in original['meshes'][oldnode['mesh']]['primitives'] if 'material' in p]
        available={doc['materials'][i].get('name'):i for i in old_mats}
        palette=[]
        source_mesh=bank['meshes'][node['mesh']]
        for prim in source_mesh['primitives']:
            name=bank['materials'][prim['material']].get('name')
            if name in available:material=available[name]
            elif name.startswith('Skin_'):
                choices=[i for n,i in available.items() if n.startswith('Skin_')]
                assert len(set(choices))==1,(pid,'ambiguous skin palette',available,name)
                material=choices[0]
            else:
                choices=[i for i,m in enumerate(doc['materials']) if m.get('name')==name]
                assert len(choices)==1,(pid,'material missing',name,available)
                material=choices[0]
            palette.append(material)
        cachekey=(pid,tuple(jointmap) if jointmap is not None else None,rigid,tuple(palette))
        if cachekey in meshcache:return meshcache[cachekey]
        mesh=copy.deepcopy(source_mesh)
        for prim,material in zip(mesh['primitives'],palette):
            prim['material']=material
            prim['attributes']={k:import_accessor(v,jointmap if k.startswith('JOINTS_') else None) for k,v in prim['attributes'].items()}
            prim['indices']=import_accessor(prim['indices'])
            if rigid is not None:
                count=doc['accessors'][prim['attributes']['POSITION']]['count']
                for key,component,values,fmt in [('JOINTS_0',5123,(rigid,0,0,0),'H'),('WEIGHTS_0',5126,(1.,0.,0.,0.),'f')]:
                    binary.extend(b'\0'*((-len(binary))%4));offset=len(binary);packed=struct.pack('<'+fmt*4,*values)*count;binary.extend(packed)
                    vi=len(doc['bufferViews']);doc['bufferViews'].append(dict(buffer=0,byteOffset=offset,byteLength=len(packed),target=34962))
                    ai=len(doc['accessors']);doc['accessors'].append(dict(bufferView=vi,componentType=component,count=count,type='VEC4'))
                    prim['attributes'][key]=ai
        result=len(doc['meshes']);doc['meshes'].append(mesh);meshcache[cachekey]=result;return result
    for index,oldnode in enumerate(old_nodes):
        if 'mesh' not in oldnode:continue
        parent_id=oldnode.get('extras',{}).get('asset_id')
        assert parent_id in mapping,('unknown mesh asset',parent_id,oldnode.get('name'))
        ids=[force_part] if force_part else mapping[parent_id] if assembly else [parent_id]
        assert ids==[parent_id], 'Budget changes must not split parts'
        for pid in ids:
            node=doc['nodes'][index]
            node['mesh']=mesh_for(pid,oldnode)
            node.setdefault('extras',{}).update(asset_id=pid,triangle_count=assets[pid]['triangles'],max_triangles=assets[pid]['triangle_budget'],
                                                budget_parent=parent_id,budget_revision=REVISION,functional_role=assets[pid]['functional_role'])
            added.append(pid)
    assert doc.get('skins',[])==before_skins
    assert animation_hash(doc,binary)==before_anim
    assert len(doc['nodes'])==len(old_nodes),'Mesh migration must not add scene nodes'
    doc.setdefault('asset',{}).setdefault('extras',{})['part_budget_revision']=REVISION
    return doc,binary,added,before_anim

def run():
    baseline=zipfile.ZipFile(ROOT/'Source/Archives/Pre200Delivery_20261026.zip')
    originals=catalog['exports'];single={e['mesh_parts'][0]:e for e in originals if e['type']=='module' and len(e['mesh_parts'])==1}
    assert set(single)==set(mapping)
    exports=[];report={'files':{},'new_part_files':0,'animation_preservation':{}}
    for index,e in enumerate(originals):
        relative=e['file'].replace('\\','/');path=ROOT/relative
        # Geometry revisions must retain the newest animation payloads, including
        # directional clips authored after the frozen recovery snapshot.
        raw=path.read_bytes()
        class BytesFile:
            def read_bytes(self):return raw
        doc,binary=read_glb(BytesFile())
        is_part=e['type']=='module' and len(e['mesh_parts'])==1
        result,payload,ids,anim=rewrite(doc,binary,not is_part)
        final,buf=save(result,payload,path)
        assert animation_hash(final,buf)==anim
        updated={**e,'mesh_parts':list(dict.fromkeys(ids)),'budget_revision':REVISION,'clips':len(final.get('animations',[]))}
        exports.append(updated);report['files'][updated['file']]={'meshes':len(final['meshes']),'parts':ids,'clips':len(final.get('animations',[]))}
        if final.get('animations'):report['animation_preservation'][updated['file']]=anim
        if index%200==0:print('GLB_BUDGET_MIGRATION',index,'/',len(originals),flush=True)
    catalog['exports']=exports
    (ROOT/'Documentation/asset_catalog.json').write_text(json.dumps(catalog,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    report['summary']={'files':len(exports),'parts':len(assets),'max_triangles':max(a['triangles'] for a in assets.values()),'animated_files_preserved':len(report['animation_preservation'])}
    report['all_checks_passed']=True
    (ROOT/'Validation/size_budget_glb_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('GLB_BUDGET_MIGRATION_COMPLETE',json.dumps(report['summary']),flush=True)

if __name__=='__main__':run()
