"""Compose Blender render QA sheets and GIF previews (no asset edits)."""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFont, ImageOps
ROOT=Path(__file__).resolve().parents[1];P=ROOT/'Preview'
FONT_PATH='C:/Windows/Fonts/arial.ttf'
def font(size):return ImageFont.truetype(FONT_PATH,size)
durations={'Walk':1000,'Run':667,'Turn_Left_90':800,'Attack_Melee':800,'Attack_Fire':400,'Jump_Full':1200}
for clip,duration in durations.items():
    frames=[Image.open(P/f'animation_{clip}_{i:02}.png').convert('RGB') for i in range(8)]
    sheet=Image.new('RGB',(1680,1100),(19,29,35));d=ImageDraw.Draw(sheet)
    d.text((24,15),f'{clip} / HUMAN + 4 / 6 / 2 LEGS / 30 fps source',font=font(25),fill=(214,233,231))
    for i,im in enumerate(frames):
        thumb=ImageOps.contain(im,(420,490),Image.Resampling.LANCZOS)
        x=(i%4)*420;y=70+(i//4)*510
        sheet.paste(thumb,(x,y+(450-thumb.height)//2));d.text((x+15,y+445),f'phase {i/7:.2f}',font=font(21),fill=(138,214,221))
    sheet.save(P/f'contact_{clip}.jpg',quality=91)
    gif=[]
    for i,im in enumerate(frames[:-1]):
        im=im.copy();d=ImageDraw.Draw(im);d.text((18,15),clip,font=font(22),fill=(222,244,240));gif.append(im.quantize(colors=192))
    gif[0].save(P/f'{clip}.gif',save_all=True,append_images=gif[1:],duration=round(duration/7),loop=0,disposal=2)

access=[Image.open(P/f'vehicle_access_{i:02}.png').convert('RGB') for i in range(14)]
access[0].save(P/'Vehicle_Access.gif',save_all=True,append_images=access[1:],duration=150,loop=0,disposal=2)
canvas=Image.new('RGB',(1600,2540),(19,29,35));d=ImageDraw.Draw(canvas)
d.text((30,15),'ERMA / HALO  |  MODULAR LOW-POLY RESOURCE LIBRARY',font=font(30),fill=(225,235,231))
panels=[('06_body_profiles.png','6 body profiles / sex x mass / shared skeleton'),('07_outfit_purposes.png','9 purpose-built outfit forms / 34 separate accessories'),('09_vehicles.png','5 modular hover vehicle types / 122 parts'),('11_vehicle_exploded.png','Within-type part replacement / exploded assemblies'),('01_characters.png','10 character combinations / 23-bone shared skeleton'),('02_creatures.png','4 / 6 / 2 legs / modular heads and limbs'),('03_halo_settlement.png','5 modular SF building assemblies'),('04_interior.png','Habitat interior / furniture and equipment'),('05_environment.png','Terrain / roads / alien nature'),(None,'8 seamless PBR materials / BaseColor + Normal + ORM')]
for i,(name,label) in enumerate(panels):
    x=(i%2)*800;y=70+(i//2)*490
    if name:
        image=Image.open(P/name).convert('RGB');thumb=ImageOps.contain(image,(785,435),Image.Resampling.LANCZOS)
        canvas.paste(thumb,(x+(800-thumb.width)//2,y+(435-thumb.height)//2))
    else:
        tiles=sorted((ROOT/'Export/Textures').glob('*BaseColor.png'))
        for j,path in enumerate(tiles):
            tile=Image.open(path).convert('RGB').resize((162,145),Image.Resampling.LANCZOS)
            tx=x+22+(j%4)*193;ty=y+30+(j//4)*190
            canvas.paste(tile,(tx,ty));d.text((tx,ty+150),path.stem.replace('_BaseColor','').replace('Terrain_','').replace('Habitat_',''),font=font(15),fill=(206,225,220))
    d.text((x+18,y+445),label,font=font(20),fill=(142,213,219))
canvas.save(P/'00_asset_overview.jpg',quality=92)
print('Preview sheets and GIFs created')
