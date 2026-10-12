#!/bin/bash
# Starts the installed lab build of amdgpu_drm once on the X399.
#
#   haiku/tools/x399-arm.sh ["param value" ...]
#
# Writes the driver settings (Linux module parameters, one per argument),
# creates the one-shot arm file and links the add-on into dev/dri again,
# which devfs sees as a new driver and probes. The driver deletes the arm
# file before it touches the GPU.
set -euo pipefail
CONFIG=/mnt/HaikuWork/x399/ssh/config
SSH="ssh -F $CONFIG -o ConnectTimeout=10 ws-haiku"
SETTINGS=/boot/home/config/settings/kernel/drivers
DRV=/boot/home/config/non-packaged/add-ons/kernel/drivers

params=""
for p in "$@"; do
	params="$params$p
"
done
$SSH "mkdir -p $SETTINGS && cat > $SETTINGS/amdgpu_drm && touch $SETTINGS/amdgpu_drm.arm && rm -f $DRV/dev/dri/amdgpu_drm && sync && sleep 2 && ln -s ../../bin/amdgpu_drm $DRV/dev/dri/amdgpu_drm && sync" <<< "$params"
echo "armed with: ${*:-(no parameters)}"
