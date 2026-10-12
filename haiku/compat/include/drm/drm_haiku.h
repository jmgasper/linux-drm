/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DRM_HAIKU_H
#define _DRM_HAIKU_H

/*	Haiku additions to the DRM device interface, for libdrm on Haiku.

	Haiku cannot mmap() a device, so DRM_HAIKU_IOCTL_MAP maps a buffer
	object at the fake offset the driver's GEM_MMAP ioctl returned into the
	calling team, as Linux's mmap() of the DRM file would, and
	DRM_HAIKU_IOCTL_UNMAP undoes it. The area belongs to the team; deleting
	it without UNMAP keeps the buffer referenced until the file is closed. */

#if defined(__KERNEL__) || defined(_KERNEL_MODE)
#	ifndef __user
#		define __user
#	endif
#endif

struct drm_haiku_map {
	__u64 offset;		/* in: fake offset from GEM_MMAP */
	__u64 size;			/* in: bytes, page aligned */
	__u32 flags;		/* in: DRM_HAIKU_MAP_WRITE */
	__s32 area;			/* out: Haiku area_id */
	__u64 address;		/* in: wanted address or 0; out: the address */
};

#define DRM_HAIKU_MAP_WRITE	0x1

struct drm_haiku_unmap {
	__u64 address;		/* in: address DRM_HAIKU_IOCTL_MAP returned */
};

/* 'd' 0xf0..0xff are free in the DRM core range */
#define DRM_HAIKU_IOCTL_MAP		DRM_IOWR(0xf0, struct drm_haiku_map)
#define DRM_HAIKU_IOCTL_UNMAP	DRM_IOW(0xf1, struct drm_haiku_unmap)

#endif /* _DRM_HAIKU_H */
