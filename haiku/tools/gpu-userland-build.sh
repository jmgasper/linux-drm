#!/bin/bash
# Runs ON the X399 (Haiku): builds the GPU user land for amdgpu_drm from the
# sources x399-gpu-userland.sh copied to $ROOT/src (libdrm, libglvnd, mesa)
# into $ROOT/prefix.
#
#   gpu-userland-build.sh STAGE...    stages: libdrm libva glvnd mesa-configure
#                                     mesa
#
# Environment: GPU_ROOT (default /boot/home/gpu-userland), GPU_JOBS (16),
# MESA_VULKAN_DRIVERS (default none; "amd" for RADV), GPU_PREFIX (default
# $GPU_ROOT/prefix; /boot/system/non-packaged installs it for the system,
# from build trees of its own).
#
# Everything that exchanges error numbers with libdrm is built with positive
# POSIX errors (posix_error_mapper), as Linux code expects them.
set -euo pipefail

ROOT=${GPU_ROOT:-/boot/home/gpu-userland}
JOBS=${GPU_JOBS:-16}
PREFIX=${GPU_PREFIX:-$ROOT/prefix}
SRC=$ROOT/src
BUILD=$ROOT/build
if [ "$PREFIX" != "$ROOT/prefix" ]; then
	BUILD=$ROOT/build-$(echo "$PREFIX" | tr / _)
fi
# meson 1.8.4, mako and python 3.10 with pyyaml, from the Summit Mesa build
PYTOOLS=${GPU_PYTOOLS:-/boot/home/summit-mesa/pytools}
export PATH=$PYTOOLS/bin:$PATH
export PYTHONPATH=$PYTOOLS/site
export PYTHONDONTWRITEBYTECODE=1
export PKG_CONFIG_PATH=$PREFIX/develop/lib/pkgconfig:$PREFIX/lib/pkgconfig:$PREFIX/data/pkgconfig
mkdir -p "$ROOT/tmp" "$BUILD"
export TMPDIR=$ROOT/tmp
# Haiku's runtime_loader implements only general-dynamic TLS.
TLS=-mtls-dialect=gnu
POSITIVE=-DB_USE_POSITIVE_POSIX_ERRORS
MAPPER=-lposix_error_mapper

stage_libdrm() {
	if [ ! -f "$BUILD/libdrm/build.ninja" ]; then
		meson setup "$BUILD/libdrm" "$SRC/libdrm" --prefix="$PREFIX" \
			--libdir=lib --datadir=data --includedir=develop/headers \
			--buildtype=release -Damdgpu=enabled -Dradeon=disabled \
			-Dintel=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
			-Domap=disabled -Dexynos=disabled -Dfreedreno=disabled \
			-Dtegra=disabled -Dvc4=disabled -Detnaviv=disabled \
			-Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled \
			-Dinstall-test-programs=false -Dtests=false
	fi
	ninja -C "$BUILD/libdrm" -j"$JOBS" install
}

stage_libva() {
	if [ ! -f "$BUILD/libva/build.ninja" ]; then
		meson setup "$BUILD/libva" "$SRC/libva" --prefix="$PREFIX" \
			--libdir=lib --datadir=data --includedir=develop/headers \
			--buildtype=release "-Dc_args=$POSITIVE" -Dc_link_args=$MAPPER \
			-Ddisable_drm=false -Dwith_x11=no -Dwith_glx=no -Dwith_wayland=no \
			-Dwith_win32=no
	fi
	ninja -C "$BUILD/libva" -j"$JOBS" install
}

stage_glvnd() {
	if [ ! -f "$BUILD/glvnd/build.ninja" ]; then
		meson setup "$BUILD/glvnd" "$SRC/libglvnd" --prefix="$PREFIX" \
			--libdir=lib --includedir=develop/headers --buildtype=release \
			--wrap-mode=nofallback -Dc_args=$TLS -Dcpp_args=$TLS \
			-Dx11=disabled -Dglx=disabled -Dhgl=true -Dgles1=false \
			-Dgles2=true -Degl=true
	fi
	ninja -C "$BUILD/glvnd" -j"$JOBS" install
}

stage_mesa_configure() {
	rm -rf "$BUILD/mesa"
	meson setup "$BUILD/mesa" "$SRC/mesa" --prefix="$PREFIX" --libdir=lib \
		--includedir=develop/headers --buildtype=release --wrap-mode=nofallback \
		"-Dc_args=$TLS $POSITIVE" "-Dcpp_args=$TLS $POSITIVE" \
		-Dc_link_args=$MAPPER -Dcpp_link_args=$MAPPER \
		-Dplatforms=haiku -Degl=enabled -Dglvnd=enabled -Dglx=disabled \
		-Dgbm=disabled -Dopengl=true -Dgles1=disabled -Dgles2=enabled \
		-Dgallium-drivers=radeonsi,llvmpipe,softpipe \
		"-Dvulkan-drivers=${MESA_VULKAN_DRIVERS:-}" \
		-Damd-use-llvm=false -Dllvm=enabled -Dshared-llvm=enabled \
		-Dgallium-va=disabled -Dgallium-rusticl=false \
		-Dshader-cache=enabled -Dxmlconfig=disabled -Dexpat=disabled \
		-Dzstd=disabled -Dzlib=enabled -Dvalgrind=disabled \
		-Dlibunwind=disabled -Dlmsensors=disabled -Dspirv-tools=disabled \
		-Dbuild-tests=false -Dtools=
}

stage_mesa() {
	ninja -C "$BUILD/mesa" -j"$JOBS" ${NINJA_KEEP_GOING:+-k 0}
	ninja -C "$BUILD/mesa" install
}

for stage in "$@"; do
	echo "== $stage ($(date +%H:%M:%S))"
	"stage_${stage//-/_}"
done
