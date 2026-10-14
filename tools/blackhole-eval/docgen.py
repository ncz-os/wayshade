import csv,sys,pathlib
from PIL import Image,ImageDraw
tsv=pathlib.Path(sys.argv[1]).read_text().splitlines()
rows=[l.split("\t") for l in tsv if l.strip() and not l.startswith("#")]
groups={}
for r in rows: groups.setdefault(r[8],[]).append(r)
o=["# Black Hole options","",
"`blackhole_gles3` reads the same options from three places. Precedence, highest first:","",
"1. command line `--option=value` (booleans also `--lensing` and `--no-lensing`)",
"2. environment `NCZ_BLACKHOLE_<OPTION>` (upper case, `-` written as `_`)",
"3. key file `$XDG_CONFIG_HOME/ncz-screensavers/blackhole.conf` (`~/.config/...` when unset), group `[blackhole]`, one `option=value` per line; `NCZ_BLACKHOLE_CONFIG` overrides the path",
"4. built-in defaults","",
"An unknown option or an invalid value (wrong type, outside its range, not one of the choices) is reported once on stderr and ignored: the next lower source applies. `--help` and `--list-options` print everything with type, range and default. The declarative schema for settings UIs is `assets/screensaver-chooser/options/blackhole.tsv` (installed to `/usr/share/ncz-screensavers/options/`); a build test keeps it identical to what `blackhole_gles3 --dump-schema` prints.","",
"Legacy names still work: `NCZ_BLACKHOLE_COLORS` sets `palette` (`stylised` is accepted), `NCZ_BLACKHOLE_FIXED_SEED` sets `seed`.","",
"Examples:","","```","blackhole_gles3 --palette=slingshot --flyby=slingshot --spin=0.95",
"blackhole_gles3 --palette=whitehole --cycle-palettes=slingshot,singularity --cycle-interval=60 --palette-transition=10",
"NCZ_BLACKHOLE_FLYBY=random NCZ_BLACKHOLE_DURATION=45 blackhole_gles3",
"printf '[blackhole]\\npalette=singularity\\nbloom=0.5\\n' > ~/.config/ncz-screensavers/blackhole.conf","```",""]
for g,rs in groups.items():
    o+=[f"## {g}","","| Option | Type | Default | Range / choices | Description |","|---|---|---|---|---|"]
    for r in rs:
        name,typ,default,lo,hi,choices,label,desc,_,env=r
        rng=choices.replace(",",", ") if choices else (f"{lo} to {hi}" if lo!="" else "")
        o.append(f"| `--{name}` | {typ} | `{default or '(empty)'}` | {rng} | {desc} |")
    o.append("")
print("\n".join(o))
