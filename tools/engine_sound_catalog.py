#!/usr/bin/env python3
"""Analyze Carbon engine banks without transcoding them.
Outputs CSV metrics and optional preview WAVs; source audio is never modified.
"""
from pathlib import Path
import argparse,csv,re,wave,math
import numpy as np

def readwav(p):
    with wave.open(str(p),'rb') as w:
        sr=w.getframerate(); ch=w.getnchannels(); sw=w.getsampwidth()
        if sw!=2: raise ValueError(f"{p}: expected PCM16")
        x=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(np.float32)/32768.0
    if ch>1:x=x.reshape(-1,ch).mean(1)
    return sr,x

def features(p):
    sr,x=readwav(p); x=x-x.mean(); n=min(len(x),65536)
    if n<128:return sr,len(x)/sr,0,0,0
    y=x[:n]*np.hanning(n); s=np.abs(np.fft.rfft(y))**2; f=np.fft.rfftfreq(n,1/sr)
    total=float(s.sum())+1e-20
    centroid=float((f*s).sum()/total); c=np.cumsum(s)/total
    roll=float(f[min(len(f)-1,int(np.searchsorted(c,.85)))])
    rms=float(np.sqrt(np.mean(x*x)))
    return sr,len(x)/sr,rms,centroid,roll

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('engine_dir',type=Path); ap.add_argument('--out',type=Path,default=Path('engine_bank_catalog.csv')); a=ap.parse_args()
    rx=re.compile(r'car_(\d+)_(eng|exh)_mb_ee \[(\d+)\]\.wav$',re.I); rows=[]
    for p in sorted(a.engine_dir.glob('*.wav')):
        m=rx.match(p.name)
        if not m:continue
        sr,dur,rms,cent,roll=features(p)
        rows.append([int(m[1]),m[2].lower(),int(m[3]),sr,dur,rms,cent,roll,p.name])
    with a.out.open('w',newline='') as f:
        w=csv.writer(f);w.writerow(['bank','layer','anchor','sample_rate','duration_s','rms','spectral_centroid_hz','rolloff85_hz','file']);w.writerows(rows)
    print(f'{len(rows)} samples -> {a.out}')
if __name__=='__main__':main()
