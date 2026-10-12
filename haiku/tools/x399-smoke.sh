#!/bin/bash
# Runs the amdgpu_drm smoke test on the X399 against the started driver.
#
#   haiku/tools/x399-smoke.sh [last stage [hold]]
#                                            run stages 1..last (default all),
#                                            wait hold seconds, unmap, close
#   haiku/tools/x399-smoke.sh progress       after a crash: the stages reached
#                                            and the end of the driver log
#
# Each stage is written to a preallocated progress file and synced before it
# runs, so the stage that brought the machine down survives the power cycle.
set -euo pipefail
CONFIG=/mnt/HaikuWork/x399/ssh/config
SSH="ssh -F $CONFIG -o ConnectTimeout=10 ws-haiku"
DIR=/boot/home/amdgpu-drm
PROGRESS=/boot/home/amdgpu_smoke.progress

if [ "${1:-}" = progress ]; then
	$SSH "tr -d '\\000' < $PROGRESS; echo ---; tr -d '\\000' < /boot/home/amdgpu_drm.log | tail -40"
	exit 0
fi
last=${1:-99}
hold=${2:-0}
$SSH "rm -f $PROGRESS && dd if=/dev/zero of=$PROGRESS bs=4096 count=4 2>/dev/null && sync"
$SSH "$DIR/amdgpu_smoke /dev/dri/renderD128 $last $PROGRESS $hold"
