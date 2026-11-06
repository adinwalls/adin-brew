import numpy as np, sys
sr=48000
def yin_track(x, hop=480, win=2048, fmin=65, fmax=1100):
    res=[]
    for s in range(0,len(x)-win-int(sr/fmin),hop):
        fr=x[s:s+win+int(sr/fmin)]
        if np.sqrt(np.mean(fr[:win]**2))<0.01: res.append(np.nan); continue
        W=win; taus=np.arange(1,int(sr/fmin))
        d=np.array([np.sum((fr[:W]-fr[t:t+W])**2) for t in taus])
        c=d*taus/np.maximum(np.cumsum(d),1e-12)
        lo=int(sr/fmax); idx=None
        for i in range(lo,len(c)-1):
            if c[i]<0.15:
                while i+1<len(c) and c[i+1]<c[i]: i+=1
                idx=i;break
        if idx is None or idx+1>=len(c): res.append(np.nan); continue
        a,b,cc=c[idx-1],c[idx],c[idx+1]; den=a-2*b+cc
        tau=taus[idx]+ (0.5*(a-cc)/den if abs(den)>1e-12 else 0)
        res.append(sr/tau)
    return np.array(res)
def cents_off(f):
    m=69+12*np.log2(f/440); return (m-np.round(m))*100
if __name__=="__main__":
    for fn in sys.argv[1:]:
        x=np.fromfile(fn,np.float32).astype(float)
        f=yin_track(x); c=cents_off(f[~np.isnan(f)])
        print(f"{fn:22s} voiced frames {len(c):4d}  |cents| mean {np.mean(np.abs(c)):5.1f}  median {np.median(np.abs(c)):5.1f}  p90 {np.percentile(np.abs(c),90):5.1f}")
