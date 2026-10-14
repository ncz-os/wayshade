# magmasimplex palette and footage sources (research note, not packaged)

Provenance for docs/MAGMASIMPLEX-60S.md and src/magmasimplex_palettes.c. Names
of makers, products, sites and video channels are kept here only (repository
rule: no manufacturer, brand or product references in code, options, scenes or
shipped docs). Nothing from these sources is copied into the repository except
color values sampled from them (facts) and statistics measured from footage.

| id prefix in the saver | source |
|---|---|
| code-NN | USA Lava Lite color codes 1992-2004, Lava Library reference chart, https://lava-library.com/color-code-reference/ (sampled programmatically; chart analysis only) |
| era-60s-*, era-70s-*, era-80s-* | Lava Library era pages https://lava-library.com/home/1960s/ and /home/1970s/, Crestworth page https://lava-library.com/crestworth-1963-1979/ (Port = ruby in rose, Starboard = topaz in blue-green), Oozing Goo timelines (1960s-1990s), Enchantress (1968) mists, Glitter Lite (1969); research summary on build-host references-data/pre1990-lava-summary.md |
| footage-* | "5-hour Real Lava Lamp (three lamps)", Al Sweigart, YouTube EIrtkZBfsHA (real time); "Time-Lapse of a Lava Lamp Starting + Highlights", Rainerius, YouTube ixUPSK5H0vM (~100x); "Lavalamp Time Lapse 4k", 12voltvids, YouTube Q_7gE0uSs3U. Operator-supplied, analysis only, copies on build-host references-video/ |

Lines of the design doc that carried these names before the rename:

       the original colourway table, add real lamp color codes (Lava Library
    - "Lavalamp Time Lapse 4k", 12voltvids, YouTube `Q_7gE0uSs3U`, 309 s, analyzed
    - "5-hour Real Lava Lamp (three lamps)", Al Sweigart, YouTube `EIrtkZBfsHA`,
    - "Time-Lapse of a Lava Lamp Starting + Highlights", Rainerius, YouTube
    | warm-up to steady state | about 2 h for a real lamp; wax still heating at 1 h | Rainerius description (published by the video's author), ~100x time-lapse | `startup=true`: 120 simulated s (about 90 s wall), then 25 % longer per depth layer | compressed on purpose |
    | lava-library-era | 20 1960s + 3 1970s/80s | pairings the Lava Library era pages, the Crestworth page and the Oozing Goo timelines document in words (research summary on build-host `references-data/`), with colors chosen for the words: yellow/orange and blue/green (the standard sixties pair), clear/red, Champagne Mist (cream in hazy amber), fluorescent green/red, blue-green/yellow, lavender/cream (the 1965 original) and lavender/blue, the 1968 Enchantress mists (blue mist/blue, orange mist/yellow, green mist/lime, green mist/mint and red), the prototypes red/pale blue and amber/yellow, Glitter Lite (emerald, gold, silver), Crestworth Port (ruby in rose) and Starboard (topaz in blue-green), 1970s blue/white, 1980s yellow/red and clear/gold |
    | lavalite | 43 | USA color codes 1992-2004 (Lava Library reference chart), ids keep the code numbers. Colors were SAMPLED: each swatch cropped, outline and specular pixels dropped, 2-means clustering, liquid = the cluster that dominates the swatch edges. Corrections: 08, 21, 22 and 25 had the clusters swapped (fixed by the liquid named first); 16a's wax was taken as the median of the dark pixels; white wax (03, 05, 26, glitter lamps) set to white, since the chart draws it tinted by the liquid. The chart itself is not reproduced anywhere in the repository |
    brand references. The coordinator asked for the `lavalite-NN` ids, and this
