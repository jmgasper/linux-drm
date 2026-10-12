#!/bin/bash
# A Haiku x86_64 virtual machine for testing amdgpu_drm without a GPU
# (settings "haiku_selftest 1"). The serial console, where a kernel panic
# shows, is logged to $DIR/serial.log.
#
#   haiku/tools/qemu-vm.sh start|stop|ssh <command>|deploy
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BASE=${BASE:-/mnt/HaikuWork/x399/evidence/wx5100/qemu-isolated/disk.qcow2}
DIR=${DIR:-/mnt/HaikuWork/x399/build/amdgpu-drm-qemu}
PORT=${PORT:-2240}
KEY=/mnt/HaikuWork/apps/burrow/.vm/id_ed25519
SSH="ssh -p $PORT -i $KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 user@127.0.0.1"
mkdir -p "$DIR"

case "${1:-}" in
	start)
		[ -f "$DIR/overlay.qcow2" ] || qemu-img create -q -f qcow2 -b "$BASE" -F qcow2 "$DIR/overlay.qcow2"
		qemu-system-x86_64 -enable-kvm -cpu host -m 4096 -smp 4 \
			-drive file="$DIR/overlay.qcow2",format=qcow2,if=ide \
			-netdev user,id=n,hostfwd=tcp:127.0.0.1:$PORT-:22 -device e1000,netdev=n \
			-vga std -display none -vnc 127.0.0.1:40 \
			-qmp unix:"$DIR/qmp.sock",server=on,wait=off \
			-serial file:"$DIR/serial.log" -pidfile "$DIR/qemu.pid" -daemonize
		for i in $(seq 1 60); do
			$SSH true 2>/dev/null && { echo "up"; exit 0; }
			sleep 2
		done
		echo "no ssh" >&2; exit 1 ;;
	stop)
		[ -f "$DIR/qemu.pid" ] && kill "$(cat "$DIR/qemu.pid")" 2>/dev/null || true ;;
	ssh)
		shift; $SSH "$@" ;;
	deploy)
		NP=/boot/home/config/non-packaged/add-ons/kernel/drivers
		$SSH "mkdir -p $NP/bin $NP/dev/dri && cat > $NP/bin/amdgpu_drm.tmp && chmod 755 $NP/bin/amdgpu_drm.tmp && mv -f $NP/bin/amdgpu_drm.tmp $NP/bin/amdgpu_drm && ln -sf ../../bin/amdgpu_drm $NP/dev/dri/amdgpu_drm && sync" < "$ROOT/out/amdgpu_drm" ;;
	selftest)
		# deploy, start once with "haiku_selftest 1" (plus any parameters
		# given), then show the self test or the kernel debugger's report
		shift
		"$0" deploy
		offset=$(stat -c %s "$DIR/serial.log")
		S=/boot/home/config/settings/kernel/drivers
		NP=/boot/home/config/non-packaged/add-ons/kernel/drivers
		params="haiku_selftest 1"
		for p in "$@"; do params="$params
$p"; done
		$SSH "mkdir -p $S && cat > $S/amdgpu_drm && touch $S/amdgpu_drm.arm && rm -f $NP/dev/dri/amdgpu_drm && sync && sleep 2 && ln -s ../../bin/amdgpu_drm $NP/dev/dri/amdgpu_drm && sync" <<< "$params"
		for i in $(seq 1 40); do
			sleep 2
			if tail -c +$((offset + 1)) "$DIR/serial.log" | grep -aq "Kernel Debugging Land"; then
				tail -c +$((offset + 1)) "$DIR/serial.log" | strings | grep -a -A40 "PANIC\|Welcome to Kernel"
				exit 1
			fi
			if $SSH "grep -aq 'self test: stopped' /var/log/syslog" 2>/dev/null; then
				$SSH "grep -a -E 'amdgpu' /var/log/syslog | tail -60"
				exit 0
			fi
		done
		echo "no result" >&2; exit 1 ;;
	*)
		echo "usage: $0 start|stop|ssh <command>|deploy|selftest [param...]" >&2; exit 1 ;;
esac
