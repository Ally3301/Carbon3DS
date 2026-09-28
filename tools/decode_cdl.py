#!/usr/bin/env python3
"""Decode Zeebo CDL static scenery from the original EAGL command stream.
Evidence: nfs.mod ARM 0x1578c4..0x157ed8 (the text decompile misses UXTH),
FUN_001519fc reference rebasing, FUN_000ac580 texture territories.
No PSP emulator or executable code from the dump is run.
"""
import argparse, collections, hashlib, json, math, struct
from pathlib import Path
from inspect_cdl import inspect

U32=struct.Struct('<I'); VERTEX=struct.Struct('<2h4B4h')
IDENTITY=(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)
class DecodeError(ValueError): pass

def word(data, offset):
    if offset<0 or offset+4>len(data): raise DecodeError(f'word out of range at {offset:#x}')
    return U32.unpack_from(data,offset)[0]

def triangles(first, end, parity=0):
    # O stream EAGL do Carbon contém draws de 2 vértices em algumas seções.
    # Eles não formam triângulo e devem ser tratados como no-op, não como erro.
    if first < 0 or end < first or end > 65536 or parity not in (0, 1):
        raise DecodeError(f'invalid strip first={first} end={end} parity={parity}')
    if end - first < 3:
        return
    for i in range(first, end - 2):
        yield (i+1, i, i+2) if (i-first+parity) & 1 else (i, i+1, i+2)

class Section:
    def __init__(self,path):
        self.path=Path(path);self.data=self.path.read_bytes();self.structure=inspect(self.data)
        self.h=struct.unpack_from('<16I',self.data)
        self.exports=self.references(self.h[1],self.h[2])
        self.imports=self.references(self.h[3],self.h[4])
    def references(self,count,offset):
        base=64+offset
        return [(word(self.data,base+i*4),*struct.unpack_from('<3I',self.data,base+count*4+i*12)) for i in range(count)]
    def texture_table(self,kind):
        candidates=[(a,b) for typ,a,b,_ in self.imports if typ==kind]
        if len(candidates)!=1: raise DecodeError(f'{self.path.name}: no texture table type {kind}')
        a,end=candidates[0];n=word(self.data,64+a)
        if 4+n*12>end-a: raise DecodeError('texture table out of range')
        return [word(self.data,64+a+4+i*12+4) for i in range(n)]

class Library:
    def __init__(self,root):self.root=Path(root);self.cache={}
    def section(self,name):
        if name not in self.cache:self.cache[name]=Section(self.root/(name+'.cdl'))
        return self.cache[name]
    def material(self,section,ptr):
        for typ,a,b,_ in section.imports:
            if a<=ptr<b:
                offset=ptr-a
                if offset<4 or (offset-4)%12: raise DecodeError('texture pointer not at entry start')
                index=(offset-4)//12
                if typ==0: source=section;tablekind=0
                elif typ==1: source=self.section(section.path.stem[0]+'0');tablekind=4 if source.path.stem=='Z0' else 1
                elif typ==2: source=self.section('X0');tablekind=1
                elif typ==4: source=self.section('Z0');tablekind=4
                elif typ==3:return {'id':'runtime_special','kind':3,'index':index,'msh':None,'offset':None}
                else:raise DecodeError('unknown texture reference type')
                table=source.texture_table(tablekind)
                if index>=len(table):raise DecodeError('texture index exceeds shared table')
                return {'id':source.path.stem+'_'+str(index),'kind':typ,'index':index,'msh':str(source.path.with_suffix('.msh')),'offset':table[index]}
        raise DecodeError(f'unresolved texture pointer {ptr:#x}')


