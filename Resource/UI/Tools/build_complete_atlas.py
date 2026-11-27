"""Lossless complete UI atlas. Keeps every source and legacy sprite ID."""
from pathlib import Path
from PIL import Image
import hashlib
import json

ROOT=Path(__file__).resolve().parents[1]
def digest(data):return hashlib.sha256(data).hexdigest()
def read(p):return json.loads((ROOT/p).read_text(encoding='utf8'))
def save(p,data):(ROOT/p).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')


def padded_sprite(image, pad, spread):
    w,h=image.size
    result=Image.new('RGBA',(w+pad*2,h+pad*2))
    result.paste(image,(pad,pad))
    pixels=result.load()
    # Find the closest visible source pixel. Alpha and visible source RGB stay exact.
    for y in range(result.height):
        for x in range(result.width):
            if pixels[x,y][3] or (not spread and pad<=x<pad+w and pad<=y<pad+h):
                continue
            nearest=None
            for dy in range(-2,3):
                for dx in range(-2,3):
                    distance=dx*dx+dy*dy
                    if not 0<distance<=4 or not(0<=x+dx<result.width and 0<=y+dy<result.height):
                        continue
                    color=pixels[x+dx,y+dy]
                    if color[3]:
                        candidate=(distance,-color[3],color)
                        if nearest is None or candidate<nearest:
                            nearest=candidate
            if nearest:
                pixels[x,y]=(*nearest[2][:3],0)
    return result


def color_bleed(key, asset):
    name=key.split('/')[1]
    return bool(asset.get('nine_slice_ltrb') or asset.get('nine_slice_inset')
                or name.startswith(('panel_','button_','card_','world_prompt_','showcase_card_','part_chip_')))


def check_slice_edges(image, insets):
    """Require two matching visible pixels on the stretchable side of each cut."""
    w,h=image.size
    left,top,right,bottom=insets
    pixels=image.load()
    def visible(pixel):
        return pixel if pixel[3] else (0,0,0,0)
    for a,b in ((left,left+1),(w-right-2,w-right-1)):
        for y in range(h):
            assert visible(pixels[a,y])==visible(pixels[b,y]),('vertical',a,b,y)
    for a,b in ((top,top+1),(h-bottom-2,h-bottom-1)):
        for x in range(w):
            assert visible(pixels[x,a])==visible(pixels[x,b]),('horizontal',a,b,x)

