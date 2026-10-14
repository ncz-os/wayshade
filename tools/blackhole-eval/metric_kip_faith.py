import sys,os,glob,re
import numpy as np
from PIL import Image
root=sys.argv[1]; modes=["stylized","kipthorne","faithful"]
def load(p): return np.asarray(Image.open(p).convert("RGB").resize((480,270)),dtype=np.float64)/255.
files={}
for d in glob.glob(root+"/out_*"):
    if not os.path.isdir(d): continue
    m=re.match(r".*/out_(\w+?)_(\d+)$",d); mode,seed=m.groups()
    for f in sorted(glob.glob(d+"/frame_*.png"))[1:]:
        files[(seed,os.path.basename(f)[6:14],mode)]=f
keys=sorted({(k[0],k[1]) for k in files})
rows=[]
for s,f in keys:
    if not all((s,f,m) in files for m in modes): continue
    im={m:load(files[(s,f,m)]) for m in modes}
    mask=(im["kipthorne"].mean(2)>0.3)&(im["faithful"].mean(2)>0.12)&(im["kipthorne"][...,0]>im["kipthorne"][...,2]+0.05)
    if mask.sum()<3000: continue
    ys,xs=np.nonzero(mask); cx=xs.mean()
    def chroma(a): 
        v=a[mask]; return (v/v.sum(1,keepdims=True))
    c={m:chroma(im[m]).mean(0) for m in modes}
    dkf=np.linalg.norm(c["kipthorne"]-c["faithful"])
    ppd=float(np.linalg.norm(chroma(im["kipthorne"])-chroma(im["faithful"]),axis=1).mean())
    dsk=np.linalg.norm(c["stylized"]-c["kipthorne"])
    def lr(m):
        l=im[m].mean(2)[mask]; return l[xs<cx].mean()/l[xs>=cx].mean()
    def chlr(m):
        ch=chroma(im[m]); return np.linalg.norm(ch[xs<cx].mean(0)-ch[xs>=cx].mean(0))
    def spread(m): return float(chroma(im[m]).std(0).sum())
    rows.append((s,f,mask.sum(),ppd,dsk,lr("kipthorne"),lr("faithful"),chlr("kipthorne"),chlr("faithful"),spread("kipthorne"),spread("faithful")))
    print("seed %s frame %s px=%d | per-pixel chroma dist kip-faith %.3f stylized-kip %.3f | L/R lum kip %.2f faith %.2f | L-R chroma kip %.3f faith %.3f | chroma spread kip %.3f faith %.3f"%rows[-1])
if rows:
    a=np.array([r[3] for r in rows]); print("N=%d per-pixel chroma dist kip-faith min %.3f mean %.3f"%(len(a),a.min(),a.mean()))
