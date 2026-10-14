import sys,os,glob
from PIL import Image
root,seeds,out=sys.argv[1],sys.argv[2].split(","),sys.argv[3]
W,H=640,360
sh=Image.new("RGB",(W*3,H*len(seeds)))
for j,s in enumerate(seeds):
    for i,m in enumerate(["stylized","kipthorne","faithful"]):
        fs=sorted(glob.glob(f"{root}/out_{m}_{s}/frame_*.png"))
        sh.paste(Image.open(fs[min(2,len(fs)-1)]).convert("RGB").resize((W,H)),(i*W,j*H))
sh.save(out)