def decode(section,library):
    data=section.data;vertices=[];faces=[];batches=[];materials={};lookup={}
    stats=collections.Counter();max_error=0.;skipped=[]
    for record in section.structure['display_lists']:
        typ=record['type']
        if typ not in (0,1,4):skipped.append({'type':typ,'id':record['id'],'reason':'dynamic/local instance or shared list'});continue
        content=record['content_offset'];lh=struct.unpack_from('<16I',data,content)
        start=64+lh[4];end=start+lh[2]
        if not content<=start<end<=record['next_offset']:raise DecodeError('command range invalid')
        pc=start;matrix=None;vbase=None;base=0;texupper=0;material=None;textured=True;cull=False
        parent_end=0;first=None;ambient=(1.,1.,1.);terminated=False
        def emit(a,z,parity,box=None):
            nonlocal max_error
            if vbase is None or matrix is None:raise DecodeError('draw lacks vertex base/matrix')
            if not any(vbase>=64+x and vbase+z*16<=64+y for _,x,y,_ in section.exports):raise DecodeError('vertices outside declared reference range')
            key=material['id'] if textured and material else 'untextured'
            if textured and material is None:raise DecodeError('textured draw lacks material')
            if key not in materials:materials[key]=material if key!='untextured' else {'id':key,'msh':None,'offset':None}
            converted={}
            for i in range(a,z):
                packed=VERTEX.unpack_from(data,vbase+i*16)
                u,v,*rest=packed;rgba=rest[:4];q=rest[4:]
                if q[3]==0:raise DecodeError('zero homogeneous W')
                local=[x/(4*q[3]) for x in q[:3]]
                pos=tuple(sum(matrix[k*4+t]*local[k] for k in range(3))+matrix[12+t] for t in range(3))
                if not all(math.isfinite(x) and abs(x)<1e7 for x in pos):raise DecodeError('invalid transformed position')
                if box:
                    error=max(abs(pos[t]-box[t])-box[t+3] for t in range(3));max_error=max(max_error,error)
                    stats['bbox_vertex_checks']+=1
                    if error>0.05: stats['bbox_failures']+=1
                # Raw UV remains exact; normalization is a separate unresolved layer.
                color=tuple(round(rgba[k]*ambient[k]) for k in range(3))+(rgba[3],)
                vk=(vbase,i,matrix,ambient)
                if vk not in lookup:
                    lookup[vk]=len(vertices);vertices.append((*pos,u,v,*color))
                converted[i]=lookup[vk]
            begin=len(faces)
            for tri in triangles(a,z,parity):
                ids=tuple(converted[i] for i in tri)
                p0,p1,p2=(vertices[i][:3] for i in ids)
                edge1=[p1[k]-p0[k] for k in range(3)];edge2=[p2[k]-p0[k] for k in range(3)]
                cross=[edge1[1]*edge2[2]-edge1[2]*edge2[1],edge1[2]*edge2[0]-edge1[0]*edge2[2],edge1[0]*edge2[1]-edge1[1]*edge2[0]]
                if sum(x*x for x in cross)<1e-14:stats['degenerate_triangles']+=1;continue
                faces.append(ids)
            if len(faces)>begin:
                if batches and batches[-1]['material']==key and batches[-1]['type']==typ and batches[-1]['cull']==cull and batches[-1]['start']+batches[-1]['count']==begin:
                    batches[-1]['count']+=len(faces)-begin
                else:batches.append({'start':begin,'count':len(faces)-begin,'material':key,'type':typ,'cull':cull})
            stats['strips']+=1
        while pc+4<=end:
            cmd=word(data,pc);op=cmd>>24;arg=cmd&0xffffff
            stats[f'op_{op:02x}']+=1
            if op==0x0b:terminated=True;break
            if op==0x10:base=(arg&0xff0000)<<8
            elif op==0x01:
                vbase=64+(base|arg)
                if not any(64+a<=vbase<64+b for _,a,b,_ in section.exports):raise DecodeError('unresolved vertex reference')
            elif op==0x2a:
                if pc+68>end:raise DecodeError('truncated matrix')
                matrix=struct.unpack_from('<16f',data,pc+4)
                if not all(math.isfinite(v) for v in matrix):raise DecodeError('non-finite matrix')
                if any(abs(matrix[i])>1e-6 for i in (3,7,11)) or abs(matrix[15]-1)>1e-6:raise DecodeError('non-affine matrix')
                pc+=68;continue
            elif op==0xa8:texupper=(arg>>16)<<24
            elif op==0xa0:material=library.material(section,texupper|arg)
            elif op==0x1e:textured=bool(arg)
            elif op==0x1d:cull=bool(arg)
            elif op==0x5c:ambient=tuple(((arg>>(i*8))&255)/255 for i in range(3))
            elif op==0xfb:first=arg
            elif op==0xfc:
                if first is None:raise DecodeError('count without first vertex')
                parent_end=first+arg
                if pc+4>=end:raise DecodeError('truncated draw')
                if word(data,pc+4)>>24!=0xfe:emit(first,parent_end,0)
            elif op==0xfd:
                if pc+28>end:raise DecodeError('truncated culling block')
                target=pc+(arg&0xfffff)
                if not pc+28<=target<=end:raise DecodeError('invalid culling branch')
                pc+=28;continue
            elif op==0xfe:
                if pc+32>end:raise DecodeError('truncated strip bounds')
                count=arg&65535;pair=word(data,pc+4);a,z=pair>>16,pair&65535
                box=struct.unpack_from('<6f',data,pc+8)
                if not all(math.isfinite(x) for x in box) or any(x<0 for x in box[3:]):raise DecodeError('invalid bounds')
                if count==1:
                    if first is None or a<first or z>parent_end:raise DecodeError('strip outside draw batch')
                    emit(a,z,(arg>>16)&15,box)
                else:stats['hierarchy_nodes']+=1
                pc+=32;continue
            else:raise DecodeError(f'unsupported opcode {op:#x} at {pc:#x}')
            pc+=4
        if not terminated:raise DecodeError('missing return')
    if stats['bbox_failures']:raise DecodeError(f"{stats['bbox_failures']} vertices exceed stored bounds; max error {max_error}")
    bounds=[[min(v[k] for v in vertices) for k in range(3)],[max(v[k] for v in vertices) for k in range(3)]] if vertices else None
    return {'vertices':vertices,'faces':faces,'batches':batches,'materials':materials,'bounds':bounds,
            'stats':dict(stats),'bbox_max_error':max_error,'excluded_lists':skipped,'source_sha256':hashlib.sha256(data).hexdigest()}

