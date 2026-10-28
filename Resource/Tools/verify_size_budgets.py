"""Compare actual deliveries with the frozen pre-migration pack, including rigs."""
import json,math,zipfile,hashlib
from pathlib import Path
from verify_resource_pack import read_glb,accessor,FORMATS,WIDTHS
from part_budget_policy import triangle_budget,REVISION
ROOT=Path(__file__).resolve().parents[1]

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

def run():
    z=zipfile.ZipFile(ROOT/'Source/Archives/Pre200Delivery_20261026.zip')
    old=json.loads(z.read('Documentation/asset_catalog.json'));cat=json.loads((ROOT/'Documentation/asset_catalog.json').read_text(encoding='utf-8'))
    assets={a['id']:a for a in cat['assets']};audit=json.loads((ROOT/'Validation/part_budget_audit.json').read_text())
    assert assets.keys()=={a['id'] for a in old['assets']}
    paths={e['file'].replace('\\','/') for e in cat['exports']}
    assert paths=={e['file'].replace('\\','/') for e in old['exports']}
    assert {str(p.relative_to(ROOT)).replace('\\','/') for p in (ROOT/'Export').rglob('*.glb')}==paths
    for a in assets.values():
        assert a['triangle_budget']==triangle_budget(a['size_diagonal_m'],audit['largest_reference']['diagonal_m'])
        assert 0<a['triangles']<=a['triangle_budget']<=200
    expected_clips={c['clip'] for c in json.loads((ROOT/'Documentation/animation_clips.json').read_text())['HUMAN_7H']}
    report=dict(revision=REVISION,parts=len(assets),files=len(paths),new_part_files=0,animated_files=0,refined_character_files=0,mesh_nodes=0,max_triangles=0,errors=[])
    class BytesFile:
        def __init__(self,data):self.data=data
        def read_bytes(self):return self.data
    for number,e in enumerate(cat['exports']):
        try:
            path=e['file'].replace('\\','/');a,ab=read_glb(BytesFile(z.read(path)));b,bb=read_glb(ROOT/path)
            assert len(a['nodes'])==len(b['nodes']) and a['scenes']==b['scenes']
            if e['type']=='character':
                old_materials={m['name']:m for m in a.get('materials',[])}
                assert all(m==old_materials[m['name']] for m in b.get('materials',[])), 'material values changed'
                assert {an['name'] for an in a['animations']}<=expected_clips=={an['name'] for an in b['animations']}
                report['refined_character_files']+=1
            else:
                assert a.get('materials')==b.get('materials')
                assert animation_hash(a,ab)==animation_hash(b,bb)
            if b.get('animations'):report['animated_files']+=1
            for na,nb in zip(a['nodes'],b['nodes']):
                assert {k:v for k,v in na.items() if k not in ['mesh','extras']}=={k:v for k,v in nb.items() if k not in ['mesh','extras']}
                if 'mesh' not in nb:continue
                report['mesh_nodes']+=1;pid=nb['extras']['asset_id'];assert pid==na['extras']['asset_id']
                count=0
                for p in b['meshes'][nb['mesh']]['primitives']:
                    attrs=p['attributes'];ps=accessor(b,bb,attrs['POSITION']);ii=[i[0] for i in accessor(b,bb,p['indices'])];count+=len(ii)//3
                    assert {'NORMAL','TEXCOORD_0','TANGENT'}<=attrs.keys()
                    nn=accessor(b,bb,attrs['NORMAL']);tt=accessor(b,bb,attrs['TANGENT'])
                    for n,t in zip(nn,tt):
                        assert abs(sum(x*x for x in n)-1)<.004 and abs(sum(x*x for x in t[:3])-1)<.004
                        assert abs(sum(x*y for x,y in zip(n,t)))<.002
                    for j in range(0,len(ii),3):
                        u,v,w=[ps[i] for i in ii[j:j+3]];x=[v[k]-u[k] for k in range(3)];y=[w[k]-u[k] for k in range(3)]
                        cross=[x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]]
                        assert sum(q*q for q in cross)>1e-24,(pid,'degenerate surface')
                assert count==assets[pid]['triangles']<=assets[pid]['triangle_budget']<=200
                report['max_triangles']=max(report['max_triangles'],count)
            assert len(a.get('skins',[]))==len(b.get('skins',[]))
            for sa,sb in zip(a.get('skins',[]),b.get('skins',[])):
                assert {k:v for k,v in sa.items() if k!='inverseBindMatrices'}=={k:v for k,v in sb.items() if k!='inverseBindMatrices'}
                assert accessor(a,ab,sa['inverseBindMatrices'])==accessor(b,bb,sb['inverseBindMatrices'])
        except Exception as exc:report['errors'].append([e['file'],str(exc)])
        if number%500==0:print('VERIFY_SIZE_BUDGET',number,flush=True)
    report['all_checks_passed']=not report['errors']
    (ROOT/'Validation/size_budget_validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report),flush=True);assert report['all_checks_passed']

if __name__=='__main__':run()
