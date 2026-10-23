from pathlib import Path
from PIL import Image,ImageDraw,ImageFont,ImageOps
import json
ROOT=Path(__file__).resolve().parents[1];folder=ROOT/'Preview/Civic'
font=lambda size:ImageFont.truetype('C:/Windows/Fonts/arial.ttf',size)
clips=sorted({p.stem.rsplit('_',1)[0] for p in folder.glob('*_00.png')})
meta=json.loads((ROOT/'Documentation/animation_clips.json').read_text(encoding='utf-8'))
lengths={c['clip']:c['duration_seconds'] for c in meta['HUMAN_7H']}
for clip in clips:
    images=[Image.open(p).convert('RGB') for p in sorted(folder.glob(clip+'_[0-9][0-9].png'))]
    count=len(images);w=320;h=350;columns=min(7,count);rows=(count+columns-1)//columns
    sheet=Image.new('RGB',(columns*w,rows*h+65),(19,28,37));draw=ImageDraw.Draw(sheet)
    draw.text((18,15),clip+' / actual Blender skin / 30 fps source',font=font(25),fill=(218,235,236))
    for i,im in enumerate(images):
        x=i%columns*w;y=i//columns*h+60
        sheet.paste(ImageOps.contain(im,(w,h-30)),(x,y))
        draw.text((x+12,y+h-30),f'{i/(count-1):.2f}',font=font(19),fill=(117,213,215))
    sheet.save(folder/f'contact_{clip}.jpg',quality=91)
    gif=[]
    for im in images[:-1]:
        frame=im.copy();ImageDraw.Draw(frame).text((15,15),clip,font=font(20),fill=(239,245,239));gif.append(frame.quantize(colors=192))
    duration=lengths.get(clip,1.4 if clip.startswith('GetUp') else .8)
    gif[0].save(folder/f'{clip}.gif',save_all=True,append_images=gif[1:],duration=round(duration*1000/(count-1)),loop=0,disposal=2)
overview=Image.new('RGB',(7*260,len(clips)*285+55),(19,28,37));draw=ImageDraw.Draw(overview)
draw.text((18,12),'CIVIC / HUMAN MOTION REVIEW',font=font(26),fill=(223,239,236))
for row,clip in enumerate(clips):
    paths=sorted(folder.glob(clip+'_[0-9][0-9].png'))
    for i in range(7):
        im=Image.open(paths[round(i*(len(paths)-1)/6)]).convert('RGB')
        overview.paste(im.resize((260,260)),(i*260,55+row*285))
    draw.text((14,55+row*285+257),clip,font=font(20),fill=(143,220,218))
overview.save(folder/'motion_overview.jpg',quality=90)
print('CIVIC_SHEETS_COMPOSED',len(clips))
