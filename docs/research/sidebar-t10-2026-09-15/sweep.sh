#!/bin/sh
# Runs the probe over both fields and a set of tone alphas, converts every
# frame to sRGB, and stops if any shot was refused. Both parentings of the
# tone are shot for the alpha given as $1: under the pill (the view) and over
# it (painted by the strip).
cd "$(dirname "$0")" || exit 1
mkdir -p out srgb
run() { # tag, args...
  tag=$1; shift
  ./t10probe --out out --x 40 --tag "$tag" "$@" || { echo "REFUSED: $tag"; exit 2; }
  sips --matchTo '/System/Library/ColorSync/Profiles/sRGB Profile.icc' "out/$tag.png" --out "srgb/$tag.png" > /dev/null 2>&1 || exit 3
}
pick=$1
for f in 0.10 0.82; do
  run "f$f-today"    --field $f --alpha 0    --seam 1
  run "f$f-control"  --field $f --alpha 0    --seam 0
  for a in $*; do
    run "f$f-a$a"    --field $f --alpha $a   --seam 0
  done
  run "f$f-over$pick" --field $f --alpha $pick --seam 0 --tone-over 1
done
grep -h 'DONE\|RADII\|FOREIGN' out/*.log | sort | uniq -c
