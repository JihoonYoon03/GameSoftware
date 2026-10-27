from pathlib import Path
from PIL import Image,ImageDraw,ImageFont,ImageOps
import json,time
ROOT=Path(__file__).resolve().parents[1];folder=ROOT/'Preview/Civic'
font=lambda size:ImageFont.truetype('C:/Windows/Fonts/arial.ttf',size)
clips=sorted({p.stem.rsplit('_',1)[0] for p in folder.glob('*_00.png')})
meta=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))
lengths={c['clip']:c['duration_seconds'] for c in meta['HUMAN_7H']}
lengths.update(Before_After=1.,Before_After_Run=2/3,Bike_Contact=.5)
loops={c['clip']:c['loop'] for c in meta['HUMAN_7H']}
def save_artifact(im,path,**options):
    stage=path.with_name(path.stem+'.new'+path.suffix)
    im.save(stage,**options)
    for attempt in range(10):
        try:stage.replace(path);break
        except PermissionError:
            if attempt==9:raise
            time.sleep(.2*(attempt+1))
for clip in clips:
    images=[Image.open(p).convert('RGB') for p in sorted(folder.glob(clip+'_[0-9][0-9].png'))]
    count=len(images);w=320;h=350;columns=min(7,count);rows=(count+columns-1)//columns
    sheet=Image.new('RGB',(columns*w,rows*h+65),(19,28,37));draw=ImageDraw.Draw(sheet)
    draw.text((18,15),clip+' / actual Blender skin / 30 fps source',font=font(25),fill=(218,235,236))
    for i,im in enumerate(images):
        x=i%columns*w;y=i//columns*h+60
        sheet.paste(ImageOps.contain(im,(w,h-30)),(x,y))
        draw.text((x+12,y+h-30),f'{i/(count-1):.2f}',font=font(19),fill=(117,213,215))
    save_artifact(sheet,folder/f'contact_{clip}.jpg',quality=91)
    gif=[]
    for im in (images[:-1] if loops.get(clip,True) else images):
        frame=im.copy();ImageDraw.Draw(frame).text((15,15),clip,font=font(20),fill=(239,245,239));gif.append(frame.quantize(colors=192))
    duration=lengths.get(clip,1.4 if clip.startswith('GetUp') else .8)
    options={} if clip.startswith('Death_') else {'loop':0}
    # GIF delays use 10 ms ticks; distribute the rounding error so a 1 s walk
    # does not silently play in 0.9 s after integer-delay quantization.
    ticks=round(duration*100)
    delays=[max(10,10*(round((i+1)*ticks/(count-1))-round(i*ticks/(count-1)))) for i in range(len(gif))]
    save_artifact(gif[0],folder/f'{clip}.gif',save_all=True,append_images=gif[1:],duration=delays,disposal=2,**options)
overview=Image.new('RGB',(7*260,len(clips)*285+55),(19,28,37));draw=ImageDraw.Draw(overview)
draw.text((18,12),'CIVIC / HUMAN MOTION REVIEW',font=font(26),fill=(223,239,236))
for row,clip in enumerate(clips):
    paths=sorted(folder.glob(clip+'_[0-9][0-9].png'))
    for i in range(7):
        im=Image.open(paths[round(i*(len(paths)-1)/6)]).convert('RGB')
        overview.paste(ImageOps.pad(im,(260,260),color=(19,28,37)),(i*260,55+row*285))
    draw.text((14,55+row*285+257),clip,font=font(20),fill=(143,220,218))
save_artifact(overview,folder/'motion_overview.jpg',quality=90)

# A compact delivery sheet keeps the full review readable without scrolling
# through all 28 contact sequences. Preserve the camera image aspect ratios.
korean_font=lambda size:ImageFont.truetype('C:/Windows/Fonts/malgun.ttf',size)
delivery=Image.new('RGB',(1800,930),(19,28,37));draw=ImageDraw.Draw(delivery)
draw.text((25,12),'도시 반응 애니메이션 · 동작 개선',font=korean_font(29),fill=(223,239,236))
draw.text((27,53),f'인체 {len(meta["HUMAN_7H"])}클립 · 조준 이동 16개 · 차량 반응 21개',font=korean_font(20),fill=(143,220,218))
panels=[('Before_After_03.png','걷기 개선 전 / 후'),('Knockdown_Front_07.png','충격과 넘어짐'),
        ('GetUp_Front_06.png','지지하며 일어나기'),('Bike_Contact_03.png','바이크 탑승 접점'),
        ('body_profiles.png','남녀 · 체형 6종'),('vehicles.png','차량 5종 반응')]
for i,(name,label) in enumerate(panels):
    x=(i%3)*600;y=95+(i//3)*410
    with Image.open(folder/name) as source:
        panel=ImageOps.pad(source.convert('RGB'),(590,355),color=(19,28,37))
    delivery.paste(panel,(x+5,y))
    draw.text((x+20,y+360),label,font=korean_font(23),fill=(205,232,229))
save_artifact(delivery,folder/'delivery_overview.jpg',quality=93)
print('CIVIC_SHEETS_COMPOSED',len(clips))
