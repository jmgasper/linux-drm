/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Memory: kmalloc, vmalloc, pages and their registry, vmap, DMA, ioremap,
	user copies.

	kmalloc() blocks carry a header with their size, which gives ksize() and
	krealloc(), and are aligned like Linux's (power-of-two sizes naturally,
	up to a page). vmalloc() memory is an area. */

#include <lx/haiku.h>


/* #pragma mark - kmalloc */

struct kmalloc_header {
	void *base;
	size_t size;
};

#define KMALLOC_MAGIC_SIZE	sizeof(struct kmalloc_header)


static size_t
kmalloc_alignment(size_t size)
{
	size_t align = 16;
	if (size >= 32 && size <= PAGE_SIZE && is_power_of_2(size))
		align = size;
	return align;
}


/* for the watchdog: what Linux code has allocated and not freed */
static long lx_kmalloc_live, lx_kmalloc_bytes, lx_pages_live;
static LIST_HEAD(lx_kmem_caches);
static DEFINE_MUTEX(lx_kmem_caches_lock);


void *
lx_kmalloc(size_t size, gfp_t flags)
{
	size_t align;
	char *base, *object;
	struct kmalloc_header *header;

	if (size == 0)
		return ZERO_SIZE_PTR;
	align = kmalloc_alignment(size);
	base = lx_haiku_malloc(size + align + KMALLOC_MAGIC_SIZE);
	if (base == NULL) {
		if (!(flags & __GFP_NOWARN))
			pr_warn("linux compat: kmalloc(%zu) failed\n", size);
		return NULL;
	}
	object = (char *)ALIGN((uintptr_t)base + KMALLOC_MAGIC_SIZE, align);
	header = (struct kmalloc_header *)object - 1;
	header->base = base;
	header->size = size;
	__atomic_add_fetch(&lx_kmalloc_live, 1, __ATOMIC_RELAXED);
	__atomic_add_fetch(&lx_kmalloc_bytes, (long)size, __ATOMIC_RELAXED);
	if (flags & __GFP_ZERO)
		memset(object, 0, size);
	return object;
}


void
lx_kfree(const void *object)
{
	struct kmalloc_header *header;
	if (ZERO_OR_NULL_PTR(object))
		return;
	if (((uintptr_t)object & (PAGE_SIZE - 1)) == 0
		&& lx_haiku_area_free((void *)object) == 0)
		return;		/* vmalloc() memory passed to kvfree() */
	header = (struct kmalloc_header *)object - 1;
	__atomic_sub_fetch(&lx_kmalloc_live, 1, __ATOMIC_RELAXED);
	__atomic_sub_fetch(&lx_kmalloc_bytes, (long)header->size, __ATOMIC_RELAXED);
	lx_haiku_free(header->base);
}


size_t
ksize(const void *object)
{
	if (ZERO_OR_NULL_PTR(object))
		return 0;
	return ((const struct kmalloc_header *)object - 1)->size;
}


void *
lx_krealloc(const void *object, size_t new_size, gfp_t flags)
{
	void *result;
	size_t old_size;
	if (new_size == 0) {
		lx_kfree(object);
		return ZERO_SIZE_PTR;
	}
	old_size = ksize(object);
	if (old_size >= new_size && !ZERO_OR_NULL_PTR(object))
		return (void *)object;
	result = lx_kmalloc(new_size, flags & ~__GFP_ZERO);
	if (result == NULL)
		return NULL;
	if (old_size > 0)
		memcpy(result, object, old_size);
	if (flags & __GFP_ZERO)
		memset((char *)result + old_size, 0, new_size - old_size);
	lx_kfree(object);
	return result;
}


void *
lx_vmalloc(size_t size, gfp_t flags)
{
	if (size == 0)
		return NULL;
	return lx_haiku_area_alloc(size, (flags & __GFP_ZERO) != 0);
}


void
lx_vfree(const void *address)
{
	if (address == NULL)
		return;
	if (lx_haiku_area_free((void *)address) != 0)
		lx_kfree(address);
}


bool
is_vmalloc_addr(const void *address)
{
	return lx_haiku_is_area_alloc(address);
}


