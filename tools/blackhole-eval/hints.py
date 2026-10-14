"""Parse sweep3 output (o6sweep3.sh) into per-point tables and render-hints.tsv (capped point = real default)."""
import re, sys, collections
src = sys.argv[1]; host = sys.argv[2]
hdr = [l for l in open(src) if not l.startswith(("blackhole", "hyprsaver", "xshadertoy", "DONE"))]
rows = collections.defaultdict(lambda: collections.defaultdict(dict))   # hack -> point -> scale -> (fps,p95,size)
for l in open(src):
    m = re.match(r"(\S+) (native4k|default|capped) (\S+) (\S+) \[stats\].*steady n=(\d+) p50=([\d.]+) p95=([\d.]+)", l)
    if m:
        h, p, s, sz, n, p50, p95 = m.groups()
        rows[h][p][s] = (int(n) / 7.0, float(p95), sz)
    elif re.match(r"(\S+) (native4k|default|capped) ", l):
        h, p, s = l.split()[:3]; rows[h][p][s] = (0.0, 0.0, "?")
def short(h): return h.replace("_gles3", "")
def best(r, thr=45):
    f1 = r.get("1", (0, 0, ""))[0]
    if f1 >= thr: return None
    for s in ("0.75", "0.5", "0.35"):
        if s in r and r[s][0] >= thr: return s
    c = [(r[s][0], s) for s in ("0.75", "0.5", "0.35") if s in r]
    return max(c)[1] if c else "?"
out = []
for p, title in (("native4k", "native 3840x2160 (output pixels, scale 1)"), ("default", "2194x1234 (4K at output scale 1.75, no cap)"), ("capped", "capped to 1080 lines (Mali default, 1919x1080)")):
    hs = [h for h in sorted(rows) if p in rows[h]]
    ok = [h for h in hs if rows[h][p].get("1", (0,))[0] >= 55]
    out.append(f"\n### {title}: {len(ok)} of {len(hs)} hacks hold 55 fps or more at scale 1\n")
    out.append("| Hack | 1.0 | 0.75 | 0.5 | 0.35 |\n|---|---|---|---|---|")
    for h in hs:
        r = rows[h][p]
        if r.get("1", (0,))[0] >= 55 and h != "blackhole_gles3": continue
        cells = [f"{r[s][0]:.0f} ({r[s][1]:.0f})" if s in r else "-" for s in ("1", "0.75", "0.5", "0.35")]
        out.append(f"| {short(h)} | " + " | ".join(cells) + " |")
open(sys.argv[3], "w").write("".join(hdr) + "\n".join(out) + "\n")
tab = []
for h in sorted(rows):
    r = rows[h].get("capped", {})
    if not r: continue
    f1 = r.get("1", (0, 0, ""))[0]
    b = best(r)
    if b is None: continue
    if b == "?": continue
    note = "reaches 45 fps" if r[b][0] >= 45 else "best available, still below 45 fps"
    tab.append(f"{h}\t{b}\t{r[b][0]:.0f}\t{note} (scale 1 capped: {f1:.0f} fps)")
open(sys.argv[4], "w").write(f"""# Per-hack render scale hints for Mali-G720 (Sky1). Columns (tab separated): id, scale, measured fps at that scale, note.
# Measured on {host} (mali_kbase, ViewSonic VP2488-4K 3840x2160 at output scale 1.75, so hacks get a 2194x1234 surface), package 0.5.3 plus NCZ_TEST_SURFACE_SIZE knob.
# Operating point: the Mali default cap (1080 lines, render 1919x1080 at scale 1). A hint is the smallest reduction from the ladder 1, 0.75, 0.5, 0.35 that reaches 45 fps.
# On a 1080p panel the same scale applies to a 1080 surface (slightly smaller render than measured here; hints stay valid).
# Hacks not listed run at scale 1 (they reach 45 fps with the cap). A hint seeds NCZ_RENDER_SCALE; the adaptive mode may still step lower.
""" + "\n".join(tab) + "\n")
print(len(tab), "hints")
