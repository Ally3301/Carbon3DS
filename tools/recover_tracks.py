#!/usr/bin/env python3
"""Extract BIGF track archives without flattening colliding names; never alter dump."""
import argparse, hashlib, json, struct
from pathlib import Path, PurePosixPath

def entries(data):
    if len(data)<16 or data[:4]!=b'BIGF': raise ValueError('not BIGF')
    # These Zeebo files store total length LE and directory fields BE.
    total=struct.unpack_from('<I',data,4)[0]
    count,end=struct.unpack_from('>II',data,8)
    if total!=len(data) or not 16<=end<=len(data) or count>(end-16)//9: raise ValueError('invalid header')
    pos=16; result=[]; names=set()
    for _ in range(count):
        if pos+8>end: raise ValueError('truncated directory')
        off,size=struct.unpack_from('>II',data,pos)
        nul=data.find(b'\0',pos+8,end)
        if nul<0: raise ValueError('unterminated name')
        name=data[pos+8:nul].decode('ascii'); safe=PurePosixPath(name.replace('\\','/'))
        if safe.is_absolute() or '..' in safe.parts or ':' in name or not safe.name: raise ValueError('unsafe path')
        if str(safe) in names: raise ValueError('duplicate name')
        names.add(str(safe))
        if off<end or off>len(data) or size>len(data)-off: raise ValueError('invalid payload')
        result.append((str(safe),off,size)); pos=nul+1
    return result

def recover(source, dest, flat=None):
    report=[]
    for archive in sorted(source.glob('*.viv')):
        data=archive.read_bytes()
        for name,off,size in entries(data):
            payload=data[off:off+size]
            dst=dest/archive.stem/name
            dst.parent.mkdir(parents=True,exist_ok=True)
            dst.write_bytes(payload)
            item={'archive':archive.name,'name':name,'offset':off,'bytes':size,'sha256':hashlib.sha256(payload).hexdigest()}
            if flat:
                old=flat/name
                item['flat_status']='missing' if not old.exists() else ('identical' if old.read_bytes()==payload else 'different')
            if name.endswith('.cdl') and size>=64:
                h=struct.unpack_from('<16I',payload)
                item['cdl']={'section_id':h[0],'version':h[13],'display_lists':h[5],'special_textures':h[7],'payload_base':64}
            report.append(item)
    (dest/'inventory.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--out',type=Path,default=Path('recovered/tracks'));p.add_argument('--flat',type=Path);a=p.parse_args()
    r=recover(a.source,a.out,a.flat)
    from collections import Counter
    print('Recovered',len(r),'entries;',Counter(x.get('flat_status') for x in r))
