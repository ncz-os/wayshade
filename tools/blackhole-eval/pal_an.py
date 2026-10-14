import sys,glob,os,itertools
import numpy as np
from PIL import Image
root=sys.argv[1]; host=sys.argv[2]
pals=["stylized","kipthorne","faithful","singularity","slingshot","whitehole","eht"]
def load(p):
    im=Image.open(p).convert("RGB").resize((384,240),Image.BOX)
    return np.asarray(im,dtype=float)/255.
out=[]
for fb in ["orbit","slingshot"]:
    fr={}
    for pal in pals:
        fs=sorted(glob.glob(f"{root}/m_{pal}_{fb}/frame_*.png"))
        ok=[]
        for f in fs:
            try: Image.open(f).load(); ok.append(f)
            except Exception: pass
        fr[pal]=ok
    n=min(len(v) for v in fr.values())
    if n<3: out.append(f"{fb}: only {n} common frames"); continue
    idx=np.linspace(1,n-1,min(6,n-1)).astype(int)
    out.append(f"\n## flyby={fb}, host={host}, frames {list(idx)} of {n}")
    chroma_lit={p:[] for p in pals}; D={ (a,b):[] for a,b in itertools.combinations(pals,2)}
    for i in idx:
        im={p:load(fr[p][i]) for p in pals}
        for p in pals:
            v=im[p]; mx=v.max(2); m=mx>0.15
            if m.sum()>50:
                px=v[m]; ch=px/px.sum(1,keepdims=True); chroma_lit[p].append(np.linalg.norm(ch-1/3,axis=1).mean())
        for a,b in D:
            m=(im[a].max(2)>0.15)&(im[b].max(2)>0.15)
            if m.sum()<200: continue
            ca=im[a][m]; cb=im[b][m]
            ca=ca/ca.sum(1,keepdims=True); cb=cb/cb.sum(1,keepdims=True)
            D[(a,b)].append(np.linalg.norm(ca-cb,axis=1).mean())
    out.append("mean chroma of lit pixels (distance from neutral): "+", ".join(f"{p}={np.mean(v):.3f}" for p,v in chroma_lit.items() if v))
    out.append("pairwise per-pixel chroma distance (mean over timestamps / min over timestamps):")
    hdr="| |"+"|".join(pals)+"|"; out.append(hdr); out.append("|"+"---|"*(len(pals)+1))
    for a in pals:
        row=[]
        for b in pals:
            if a==b: row.append("-")
            else:
                k=(a,b) if (a,b) in D else (b,a)
                v=D[k]; row.append(f"{np.mean(v):.3f}/{np.min(v):.3f}" if v else "n/a")
        out.append(f"|{a}|"+"|".join(row)+"|")
    # contact sheet
    W,H=320,200; sh=Image.new("RGB",(W*len(idx),H*len(pals)))
    for r,p in enumerate(pals):
        for c,i in enumerate(idx):
            sh.paste(Image.open(fr[p][i]).convert("RGB").resize((W,H)),(c*W,r*H))
    sh.save(f"{root}/sheet_{host}_{fb}.jpg",quality=82)
print("\n".join(out))
