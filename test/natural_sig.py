# test signals with known "intended" note centres, vibrato, scoops and slides
import numpy as np
from gen import voice, midi2hz
sr=48000
def seq(parts):
    """parts: list of (midi_note, dur, centre_cents_offset, vib_depth_cents(+-), scoop_cents, glide_from_prev)"""
    f=[]; env=[]; centre=[]; vibc=[]
    for (m,d,off,vd,scoop,gap) in parts:
        N=int(d*sr); t=np.arange(N)/sr
        vib=vd*np.sin(2*np.pi*5.5*t)*np.clip((t-0.15)/0.15,0,1)
        sc=scoop*np.exp(-t/0.06)
        cents=off+vib+sc
        f.append(midi2hz(m)*2**(cents/1200)); centre.append(np.full(N,m+off/100)); vibc.append(vib)
        e=np.clip(t/0.02,0,1)*np.clip((d-t)/0.03,0,1) if gap else np.ones(N)
        env.append(e)
        if gap:
            g=int(gap*sr); f.append(np.full(g,midi2hz(m))); env.append(np.zeros(g)); centre.append(np.full(g,np.nan)); vibc.append(np.zeros(g))
    f=np.concatenate(f); env=np.concatenate(env)
    return voice(f)*env, np.concatenate(centre), np.concatenate(vibc)
