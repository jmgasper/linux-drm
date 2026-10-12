#!/bin/bash
# Copies the GPU user land sources (libdrm, libglvnd, mesa: the files git
# tracks, as they are in the working trees) to the X399 and runs
# gpu-userland-build.sh there.
#
#   haiku/tools/x399-gpu-userland.sh [sync] STAGE...
#
# "sync" alone copies; stages see gpu-userland-build.sh. Sources come from
# /mnt/HaikuWork/src/forks/<name>.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
FORKS=$(dirname "$ROOT")
CONFIG=/mnt/HaikuWork/x399/ssh/config
SSH="ssh -F $CONFIG -o ConnectTimeout=10 ws-haiku"
REMOTE=/boot/home/gpu-userland

sync_tree() {
	local name=$1
	# tracked files plus new ones that are not ignored
	(cd "$FORKS/$name" && git ls-files -z --cached --others --exclude-standard) \
		| tar -C "$FORKS/$name" --null -T - -cf - \
		| $SSH "mkdir -p $REMOTE/src/$name && tar -xmf - -C $REMOTE/src/$name"
}

if [ "${1:-}" = sync ]; then
	shift
	for name in libdrm libglvnd mesa; do
		echo "sync $name"
		sync_tree "$name"
	done
fi
$SSH "cat > $REMOTE/build.sh && chmod 755 $REMOTE/build.sh" < "$ROOT/haiku/tools/gpu-userland-build.sh"
if [ $# -gt 0 ]; then
	$SSH "$REMOTE/build.sh $*"
fi