/* #pragma mark - slab caches */


struct kmem_cache *
kmem_cache_create(const char *name, unsigned int size, unsigned int align,
	slab_flags_t flags, void (*ctor)(void *))
{
	struct kmem_cache *cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (cache == NULL)
		return NULL;
	cache->name = name;
	cache->size = size;
	cache->align = align;
	cache->flags = flags;
	cache->ctor = ctor;
	mutex_lock(&lx_kmem_caches_lock);
	list_add_tail(&cache->entry, &lx_kmem_caches);
	mutex_unlock(&lx_kmem_caches_lock);
	return cache;
}


void
kmem_cache_destroy(struct kmem_cache *cache)
{
	if (cache == NULL)
		return;
	mutex_lock(&lx_kmem_caches_lock);
	list_del(&cache->entry);
	mutex_unlock(&lx_kmem_caches_lock);
	kfree(cache);
}


void *
kmem_cache_alloc(struct kmem_cache *cache, gfp_t flags)
{
	void *object = kmalloc(cache->size, flags);
	if (object != NULL) {
		__atomic_add_fetch(&cache->live, 1, __ATOMIC_RELAXED);
		if (cache->ctor != NULL)
			cache->ctor(object);
	}
	return object;
}


void
kmem_cache_free(struct kmem_cache *cache, void *object)
{
	if (object != NULL)
		__atomic_sub_fetch(&cache->live, 1, __ATOMIC_RELAXED);
	kfree(object);
}


/*	For the watchdog: kmalloc() objects and bytes, pages, and the caches
	with the most objects. */
int
lx_mm_stats(char *buffer, int size)
{
	struct kmem_cache *cache, *top[4] = {};
	int n, i, j;
	n = scnprintf(buffer, size, "kmalloc %ld/%ld KiB pages %ld",
		__atomic_load_n(&lx_kmalloc_live, __ATOMIC_RELAXED),
		__atomic_load_n(&lx_kmalloc_bytes, __ATOMIC_RELAXED) / 1024,
		__atomic_load_n(&lx_pages_live, __ATOMIC_RELAXED));
	mutex_lock(&lx_kmem_caches_lock);
	list_for_each_entry(cache, &lx_kmem_caches, entry) {
		for (i = 0; i < 4; i++) {
			if (top[i] == NULL || READ_ONCE(cache->live) > READ_ONCE(top[i]->live)) {
				for (j = 3; j > i; j--)
					top[j] = top[j - 1];
				top[i] = cache;
				break;
			}
		}
	}
	for (i = 0; i < 4 && top[i] != NULL; i++) {
		n += scnprintf(buffer + n, size - n, " %s %ld", top[i]->name,
			READ_ONCE(top[i]->live));
	}
	mutex_unlock(&lx_kmem_caches_lock);
	return n;
}


/* #pragma mark - pages */

/*	Every allocation of pages is a run of physically contiguous pages with
	an array of struct page; the runs are in an rbtree by frame number.
	Pages that came from elsewhere (virt_to_page() of kmalloc() memory) get
	single-page entries that stay. */

struct page_run {
	struct rb_node node;
	unsigned long first_pfn;
	unsigned long count;
	void *haiku_page;			/* NULL: not ours to free */
	struct page pages[];
};

static struct rb_root page_runs = RB_ROOT;
static DEFINE_SPINLOCK(page_runs_lock);
struct page *lx_zero_page;


static struct page_run *
page_run_find(unsigned long pfn)
{
	struct rb_node *node = page_runs.rb_node;
	while (node != NULL) {
		struct page_run *run = rb_entry(node, struct page_run, node);
		if (pfn < run->first_pfn)
			node = node->rb_left;
		else if (pfn >= run->first_pfn + run->count)
			node = node->rb_right;
		else
			return run;
	}
	return NULL;
}


static void
page_run_insert(struct page_run *run)
{
	struct rb_node **link = &page_runs.rb_node, *parent = NULL;
	while (*link != NULL) {
		struct page_run *other = rb_entry(*link, struct page_run, node);
		parent = *link;
		if (run->first_pfn < other->first_pfn)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}
	rb_link_node(&run->node, parent, link);
	rb_insert_color(&run->node, &page_runs);
}


