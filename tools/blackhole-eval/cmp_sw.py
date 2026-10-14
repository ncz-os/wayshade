import sys,glob
import numpy as np
from PIL import Image
root,host=sys.argv[1],sys.argv[2]
pals=["singularity","whitehole","slingshot","stylized"]
out=[]
for fb in ["orbit","slingshot"]:
    fr={p:[f for f in sorted(glob.glob(f"{root}/m_{p}_{fb}/frame_*.png"))] for p in pals}
    n=min(len(v) for v in fr.values()); idx=np.linspace(2,n-2,6).astype(int)
    # detail crops: centre 640x640 at full size, 3 timestamps
    crops=[]
    for i in idx[[1,3,5]]:
        row=[]
        for p in pals:
            im=Image.open(fr[p][i]).convert("RGB"); W,H=im.size
            row.append(im.crop((W//2-320,H//2-320,W//2+320,H//2+320)))
        crops.append(row)
    sh=Image.new("RGB",(640*len(pals),640*3))
    for r,row in enumerate(crops):
        for c,cim in enumerate(row): sh.paste(cim,(c*640,r*640))
    sh.save(f"{root}/detail_{host}_{fb}.jpg",quality=85)
    # full frames: singularity vs whitehole at 3 timestamps side by side
    W,H=768,480; full=Image.new("RGB",(W*2,H*3))
    for r,i in enumerate(idx[[1,3,5]]):
        for c,p in enumerate(["singularity","whitehole"]):
            full.paste(Image.open(fr[p][i]).convert("RGB").resize((W,H)),(c*W,r*H))
    full.save(f"{root}/full_singularity_vs_whitehole_{host}_{fb}.jpg",quality=85)
    # metrics
    out.append(f"\n{fb} ({host}):  palette: dynamic range p1..p99 luminance | luminance bands (16-bin occupied bins >0.5%) | edge detail (mean gradient) | mean chroma")
    for p in pals:
        dr=[];bands=[];ed=[];ch=[]
        for i in idx:
            v=np.asarray(Image.open(fr[p][i]).convert("RGB").resize((768,480)),dtype=float)/255.
            l=v.mean(2); dr.append(np.percentile(l,99)-np.percentile(l,1))
            hist=np.histogram(l,bins=16,range=(0,1))[0]/l.size; bands.append((hist>0.005).sum())
            gy,gx=np.gradient(l); ed.append(np.hypot(gx,gy).mean())
            m=v.max(2)>0.15
            if m.sum()>50:
                px=v[m]; c=px/px.sum(1,keepdims=True); ch.append(np.linalg.norm(c-1/3,axis=1).mean())
        out.append(f"  {p:12s} range={np.mean(dr):.3f}  bands={np.mean(bands):.1f}  edge={np.mean(ed)*100:.2f}  chroma={np.mean(ch):.3f}")
print("\n".join(out))
