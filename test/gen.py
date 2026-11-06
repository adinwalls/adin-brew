import numpy as np, sys
sr=48000
def voice(f0_curve, sr=sr, vowel=(700,1220,2600), noise=0.003, seed=0):
    n=len(f0_curve); phase=np.cumsum(2*np.pi*f0_curve/sr)
    out=np.zeros(n); F=vowel; B=(80,90,120)
    kmax=int(sr/2/f0_curve.min())
    for k in range(1,kmax+1):
        fk=k*f0_curve; mask=fk<sr/2*0.95
        if not mask.any(): break
        amp=np.zeros(n)
        for Fi,Bi in zip(F,B): amp+= 1/np.sqrt(1+((fk-Fi)/Bi)**2)
        amp*= k**-1.0
        out+=np.where(mask,amp*np.sin(k*phase),0)
    out/=np.abs(out).max()
    out+=noise*np.random.default_rng(seed).standard_normal(n)
    return out*0.5
def midi2hz(m): return 440*2**((m-69)/12)
def melody(notes, dur=0.6, detune_cents=None, vib=30, gap=0.08):
    rng=np.random.default_rng(1); segs=[]
    for i,m in enumerate(notes):
        N=int(dur*sr); t=np.arange(N)/sr
        dc = detune_cents[i] if detune_cents is not None else rng.uniform(-40,40)
        cents = dc + vib*np.sin(2*np.pi*5.5*t)*np.clip(t/0.2,0,1)
        cents += -80*np.exp(-t/0.04)    # scoop into note
        f=midi2hz(m)*2**(cents/1200)
        env=np.clip(t/0.02,0,1)*np.clip((dur-t)/0.03,0,1)
        segs.append(voice(f)*env); segs.append(np.zeros(int(gap*sr)))
    return np.concatenate(segs)
if __name__=="__main__":
    np.random.seed(0)
    # C major melody, male range and female range
    x=melody([48,50,52,53,55,57,59,60], vib=25)
    x.astype(np.float32).tofile("male.f32")
    y=melody([67,69,71,72,74,72,71,69], vib=25)
    y.astype(np.float32).tofile("female.f32")
    z=melody([57,57,59,60], dur=1.2, detune_cents=[35,-45,30,-30], vib=0)   # steady detuned, no vibrato
    z.astype(np.float32).tofile("steady.f32")
