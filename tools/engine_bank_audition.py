#!/usr/bin/env python3
import re,wave,subprocess,tempfile,shutil
from pathlib import Path
import tkinter as tk
from tkinter import ttk,filedialog,messagebox
import numpy as np
from scipy.signal import resample_poly

SR=32000; IDLE=900.; RED=8000.
PAT=re.compile(r'car_(\d+)_(eng|exh)_mb_ee \[(\d+)\]\.wav$',re.I)

def scan(root):
    c={}
    for p in Path(root).rglob('*.wav'):
        m=PAT.match(p.name)
        if m:c.setdefault(int(m[1]),{'eng':{},'exh':{}})[m[2].lower()][int(m[3])]=p
    return c

def load(p):
    with wave.open(str(p),'rb') as w:
        ch,sw,sr,n=w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes()
        a=np.frombuffer(w.readframes(n),'<i2').astype(np.float32)/32768
    if ch>1:a=a.reshape(-1,ch).mean(1)
    if sr!=SR:
        import math
        g=math.gcd(sr,SR); a=resample_poly(a,SR//g,sr//g).astype(np.float32)
    a-=a.mean()
    peak=max(1e-6,float(np.max(np.abs(a))))
    return a*min(1,.9/peak)

def loop(a,n,speed,phase):
    x=(phase+np.arange(n,dtype=np.float64)*speed)%len(a)
    i=x.astype(np.int64); f=(x-i).astype(np.float32)
    return a[i]*(1-f)+a[(i+1)%len(a)]*f, float((phase+n*speed)%len(a))

def layer(files,rpm,n,cache,phases):
    ids=sorted(files)
    if not ids:return np.zeros(n,np.float32)
    A=np.linspace(IDLE,RED,len(ids)); j=np.searchsorted(A,rpm)
    lo=max(0,min(len(ids)-1,j-1)); hi=max(0,min(len(ids)-1,j))
    t=0 if hi==lo else float(np.clip((rpm-A[lo])/(A[hi]-A[lo]),0,1))
    def one(k):
        p=files[ids[k]]
        if p not in cache:cache[p]=load(p)
        speed=float(np.clip(rpm/max(A[k],1),.82,1.22))
        phase=phases.get(p,0.0)
        y,newphase=loop(cache[p],n,speed,phase)
        phases[p]=newphase
        return y
    x=one(lo)
    if hi!=lo:
        # Equal-power blend, but both source loops keep their own continuous phase.
        x=x*np.cos(t*np.pi/2)+one(hi)*np.sin(t*np.pi/2)
    return x

def block(car,rpm,thr,n,cache,phases):
    e=layer(car['eng'],rpm,n,cache,phases)
    x=layer(car['exh'],rpm,n,cache,phases)
    y=e*(.78-.18*thr)+x*(.22+.58*thr)
    return np.tanh(y*(1.05+.45*thr)).astype(np.float32)

def edge_fade(y,ms=18):
    # Only at the beginning/end of an entire user preview, never per synthesis block.
    f=min(len(y)//8,int(ms*.001*SR))
    if f:
        y=y.copy()
        y[:f]*=np.linspace(0,1,f,dtype=np.float32)
        y[-f:]*=np.linspace(1,0,f,dtype=np.float32)
    return y

def steady(car,rpm,thr,sec,cache):
    phases={}
    return edge_fade(block(car,rpm,thr,int(sec*SR),cache,phases))

def sweep(car,thr,sec,cache):
    B=1024; N=int(sec*SR); out=[]; phases={}
    # V1 restarted every WAV and applied a fade every ~32 ms. That produced the
    # "transcending/echo" pumping. V2 keeps source phases continuous.
    for s in range(0,N,B):
        n=min(B,N-s); q=(s+n*.5)/N
        rpm=IDLE+(RED-IDLE)*q**.78
        out.append(block(car,rpm,thr,n,cache,phases))
    return edge_fade(np.concatenate(out))

def savewav(p,a):
    with wave.open(str(p),'wb') as w:
        w.setnchannels(1);w.setsampwidth(2);w.setframerate(SR)
        w.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())

class App:
    def __init__(self,r):
        self.r=r;r.title('Carbon Engine Bank Audition');self.cars={};self.cache={};self.proc=None
        self.cid=tk.StringVar();self.rpm=tk.DoubleVar(value=2500);self.thr=tk.DoubleVar(value=.65)
        self.st=tk.StringVar(value='Selecione a pasta extraida dos WAVs.')
        f=ttk.Frame(r,padding=12);f.grid(sticky='nsew')
        ttk.Label(f,text='Carbon Engine Bank Audition',font=('',15,'bold')).grid(row=0,column=0,columnspan=3,sticky='w')
        ttk.Button(f,text='Abrir pasta dos WAVs',command=self.choose).grid(row=1,column=0,pady=8)
        self.cb=ttk.Combobox(f,textvariable=self.cid,state='readonly');self.cb.grid(row=1,column=1)
        ttk.Label(f,text='RPM').grid(row=2,column=0)
        ttk.Scale(f,from_=IDLE,to=RED,variable=self.rpm,length=430).grid(row=2,column=1,columnspan=2)
        ttk.Label(f,textvariable=self.rpm).grid(row=3,column=1,sticky='w')
        ttk.Label(f,text='Carga / throttle').grid(row=4,column=0)
        ttk.Scale(f,from_=0,to=1,variable=self.thr,length=430).grid(row=4,column=1,columnspan=2)
        ttk.Button(f,text='Ouvir RPM atual (4s)',command=self.now).grid(row=5,column=0,pady=10)
        ttk.Button(f,text='Sweep 900 -> 8000 RPM',command=self.sw).grid(row=5,column=1)
        ttk.Button(f,text='Parar',command=self.stop).grid(row=5,column=2)
        ttk.Label(f,textvariable=self.st,wraplength=650).grid(row=6,column=0,columnspan=3,sticky='w')
    def choose(self):
        d=filedialog.askdirectory()
        if not d:return
        self.cars=scan(d);self.cache.clear();ids=sorted(self.cars)
        self.cb['values']=[f'{i:02d}' for i in ids]
        if ids:self.cid.set(f'{ids[0]:02d}')
        self.st.set(f'{len(ids)} bancos encontrados. V2: fase contínua, sem fade por bloco.')
    def car(self):return self.cars[int(self.cid.get())]
    def play(self,a,msg):
        self.stop();p=Path(tempfile.gettempdir())/'carbon_engine_lab_v2.wav';savewav(p,a)
        ff=shutil.which('ffplay')
        if not ff:raise RuntimeError('ffplay nao encontrado; instale ffmpeg')
        self.proc=subprocess.Popen([ff,'-nodisp','-autoexit','-loglevel','quiet',str(p)]);self.st.set(msg)
    def now(self):
        try:self.play(steady(self.car(),self.rpm.get(),self.thr.get(),4,self.cache),f'Car {self.cid.get()} @ {self.rpm.get():.0f} RPM')
        except Exception as e:messagebox.showerror('Erro',str(e))
    def sw(self):
        try:self.play(sweep(self.car(),self.thr.get(),10,self.cache),f'Car {self.cid.get()} sweep contínuo')
        except Exception as e:messagebox.showerror('Erro',str(e))
    def stop(self):
        if self.proc and self.proc.poll() is None:self.proc.terminate()
        self.proc=None

if __name__=='__main__':
    r=tk.Tk();App(r);r.mainloop()
