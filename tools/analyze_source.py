#!/usr/bin/env python3
"""Index decompiled functions/calls/string references, without executing input."""
import hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=ROOT/'ref/jogo.c'; text=p.read_text();raw=p.read_bytes()
pattern=re.compile(r'(?m)^[^\n;{}]*\b(FUN_[0-9a-f]{8})\([^;{]*?\)\s*\n\{')
starts=list(pattern.finditer(text)); functions=[]
for i,m in enumerate(starts):
 end=starts[i+1].start() if i+1<len(starts) else len(text)
 body=text[m.start():end]
 functions.append({'name':m[1],'line':text.count('\n',0,m.start())+1,'lines':body.count('\n'),
 'calls':sorted(set(re.findall(r'\b(FUN_[0-9a-f]{8})\s*\(',body))-{m[1]}),
 'strings':sorted(set(re.findall(r'\bs_[A-Za-z0-9_]+',body))),
 'warning_count':body.count('// WARNING:')})
summary={'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'lines':text.count('\n'),
 'function_count':len(functions),'warning_count':text.count('// WARNING:'),
 'bad_instruction_count':text.count('Bad instruction'),
 'global_symbols':len(set(re.findall(r'\bDAT_[0-9a-f]{8}\b',text))),
 'functions':functions}
(ROOT/'docs/source_index.json').write_text(json.dumps(summary,indent=2)+'\n')
print({k:v for k,v in summary.items() if k!='functions'})