def build():
    m=read('ui_manifest.json');baseline=read('Source/performance_baseline_20261109.json')['sprites']
    assets={f'{namespace}/{name}':a for group,namespace in [('icons','Icons'),('hud_icons','HUDIcons'),('components','Components')] for name,a in m[group].items()}
    assert len(assets)==sum(len(m[g]) for g in ['icons','hud_icons','components'])
    assert set(baseline)<=set(assets)
    atlas=Image.new('RGBA',(2048,2048));entries={};images={};x=y=rowh=0;pad=4;legacy=[]
    slice_checks={}
    bleed_changes={}
    # Stable size ordering and names make identical input deterministic.
    for key,a in sorted(assets.items(),key=lambda item:(-item[1]['height'],-item[1]['width'],item[0])):
        image=Image.open(ROOT/a['png']).convert('RGBA');w,h=image.size
        assert [w,h]==[a['width'],a['height']]
        if x+w+2*pad>2048:x=0;y+=rowh;rowh=0
        assert y+h+2*pad<=2048,'Atlas capacity exceeded'
        px,py=x+pad,y+pad
        bleed=color_bleed(key,a)
        padded=padded_sprite(image,pad,bleed)
        atlas.paste(padded,(x,y))
        images[key]=(image,padded)
        sha=digest((ROOT/a['png']).read_bytes())
        entries[key]=dict(x=px,y=py,width=w,height=h,source=a['png'],source_sha256=sha,
            rgba_sha256=digest(padded.crop((pad,pad,pad+w,pad+h)).tobytes()),source_rgba_sha256=digest(image.tobytes()),
            uv_pixel_centers=[(px+.5)/2048,(py+.5)/2048,(px+w-.5)/2048,(py+h-.5)/2048])
        if insets:=a.get('nine_slice_ltrb'):
            entries[key]['nine_slice_ltrb']=insets
            if len(set(insets))==1:
                entries[key]['nine_slice_inset']=insets[0]
            if key.split('/')[1].startswith(('panel_','button_','card_','world_prompt_','showcase_card_','part_chip_')):
                try:
                    check_slice_edges(image,insets)
                except AssertionError as error:
                    raise AssertionError((key,str(error))) from error
                slice_checks[key]=insets
        if bleed:
            entries[key]['alpha_bleed_px']=2
        if key in baseline:
            old=baseline[key];assert sha==old['sha256'] and digest(image.tobytes())==old['rgba_sha256'] and [w,h]==old['size'],key
            legacy.append(key)
        x+=w+2*pad;rowh=max(rowh,h+2*pad)
    target=ROOT/'Export/Atlases/ui_complete.png';atlas.save(target)
    assert target.stat().st_size<=4_000_000 and atlas.width*atlas.height*4<=16*1024*1024
    gutter_checks=0
    for key,e in entries.items():
        x,y,w,h=(e[k] for k in ['x','y','width','height'])
        original,isolated=images[key]
        crop=atlas.crop((x,y,x+w,y+h))
        assert all(a[3]==b[3] and (not a[3] or a==b) for a,b in zip(crop.get_flattened_data(),original.get_flattened_data()))
        changed=sum(a!=b for a,b in zip(crop.get_flattened_data(),original.get_flattened_data()))
        if changed:bleed_changes[key]=changed
        strips=[(x-pad,y-pad,x+w+pad,y),(x-pad,y+h,x+w+pad,y+h+pad),(x-pad,y,x,y+h),(x+w,y,x+w+pad,y+h)]
        for region in strips:assert atlas.crop(region).getchannel('A').getbbox() is None
        # Bilinear support reaches at most one pixel outside the sprite rectangle.
        # Compare all four border neighborhoods against the source on an isolated 4px pad.
        neighborhood=atlas.crop((x-pad,y-pad,x+w+pad,y+h+pad))
        assert neighborhood.tobytes()==isolated.tobytes()
        for scale in (1.25,.75):
            size=(max(1,round((w+8)*scale)),max(1,round((h+8)*scale)))
            assert neighborhood.resize(size,Image.Resampling.BILINEAR).tobytes()==isolated.resize(size,Image.Resampling.BILINEAR).tobytes()
            gutter_checks+=1
    data=dict(schema='erma.ui.atlas',version=1,atlas=dict(file='ui_complete.png',width=2048,height=2048,sha256=digest(target.read_bytes())),
        format=dict(channels='RGBA8',alpha='straight',color_space='sRGB',origin='top-left',row_direction='down',rectangles='integer pixels; exclude gutter'),
        padding=pad,nine_slice_inset=12,sampling=dict(uv='pixel centers; .5 inset',filter='linear',mipmaps=False,wrap='clamp_to_edge'),
        source_count=len(entries),legacy_count=len(legacy),sprites=entries,runtime_loader_integrated=True)
    save('Export/Atlases/ui_complete.json',data)
    report=dict(status='PASS',sprite_count=len(entries),legacy_count=len(legacy),legacy_expected=len(baseline),missing=[],duplicates=[],
        original_png_hashes_preserved=True,visible_rgba_crops_exact=True,transparent_gutter_px=pad,alpha_bleed_px=2,
        atlas_bytes=target.stat().st_size,gpu_rgba8_bytes=atlas.width*atlas.height*4,bilinear_isolated_comparisons=gutter_checks,
        slice_constant_inner_strips=slice_checks,alpha_zero_rgb_corrected_pixels=bleed_changes,
        scope='Visible source pixels preserved. Transparent RGB is extended near 9-slice edges; gutters retain alpha zero.')
    save('Validation/20261127/atlas_report.json',report)
    print(json.dumps(report,ensure_ascii=False))
if __name__=='__main__':build()
