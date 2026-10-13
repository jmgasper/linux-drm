/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LX_VMA_H
#define _LX_VMA_H

/*	When a team touches a page of a buffer mapped into it, the Haiku driver
	runs the buffer's Linux fault handler with vm_area_struct.lx_haiku
	pointing here; vmf_insert_pfn_prot() records the page frames (the
	faulting page's and the ones the handler maps ahead), which the driver
	then maps into the team. */

struct lx_vma_pages {
	unsigned long prot;			/* pgprot of the last page inserted */
	unsigned long count;
	unsigned long pfns[];		/* 0: not inserted */
};

#endif /* _LX_VMA_H */
