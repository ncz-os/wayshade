import re,sys,collections
rows=collections.defaultdict(dict)
hdr=[]
for l in open(sys.argv[1]):
    if not l.startswith(("blackhole","hyprsaver","xshadertoy")):
        hdr.append(l.strip()); continue
    m=re.match(r"(\S+) (\S+) \[stats\].*steady n=(\d+) p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+)",l)
    if not m: continue
    h,s,n,p50,p95,p99,mx=m.groups()
    rows[h][s]=(int(n)/7.0,float(p50),float(p95),float(p99))  # steady window ~7 s
print("\n".join(hdr))
slow=[h for h in rows if rows[h].get("1",(99,))[0]<55]
print("hacks:",len(rows),"| below ~55 fps at scale 1:",len(slow))
print("hack | fps@1.0 (p95 ms) | @0.75 | @0.5 | @0.35")
for h in sorted(slow,key=lambda h:rows[h]["1"][0]):
    r=rows[h]
    def f(s): 
        v=r.get(s); return "%.0f (%.0f)"%(v[0],v[2]) if v else "-"
    print(h,"|",f("1"),"|",f("0.75"),"|",f("0.5"),"|",f("0.35"))
ok=[rows[h]["1"][3] for h in rows if "1" in rows[h]]
print("p99 max over all hacks at scale 1: %.1f ms; hacks with p99>25ms: %s"%(max(ok),[h for h in rows if rows[h].get('1',(0,0,0,0))[3]>25]))
