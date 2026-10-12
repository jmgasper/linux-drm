#!/bin/bash
# Builds amdgpu_drm for a stock Haiku release (not air/OS) and packages it for
# that release: the kernel add-on and the amdgpu_kms accelerant, both built
# against the release's own headers, kernel and libraries.
#
#   haiku/tools/build-release.sh RELEASE [OUTPUT_DIR]       RELEASE: r1beta6
#
# HAIKU_SOURCE  a Haiku git tree that has the release's commit (for its
#               headers and decode_edid.c) and air/OS's accelerant in its
#               working tree (src/add-ons/accelerants/amdgpu_kms,
#               headers/private/graphics/amdgpu_drm); default
#               /mnt/HaikuWork/src/haiku, the air/OS fork
# HAIKU_BUILD   a configured air/OS build directory: the cross compiler (the
#               release's GCC) and the package tool; default
#               /mnt/HaikuWork/build/master-x86_64
# WORK          downloads and build trees; default out-RELEASE
#
# The release's haiku and haiku_devel packages come from Haiku's package
# repository and have to match the SHA-256 sums below. The package requires
# amdgpu_firmwares (haiku/tools/package.sh), which is the same for every
# Haiku. On a stock Haiku, amdgpu_kms drives the monitors as one screen
# (side by side) through app_server's classic hooks; arranging and scaling
# them one by one is air/OS's app_server. The GPU user land (Mesa, libdrm,
# libva) is not built here.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
RELEASE=${1:?usage: build-release.sh r1beta6 [OUTPUT_DIR]}
case $RELEASE in
	r1beta6)
		VERSION=r1~beta6_hrev59866_79
		NEXT=r1~beta7
		COMMIT=874f0cea9d07c6091817acf8e2cef61f2b2c453f
		HAIKU_SHA256=afe015c655467867a3e4341579e1ccbb10a1927f849923e08705f5fc431a116a
		DEVEL_SHA256=22fca2d887d394edf6f2af38b8a59d68fc693f41c759cad30576d2b48018a14f
		;;
	*)
		echo "no release $RELEASE" >&2
		exit 2
		;;
esac
OUTPUT=${2:-$ROOT/out-packages-$RELEASE}
WORK=${WORK:-$ROOT/out-$RELEASE}
HAIKU_SOURCE=${HAIKU_SOURCE:-/mnt/HaikuWork/src/haiku}
HAIKU_BUILD=${HAIKU_BUILD:-/mnt/HaikuWork/build/master-x86_64}
REPOSITORY=https://eu.hpkg.haiku-os.org/haiku/$RELEASE/x86_64/$VERSION
TOOL=$HAIKU_BUILD/objects/linux/x86_64/release/tools/package/package
export LD_LIBRARY_PATH=$HAIKU_BUILD/objects/linux/lib
CROSS=$(sed -n 's/^HAIKU_CC_x86_64 ?= \(.*\)gcc ;$/\1/p' \
	"$HAIKU_BUILD/build/BuildConfig")
GCCLIB=$(dirname "$("${CROSS}gcc" -print-libgcc-file-name)")
[ -x "$TOOL" ] || { echo "no package tool in $HAIKU_BUILD" >&2; exit 1; }

# --- the release: its packages, headers and sources --------------------------
mkdir -p "$WORK/download" "$OUTPUT"
fetch() {
	local name=$1 sum=$2 file=$WORK/download/$1.hpkg
	if ! echo "$sum  $file" | sha256sum -c --quiet >/dev/null 2>&1; then
		curl -sfL -o "$file" "$REPOSITORY/packages/$name-$VERSION-1-x86_64.hpkg"
		echo "$sum  $file" | sha256sum -c --quiet \
			|| { echo "$name.hpkg does not match its SHA-256" >&2; exit 1; }
	fi
	rm -rf "$WORK/system/$name"
	mkdir -p "$WORK/system/$name"
	"$TOOL" extract -C "$WORK/system/$name" "$file"
}
fetch haiku "$HAIKU_SHA256"
fetch haiku_devel "$DEVEL_SHA256"
SYSTEM=$WORK/system/haiku
DEVEL=$WORK/system/haiku_devel/develop/lib
rm -rf "$WORK/source"
mkdir -p "$WORK/source/build/user_config_headers"
git -C "$HAIKU_SOURCE" archive "$COMMIT" headers build/config_headers \
		src/add-ons/accelerants/common/decode_edid.c \
	| tar -x -C "$WORK/source"

