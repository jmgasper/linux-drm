/* An SDMA job that never ends: POLL_REGMEM on a word that stays 0, waiting
   for 1 with an endless retry count (as sdma_v3_0's pipeline sync, but never
   satisfied). amdgpu's job timeout and recovery then run on the SDMA ring.

   sdmahang [seconds to wait for the fence] */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>

static amdgpu_device_handle sDevice;

struct buffer { amdgpu_bo_handle bo; amdgpu_va_handle va; uint64_t address; };

static int make(struct buffer* b, uint64_t size, uint32_t heap)
{
	struct amdgpu_bo_alloc_request r;
	memset(&r, 0, sizeof(r));
	r.alloc_size = size; r.phys_alignment = 4096; r.preferred_heap = heap;
	if (amdgpu_bo_alloc(sDevice, &r, &b->bo) != 0) return -1;
	if (amdgpu_va_range_alloc(sDevice, amdgpu_gpu_va_range_general, size, 4096, 0,
			&b->address, &b->va, 0) != 0
		|| amdgpu_bo_va_op(b->bo, 0, size, b->address, 0, AMDGPU_VA_OP_MAP) != 0)
		return -1;
	return 0;
}

int main(int argc, char** argv)
{
	int wait = argc > 1 ? atoi(argv[1]) : 60;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	uint32_t major, minor;
	if (fd < 0 || amdgpu_device_initialize(fd, &major, &minor, &sDevice) != 0)
		return 1;
	struct buffer word, ib;
	if (make(&word, 4096, AMDGPU_GEM_DOMAIN_GTT) || make(&ib, 4096, AMDGPU_GEM_DOMAIN_GTT))
		return 1;
	uint32_t* p;
	amdgpu_bo_cpu_map(word.bo, (void**)&p); p[0] = 0; amdgpu_bo_cpu_unmap(word.bo);
	amdgpu_bo_cpu_map(ib.bo, (void**)&p);
	uint32_t n = 0;
	p[n++] = 8 | (3u << 28) | (1u << 31);	/* POLL_REGMEM, equal, memory */
	p[n++] = (uint32_t)word.address & 0xfffffffc;
	p[n++] = (uint32_t)(word.address >> 32);
	p[n++] = 1;				/* reference: never */
	p[n++] = 0xffffffff;	/* mask */
	p[n++] = (0xfffu << 16) | 4;	/* retry forever, interval 4 */
	while (n % 8) p[n++] = 0;	/* NOP */
	amdgpu_bo_cpu_unmap(ib.bo);
	amdgpu_bo_handle handles[2] = { ib.bo, word.bo };
	amdgpu_bo_list_handle list;
	amdgpu_bo_list_create(sDevice, 2, handles, NULL, &list);
	amdgpu_context_handle context;
	amdgpu_cs_ctx_create(sDevice, &context);
	struct amdgpu_cs_ib_info info = { 0 };
	info.ib_mc_address = ib.address; info.size = n;
	struct amdgpu_cs_request request = { 0 };
	request.ip_type = AMDGPU_HW_IP_DMA; request.resources = list;
	request.number_of_ibs = 1; request.ibs = &info;
	int r = amdgpu_cs_submit(context, 0, &request, 1);
	printf("submitted: %d\n", r); fflush(stdout);
	struct amdgpu_cs_fence fence = { 0 };
	fence.context = context; fence.ip_type = AMDGPU_HW_IP_DMA; fence.fence = request.seq_no;
	uint32_t expired = 0;
	time_t start = time(NULL);
	r = amdgpu_cs_query_fence_status(&fence, (uint64_t)wait * 1000000000ULL, 0, &expired);
	uint32_t hangs = 0;
	amdgpu_cs_query_reset_state(context, &hangs, &(uint32_t){0});
	printf("fence after %ld s: %d expired %u, context reset state %u\n",
		(long)(time(NULL) - start), r, expired, hangs);
	return 0;
}
