#!/bin/bash
# Installs amdgpu_drm for good on the X399: the release build, started at
# boot when app_server looks for displays, with the amdgpu_kms accelerant.
#
#   haiku/tools/x399-install.sh [accelerant]
#
# AMDGPU_DRM_BIN names another release-configured build than out-release's
# (a call trace build, say).
#
# - the driver (out-release/amdgpu_drm) as bin/amdgpu_drm in the user's
#   non-packaged drivers, linked from dev/graphics only (see package.sh);
# - an accelerant, if one is given, in the user's non-packaged accelerants
#   (to try one out); without one the system's amdgpu_kms.accelerant (in
#   haiku.hpkg since the Haiku fork's 7c06a1f7c3) is used and a user copy
#   is removed;
# - the driver settings without the lab's parameters;
# - the native amdgpu driver and its amduvd decoder, which would start the
#   same GPU, out of the way: user copies are moved to
#   ~/config/non-packaged/disabled-wx5100, the haiku and
#   haiku_datatranslators packages' copies are blocked in
#   /boot/system/settings/packages. The VA-API decoder that takes amduvd's
#   name is the Haiku tree's amdgpu_va (build-amdgpu-va-plugin.sh).
#
# Installed builds keep a marker from their start until the GPU has run for
# 30 s: if a start brings the machine down, the next boot finds the marker
# and leaves the GPU to the framebuffer driver until it is deleted
# (~/config/settings/kernel/drivers/amdgpu_drm.starting).
# Takes effect at the next (cold) boot: a running build keeps one of its
# devices open (pin_thread() in driver.cpp), so devfs does not reload it
# when its binary changes, and the new build replaces it in place. An older
# running build without that is replaced at the next boot by the boot job
# x399-driver-safety instead - whose swap devfs may handle after it loaded
# the new build already, reloading the started GPU, which has stopped the
# machine (AMDGPU-DRM.md).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CONFIG=/mnt/HaikuWork/x399/ssh/config
SSH="ssh -F $CONFIG -o ConnectTimeout=10 ws-haiku"
NP=/boot/home/config/non-packaged
DRV=$NP/add-ons/kernel/drivers
SETTINGS=/boot/home/config/settings/kernel/drivers
BIN=${AMDGPU_DRM_BIN:-$ROOT/out-release/amdgpu_drm}
ACCELERANT=${1:-}

if ! grep -aq amdgpu_drm.starting "$BIN" || grep -aq amdgpu_drm.arm "$BIN"; then
	echo "$BIN is not a release build" >&2
	exit 1
fi

$SSH "rm -f $SETTINGS/amdgpu_drm.arm; sync"
local=$(sha256sum "$BIN" | cut -d' ' -f1)
installed=$($SSH "sha256sum $DRV/bin/amdgpu_drm 2>/dev/null" | cut -d' ' -f1)
loaded=0
$SSH "listimage 1 2>/dev/null | grep -q amdgpu_drm" && loaded=1

# Uploaded outside the drivers' directories: devfs takes any new file in
# them for a driver and starts it, and a second amdgpu on the same GPU
# brings the machine down. Then moved over the binary, unless a running
# build would be reloaded by that (see above).
STAGE=/boot/home/amdgpu-drm/amdgpu_drm.new
SWAP=
if [ "$local" = "$installed" ]; then
	echo "amdgpu_drm $local is installed already"
else
	$SSH "mkdir -p $DRV/bin $DRV/dev/graphics /boot/home/amdgpu-drm \
		&& cat > $STAGE && chmod 755 $STAGE && sync" < "$BIN"
	[ "$($SSH "sha256sum $STAGE" | cut -d' ' -f1)" = "$local" ] \
		|| { echo "upload damaged" >&2; exit 1; }
	if [ $loaded = 1 ] && ! $SSH "grep -aq 'keeping /dev/dri/renderD128 open' $DRV/bin/amdgpu_drm"; then
		echo "the running amdgpu_drm does not keep a device open: the new build is swapped in at the next boot, which may stop the machine (AMDGPU-DRM.md)" >&2
	else
		SWAP="mv -f $STAGE $DRV/bin/amdgpu_drm"
		[ $loaded = 1 ] && echo "amdgpu_drm is loaded and keeps a device open: the new build starts at the next boot" >&2
	fi
fi
$SSH "set -e
	$SWAP
	# a link made again is an entry removed and created for devfs
	[ \"\$(readlink $DRV/dev/graphics/amdgpu_drm)\" = ../../bin/amdgpu_drm ] \\
		|| ln -sf ../../bin/amdgpu_drm $DRV/dev/graphics/amdgpu_drm
	# a second link makes devfs reload the running driver (package.sh)
	rm -f $DRV/dev/dri/amdgpu_drm
	if [ -n '$ACCELERANT' ]; then
		mkdir -p $NP/add-ons/accelerants
		cp -f '$ACCELERANT' $NP/add-ons/accelerants/amdgpu_kms.accelerant
	elif [ -e /boot/system/add-ons/accelerants/amdgpu_kms.accelerant ]; then
		rm -f $NP/add-ons/accelerants/amdgpu_kms.accelerant
	elif [ ! -e $NP/add-ons/accelerants/amdgpu_kms.accelerant ]; then
		echo 'no amdgpu_kms.accelerant: name one, or install a haiku.hpkg that has it' >&2
		exit 1
	fi
	# no lab parameters
	printf '# amdgpu_drm: Linux amdgpu module parameters, one per line\n' > $SETTINGS/amdgpu_drm
	# the native driver for the same GPU, out of the way (an amduvd that
	# links libva is amdgpu_va, which stays)
	mkdir -p $NP/disabled-wx5100
	for f in $DRV/dev/dri/amdgpu $DRV/dev/graphics/amdgpu $DRV/bin/amdgpu \
			$NP/add-ons/media/plugins/amduvd; do
		[ -e \"\$f\" ] || [ -L \"\$f\" ] || continue
		case \$f in */amduvd) grep -aq libva \$f && continue;; esac
		mv -f \"\$f\" $NP/disabled-wx5100/\$(echo \"\${f#$NP/}\" | tr / _)
	done
	sync"
# The packaged copies: packagefs reads this file when it mounts /boot/system.
$SSH "P=/boot/system/settings/packages; grep -qs amdgpu_drm \$P || cat >> \$P; sync" <<'EOF'
# amdgpu_drm drives the GPU here: the native amdgpu driver and its decoder
# must not start (amdgpu_drm's haiku/tools/x399-install.sh).
Package haiku {
	BlockedEntries {
		add-ons/kernel/drivers/dev/graphics/amdgpu
		add-ons/kernel/drivers/bin/amdgpu
	}
}
Package haiku_datatranslators {
	BlockedEntries {
		add-ons/media/plugins/amduvd
	}
}
EOF
$SSH "ls -la $DRV/bin $DRV/dev/dri $DRV/dev/graphics $NP/add-ons/accelerants $NP/disabled-wx5100; cat /boot/system/settings/packages"
echo "installed amdgpu_drm $local; power cycle the workstation to start it"