static struct page_run *
page_run_create(unsigned long first_pfn, unsigned long count,
	void *haiku_page, unsigned int order)
{
	struct page_run *run = kzalloc(struct_size(run, pages, count), GFP_KERNEL);
	unsigned long i;
	if (run == NULL)
		return NULL;
	run->first_pfn = first_pfn;
	run->count = count;
	run->haiku_page = haiku_page;
	for (i = 0; i < count; i++) {
		struct page *page = &run->pages[i];
		page->lx_phys = PFN_PHYS(first_pfn + i);
		page->lx_haiku_page = haiku_page;
		page->lx_head = &run->pages[0];
		atomic_set(&page->_refcount, 1);
		INIT_LIST_HEAD(&page->lru);
	}
	run->pages[0].lx_order = order;
	run->pages[0].lx_count = count;
	return run;
}


struct page *
lx_alloc_pages(gfp_t gfp, unsigned int order)
{
	unsigned long count = 1UL << order;
	unsigned long long physical;
	unsigned long long max_address = 0;
	void *haiku_page;
	struct page_run *run;

	if (gfp & (__GFP_DMA32 | __GFP_DMA))
		max_address = 1ULL << 32;
	haiku_page = lx_haiku_pages_alloc(count, max_address,
		(gfp & __GFP_ZERO) != 0, &physical);
	if (haiku_page == NULL)
		return NULL;

	run = page_run_create(physical >> PAGE_SHIFT, count, haiku_page, order);
	if (run == NULL) {
		lx_haiku_pages_free(haiku_page, count);
		return NULL;
	}
	spin_lock(&page_runs_lock);
	page_run_insert(run);
	spin_unlock(&page_runs_lock);
	__atomic_add_fetch(&lx_pages_live, (long)count, __ATOMIC_RELAXED);
	return &run->pages[0];
}


void
lx_free_pages(struct page *page, unsigned int order)
{
	struct page_run *run;
	if (page == NULL)
		return;
	spin_lock(&page_runs_lock);
	run = page_run_find(page_to_pfn(page));
	if (run == NULL || run->haiku_page == NULL
		|| &run->pages[0] != page) {
		spin_unlock(&page_runs_lock);
		WARN(1, "freeing pages that were not allocated (pfn %lx)\n",
			page_to_pfn(page));
		return;
	}
	rb_erase(&run->node, &page_runs);
	spin_unlock(&page_runs_lock);
	__atomic_sub_fetch(&lx_pages_live, (long)run->count, __ATOMIC_RELAXED);
	lx_haiku_pages_free(run->haiku_page, run->count);
	kfree(run);
}


void
put_page(struct page *page)
{
	if (atomic_dec_and_test(&page->_refcount)) {
		struct page *head = page->lx_head != NULL ? page->lx_head : page;
		if (head == page && page->lx_haiku_page != NULL)
			lx_free_pages(page, page->lx_order);
	}
}


struct page *
lx_pfn_to_page(unsigned long pfn)
{
	struct page_run *run;
	spin_lock(&page_runs_lock);
	run = page_run_find(pfn);
	if (run == NULL) {
		/* a page we did not allocate: describe it from now on */
		spin_unlock(&page_runs_lock);
		run = page_run_create(pfn, 1, NULL, 0);
		if (run == NULL)
			return NULL;
		spin_lock(&page_runs_lock);
		if (page_run_find(pfn) == NULL)
			page_run_insert(run);
		else {
			kfree(run);
			run = page_run_find(pfn);
		}
	}
	spin_unlock(&page_runs_lock);
	return &run->pages[pfn - run->first_pfn];
}


phys_addr_t
lx_virt_to_phys(const volatile void *address)
{
	return lx_haiku_virt_to_phys((const void *)address);
}


unsigned long
__get_free_pages(gfp_t gfp, unsigned int order)
{
	struct page *page = lx_alloc_pages(gfp, order);
	return page != NULL ? (unsigned long)page_address(page) : 0;
}


void
free_pages(unsigned long address, unsigned int order)
{
	if (address != 0)
		lx_free_pages(virt_to_page((void *)address), order);
}


