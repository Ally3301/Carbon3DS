#!/usr/bin/env python3
"""CPU layout preview; header coordinates match the runtime's bottom-up projection.
Does not emulate PICA filtering. Saves a full screen and enlarged comparison.
"""
import argparse, math, re
from pathlib import Path
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
CUTS=ROOT/'assets/normalized/ui/hud_original/cuts'
L={k:float(v) for k,v in re.findall(r'#define (HUD_\w+) ([\d.]+)f',(ROOT/'include/hud_layout.h').read_text())}
def load(n): return Image.open(CUTS/('tach_'+n+'.png')).convert('RGBA')
def draw(dst,im,x,y,w=None,h=None,tint=(255,255,255,255)):
    im=im.copy()
    if w is not None: im=im.resize((max(1,round(w)),max(1,round(h))),Image.Resampling.BILINEAR)
    im=Image.merge('RGBA',tuple(c.point(lambda a,t=t:a*t//255) for c,t in zip(im.split(),tint)))
    dst.alpha_composite(im,(round(x),round(y)))
def render(speed,gear,rpm,nitro):
    out=Image.new('RGBA',(400,240),(17,25,32,255)); x,y=L['HUD_X'],L['HUD_Y']
    draw(out,load('face'),x,y,tint=(216,255,255,255))
    if rpm>6900: draw(out,load('redline'),x,y,tint=(208,255,255,255))
    a=L['HUD_NEEDLE_START']-max(0,min(1,(rpm-900)/6900))*L['HUD_NEEDLE_SWEEP']
    needle=load('needle'); rot=needle.rotate(math.degrees(a),resample=Image.Resampling.BICUBIC,expand=True)
    draw(out,rot,x+L['HUD_PIVOT_X']+21*math.cos(a)-rot.width/2,y+L['HUD_PIVOT_Y']-21*math.sin(a)-rot.height/2)
    fill=max(0,min(1,nitro))
    if fill>0:
        bar=load('nitro'); clip=bar.crop((0,0,max(1,round(60*fill)),33))
        draw(out,clip,x+L['HUD_BAR_X'],y+L['HUD_BAR_Y'],L['HUD_BAR_W']*fill,L['HUD_BAR_H'])
        draw(out,load('nitro_icon'),x+L['HUD_ICON_X'],y+L['HUD_ICON_Y'])
    nums=load('speed_digits')
    for i,ch in enumerate(f'{speed%1000:03d}'):
        n=int(ch);glyph=nums.crop((round(n*12.2),0,round((n+1)*12.2),15))
        draw(out,glyph,x+31+i*9.5,y+96-41-12,9.5,12)
    if 1<=gear<=6:
        g=load('gear_digits');cell=93/8
        draw(out,g.crop((round((gear-1)*cell),0,round(gear*cell),15)),x+39,y+96-71-12,12,12)
    return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--speed',type=int,default=94);p.add_argument('--gear',type=int,default=3);p.add_argument('--rpm',type=float,default=4200);p.add_argument('--nitro',type=float,default=1);a=p.parse_args()
    out=render(a.speed,a.gear,a.rpm,a.nitro); out.save(ROOT/'build/hud_preview.png')
    panels=Image.new('RGBA',(86*4*4,408),(17,25,32,255));d=ImageDraw.Draw(panels)
    face=Image.new('RGBA',(86,96),(17,25,32,255));face.alpha_composite(load('face'))
    panels.alpha_composite(face.resize((344,384)),(0,24));d.text((4,4),'Face original / trilho escuro',fill='white')
    for i,n in enumerate((1,.5,0),1):
        im=render(a.speed,a.gear,a.rpm,n).crop((296,130,382,226)).resize((344,384))
        panels.alpha_composite(im,(i*344,24));d.text((i*344+4,4),f'Nitro {n:.0%}',fill='white')
    panels.save(ROOT/'build/hud_alignment.png')
    print(ROOT/'build/hud_alignment.png')
if __name__=='__main__':main()
