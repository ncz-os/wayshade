#!/bin/bash
# mksheets.sh : contact sheets legacy/classic/enhanced (3 timestamps each) per hack
F=/System/Library/Fonts/Helvetica.ttc
frames=(0000003c 000000f0 000001e0)
row() { # label dir out : three frames at ~20%, 55%, 90% of what was captured
  local lab=$1 dir=$2 out=$3 t=() n i
  local all=($(ls $dir/frame_*.png))
  n=${#all[@]}
  for p in 20 55 90; do i=$(( n * p / 100 )); [ $i -ge $n ] && i=$((n-1)); t+=("${all[$i]}"); done
  magick "${t[@]}" -resize 640x400! +append \( -size 150x400 xc:'#111' -font $F -fill white -pointsize 22 -gravity center -annotate 0 "$lab" \) +swap +append $out
}
for h in voronoi gravitywell hexstrut cubestorm crackberg cityflow noof geodesic gibson; do
  row "legacy GL1" ref/$h/run1 sheets/_r1.png
  row "classic" shots/$h-classic sheets/_r2.png
  row "enhanced" shots/$h-enhanced sheets/_r3.png
  magick sheets/_r1.png sheets/_r2.png sheets/_r3.png -append -quality 84 sheets/$h.jpg
done
for h in hypertorus klein projectiveplane; do
  row "legacy (GLSL)" ref2/$h/run1 sheets/_r1.png
  row "this build" shots/$h-upstream sheets/_r2.png
  magick sheets/_r1.png sheets/_r2.png -append -quality 84 sheets/$h.jpg
done
rm -f sheets/_r*.png; ls -la sheets
