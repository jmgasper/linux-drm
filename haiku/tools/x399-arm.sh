#!/bin/bash
# Starts the installed lab build of amdgpu_drm once on the X399.
#
#   haiku/tools/x399-arm.sh ["param value" ...]
#
# Writes the driver settings (Linux module parameters, one per argument),
# creates the arm file and touches the add-on: devfs watches the binaries of
# the drivers it knows and reloads one whose modification time changes. That
# is one event and one reload; removing and creating the dev/dri link instead
# queues two, and the second unloads the driver right after it started. The
# arm file is good for this boot only: after a crash, the next boot does not
# start the driver. Then waits for the start.
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
# the log file the driver overwrites (log_file): 8 MB of zeros, on disk
$SSH "rm -f /boot/home/amdgpu_drm.log && dd if=/dev/zero of=/boot/home/amdgpu_drm.log bs=1048576 count=8 2>/dev/null && sync"
# let devfs finish with earlier changes (x399-deploy.sh) first
sleep 10
$SSH "mkdir -p $SETTINGS && cat > $SETTINGS/amdgpu_drm && touch $SETTINGS/amdgpu_drm.arm && sync" <<< "$params"
# devfs knows the driver once it has scanned /dev/dri
$SSH "ls /dev/dri >/dev/null; sleep 2; touch $DRV/bin/amdgpu_drm"
echo "armed with: ${*:-(no parameters)}"
for i in $(seq 1 60); do
	sleep 2
	if $SSH "test -e /dev/dri/card0" 2>/dev/null; then
		# lab builds keep a device open themselves (see driver.cpp)
		echo "started"
		exit 0
	fi
done
echo "no /dev/dri/card0 after 2 minutes" >&2
exit 1