void *
alloc_pages_exact(size_t size, gfp_t gfp)
{
	return (void *)__get_free_pages(gfp, get_order(size));
}


void
free_pages_exact(void *virt, size_t size)
{
	free_pages((unsigned long)virt, get_order(size));
}


/* see set_pages_wc() */
void
lx_flush_virt_range(const void *address, unsigned long size)
{
	const char *line = (const char *)((unsigned long)address & ~63UL);
	const char *end = (const char *)address + size;
	for (; line < end; line += 64)
		__asm__ __volatile__("clflush %0" : "+m" (*(volatile char *)line));
	__asm__ __volatile__("mfence" ::: "memory");
}


void
lx_flush_page_range(struct page *page, unsigned long count)
{
	unsigned long i;
	for (i = 0; i < count; i++)
		lx_flush_virt_range(page_address(page + i), PAGE_SIZE);
}


void
lx_flush_page_array(struct page **pages, unsigned long count)
{
	unsigned long i;
	for (i = 0; i < count; i++)
		lx_flush_virt_range(page_address(pages[i]), PAGE_SIZE);
}


void *
vmap(struct page **pages, unsigned int count, unsigned long flags,
	pgprot_t prot)
{
	unsigned long long *physical;
	void *address;
	unsigned int i;
	int type = LX_HAIKU_MAP_WB;

	if ((pgprot_val(prot) & _PAGE_CACHE_MASK) == 0x18)
		type = LX_HAIKU_MAP_UC;
	else if (pgprot_val(prot) & 0x8)
		type = LX_HAIKU_MAP_WC;

	physical = kmalloc_array(count, sizeof(*physical), GFP_KERNEL);
	if (physical == NULL)
		return NULL;
	for (i = 0; i < count; i++)
		physical[i] = page_to_phys(pages[i]);
	address = lx_haiku_vmap(physical, count, type);
	kfree(physical);
	return address;
}


void
vunmap(const void *address)
{
	if (address != NULL)
		lx_haiku_vunmap((void *)address);
}


unsigned long
totalram_pages(void)
{
	return lx_haiku_total_pages();
}


void
si_meminfo(struct sysinfo *info)
{
	memset(info, 0, sizeof(*info));
	info->totalram = lx_haiku_total_pages();
	info->freeram = lx_haiku_free_pages();
	info->mem_unit = PAGE_SIZE;
}


struct cpuinfo_x86 boot_cpu_data;


static void
lx_cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
	__asm__ volatile("cpuid" : "=a" (*a), "=b" (*b), "=c" (*c), "=d" (*d)
		: "a" (leaf), "c" (0));
}


/*	boot_cpu_data from CPUID: amdgpu decides by the vendor whether the host
	takes part in PCIe link speed switching, and with a zeroed one (Intel)
	it gets the platform's link capabilities wrong. */
void lx_cpu_init(void);

void
lx_cpu_init(void)
{
	u32 a, b, c, d, family, model;
	lx_cpuid(0, &a, &b, &c, &d);
	if (b == 0x68747541 && d == 0x69746e65 && c == 0x444d4163)
		boot_cpu_data.x86_vendor = X86_VENDOR_AMD;		/* AuthenticAMD */
	else if (b == 0x6f677948 && d == 0x6e65476e && c == 0x656e6975)
		boot_cpu_data.x86_vendor = X86_VENDOR_HYGON;	/* HygonGenuine */
	else
		boot_cpu_data.x86_vendor = X86_VENDOR_INTEL;
	lx_cpuid(1, &a, &b, &c, &d);
	family = (a >> 8) & 0xf;
	model = (a >> 4) & 0xf;
	if (family == 0xf)
		family += (a >> 20) & 0xff;
	if (family >= 6)
		model |= ((a >> 16) & 0xf) << 4;
	boot_cpu_data.x86 = family;
	boot_cpu_data.x86_model = model;
	boot_cpu_data.x86_stepping = a & 0xf;
	boot_cpu_data.x86_clflush_size = ((b >> 8) & 0xff) * 8;
	boot_cpu_data.x86_cache_alignment = boot_cpu_data.x86_clflush_size;
}

