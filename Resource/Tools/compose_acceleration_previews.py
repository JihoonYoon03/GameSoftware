"""Compose contact sheets and time-correct GIFs from the actual Blender render frames."""
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
folder=ROOT/'Preview/Acceleration'
specs=json.loads((ROOT/'Validation/acceleration_authoring.json').read_text())['clips']
font=ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf',18)
big=ImageFont.truetype(r'C:\Windows\Fonts\malgun.ttf',25)
kfont=ImageFont.truetype(r'C:\Windows\Fonts\malgun.ttf',18)
for name,info in specs.items():
    frames=[Image.open(folder/f'{name}_{i:02}.png').convert('RGB') for i in range(9)]
    sheet=Image.new('RGB',(800,454),(23,32,37));d=ImageDraw.Draw(sheet)
    d.text((12,8),f'{name}   {info["duration_seconds"]:.2f}s',font=font,fill='white')
    for i in range(8):
        tile=frames[i if i<7 else 8].resize((200,200),Image.Resampling.LANCZOS)
        sheet.paste(tile,((i%4)*200,34+(i//4)*210))
        d.text(((i%4)*200+7,34+(i//4)*210+180),f'{(i if i<7 else 8)/8:.3f}',font=font,fill=(240,240,240))
    sheet.save(folder/(name+'_contact.png'))
    duration=max(20,round(info['duration_seconds']/8*1000))
    if info['loop']:
        gifframes=frames[:-1];durations=[duration]*8
    else:
        gifframes=frames;durations=[duration]*9;durations[-1]=450
    gifframes[0].save(folder/(name+'.gif'),save_all=True,append_images=gifframes[1:],duration=durations,loop=0,disposal=2)
chosen=['Walk_Start_L','Run_Start_L','Run_Stop_L','Run_To_Sprint_L','Sprint','Sprint_Stop_R']
labels=['걷기 출발','달리기 출발','달리기 제동','달리기 → 스프린트','스프린트 반복','스프린트 제동']
sheet=Image.new('RGB',(1200,800),(23,32,37));d=ImageDraw.Draw(sheet)
d.text((22,14),'가감속 모션 · 기존 63개 보존 + 새 19개 = 총 82개',font=big,fill=(239,245,245))
for row,(name,label) in enumerate(zip(chosen,labels)):
    y=65+row*120;d.text((12,y+42),label,font=kfont,fill=(220,235,237))
    for j,i in enumerate([0,2,4,6,8]):
        tile=Image.open(folder/f'{name}_{i:02}.png').convert('RGB').resize((170,115),Image.Resampling.LANCZOS)
        sheet.paste(tile,(320+j*172,y))
sheet.save(folder/'delivery_overview.jpg',quality=94)
print('ACCELERATION_GIFS_AND_SHEETS',len(specs))
