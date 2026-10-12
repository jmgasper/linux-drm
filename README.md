# linux-drm for air/OS

Linux's DRM core, TTM, the GPU scheduler, dma-buf fences and the AMD GPU
driver (amdgpu, Display Core, power management), built as a Haiku kernel
add-on for air/OS.

- `linux/` - the Linux sources this port compiles, imported unchanged by
  `tools/import-linux.sh` from a stable release (see `linux/VERSION`). Haiku
  changes to them are separate commits, so a re-import shows them as a diff.
- `haiku/compat/` - the Linux kernel API on Haiku's kernel.
- `haiku/amdgpu/` - the Haiku driver: PCI device, interrupts, `/dev/dri`
  nodes, memory mapping into teams.

Licensing (details in COPYING): the Linux files keep their own licences
(most of DRM and amdgpu is MIT; dma-buf, parts of the DRM core and the
copied Linux library code are GPL-2.0). The add-on built from them is
therefore distributed under the GPL-2.0; the compat layer and the Haiku
driver code are MIT. No firmware is kept here.

## Stock Haiku releases

`haiku/tools/build-release.sh r1beta6` builds the add-on and the amdgpu_kms
accelerant against Haiku R1/beta6 itself (its packages from Haiku's
repository, its headers, its kernel and libroot) and packages them for it
(`out-packages-r1beta6/`); the package requires `amdgpu_firmwares`
(`haiku/tools/package.sh`). On a stock Haiku, app_server drives the monitors
as one screen through the accelerant's classic hooks; arranging and scaling
them one by one needs air/OS's app_server. Southern and Sea Islands GPUs stay
with Haiku's radeon_hd unless `si_support 1` / `cik_support 1` are set.

Verified so far in QEMU on a stock R1/beta6 (no AMD GPU there): pkgman
installs the package, and with `haiku_selftest 1` in the driver settings the
add-on starts the DRM core and amdgpu and passes its self test (19 checks of
the compat layer). Not yet run on beta6 with a GPU. The GPU user land (Mesa,
libdrm, libva) is not built for releases yet.
