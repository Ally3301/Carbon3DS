#!/usr/bin/env python3
"""Build fixed, 3DS-friendly cyan numeral atlas for the race HUD."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/generated/hud_digits_ai.png'
OUT.parent.mkdir(parents=True,exist_ok=True)
cell=64; atlas=Image.new('RGBA',(256,256),(0,0,0,0))
# A condensed bold monospace base keeps every digit centered in its cell.
font_path='/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
font=ImageFont.truetype(font_path,48)
for index,ch in enumerate('0123456789R'):
    x=(index%4)*cell; y=(index//4)*cell
    layer=Image.new('RGBA',(cell,cell),(0,0,0,0)); d=ImageDraw.Draw(layer)
    box=d.textbbox((0,0),ch,font=font); w=box[2]-box[0]; h=box[3]-box[1]
    px=(cell-w)//2; py=(cell-h)//2-box[1]
    # Low-cost glow layer plus crisp cyan-blue pixel face.
    d.text((px,py),ch,font=font,fill=(20,180,210,170),stroke_width=1,stroke_fill=(0,45,64,200))
    glow=layer.filter(ImageFilter.GaussianBlur(1.2)); atlas.alpha_composite(glow,(x,y))
    atlas.alpha_composite(layer,(x,y))
atlas.save(OUT)
print(OUT)
