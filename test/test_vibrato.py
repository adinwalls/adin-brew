import numpy as np, subprocess
from natural_sig import seq, sr
from measure import yin_track
parts=[(57,1.3,35,35,0,0.12),(60,1.3,-40,30,0,0.12),(62,1.3,25,40,0,0.12),(64,1.3,-30,25,0,0.12),(65,1.3,40,35,0,0.12)]
x,cen,vib=seq(parts); x.astype(np.float32).tofile('vib.f32')
def analyse(y):
    f=yin_track(y,hop=240); m=69+12*np.log2(f/440)
    res=[]; pos=0
    for (n,d,off,vd,sc,gap) in parts:
        a=int((pos+0.45)*sr/240); b=int((pos+d-0.1)*sr/240); seg=m[a:b]; seg=seg[~np.isnan(seg)]
        k=int(0.18*sr/240); ma=np.convolve(seg,np.ones(k)/k,'same')[k:-k]; r=(seg[k:-k]-ma)
        res.append(((np.mean(ma)-n)*100, np.std(r)*np.sqrt(2)*100))
        pos+=d+gap
    return np.array(res)
def run(*args):
    subprocess.run(['./harness2','vib.f32','/tmp/v.f32',*args],check=True,capture_output=True)
    return np.fromfile('/tmp/v.f32',np.float32).astype(float)
print(f"{'setting':34s} {'centre error (c)':>18s} {'vibrato depth (c)':>18s}")
for name,y in [("input",x),
               ("classic, retune 0",run('mode=0','retune=0','scale=1')),
               ("natural, retune 0, vib 100%",run('mode=1','retune=0','scale=1','flex=0','humanize=0')),
               ("natural, retune 0, vib 50%",run('mode=1','retune=0','scale=1','flex=0','humanize=0','vibrato=0.5')),
               ("natural, retune 0, vib 200%",run('mode=1','retune=0','scale=1','flex=0','humanize=0','vibrato=2')),
               ("natural, retune 0, vib 0%",run('mode=1','retune=0','scale=1','flex=0','humanize=0','vibrato=0')),
               ("natural defaults (20ms,flex30,hum30)",run('mode=1','retune=20','scale=1'))]:
    r=analyse(y); print(f"{name:34s} {np.mean(abs(r[:,0])):12.1f}       {np.mean(r[:,1]):12.1f}")
