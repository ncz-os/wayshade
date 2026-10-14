#!/bin/bash
# usage: SSHPASS=<login> bhrender.sh <local-repo-with-build-amd64> ; renders modes on build-host (password auth via sshpass -e)
set -u
: "${SSHPASS:?set SSHPASS in the environment (never on a command line)}"
export SSHPASS
P="sshpass -e ssh -o PubkeyAuthentication=no -o StrictHostKeyChecking=no build-host@192.0.2.85"
SRC=${1:?local repo dir}
rm -rf bhstage && mkdir -p bhstage/vendor/blackhole
cp $SRC/build-amd64/blackhole_gles3 bhstage/
cp $SRC/vendor/blackhole/blackhole.frag bhstage/vendor/blackhole/
$P 'rm -rf ~/bhtest && mkdir -p ~/bhtest'
sshpass -e scp -o PubkeyAuthentication=no -q -r bhstage/* build-host@192.0.2.85:bhtest/
for mode in ${MODES:-stylized kipthorne faithful}; do
 for seed in ${SEEDS:-7}; do
  $P "cd ~/bhtest && export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 && rm -rf out_${mode}_$seed && NCZ_BLACKHOLE_COLORS=$mode NCZ_BLACKHOLE_SEED=$seed NCZ_FRAME_DUMP=out_${mode}_$seed timeout --kill-after=2 12 ./blackhole_gles3 > out_${mode}_$seed.log 2>&1; echo rc=\$?; ls out_${mode}_$seed | head -20; grep -ci 'error' out_${mode}_$seed.log"
 done
done
mkdir -p shots
sshpass -e scp -o PubkeyAuthentication=no -q -r build-host@192.0.2.85:bhtest/out_* shots/ 
ls shots
