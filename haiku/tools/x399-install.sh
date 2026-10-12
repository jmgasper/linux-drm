#!/bin/bash
# Installs amdgpu_drm for good on the X399: the release build, started at
# boot when app_server looks for displays, with the amdgpu_kms accelerant.
#
#   haiku/tools/x399-install.sh [accelerant]
#
# - the driver (out-release/amdgpu_drm) as bin/amdgpu_drm in the user's
#   non-packaged drivers, linked from dev/dri and dev/graphics;
# - the accelerant (default: the one built on the workstation by
#   haiku-accel) in the user's non-packaged accelerants;
# - the driver settings without the lab's parameters;
# - Codex's native amdgpu driver and amduvd decoder, which would start the
#   same GPU, are moved to ~/config/non-packaged/disabled-wx5100.
#
# Installed builds keep a marker from their start until the GPU has run for
# 30 s: if a start brings the machine down, the next boot finds the marker
# and leaves the GPU to the framebuffer driver until it is deleted
# (~/config/settings/kernel/drivers/amdgpu_drm.starting).
# Takes effect at the next (cold) boot.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CONFIG=/mnt/HaikuWork/x399/ssh/config
SSH="ssh -F $CONFIG -o ConnectTimeout=10 ws-haiku"
NP=/boot/home/config/non-packaged
DRV=$NP/add-ons/kernel/drivers
SETTINGS=/boot/home/config/settings/kernel/drivers
BIN=$ROOT/out-release/amdgpu_drm
ACCELERANT=${1:-/boot/home/gpu-userland/src/haiku-accel/out/amdgpu_kms.accelerant}

if ! grep -aq amdgpu_drm.starting "$BIN" || grep -aq amdgpu_drm.arm "$BIN"; then
	echo "$BIN is not a release build" >&2
	exit 1
fi

$SSH "rm -f $SETTINGS/amdgpu_drm.arm; sync"
if $SSH "listimage 1 2>/dev/null | grep -q amdgpu_drm"; then
	echo "amdgpu_drm is loaded; installing for the next boot" >&2
fi

# Uploaded outside the drivers' directories: devfs takes any new file in
# them for a driver and starts it, and a second amdgpu on the same GPU
# brings the machine down.
STAGE=/boot/home/amdgpu-drm/amdgpu_drm.new
$SSH "mkdir -p $DRV/bin $DRV/dev/dri $DRV/dev/graphics /boot/home/amdgpu-drm \
	&& cat > $STAGE && chmod 755 $STAGE" < "$BIN"
$SSH "set -e
	mv -f $STAGE $DRV/bin/amdgpu_drm
	ln -sf ../../bin/amdgpu_drm $DRV/dev/dri/amdgpu_drm
	ln -sf ../../bin/amdgpu_drm $DRV/dev/graphics/amdgpu_drm
	mkdir -p $NP/add-ons/accelerants
	cp -f $ACCELERANT $NP/add-ons/accelerants/amdgpu_kms.accelerant
	# no lab parameters
	printf '# amdgpu_drm: Linux amdgpu module parameters, one per line\n' > $SETTINGS/amdgpu_drm
	# Codex's driver for the same GPU, out of the way
	mkdir -p $NP/disabled-wx5100
	for f in $DRV/dev/dri/amdgpu $DRV/bin/amdgpu $NP/add-ons/media/plugins/amduvd; do
		[ -e \"\$f\" ] || [ -L \"\$f\" ] || continue
		mv -f \"\$f\" $NP/disabled-wx5100/\$(echo \"\${f#$NP/}\" | tr / _)
	done
	sync"
local=$(sha256sum "$BIN" | cut -d' ' -f1)
remote=$($SSH "sha256sum $DRV/bin/amdgpu_drm" | cut -d' ' -f1)
[ "$local" = "$remote" ] || { echo "checksum mismatch" >&2; exit 1; }
$SSH "ls -la $DRV/dev/dri $DRV/dev/graphics $NP/add-ons/accelerants $NP/disabled-wx5100"
echo "installed amdgpu_drm $local; power cycle the workstation to start it"
