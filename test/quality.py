# Objective quality: (1) identity SNR when no shift is applied, (2) harmonicity (HNR) of output vs input
import numpy as np, sys, subprocess
sr=48000
import os
EXTRA=os.environ.get("EXTRA","").split()
def run(inp, out, key=0, scale=0, retune=0, amount=1, tr=0):
    subprocess.run(["./harness2",inp,out,f"key={key}",f"scale={scale}",f"retune={retune}",f"amount={amount}",f"transpose={tr}"]+EXTRA,check=True,capture_output=True)
    return np.fromfile(out,np.float32).astype(float)
def hnr_frames(x, fmin=70, fmax=900, win=2048, hop=1024):
    out=[]
    for s in range(0,len(x)-win-int(sr/fmin),hop):
        a=x[s:s+win]
        if np.sqrt(np.mean(a**2))<0.01: out.append(np.nan); continue
        best=0
        for tau in range(int(sr/fmax), int(sr/fmin)):
            b=x[s+tau:s+tau+win]
            r=np.dot(a,b)/np.sqrt(np.dot(a,a)*np.dot(b,b)+1e-12)
            if r>best: best=r
        out.append(best)
    return np.array(out)
def hnr_db(r): r=np.clip(r,1e-4,0.9999); return 10*np.log10(r/(1-r))
for name in sys.argv[1:]:
    x=np.fromfile(name+'.f32',np.float32).astype(float)
    ident=run(name+'.f32','/tmp/id.f32',amount=0)
    n=min(len(x),len(ident)); snr=10*np.log10(np.sum(x[:n]**2)/np.sum((x[:n]-ident[:n])**2+1e-20))
    # the engine may shift whole notes by a fraction of a period (inaudible), so compare frame-wise at the best lag
    fs=[]; F=2400
    for st in range(F, n-2*F, F):
        a=x[st:st+F]
        if np.sqrt(np.mean(a**2))<0.02: continue
        seg=ident[st-800:st+F+800]
        c=np.correlate(seg,a,'valid'); lag=np.argmax(c)
        b=seg[lag:lag+F]; fs.append(10*np.log10(np.sum(a**2)/np.sum((a-b)**2+1e-20)))
    best=np.median(fs)
    hard=run(name+'.f32','/tmp/hard.f32',retune=0,amount=1)
    tr=run(name+'.f32','/tmp/tr.f32',amount=0,tr=3)
    hi=hnr_frames(x); ok=~np.isnan(hi)&(hi>0.5)
    res=[]
    for y in (ident,hard,tr):
        h=hnr_frames(y); res.append(np.nanmedian(hnr_db(h[ok]))-np.nanmedian(hnr_db(hi[ok])))
    print(f"{name:10s} identity SNR {snr:6.1f} dB (per-frame aligned {best:5.1f})   HNR change: no-shift {res[0]:+5.1f} dB, hard-tune {res[1]:+5.1f} dB, +3st {res[2]:+5.1f} dB   (input HNR {np.median(hnr_db(hi[ok])):.1f} dB)")
