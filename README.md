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
