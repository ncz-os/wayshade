import numpy as np,sys
from PIL import Image
def km(px,k,it=25,seed=1):
    rng=np.random.default_rng(seed); c=px[rng.choice(len(px),k,replace=False)].astype(float)
    for _ in range(it):
        d=((px[:,None,:]-c[None])**2).sum(2); a=d.argmin(1)
        for i in range(k):
            if (a==i).any(): c[i]=px[a==i].mean(0)
    return c,np.bincount(a,minlength=k)/len(px)
def hexc(c): return "#%02x%02x%02x"%tuple(int(round(x)) for x in c)
def report(name,path,crop=None,k=8):
    im=Image.open(path).convert("RGB")
    if crop: im=im.crop(crop)
    im=im.resize((320,int(320*im.size[1]/im.size[0])))
    px=np.asarray(im).reshape(-1,3).astype(float)
    c,f=km(px,k); o=np.argsort(-f)
    print(name,[ (hexc(c[i]),round(f[i],3)) for i in o])
    lum=px.mean(1)
    for q in (50,90,99,99.9): 
        m=px[lum>=np.percentile(lum,q)]; print("  top%.1f%% mean"%(100-q),hexc(m.mean(0)))
report("singularity default","ref/default.png",k=5)
im=np.asarray(Image.open("ref/default.png").convert("RGB")); print(" default: min",im.reshape(-1,3).min(0),"pixel(0,0)",im[5,5],"max",im.reshape(-1,3).max(0), "white logo p99.99", np.percentile(im.reshape(-1,3),99.99,axis=0))
lum=im.mean(2); w=im[lum>240]; print(" logo white mean",hexc(w.mean(0)),len(w)); b=im[lum<4]; print(" black mean",hexc(b.mean(0)),len(b)); g=im[(lum>20)&(lum<60)]; print(" grey gradient mean",hexc(g.mean(0)))
report("sling full","ref/sling.jpg",crop=(0,300,3840,1860),k=10)
report("sling sky (top-left)","ref/sling.jpg",crop=(0,300,1200,1100),k=4)
report("sling disk (bottom-right)","ref/sling.jpg",crop=(1800,1200,3840,1860),k=5)
report("whitehole full","ref/whitehole.png",k=10)
report("whitehole sky (corner)","ref/whitehole.png",crop=(0,0,400,250),k=3)
report("whitehole ring band","ref/whitehole.png",crop=(700,0,1250,800),k=8)
