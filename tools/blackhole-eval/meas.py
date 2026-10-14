import re,collections
rows=collections.defaultdict(dict)
for l in open("sweep_o6n.txt"):
    m=re.match(r"(\S+) (\S+) \[stats\].*steady n=(\d+) p50=([\d.]+) p95=([\d.]+) p99=([\d.]+)",l)
    if m:
        h,s,n,p50,p95,p99=m.groups(); rows[h][s]=(int(n)/7.0,float(p95))
allh=sorted(rows)
slow=sorted([h for h in allh if rows[h].get("1",(99,))[0]<55],key=lambda h:rows[h]["1"][0])
md=["# Render scale measurements (Mali-G720)\n",
"Host O6N (Cix Sky1, Mali-G720-Immortalis, mali_kbase; cmdline module_blacklist=panthor,...; 1920x1080 output), package 0.4.0, %d shader hacks (Black Hole, 35 hyprsaver, 36 xshadertoy), each run 12 s under `timeout` with `--render-scale=S --render-scale-mode=fixed` at S = 1 and, for hacks under about 52 fps, also 0.75, 0.5, 0.35. fps is the steady-state frame count over about 7 s; p95 is the 95th percentile frame interval in ms (`[stats]` line). Raw lines: `docs/render-scale-o6n-sweep.txt`.\n"%len(allh),
"At native 1080p, %d of %d hacks hold 55 fps or more. %d hacks are below 55 fps at scale 1; the table shows the effect of the render scale (fps, p95 ms):\n"%(len(allh)-len(slow),len(allh),len(slow)),
"| Hack | 1.0 | 0.75 | 0.5 | 0.35 |","|---|---|---|---|---|"]
for h in slow:
    r=rows[h]
    f=lambda s:("%.0f (%.0f)"%(r[s][0],r[s][1])) if s in r else "-"
    md.append(f"| {h} | {f('1')} | {f('0.75')} | {f('0.5')} | {f('0.35')} |")
md+=["","Findings:\n",
"* Black Hole runs at 59 fps with p95 17.3 ms at native 1080p (adaptive ray-step tier medium).",
"* Halving the render height (scale 0.5) brings all but alienbeacon to 45 to 60 fps; alienbeacon needs 0.35 (33 fps, p95 37 ms) and is the heaviest shader in the set.",
"* The ladder in auto mode (1, 0.75, 0.5, 0.35, one step per 3 s while the 95th percentile frame time exceeds 40 ms) therefore converges for every hack in the set. `assets/screensaver-chooser/render-hints.tsv` lists, per slow hack, the smallest reduction that reaches 45 fps, so a launcher can start there and skip the ramp.",
"* On a 4K Sky1 panel the harness already caps the render height at 1080 (Mali default), so the render cost equals the 1080p case measured here. The MS-R1 (same SoC, 4K panel) sweep could not be completed: its desktop session ended during the run and the host was at the greeter.\n",
"Image quality: scale 0.5 renders a quarter of the pixels and is upscaled with the compositor's linear filter (wp_viewport); shader hacks are smooth gradients, so the loss is softness rather than artifacts; 0.35 is visibly soft."]
open("docsout/RENDER-SCALE-MEASUREMENTS.md","w").write("\n".join(md)+"\n")
