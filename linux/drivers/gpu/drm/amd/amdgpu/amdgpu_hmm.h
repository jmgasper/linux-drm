/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */
/*
 * Changed for air/OS (Haiku), 2026: userptr buffers wire their pages instead
 * of following them with an MMU notifier (haiku/amdgpu/userptr.c). The
 * changes are in the history of https://github.com/jmgasper/linux-drm
 */
#ifndef __AMDGPU_MN_H__
#define __AMDGPU_MN_H__

#include <linux/types.h>
#include <linux/hmm.h>
#include <linux/rwsem.h>
#include <linux/workqueue.h>
#include <linux/interval_tree.h>
#include <linux/mmu_notifier.h>

int amdgpu_hmm_range_get_pages(struct mmu_interval_notifier *notifier,
			       uint64_t start, uint64_t npages, bool readonly,
			       void *owner,
			       struct hmm_range **phmm_range);
bool amdgpu_hmm_range_get_pages_done(struct hmm_range *hmm_range);

#if defined(CONFIG_HMM_MIRROR) || defined(__HAIKU_LINUX_COMPAT__)
int amdgpu_hmm_register(struct amdgpu_bo *bo, unsigned long addr);
void amdgpu_hmm_unregister(struct amdgpu_bo *bo);
#else
static inline int amdgpu_hmm_register(struct amdgpu_bo *bo, unsigned long addr)
{
	DRM_WARN_ONCE("HMM_MIRROR kernel config option is not enabled, "
		      "add CONFIG_ZONE_DEVICE=y in config file to fix this\n");
	return -ENODEV;
}
static inline void amdgpu_hmm_unregister(struct amdgpu_bo *bo) {}
#endif

#ifdef __HAIKU_LINUX_COMPAT__
/* the wired pages of a userptr buffer (haiku/amdgpu/userptr.c) */
struct drm_gem_object;
struct lx_userptr;
int lx_userptr_create(struct drm_gem_object *gobj, unsigned long address,
		      unsigned long count, bool writable, bool anon_only,
		      struct lx_userptr **userptr);
void lx_userptr_detach(struct lx_userptr *userptr);
void lx_userptr_release(struct lx_userptr *userptr);
int lx_userptr_get_pages(struct lx_userptr *userptr,
			 struct hmm_range **range);
bool lx_userptr_get_pages_done(struct hmm_range *range);
#endif

#endif
