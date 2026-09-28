#!/usr/bin/env python3
"""Structural CDL inventory based on FUN_000ac580 / FUN_000a5028.
This does not pretend to decode EAGL display commands into triangles.
"""
import json,struct
from pathlib import Path

def inspect(data):
    if len(data)<64: raise ValueError('short CDL')
    h=struct.unpack_from('<16I',data)
    if h[13]!=1: raise ValueError('unknown version')
    base=64
    for count,off in [(h[1],h[2]),(h[3],h[4])]:
        if base+off+count*16>len(data): raise ValueError('reference table outside CDL')
    if base+h[8]+h[7]*128>len(data): raise ValueError('special texture table outside CDL')
    pos=base+h[6]; lists=[];seen=set()
    for i in range(h[5]):
        if pos in seen or pos+56>len(data): raise ValueError('invalid list chain')
        seen.add(pos);d=struct.unpack_from('<14I',data,pos)
        relocation,content,next_=base+d[11],base+d[12],base+d[13]
        if not pos+56<=relocation<=content<=next_<=len(data): raise ValueError('bad list offsets')
        lists.append({'type':d[0],'id':d[1],'header_offset':pos,'relocation_offset':relocation,
                      'content_offset':content,'next_offset':next_,'declared_work_bytes':d[10],
                      'content_bytes':next_-content})
        pos=next_
    return {'section_id':h[0],'version':h[13],'imports':h[3],'exports':h[1],
            'special_textures':h[7],'display_lists':lists}
if __name__=='__main__':
    result=[]
    for p in sorted(Path('recovered/tracks').rglob('*.cdl')):
        try: item=inspect(p.read_bytes())
        except ValueError as e: item={'error':str(e)}
        result.append({'path':str(p),**item})
    Path('docs/cdl_index.json').write_text(json.dumps(result,indent=2)+'\n')
    print('CDL:',len(result),'validated:',sum('error' not in x for x in result))
    print('Failures:',[(x['path'],x['error']) for x in result if 'error' in x][:10])
