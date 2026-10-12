#!/bin/bash
# Runs ON the X399 (Haiku): builds the GPU user land for amdgpu_drm from the
# sources x399-gpu-userland.sh copied to $ROOT/src (libdrm, libglvnd, mesa)
# into $ROOT/prefix.
#
#   gpu-userland-build.sh STAGE...    stages: libdrm libva glvnd mesa-configure
#                                     mesa ffmpeg bundle
#
# Environment: GPU_ROOT (default /boot/home/gpu-userland), GPU_JOBS (16),
# MESA_VULKAN_DRIVERS (default none; "amd" for RADV), MESA_GALLIUM_VA
# (default disabled; "enabled" builds radeonsi_drv_video), MESA_PLATFORMS
# (default haiku; "haiku,wayland" adds VK_KHR_wayland_surface and EGL on
# Wayland, for Haiku's in-process Wayland server), GPU_PREFIX (default
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
LINK=
if [ "$PREFIX" != "$ROOT/prefix" ]; then
	# Installed for the system: the libraries find each other there whatever
	# LIBRARY_PATH a program sets (Summit's leaves non-packaged out).
	LINK="-Wl,-rpath,$PREFIX/lib"
	MAPPER="$MAPPER $LINK"
fi

stage_libdrm() {
	if [ ! -f "$BUILD/libdrm/build.ninja" ]; then
		meson setup "$BUILD/libdrm" "$SRC/libdrm" --prefix="$PREFIX" \
			--libdir=lib --datadir=data --includedir=develop/headers \
			--buildtype=release -Damdgpu=enabled -Dradeon=disabled \
			-Dintel=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
			-Domap=disabled -Dexynos=disabled -Dfreedreno=disabled \
			-Dtegra=disabled -Dvc4=disabled -Detnaviv=disabled \
			-Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled \
			-Dinstall-test-programs=false -Dtests=false ${LINK:+"-Dc_link_args=$LINK"}
	fi
	ninja -C "$BUILD/libdrm" -j"$JOBS" install
}

stage_libva() {
	if [ ! -f "$BUILD/libva/build.ninja" ]; then
		meson setup "$BUILD/libva" "$SRC/libva" --prefix="$PREFIX" \
			--libdir=lib --datadir=data --includedir=develop/headers \
			--buildtype=release "-Dc_args=$POSITIVE" "-Dc_link_args=$MAPPER" \
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
		"-Dc_link_args=$MAPPER" "-Dcpp_link_args=$MAPPER" \
		"-Dplatforms=${MESA_PLATFORMS:-haiku}" -Degl=enabled -Dglvnd=enabled -Dglx=disabled \
		-Dgbm=disabled -Dopengl=true -Dgles1=disabled -Dgles2=enabled \
		-Dgallium-drivers=radeonsi,llvmpipe,softpipe \
		"-Dvulkan-drivers=${MESA_VULKAN_DRIVERS:-}" \
		-Damd-use-llvm=false -Dllvm=enabled -Dshared-llvm=enabled \
		"-Dgallium-va=${MESA_GALLIUM_VA:-disabled}" '-Dvideo-codecs=all' \
		-Dgallium-rusticl=false \
		-Dshader-cache=enabled -Dxmlconfig=disabled -Dexpat=disabled \
		-Dzstd=disabled -Dzlib=enabled -Dvalgrind=disabled \
		-Dlibunwind=disabled -Dlmsensors=disabled -Dspirv-tools=disabled \
		-Dbuild-tests=false -Dtools=
}

stage_mesa() {
	ninja -C "$BUILD/mesa" -j"$JOBS" ${NINJA_KEEP_GOING:+-k 0}
	ninja -C "$BUILD/mesa" install
	# libglvnd looks for EGL vendors in add-ons/opengl/egl_vendor.d on Haiku
	mkdir -p "$PREFIX/add-ons/opengl/egl_vendor.d"
	cp -f "$PREFIX/data/glvnd/egl_vendor.d/50_mesa.json" \
		"$PREFIX/add-ons/opengl/egl_vendor.d/"
	# and the Vulkan loader for drivers in add-ons/vulkan/icd.d
	if [ -f "$PREFIX/data/vulkan/icd.d/radeon_icd.x86_64.json" ]; then
		mkdir -p "$PREFIX/add-ons/vulkan/icd.d"
		cp -f "$PREFIX/data/vulkan/icd.d/radeon_icd.x86_64.json" \
			"$PREFIX/add-ons/vulkan/icd.d/"
	fi
}

# libavcodec with VA-API, static, for the amdgpu_va media add-on (H.264 and
# HEVC decoders only): from src/ffmpeg (the air/OS fork) into $ROOT/ffmpeg-va
stage_ffmpeg() {
	rm -rf "$BUILD/ffmpeg-va"
	mkdir -p "$BUILD/ffmpeg-va"
	(cd "$BUILD/ffmpeg-va" && "$SRC/ffmpeg/configure" \
		--prefix="$ROOT/ffmpeg-va" --enable-static --disable-shared \
		--enable-pic --disable-programs --disable-doc --disable-avdevice \
		--disable-avformat --disable-avfilter --disable-swresample \
		--disable-swscale --disable-network --disable-autodetect \
		--disable-everything --enable-decoder=h264,hevc \
		--enable-parser=h264,hevc --enable-hwaccel=h264_vaapi,hevc_vaapi \
		--enable-vaapi \
		&& make -j"$JOBS" && make install)
}

# The system install as one archive, for another machine: extract it with
# "tar -xzf <archive> -C /" (Haiku's tar), then reboot. Built from the
# /boot/system/non-packaged build trees (GPU_PREFIX), with the add-on manifests
# and the amduvd (VA-API) media add-on.
stage_bundle() {
	local stage=$ROOT/tmp/bundle
	rm -rf "$stage"
	for tree in libdrm libva glvnd mesa; do
		DESTDIR=$stage ninja -C "$BUILD/$tree" install > /dev/null
	done
	local np=$stage$PREFIX
	mkdir -p "$np/add-ons/opengl/egl_vendor.d" "$np/add-ons/vulkan/icd.d" \
		"$np/add-ons/media/plugins"
	cp -f "$np/data/glvnd/egl_vendor.d/50_mesa.json" \
		"$np/add-ons/opengl/egl_vendor.d/"
	[ -f "$np/data/vulkan/icd.d/radeon_icd.x86_64.json" ] \
		&& cp -f "$np/data/vulkan/icd.d/radeon_icd.x86_64.json" \
			"$np/add-ons/vulkan/icd.d/"
	local plugin=/boot/home/config/non-packaged/add-ons/media/plugins/amduvd
	[ -f "$plugin" ] && cp -f "$plugin" "$np/add-ons/media/plugins/amduvd"
	local archive=$ROOT/amdgpu-userland-$(date +%Y%m%d).tar.gz
	(cd "$stage" && tar -czf "$archive" boot)
	rm -rf "$stage"
	echo "$archive: $(du -h "$archive" | cut -f1)"
}

for stage in "$@"; do
	echo "== $stage ($(date +%H:%M:%S))"
	"stage_${stage//-/_}"
done
