import numpy as np, subprocess, sys
from gen import voice
sr=48000
MAJ=[0,2,4,5,7,9,11]; MIN=[0,2,3,5,7,8,10]
W=[.22,.12,.17,.10,.20,.11,.08]
names="C C# D D# E F F# G G# A A# B".split()
def melody(tonic, minor, detune, seconds=20, seed=0, pent=False):
    rng=np.random.default_rng(seed)
    sc=MIN if minor else MAJ
    degs=list(range(7)); w=np.array(W)
    if pent:  # major pentatonic: degrees 1 2 3 5 6 only
        degs=[0,1,2,4,5]; w=np.array([.25,.15,.2,.25,.15])
    w=w/w.sum(); base=57+((tonic-57)%12)
    if base>62: base-=12
    f0=[]; t=0; prev=0
    while t<seconds:
        L=rng.integers(4,7)
        for i in range(L):
            if i==L-1: d=0 if rng.random()<0.65 else (4 if not pent else 3)
            else:
                # prefer steps: pick weighted near previous degree
                cand=np.array(degs); pw=w*np.exp(-np.abs(cand-prev)/1.5); pw/=pw.sum(); d=rng.choice(cand,p=pw)
            semis=sc[d] if not pent else MAJ[d]
            if minor and d==6 and rng.random()<0.4: semis=11   # harmonic-minor leading tone
            oct=12 if (d<2 and rng.random()<0.3) else 0
            dur=rng.uniform(0.2,0.5) if i<L-1 else rng.uniform(0.6,1.0)
            N=int(dur*sr); tt=np.arange(N)/sr
            cents=detune+rng.normal(0,12)+ (20*np.sin(2*np.pi*5.5*tt)*(tt>0.3))
            f0.append(440*2**((base+semis+oct-69+cents/100)/12)*np.ones(N)); prev=d; t+=dur
        f0.append(np.full(int(0.25*sr),-1.0)); t+=0.25
    f=np.concatenate(f0); env=(f>0).astype(float); f[f<0]=200
    env=np.convolve(env,np.ones(480)/480,'same')
    return voice(f,seed=seed)*env
def detect(x):
    x.astype(np.float32).tofile('/tmp/k.f32')
    out=subprocess.run(['./harness2','/tmp/k.f32','/tmp/ko.f32','learn=1','mode=1'],check=True,capture_output=True,text=True).stdout
    d=dict(kv.split('=') for kv in out.split())
    return d
if __name__ == '__main__':
    ok=rel=wrong=0; offs=[]; confs=[]; fails=[]
    rng=np.random.default_rng(42)
    for k in range(24):
        tonic=k%12; minor=k>=12; det=float(rng.uniform(-40,40))
        d=detect(melody(tonic,minor,det,seed=k))
        want=names[tonic]+("m" if minor else ""); relk=names[(tonic+3)%12] if minor else names[(tonic+9)%12]+"m"
        got=d['key']; confs.append(float(d['conf'])); offs.append(float(d['offset'])-det)
        if got==want: ok+=1
        elif got==relk: rel+=1; fails.append(f"{want}->{got}(relative)")
        else: wrong+=1; fails.append(f"{want}->{got}")
    print(f"24 keys, random tuning offsets ±40c, 20 s melodies: exact {ok}/24, relative major/minor {rel}/24, wrong {wrong}/24")
    print(f"tuning offset error: mean {np.mean(np.abs(offs)):.1f} c, max {np.max(np.abs(offs)):.1f} c; confidence median {np.median(confs):.2f}")
    if fails: print("misses:", ", ".join(fails))
    # ambiguous case: major pentatonic melody
    cs=[]
    for s in range(4):
        d=detect(melody(7,False,0,seed=100+s,pent=True)); cs.append((d['key'],d['conf'],d['alt']))
    print("G major pentatonic melodies (ambiguous):", cs)
    # short input
    d=detect(melody(2,False,10,seconds=4,seed=7)); print("4-second D major snippet:", d['key'], "conf", d['conf'], "secs", d['secs'])