def write_obj(mesh,path):
    with path.open('w') as out:
        out.write('# Original CDL coordinates: Z up. UVs intentionally omitted (raw stored in metadata/native file).\n')
        for x,y,z,u,v,r,g,b,a in mesh['vertices']:out.write(f'v {x:.7g} {y:.7g} {z:.7g} {r/255:.5f} {g/255:.5f} {b/255:.5f}\n')
        for n,batch in enumerate(mesh['batches']):
            out.write(f"g pass{batch['type']}_{batch['material']}_{n}\n")
            for a,b,c in mesh['faces'][batch['start']:batch['start']+batch['count']]:out.write(f'f {a+1} {b+1} {c+1}\n')

def write_native(mesh,path):
    if len(mesh['vertices'])>65535:raise DecodeError('native section exceeds 16-bit indices')
    bounds=mesh['bounds'];origin=[(bounds[0][i]+bounds[1][i])/2 for i in range(3)]
    verts=[]
    for x,y,z,u,v,r,g,b,a in mesh['vertices']:verts.append((x-origin[0],z-origin[2],-(y-origin[1]),float(u),float(v),r,g,b,a))
    box=[min(v[i] for v in verts) for i in range(3)]+[max(v[i] for v in verts) for i in range(3)]
    with path.open('wb') as out:
        out.write(struct.pack('<4s5I6f',b'N3T1',1,len(verts),len(mesh['faces'])*3,len(mesh['batches']),24,*box))
        for v in verts:out.write(struct.pack('<5f4B',*v))
        for face in mesh['faces']:out.write(struct.pack('<3H',*face))
        materials=list(mesh['materials'])
        for group in mesh['batches']:out.write(struct.pack('<4I',group['start']*3,group['count']*3,materials.index(group['material']),group['type']|(int(group['cull'])<<8)))
    return origin

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,default=Path('recovered/tracks/opwd_3000'));ap.add_argument('--out',type=Path,default=Path('recovered/geometry/opwd_3000'));ap.add_argument('--section',action='append');ap.add_argument('--obj',action='store_true');args=ap.parse_args()
    library=Library(args.root);args.out.mkdir(parents=True,exist_ok=True);report=[]
    files=[args.root/(s+'.cdl') for s in args.section] if args.section else sorted(args.root.glob('*.cdl'))
    for path in files:
        try:
            mesh=decode(library.section(path.stem),library)
            record={k:v for k,v in mesh.items() if k not in ('vertices','faces')}
            record.update(section=path.stem,vertices=len(mesh['vertices']),triangles=len(mesh['faces']))
            if mesh['vertices']:
                record['origin']=write_native(mesh,args.out/(path.stem+'.n3t'))
                if args.obj:write_obj(mesh,args.out/(path.stem+'.obj'))
                (args.out/(path.stem+'.json')).write_text(json.dumps(record,indent=2)+'\n')
            report.append(record)
        except (ValueError,IndexError,struct.error,FileNotFoundError) as e:report.append({'section':path.stem,'error':str(e)});print(path.stem,'ERROR',str(e),flush=True)
    (args.out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Sections',len(report),'errors',sum('error' in r for r in report),'triangles',sum(r.get('triangles',0) for r in report),'bbox checks',sum(r.get('stats',{}).get('bbox_vertex_checks',0) for r in report),flush=True)
if __name__=='__main__':main()
