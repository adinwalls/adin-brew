import numpy as np, subprocess
from gen import voice, midi2hz
from measure import yin_track
sr=48000
# phrase: held note (+30c) -> 400 ms slide up 4 semitones -> held note (-30c); then 3 notes with -150c scoops
segs=[]; lab=[]
def hold(m,off,d): segs.append(np.full(int(d*sr),m+off/100)); lab.append(np.full(int(d*sr),0))
def slide(a,b,d): segs.append(np.linspace(a,b,int(d*sr))); lab.append(np.full(int(d*sr),1))
hold(57,30,0.8); slide(57.3,61-0.3,0.4); hold(61,-30,0.8)
for m in [64,62,60]:
    N=int(0.7*sr); t=np.arange(N)/sr; segs.append(m-1.5*np.exp(-t/0.05)+0.25); l=np.zeros(N); l[:int(0.12*sr)]=2; lab.append(l)
m=np.concatenate(segs); lab=np.concatenate(lab)
x=voice(midi2hz(m)); x.astype(np.float32).tofile('flex.f32')
def track(y): f=yin_track(y,hop=240); return 69+12*np.log2(f/440)
mi=track(x); L=len(mi); li=lab[::240][:L]; tm=m[::240][:L]
def run(*a):
    subprocess.run(['./harness2','flex.f32','/tmp/f.f32',*a],check=True,capture_output=True)
    return track(np.fromfile('/tmp/f.f32',np.float32).astype(float))
print(f"{'setting':40s} {'slide kept':>11s} {'scoops kept':>12s} {'held-note error':>16s}")
for name,args in [("classic, retune 0",['mode=0','retune=0']),
                  ("natural retune 0, flex 0, transition 0",['mode=1','retune=0','flex=0','transition=0','humanize=0']),
                  ("natural retune 0, flex 30, transition 0",['mode=1','retune=0','flex=0.3','transition=0','humanize=0']),
                  ("natural retune 0, flex 0, transition 40",['mode=1','retune=0','flex=0','transition=40','humanize=0']),
                  ("natural retune 0, flex 30, transition 40",['mode=1','retune=0','flex=0.3','transition=40','humanize=0']),
                  ("natural retune 20, flex 30, trans 40, hum 30",['mode=1','retune=20'])]:
    mo=run(*args); n=min(len(mo),L); d=(mo[:n]-mi[:n])*100; ok=~np.isnan(d)
    sl=ok&(li[:n]==1); sc=ok&(li[:n]==2)
    # held: last 60% of each held note, compare to nearest semitone
    held=ok&(li[:n]==0)
    herr=np.nanmedian(abs((mo[:n][held]-np.round(mo[:n][held]))*100))
    print(f"{name:40s} {np.sqrt(np.mean(d[sl]**2)):8.1f} c {np.sqrt(np.mean(d[sc]**2)):9.1f} c {herr:13.1f} c")
print("(slide/scoop columns: RMS deviation from the sung pitch, lower = gesture preserved)")
