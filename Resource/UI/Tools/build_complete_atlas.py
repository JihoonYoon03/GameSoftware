"""Lossless complete UI atlas. Keeps every source and legacy sprite ID."""
from pathlib import Path
from PIL import Image
import hashlib
import json

ROOT=Path(__file__).resolve().parents[1]
def digest(data):return hashlib.sha256(data).hexdigest()
def read(p):return json.loads((ROOT/p).read_text(encoding='utf8'))
def save(p,data):(ROOT/p).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def build():
    m=read('ui_manifest.json');baseline=read('Source/performance_baseline_20261109.json')['sprites']
    assets={f'{namespace}/{name}':a for group,namespace in [('icons','Icons'),('hud_icons','HUDIcons'),('components','Components')] for name,a in m[group].items()}
    assert len(assets)==sum(len(m[g]) for g in ['icons','hud_icons','components'])
    assert set(baseline)<=set(assets)
    atlas=Image.new('RGBA',(2048,2048));entries={};images={};x=y=rowh=0;pad=4;legacy=[]
    # Stable size ordering and names make identical input deterministic.
    for key,a in sorted(assets.items(),key=lambda item:(-item[1]['height'],-item[1]['width'],item[0])):
        image=Image.open(ROOT/a['png']).convert('RGBA');w,h=image.size
        assert [w,h]==[a['width'],a['height']]
        if x+w+2*pad>2048:x=0;y+=rowh;rowh=0
        assert y+h+2*pad<=2048,'Atlas capacity exceeded'
        px,py=x+pad,y+pad;atlas.paste(image,(px,py));images[key]=image
        sha=digest((ROOT/a['png']).read_bytes())
        entries[key]=dict(x=px,y=py,width=w,height=h,source=a['png'],source_sha256=sha,rgba_sha256=digest(image.tobytes()),
            uv_pixel_centers=[(px+.5)/2048,(py+.5)/2048,(px+w-.5)/2048,(py+h-.5)/2048])
        if key in baseline:
            old=baseline[key];assert sha==old['sha256'] and digest(image.tobytes())==old['rgba_sha256'] and [w,h]==old['size'],key
            legacy.append(key)
        x+=w+2*pad;rowh=max(rowh,h+2*pad)
    target=ROOT/'Export/Atlases/ui_complete.png';atlas.save(target)
    gutter_checks=0
    for key,e in entries.items():
        x,y,w,h=(e[k] for k in ['x','y','width','height']);assert atlas.crop((x,y,x+w,y+h)).tobytes()==images[key].tobytes()
        strips=[(x-pad,y-pad,x+w+pad,y),(x-pad,y+h,x+w+pad,y+h+pad),(x-pad,y,x,y+h),(x+w,y,x+w+pad,y+h)]
        for region in strips:assert atlas.crop(region).getchannel('A').getbbox() is None
        # Bilinear support reaches at most one pixel outside the sprite rectangle.
        # Compare all four border neighborhoods against the source on an isolated 4px pad.
        isolated=Image.new('RGBA',(w+2*pad,h+2*pad));isolated.paste(images[key],(pad,pad))
        neighborhood=atlas.crop((x-pad,y-pad,x+w+pad,y+h+pad))
        assert neighborhood.tobytes()==isolated.tobytes()
        for scale in (1.25,.75):
            size=(max(1,round((w+8)*scale)),max(1,round((h+8)*scale)))
            assert neighborhood.resize(size,Image.Resampling.BILINEAR).tobytes()==isolated.resize(size,Image.Resampling.BILINEAR).tobytes()
            gutter_checks+=1
    data=dict(schema='erma.ui.atlas',version=1,atlas=dict(file='ui_complete.png',width=2048,height=2048,sha256=digest(target.read_bytes())),
        format=dict(channels='RGBA8',alpha='straight',color_space='sRGB',origin='top-left',row_direction='down',rectangles='integer pixels; exclude gutter'),
        padding=pad,nine_slice_inset=12,sampling=dict(uv='pixel centers; .5 inset',filter='linear',mipmaps=False,wrap='clamp_to_edge'),
        source_count=len(entries),legacy_count=len(legacy),sprites=entries,runtime_loader_integrated=False)
    save('Export/Atlases/ui_complete.json',data)
    report=dict(status='PASS',sprite_count=len(entries),legacy_count=len(legacy),legacy_expected=len(baseline),missing=[],duplicates=[],
        original_png_hashes_preserved=True,rgba_crops_exact=True,transparent_gutter_px=pad,bilinear_isolated_comparisons=gutter_checks,
        scope='Exact source pixels and bilinear isolation, without mipmaps. Loader, draw batching and startup timing have not been changed or measured.')
    save('Validation/20261109/atlas_report.json',report)
    print(json.dumps(report,ensure_ascii=False))
if __name__=='__main__':build()