# --- the kernel add-on --------------------------------------------------------
make -f "$ROOT/haiku/build/Makefile" -j"$(nproc)" all OUT="$WORK/driver" \
	HAIKU_BUILD="$HAIKU_BUILD" HAIKU_TOP="$WORK/source" \
	HAIKU_KERNEL="$DEVEL/_KERNEL_" \
	HAIKU_VERSION_GLUE="$DEVEL/haiku_version_glue.o" > "$WORK/driver.log" 2>&1 \
	|| { tail -20 "$WORK/driver.log" >&2; exit 1; }
DRIVER=$WORK/driver/amdgpu_drm

# --- the accelerant -------------------------------------------------------------
H=$WORK/source/headers
FLAGS=(-O2 -fPIC -nostdinc -Wall -DHAIKU_TARGET_PLATFORM_HAIKU
	-I"$HAIKU_SOURCE/headers/private/graphics/amdgpu_drm"
	-I"$H/private/graphics/common" -I"$H/private/graphics" -I"$H/private/shared"
	-I"$H/os" -I"$H/os/app" -I"$H/os/interface" -I"$H/os/support"
	-I"$H/os/kernel" -I"$H/os/drivers" -I"$H/os/storage" -I"$H/os/device"
	-I"$H/os/add-ons/graphics"
	-I"$H/posix" -I"$H" -isystem "$GCCLIB/include")
