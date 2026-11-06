import numpy as np, subprocess, sys
sys.path.insert(0,'.')
from gen import voice, midi2hz
sr=48000
def note_seq(notes, cents, dur=1.0, vib=0, scoop=True, seed=0):
    out=[]
    for i,(m,c) in enumerate(zip(notes,cents)):
        N=int(dur*sr); t=np.arange(N)/sr
        cc=c+vib*np.sin(2*np.pi*5.5*t)+(-80*np.exp(-t/0.04) if scoop else 0)
        env=np.clip(t/0.02,0,1)*np.clip((dur-t)/0.03,0,1)
        out.append(voice(midi2hz(m)*2**(cc/1200),seed=i)*env); out.append(np.zeros(int(.1*sr)))
    return np.concatenate(out)
notes=[45,48,50,52,55,57,60,64]
off=[35,-40,25,-30,45,-20,30,-45]
x=note_seq(notes,off); ideal=note_seq(notes,[0]*8,scoop=False)
x.astype(np.float32).tofile('lsd_in.f32')
def spec(y):
    F=4096; w=np.hanning(F); S=[]
    for s in range(0,len(y)-F,2048):
        fr=y[s:s+F]
        if np.sqrt(np.mean(fr**2))<0.02: S.append(None); continue
        S.append(20*np.log10(np.abs(np.fft.rfft(fr*w))[:int(8000/sr*F)]+1e-6))
    return S
def lsd(a,b):
    A=spec(a); B=spec(b); d=[]
    for p,q in zip(A,B):
        if p is None or q is None: continue
        # only the steady part of notes is meaningful
        d.append(np.sqrt(np.mean((p-q)**2)))
    return np.median(d)
for exe in sys.argv[1:]:
    def run(retune,amount,tr):
        subprocess.run([exe,'lsd_in.f32','/tmp/o.f32',str(sr),'0','0',str(retune),str(amount),str(tr)],check=True,capture_output=True)
        return np.fromfile('/tmp/o.f32',np.float32).astype(float)
    y=run(0,1,0)
    print(f"{exe:16s} hard-tune vs perfect-pitch reference: LSD {lsd(y,ideal):5.2f} dB   (untuned input vs reference: {lsd(x,ideal):5.2f} dB)")
