/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LX_VMA_H
#define _LX_VMA_H

/*	While the Haiku driver maps a buffer into a team, it runs the buffer's
	Linux fault handler for every page with vm_area_struct.lx_haiku pointing
	here; vmf_insert_pfn_prot() records the page frames, which the driver
	then maps as one area. */

struct lx_vma_pages {
	unsigned long prot;			/* pgprot of the last page inserted */
	unsigned long count;
	unsigned long pfns[];		/* 0: not inserted */
};

#endif /* _LX_VMA_H */