ACCELERANT_SOURCE=$HAIKU_SOURCE/src/add-ons/accelerants/amdgpu_kms
rm -rf "$WORK/accelerant"
mkdir -p "$WORK/accelerant"
objects=()
for source in "$ACCELERANT_SOURCE"/*.cpp; do
	object=$WORK/accelerant/$(basename "$source" .cpp).o
	"${CROSS}g++" -std=gnu++17 -fno-exceptions -fno-rtti "${FLAGS[@]}" \
		-include "$ROOT/haiku/release/airos_accelerant.h" -c "$source" -o "$object"
	objects+=("$object")
done
"${CROSS}gcc" "${FLAGS[@]}" -c \
	"$WORK/source/src/add-ons/accelerants/common/decode_edid.c" \
	-o "$WORK/accelerant/decode_edid.o"
objects+=("$WORK/accelerant/decode_edid.o")
ACCELERANT=$WORK/accelerant/amdgpu_kms.accelerant
"${CROSS}gcc" -shared -nostdlib -Wl,--no-undefined -o "$ACCELERANT" \
	"$DEVEL/crti.o" "$GCCLIB/crtbeginS.o" "$DEVEL/init_term_dyn.o" \
	"${objects[@]}" "$SYSTEM/lib/libroot.so" \
	"$GCCLIB/libgcc.a" "$GCCLIB/crtendS.o" "$DEVEL/crtn.o"

# --- the package ----------------------------------------------------------------
linuxVersion=$(git -C "$ROOT" rev-parse --abbrev-ref HEAD | sed -n 's/^airos-//p')
linuxVersion=${linuxVersion:-6.18.52}
commit=$(git -C "$ROOT" rev-parse --short HEAD)
stamp=$(git -C "$ROOT" log -1 --format=%cd --date=format:%Y%m%d)
driverVersion="$linuxVersion~git$stamp"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/add-ons/kernel/drivers/bin" \
	"$STAGE/add-ons/kernel/drivers/dev/graphics" \
	"$STAGE/add-ons/kernel/drivers/dev/dri" "$STAGE/add-ons/accelerants" \
	"$STAGE/documentation/packages/amdgpu_drm"
install -m 755 "$DRIVER" "$STAGE/add-ons/kernel/drivers/bin/amdgpu_drm"
ln -s ../../bin/amdgpu_drm "$STAGE/add-ons/kernel/drivers/dev/graphics/amdgpu_drm"
ln -s ../../bin/amdgpu_drm "$STAGE/add-ons/kernel/drivers/dev/dri/amdgpu_drm"
install -m 755 "$ACCELERANT" "$STAGE/add-ons/accelerants/amdgpu_kms.accelerant"
cat > "$STAGE/documentation/packages/amdgpu_drm/README" <<EOF
amdgpu_drm $driverVersion (linux-drm $commit) for Haiku $VERSION: Linux
$linuxVersion's amdgpu driver with the DRM core, TTM, the GPU scheduler and
Display Core, running on Haiku through a Linux compatibility layer, and the
amdgpu_kms accelerant that app_server drives the monitors with. Built for
this Haiku release, from air/OS (https://github.com/jmgasper/haiku).

It publishes /dev/dri/card<n> and renderD<n> (the DRM interface libdrm,
Mesa's radeonsi and RADV, and libva use) and /dev/graphics/amdgpu_drm_<n>
for app_server. Monitors are driven as one screen, side by side; Screen
can choose one monitor's mode instead.

GPUs: AMD Radeon from Volcanic Islands (Tonga, Fiji, Polaris) on, Vega,
RDNA 1 to 4, and APUs from Carrizo on. Southern and Sea Islands GPUs (HD 7000,
R7/R9 200) stay with Haiku's radeon_hd; "si_support 1" or "cik_support 1"
in ~/config/settings/kernel/drivers/amdgpu_drm hands them to amdgpu_drm,
and radeon_hd then has to be blocked (BlockedEntries in
/boot/system/settings/packages).

Module parameters: one "name value" line each in that settings file
(Linux amdgpu's parameters). A start that brings the machine down is not
repeated: the marker ~/config/settings/kernel/drivers/amdgpu_drm.starting
lives until the GPU has run for 30 seconds, and a boot that finds it leaves
the GPU to the framebuffer driver until it is deleted.

Licence: GNU GPL v2 (the add-on combines GPL-2.0 and MIT sources; the
accelerant is MIT). Complete source: https://github.com/jmgasper/linux-drm
at commit $commit, and air/OS's src/add-ons/accelerants/amdgpu_kms.
EOF
cat > "$STAGE/.PackageInfo" <<EOF
name			amdgpu_drm
version			$driverVersion-1
architecture	x86_64
summary			"Linux's amdgpu driver for AMD Radeon GPUs"
description		"amdgpu_drm runs Linux $linuxVersion's amdgpu, DRM, TTM and Display \
Core on Haiku: kernel mode setting for app_server (the amdgpu_kms accelerant), \
and the DRM interface for OpenGL (radeonsi), Vulkan (RADV) and video \
decoding and encoding (VA-API). Built for Haiku $VERSION."
packager		"air/OS"
vendor			"air/OS"
copyrights		{ "Advanced Micro Devices, Inc." "The Linux kernel contributors" "air/OS" }
licenses		{ "MIT" "GNU GPL v2" }
source-urls		{ "https://github.com/jmgasper/linux-drm" }
provides		{ amdgpu_drm = $linuxVersion }
requires		{
	haiku >= $VERSION
	haiku < $NEXT
	amdgpu_firmwares
}
EOF
package=$OUTPUT/amdgpu_drm-$driverVersion-1-x86_64.hpkg
rm -f "$package"
(cd "$STAGE" && "$TOOL" create -q "$package")
echo "$package (for Haiku $VERSION)"
