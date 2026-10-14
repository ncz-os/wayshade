import re,sys
def parse(path,host):
    rows=[]
    for l in open(path):
        m=re.match(r'^(\S+) (perf-\w+) rc=\d+ .*steady n=(\d+) p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+)',l)
        if not m: continue
        h,tag,n,p50,p95,p99,mx=m.groups()
        var={'perf-legacy':'legacy','perf-classic':'classic','perf-enh':'enhanced','perf-upstream':'upstream'}[tag]
        hid=h.replace('_legacy','')
        fps=1000/float(p50) if float(p50)>0 else 0
        rows.append((host,hid,var,fps,float(p50),float(p95),float(p99)))
    return rows
if __name__=='__main__':
    for a in sys.argv[1:]:
        host,path=a.split('=')
        for r in parse(path,host): print("%s\t%s\t%s\t%.1f\t%.2f\t%.2f\t%.2f"%r)
