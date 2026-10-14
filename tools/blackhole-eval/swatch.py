from PIL import Image,ImageDraw
def h(c): return tuple(int(c[i:i+2],16) for i in (1,3,5))
rows=[
 ("singularity (design)",["#000000","#0a0a0a","#242424","#989898","#dcdcdc","#ffffff"],"sampled from the Singularity desktop default wallpaper: background #000000, mark #ffffff, neutral greys"),
 ("slingshot (design)",["#5d2014","#b04a2a","#ffb347","#ffe08a","#fff3c8","#cfe3ff"],"sampled from the supplied Strange New Worlds frame: rust disk, gold arc, blue-white limb"),
 ("slingshot (reference k-means)",["#311310","#4d2621","#73403a","#ae766a","#f6e6dc","#1a1c27"],"disk region and sky of the reference image (color reference only)"),
 ("whitehole (design)",["#10334a","#3a6d8f","#4fd0c8","#7fe6dc","#cfe8ff","#ffffff"],"deep teal-navy, steel blue, teal/seafoam, ice, white core"),
 ("whitehole (reference k-means)",["#030e1b","#0e2338","#30546d","#6491aa","#92bacc","#d3edf1"],"sky and ring band of the reference image (color reference only)"),
 ("kipthorne",["#8a5a10","#c48a22","#e0a83a","#f0c060","#ffe0a0","#fff2d8"],"amber blackbody 1900 K to 4300 K, symmetric"),
]
W,C,H=200,100,64
im=Image.new("RGB",(W+C*6+20,H*len(rows)),(24,24,24)); d=ImageDraw.Draw(im)
for r,(n,cs,note) in enumerate(rows):
    d.text((6,r*H+8),n,fill=(230,230,230)); d.text((6,r*H+26),note[:30],fill=(150,150,150))
    for i,c in enumerate(cs):
        x=W+i*C; d.rectangle([x,r*H+4,x+C-4,r*H+H-24],fill=h(c)); d.text((x+2,r*H+H-20),c,fill=(220,220,220))
im.save("palette-swatches.png")
