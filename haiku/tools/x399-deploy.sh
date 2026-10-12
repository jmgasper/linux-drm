#!/bin/bash
# Installs a test build of amdgpu_drm and the Polaris firmware on the X399
# lab workstation, without loading it.
#
#   haiku/tools/x399-deploy.sh [firmware dir]
#
# OUT=<build output dir> picks another build (default out/, e.g. out-trace).
# The add-on goes to the user's non-packaged drivers as bin/amdgpu_drm,
# linked from dev/dri/. Build it with ARMED_ONLY=1: it then starts only when
# x399-arm.sh has created its one-shot arm file, and deletes that file
# before touching the GPU, so a start that brings the machine down is not
# repeated at the next boot.
# A loaded driver's binary must not be replaced: devfs reloads it underneath
# itself. This refuses when the add-on is loaded, unless HOT=1: then the lab
# build is asked to close its own device file and devfs unloads it.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
FIRMWARE=${1:-/mnt/HaikuWork/x399/build/amdgpu-drm-stage/firmware}
BIN=${OUT:-$ROOT/out}/amdgpu_drm
CONFIG=/mnt/HaikuWork/x399/ssh/config
SSH="ssh -F $CONFIG -o ConnectTimeout=10 ws-haiku"
NP=/boot/home/config/non-packaged
DRV=$NP/add-ons/kernel/drivers

# no start from the file system events of this installation
$SSH "rm -f /boot/home/config/settings/kernel/drivers/amdgpu_drm.arm; sync"
if $SSH "listimage 1 2>/dev/null | grep -q amdgpu_drm"; then
	if [ "${HOT:-}" != 1 ]; then
		echo "amdgpu_drm is loaded on the workstation; power cycle it first," \
			"or HOT=1 to unload it (tests the teardown)" >&2
		exit 1
	fi
	# lab builds keep a device open; with nothing else open, replacing the
	# binary makes devfs unload the running driver
	$SSH "/boot/home/amdgpu-drm/lab_unpin"
fi

tar -C "$FIRMWARE" -cf - . | $SSH "mkdir -p $NP/data/firmware && tar -xmf - -C $NP/data/firmware"
$SSH "cat > $DRV/bin/amdgpu_drm.tmp && chmod 755 $DRV/bin/amdgpu_drm.tmp && mv -f $DRV/bin/amdgpu_drm.tmp $DRV/bin/amdgpu_drm" < "$BIN"
$SSH "rm -rf $DRV/dev/amdgpu_test && mkdir -p $DRV/dev/dri && ln -sf ../../bin/amdgpu_drm $DRV/dev/dri/amdgpu_drm && sync"
# the test programs, built on the workstation
tar -C "$ROOT" -cf - haiku/tests linux/include/uapi/drm haiku/compat/include/drm/drm_haiku.h \
	| $SSH "mkdir -p /boot/home/amdgpu-drm && cd /boot/home/amdgpu-drm && tar -xmf - \
		&& gcc -O2 -Ilinux/include/uapi -Ihaiku/compat/include -o amdgpu_smoke haiku/tests/amdgpu_smoke.c \
		&& gcc -O2 -o lab_unpin haiku/tests/lab_unpin.c \
		&& gcc -O2 -o lab_bench haiku/tests/lab_bench.c"
local=$(sha256sum "$BIN" | cut -d' ' -f1)
remote=$($SSH "sha256sum $DRV/bin/amdgpu_drm" | cut -d' ' -f1)
[ "$local" = "$remote" ] || { echo "checksum mismatch" >&2; exit 1; }
echo "installed amdgpu_drm $local"