bool
lx_cpu_has(int feature)
{
	return lx_haiku_cpu_has(feature);
}


int
lx_wbinvd_on_all_cpus(void)
{
	lx_haiku_wbinvd_all();
	return 0;
}


void
clflush_cache_range(void *address, unsigned int size)
{
	lx_haiku_clflush_range(address, size);
}


/* #pragma mark - DMA */


void *
dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
	gfp_t flag, unsigned long attrs)
{
	struct page *page = lx_alloc_pages(flag | __GFP_ZERO, get_order(size));
	if (page == NULL)
		return NULL;
	*dma_handle = page_to_phys(page);
	return page_address(page);
}


void
dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
	dma_addr_t dma_handle, unsigned long attrs)
{
	if (cpu_addr != NULL)
		lx_free_pages(virt_to_page(cpu_addr), get_order(size));
}


int
dma_map_sg_attrs(struct device *dev, struct scatterlist *sgl, int nents,
	enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;
	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = sg_phys(sg);
		sg_dma_len(sg) = sg->length;
	}
	return nents;
}


int
dma_map_sgtable(struct device *dev, struct sg_table *sgt,
	enum dma_data_direction dir, unsigned long attrs)
{
	int nents = dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
	sgt->nents = nents;
	return 0;
}


int
dma_set_mask(struct device *dev, u64 mask)
{
	dev->lx_dma_mask = mask;
	dev->dma_mask = &dev->lx_dma_mask;
	return 0;
}


int
dma_set_coherent_mask(struct device *dev, u64 mask)
{
	dev->coherent_dma_mask = mask;
	return 0;
}


u64
dma_get_mask(struct device *dev)
{
	return dev->dma_mask != NULL ? *dev->dma_mask : DMA_BIT_MASK(32);
}


/* #pragma mark - ioremap */


void __iomem *
lx_ioremap(phys_addr_t offset, size_t size, int type)
{
	return (void __iomem *)lx_haiku_map_physical(offset, size, type);
}


void
lx_iounmap(volatile void __iomem *address)
{
	if (address != NULL)
		lx_haiku_unmap_physical((void __force *)address);
}


struct resource iomem_resource = {
	.name = "PCI mem",
	.start = 0,
	.end = -1,
	.flags = IORESOURCE_MEM,
};


/* #pragma mark - teams' memory */


unsigned long
lx_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (lx_haiku_user_memcpy(to, (const void __force *)from, n) != 0) {
		memset(to, 0, n);
		return n;
	}
	return 0;
}


unsigned long
lx_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return lx_haiku_user_memcpy((void __force *)to, from, n) != 0 ? n : 0;
}


unsigned long
lx_clear_user(void __user *to, unsigned long n)
{
	static const char zeros[256];
	while (n > 0) {
		unsigned long chunk = min(n, sizeof(zeros));
		if (lx_haiku_user_memcpy((void __force *)to, zeros, chunk) != 0)
			return n;
		to = (char __user *)to + chunk;
		n -= chunk;
	}
	return 0;
}


long
strncpy_from_user(char *dst, const char __user *src, long count)
{
	long length = lx_haiku_user_strlcpy(dst, (const char __force *)src, count);
	if (length < 0)
		return -EFAULT;
	return min(length, count);
}


long
strnlen_user(const char __user *str, long n)
{
	char buffer[256];
	long length;
	if (n > (long)sizeof(buffer))
		n = sizeof(buffer);
	length = lx_haiku_user_strlcpy(buffer, (const char __force *)str, n);
	if (length < 0)
		return 0;
	return length + 1;
}


/* #pragma mark - things graphics memory does without */


struct file *
shmem_file_setup(const char *name, loff_t size, unsigned long flags)
{
	return ERR_PTR(-ENOSYS);
}


struct shrinker *
shrinker_alloc(unsigned int flags, const char *fmt, ...)
{
	return kzalloc(sizeof(struct shrinker), GFP_KERNEL);
}


void
shrinker_register(struct shrinker *shrinker)
{
}


void
shrinker_free(struct shrinker *shrinker)
{
	kfree(shrinker);
}
