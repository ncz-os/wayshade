import sys,glob,os
import numpy as np
from PIL import Image
root=sys.argv[1]
def m(im):
    mx=im.max(2); hh,ww=mx.shape
    t=mx[:hh//8*8,:ww//8*8].reshape(hh//8,8,ww//8,8).max((1,3))
    return (t>16).mean(),(mx>16).mean()
for h in sorted(os.listdir(root)):
    res=[]
    for f in sorted(glob.glob(f"{root}/{h}/frame_*.png"))[1:]:
        try: img=Image.open(f).convert("RGB"); img.load()
        except Exception: continue
        w,hh=img.size
        s=img.resize((w//8,hh//8),Image.BOX)   # grim -s 0.125 area average
        t8,p8=m(np.asarray(s))
        tn,pn=m(np.asarray(img))
        res.append((t8,p8,tn,pn))
    a=np.array(res)
    print("%-20s gate-style (1/8 downscale): tiles %.2f (min %.2f max %.2f) lit-px %.3f | native: tiles %.2f lit-px %.3f"%(h,a[:,0].mean(),a[:,0].min(),a[:,0].max(),a[:,1].mean(),a[:,2].mean(),a[:,3].mean()))
