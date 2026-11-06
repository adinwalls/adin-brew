import numpy as np, subprocess
from gen import voice, midi2hz
from measure import yin_track
sr=48000
segs=[]; env=[]
# fast run of short notes (150 ms, each 35c off), then a 2 s sustained note with a slow 30c drift
for m,o in [(60,35),(62,-35),(64,30),(65,-30),(67,35),(65,-35)]:
    segs.append(np.full(int(.15*sr),m+o/100))
t=np.arange(int(2*sr))/sr; segs.append(64+0.15*np.sin(2*np.pi*0.6*t))   # slow +-15c drift (not vibrato)
m=np.concatenate(segs); x=voice(midi2hz(m)); x.astype(np.float32).tofile('hum.f32')
def track(y): f=yin_track(y,hop=240); return 69+12*np.log2(f/440)
mi=track(x); L=len(mi)
runEnd=int(0.9*sr/240)
def run(*a):
    subprocess.run(['./harness2','hum.f32','/tmp/h.f32',*a],check=True,capture_output=True)
    return track(np.fromfile('/tmp/h.f32',np.float32).astype(float))
print(f"{'setting':32s} {'short-note error':>17s} {'sustain drift kept':>19s}")
for name,a in [("natural retune 15, humanize 0",['retune=15','humanize=0']),("natural retune 15, humanize 30",['retune=15','humanize=0.3']),("natural retune 15, humanize 100",['retune=15','humanize=1'])]:
    mo=run('mode=1','flex=0','transition=0',*a)
    n=min(len(mo),L); s=[]
    for i in range(6):
        a0=int((i*0.15+0.08)*sr/240); b0=int((i*0.15+0.14)*sr/240); seg=mo[a0:b0]; s.append(np.nanmedian(abs(seg-np.round(seg)))*100)
    sus=slice(int(1.4*sr/240),n-10); din=np.nanstd(mi[sus]); dout=np.nanstd(mo[sus])
    print(f"{name:32s} {np.mean(s):13.1f} c   {100*dout/din:14.0f} %")
