#!/bin/bash
# Copy the part of a Linux source tree this port builds into linux/.
#   tools/import-linux.sh <linux source tree>
# Only files are added or replaced; the Haiku changes are separate commits on
# top, so re-importing a newer stable release shows them as a diff.
set -euo pipefail
SRC=$(realpath "$1")
DST=$(dirname "$(realpath "$0")")/../linux
mkdir -p "$DST"
cd "$SRC"
{
	# DRM core, helpers, TTM, the scheduler and the AMD driver
	find drivers/gpu/drm -maxdepth 1 -type f \( -name 'drm_*.[ch]' -o -name '*.h' \)
	find drivers/gpu/drm/ttm drivers/gpu/drm/scheduler drivers/gpu/drm/display \
		-type f -name '*.[ch]' -not -path '*/tests/*'
	find drivers/gpu/drm/amd -type f \( -name '*.[ch]' -o -name 'Makefile' \
		-o -name '*.S' \) -not -path '*/amdkfd/*' -not -path '*/tests/*'
	find drivers/gpu/drm/amd/amdkfd -type f -name '*.h'
	# fences, reservation objects, dma-buf and sync files
	find drivers/dma-buf -maxdepth 1 -type f -name '*.[ch]'
	# DRM headers, their UAPI, and the Linux headers of the subsystems above
	find include/drm include/uapi/drm -type f -name '*.h'
	ls include/linux/dma-fence*.h include/linux/dma-resv.h include/linux/dma-buf.h \
		include/linux/sync_file.h include/linux/iosys-map.h include/linux/dma-heap.h \
		include/uapi/linux/dma-buf.h include/uapi/linux/sync_file.h
	echo COPYING
	ls LICENSES/preferred/* LICENSES/exceptions/* 2>/dev/null || true
} | sort -u > "$DST/.files"
tar -cf - -T "$DST/.files" | tar -xf - -C "$DST"
make -s kernelversion > "$DST/VERSION" 2>/dev/null || sed -n 's/^VERSION = //p;s/^PATCHLEVEL = //p;s/^SUBLEVEL = //p' Makefile | paste -sd. > "$DST/VERSION"
echo "imported $(wc -l < "$DST/.files") files from Linux $(cat "$DST/VERSION")"
