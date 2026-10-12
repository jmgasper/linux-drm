/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	The Linux side of the Haiku driver: starting the DRM core and amdgpu,
	probing the GPUs, and the operations on DRM files the device hooks of
	driver.cpp forward. Everything here is called with plain C types
	(glue.h). */

#include <lx/haiku.h>
#include <lx/vma.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_vma_manager.h>

#include "glue.h"

#ifndef DRM_MAJOR
#	define DRM_MAJOR 226
#endif


/* module initialization functions (lx/device.h's module_init()) */
int lx_module_init_dma_buf_init(void);
int lx_module_init_drm_core_init(void);
int lx_module_init_gpu_buddy_module_init(void);
int lx_module_init_drm_display_helper_module_init(void);
int lx_module_init_drm_sched_fence_slab_init(void);
int lx_module_init_amdgpu_init(void);
void lx_module_exit_amdgpu_exit(void);
void lx_module_exit_drm_sched_fence_slab_fini(void);
void lx_module_exit_drm_display_helper_module_exit(void);
void lx_module_exit_gpu_buddy_module_exit(void);
void lx_module_exit_drm_core_exit(void);

int lx_workqueue_init(void);
void lx_workqueue_exit(void);
void lx_rcu_exit(void);
void lx_timer_exit(void);
struct pci_dev *lx_pci_dev_get(int bus, int device, int function);
struct pci_driver *lx_pci_registered_driver(void);
const struct pci_device_id *lx_pci_match(struct pci_driver *driver,
	struct pci_dev *dev);

extern const struct lx_param __start_lx_params[];
extern const struct lx_param __stop_lx_params[];

#define MAX_GPUS 8
static struct pci_dev *probed[MAX_GPUS];
static int probed_count;
static struct drm_device *fake_drm;


/* the DRM devices: the probed GPUs, and the self test's fake one */
static int
drm_devices(struct drm_device **devices, int max)
{
	int count = 0, i;
	for (i = 0; i < probed_count && count < max; i++) {
		struct drm_device *dev = pci_get_drvdata(probed[i]);
		if (dev != NULL)
			devices[count++] = dev;
	}
	if (fake_drm != NULL && count < max)
		devices[count++] = fake_drm;
	return count;
}


/* #pragma mark - parameters */


int
lx_glue_set_param(const char *name, const char *value)
{
	const struct lx_param *param;
	for (param = __start_lx_params; param < __stop_lx_params; param++) {
		int error = -EINVAL;
		if (strcmp(param->name, name) != 0)
			continue;
		if (!strcmp(param->type, "int") || !strcmp(param->type, "hexint"))
			error = kstrtoint(value, 0, param->value);
		else if (!strcmp(param->type, "uint"))
			error = kstrtouint(value, 0, param->value);
		else if (!strcmp(param->type, "long"))
			error = kstrtol(value, 0, param->value);
		else if (!strcmp(param->type, "ulong"))
			error = kstrtoul(value, 0, param->value);
		else if (!strcmp(param->type, "ullong"))
			error = kstrtoull(value, 0, param->value);
		else if (!strcmp(param->type, "bool"))
			error = kstrtobool(value, param->value);
		else if (!strcmp(param->type, "charp")) {
			char *copy = kstrdup(value, GFP_KERNEL);
			if (copy != NULL) {
				*(char **)param->value = copy;
				error = 0;
			}
		}
		pr_info("amdgpu: parameter %s = %s%s\n", name, value,
			error != 0 ? " (not accepted)" : "");
		return error;
	}
	pr_info("amdgpu: no parameter %s\n", name);
	return -ENOENT;
}


/* #pragma mark - start and stop */


int
lx_glue_init(void)
{
	int error;

	error = lx_workqueue_init();
	if (error != 0)
		return error;

	/* dma-buf needs a pseudo file system Haiku does not have: exporting
	   buffers fails, the rest works */
	lx_module_init_dma_buf_init();

	error = lx_module_init_drm_core_init();
	if (error != 0)
		goto err_workqueues;
	error = lx_module_init_gpu_buddy_module_init();
	if (error != 0)
		goto err_drm;
	error = lx_module_init_drm_display_helper_module_init();
	if (error != 0)
		goto err_buddy;
	error = lx_module_init_drm_sched_fence_slab_init();
	if (error != 0)
		goto err_display;
	error = lx_module_init_amdgpu_init();
	if (error != 0)
		goto err_sched;
	return 0;

err_sched:
	lx_module_exit_drm_sched_fence_slab_fini();
err_display:
	lx_module_exit_drm_display_helper_module_exit();
err_buddy:
	lx_module_exit_gpu_buddy_module_exit();
err_drm:
	lx_module_exit_drm_core_exit();
err_workqueues:
	lx_workqueue_exit();
	return error;
}


int
lx_glue_probe(int bus, int device, int function)
{
	struct pci_driver *driver = lx_pci_registered_driver();
	const struct pci_device_id *id;
	struct pci_dev *pdev;
	int error;

	if (driver == NULL || probed_count >= MAX_GPUS)
		return -ENODEV;
	pdev = lx_pci_dev_get(bus, device, function);
	if (pdev == NULL)
		return -ENODEV;
	id = lx_pci_match(driver, pdev);
	if (id == NULL)
		return -ENODEV;

	pr_info("amdgpu: probing %s (%04x:%04x rev %02x)\n", pci_name(pdev),
		pdev->vendor, pdev->device, pdev->revision);
	pdev->driver = driver;
	error = driver->probe(pdev, id);
	if (error != 0) {
		pr_err("amdgpu: probing %s failed: %d\n", pci_name(pdev), error);
		/* as Linux's driver core: what the probe left managed goes */
		devres_release_all(&pdev->dev);
		pdev->driver = NULL;
		return error;
	}
	probed[probed_count++] = pdev;
	return 0;
}


void lx_glue_fake_device_stop(void);

void
lx_glue_uninit(void)
{
	lx_glue_fake_device_stop();
	while (probed_count > 0) {
		struct pci_dev *pdev = probed[--probed_count];
		if (pdev->driver != NULL && pdev->driver->remove != NULL)
			pdev->driver->remove(pdev);
		devres_release_all(&pdev->dev);
		pdev->driver = NULL;
	}
	lx_module_exit_amdgpu_exit();
	lx_module_exit_drm_sched_fence_slab_fini();
	lx_module_exit_drm_display_helper_module_exit();
	lx_module_exit_gpu_buddy_module_exit();
	lx_module_exit_drm_core_exit();
	lx_workqueue_exit();
	lx_rcu_exit();
	lx_timer_exit();
	/* no code of this image may run after it is unloaded */
	lx_haiku_set_task_exit_hook(NULL);
	lx_haiku_log_file(NULL);
}


/* #pragma mark - devices */


/* the DRM minors to publish: "dri/card0", "dri/renderD128", ... */
int
lx_glue_minors(int *minors, int max)
{
	struct drm_device *devices[MAX_GPUS + 1];
	int deviceCount = drm_devices(devices, MAX_GPUS + 1);
	int count = 0, i;
	for (i = 0; i < deviceCount; i++) {
		struct drm_device *dev = devices[i];
		if (dev->primary != NULL && count < max)
			minors[count++] = dev->primary->index;
		if (dev->render != NULL && count < max)
			minors[count++] = dev->render->index;
	}
	return count;
}


/* the PCI location of the GPU a minor belongs to */
int
lx_glue_minor_location(int minor, int *bus, int *device, int *function)
{
	int i;
	for (i = 0; i < probed_count; i++) {
		struct drm_device *dev = pci_get_drvdata(probed[i]);
		if (dev == NULL)
			continue;
		if ((dev->primary != NULL && dev->primary->index == minor)
			|| (dev->render != NULL && dev->render->index == minor)) {
			*bus = probed[i]->bus->number;
			*device = PCI_SLOT(probed[i]->devfn);
			*function = PCI_FUNC(probed[i]->devfn);
			return 0;
		}
	}
	return -ENODEV;
}


static struct drm_device *
drm_device_of_minor(int minor)
{
	struct drm_device *devices[MAX_GPUS + 1];
	int count = drm_devices(devices, MAX_GPUS + 1), i;
	for (i = 0; i < count; i++) {
		struct drm_device *dev = devices[i];
		if ((dev->primary != NULL && dev->primary->index == minor)
			|| (dev->render != NULL && dev->render->index == minor))
			return dev;
	}
	return NULL;
}


void *
lx_glue_open(int minor, int flags, int *_error)
{
	struct drm_device *dev = drm_device_of_minor(minor);
	struct file *filp;
	struct inode *inode;
	int error;

	if (dev == NULL) {
		*_error = -ENODEV;
		return NULL;
	}

	filp = kzalloc(sizeof(*filp), GFP_KERNEL);
	inode = kzalloc(sizeof(*inode), GFP_KERNEL);
	if (filp == NULL || inode == NULL) {
		kfree(filp);
		kfree(inode);
		*_error = -ENOMEM;
		return NULL;
	}
	inode->i_rdev = MKDEV(DRM_MAJOR, minor);
	inode->i_mapping = &inode->i_data;
	atomic_set(&inode->i_count, 1);
	atomic_long_set(&filp->f_count, 1);
	spin_lock_init(&filp->f_lock);
	mutex_init(&filp->f_pos_lock);
	filp->f_flags = flags;
	filp->f_mode = FMODE_READ | FMODE_WRITE;
	filp->f_inode = inode;
	filp->f_op = dev->driver->fops;
	filp->lx_owner = current;

	error = filp->f_op->open(inode, filp);
	if (error != 0) {
		kfree(filp);
		kfree(inode);
		*_error = error;
		return NULL;
	}
	*_error = 0;
	return filp;
}


void
lx_glue_close(void *file)
{
	fput(file);
}


long
lx_glue_ioctl(void *_file, unsigned int cmd, void *arg)
{
	struct file *filp = _file;
	if (filp->f_op->unlocked_ioctl == NULL)
		return -ENOTTY;
	return filp->f_op->unlocked_ioctl(filp, cmd, (unsigned long)arg);
}


long
lx_glue_read(void *_file, void *buffer, unsigned long length)
{
	struct file *filp = _file;
	loff_t pos = 0;
	if (filp->f_op->read == NULL)
		return -EINVAL;
	return filp->f_op->read(filp, buffer, length, &pos);
}


/* #pragma mark - select */


struct lx_select {
	struct wait_queue_entry wait;
	struct wait_queue_head *head;
	void (*notify)(void *cookie, unsigned int events);
	void *cookie;
	poll_table table;
};


static int
lx_select_wake(struct wait_queue_entry *wait, unsigned mode, int sync,
	void *key)
{
	struct lx_select *select = container_of(wait, struct lx_select, wait);
	unsigned int events = (unsigned int)(uintptr_t)key;
	select->notify(select->cookie, events != 0 ? events : POLLIN);
	return 0;
}


static void
lx_select_queue(struct file *filp, struct wait_queue_head *head, poll_table *p)
{
	struct lx_select *select = container_of(p, struct lx_select, table);
	if (select->head != NULL)
		return;
	select->head = head;
	init_waitqueue_func_entry(&select->wait, lx_select_wake);
	add_wait_queue(head, &select->wait);
}


/* starts watching the file; returns the events ready now */
void *
lx_glue_select(void *_file, void (*notify)(void *cookie, unsigned int events),
	void *cookie, unsigned int *ready)
{
	struct file *filp = _file;
	struct lx_select *select = kzalloc(sizeof(*select), GFP_KERNEL);
	if (select == NULL)
		return NULL;
	select->notify = notify;
	select->cookie = cookie;
	select->table._qproc = lx_select_queue;
	select->table._key = ~0;
	*ready = filp->f_op->poll != NULL ? filp->f_op->poll(filp, &select->table)
		: 0;
	return select;
}


void
lx_glue_deselect(void *_select)
{
	struct lx_select *select = _select;
	if (select == NULL)
		return;
	if (select->head != NULL)
		remove_wait_queue(select->head, &select->wait);
	kfree(select);
}


/* #pragma mark - mapping buffers */


struct lx_mapping {
	struct vm_area_struct vma;
	struct lx_vma_pages *pages;
};


/*	Looks up the buffer at a fake offset (DRM_IOCTL_*_GEM_MMAP), runs its
	fault handler for every page and returns the page frames and caching;
	the caller maps them. lx_glue_unmap() ends the mapping. */
void *
lx_glue_map(void *_file, unsigned long long offset, unsigned long size,
	int writable, unsigned long **_pfns, int *_caching)
{
	struct file *filp = _file;
	struct lx_mapping *mapping;
	unsigned long count = size >> PAGE_SHIFT, i;
	int error;

	if (size == 0 || (size & ~PAGE_MASK) != 0 || (offset & ~PAGE_MASK) != 0)
		return ERR_PTR(-EINVAL);
	if (filp->f_op->mmap == NULL)
		return ERR_PTR(-ENODEV);

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (mapping == NULL)
		return ERR_PTR(-ENOMEM);
	mapping->pages = kvzalloc(struct_size(mapping->pages, pfns, count),
		GFP_KERNEL);
	if (mapping->pages == NULL) {
		kfree(mapping);
		return ERR_PTR(-ENOMEM);
	}
	mapping->pages->count = count;

	/* any page aligned address: only offsets within the area count */
	mapping->vma.vm_start = 0x100000000UL;
	mapping->vma.vm_end = mapping->vma.vm_start + size;
	mapping->vma.vm_pgoff = offset >> PAGE_SHIFT;
	mapping->vma.vm_file = filp;
	mapping->vma.vm_flags = VM_READ | VM_SHARED | VM_MAYREAD | VM_MAYSHARE
		| (writable ? VM_WRITE | VM_MAYWRITE : 0);
	mapping->vma.vm_page_prot = PAGE_SHARED;

	error = filp->f_op->mmap(filp, &mapping->vma);
	if (error != 0)
		goto err;
	if (mapping->vma.vm_ops == NULL || mapping->vma.vm_ops->fault == NULL) {
		error = -ENODEV;
		goto err_close;
	}

	mapping->vma.lx_haiku = mapping->pages;
	for (i = 0; i < count; i++) {
		struct vm_fault vmf = {};
		vm_fault_t result;
		if (mapping->pages->pfns[i] != 0)
			continue;
		vmf.vma = &mapping->vma;
		vmf.address = mapping->vma.vm_start + i * PAGE_SIZE;
		vmf.pgoff = mapping->vma.vm_pgoff + i;
		vmf.flags = writable ? FAULT_FLAG_WRITE : 0;
		vmf.gfp_mask = GFP_KERNEL;
		result = mapping->vma.vm_ops->fault(&vmf);
		if ((result & VM_FAULT_ERROR) || mapping->pages->pfns[i] == 0) {
			error = -EFAULT;
			goto err_close;
		}
	}
	mapping->vma.lx_haiku = NULL;

	switch (mapping->pages->prot & _PAGE_CACHE_MASK) {
		case 0x18:
			*_caching = LX_HAIKU_MAP_UC;
			break;
		case 0x8:
			*_caching = LX_HAIKU_MAP_WC;
			break;
		default:
			*_caching = LX_HAIKU_MAP_WB;
			break;
	}
	*_pfns = mapping->pages->pfns;
	return mapping;

err_close:
	if (mapping->vma.vm_ops != NULL && mapping->vma.vm_ops->close != NULL)
		mapping->vma.vm_ops->close(&mapping->vma);
err:
	kvfree(mapping->pages);
	kfree(mapping);
	return ERR_PTR(error);
}


void
lx_glue_unmap(void *_mapping)
{
	struct lx_mapping *mapping = _mapping;
	if (mapping->vma.vm_ops != NULL && mapping->vma.vm_ops->close != NULL)
		mapping->vma.vm_ops->close(&mapping->vma);
	kvfree(mapping->pages);
	kfree(mapping);
}


int
lx_glue_is_error(const void *pointer)
{
	return IS_ERR(pointer) ? PTR_ERR(pointer) : 0;
}


/* #pragma mark - self test */

/*	Exercises the compat layer without a GPU (settings: "haiku_selftest 1"),
	for virtual machines and before trusting a new build with the hardware. */

static struct completion selftest_done;
static atomic_t selftest_counter;

static int
selftest_thread(void *data)
{
	atomic_inc(&selftest_counter);
	complete(&selftest_done);
	while (!kthread_should_stop())
		schedule_timeout_interruptible(msecs_to_jiffies(10));
	return 42;
}


static void
selftest_work(struct work_struct *work)
{
	atomic_inc(&selftest_counter);
}


static void
selftest_timer(struct timer_list *timer)
{
	atomic_inc(&selftest_counter);
}


static enum hrtimer_restart
selftest_hrtimer(struct hrtimer *timer)
{
	atomic_inc(&selftest_counter);
	return HRTIMER_NORESTART;
}


static void
selftest_rcu(struct rcu_head *head)
{
	atomic_inc(&selftest_counter);
}


#define SELFTEST(condition, what) \
	do { \
		if (!(condition)) { \
			pr_err("amdgpu selftest: FAILED: %s\n", what); \
			failures++; \
		} else \
			pr_info("amdgpu selftest: ok: %s\n", what); \
	} while (0)

int
lx_glue_selftest(void)
{
	static DEFINE_WW_CLASS(selftest_ww_class);
	struct task_struct *thread;
	struct work_struct work;
	struct delayed_work dwork;
	struct timer_list timer;
	struct hrtimer hrtimer;
	struct rcu_head rcu;
	struct xarray xa;
	struct idr idr;
	struct ww_mutex a, b;
	struct ww_acquire_ctx ctx;
	struct page *page, *pages[4];
	char buffer[128];
	void *p, *mapped;
	u32 id;
	int failures = 0, i, result;

	snprintf(buffer, sizeof(buffer), "%d %u %x %s %pe %03d", -5, 7u, 0xbeef,
		"str", ERR_PTR(-EINVAL), 9);
	SELFTEST(!strcmp(buffer, "-5 7 beef str -EINVAL 009"), buffer);

	p = kmalloc(100, GFP_KERNEL);
	SELFTEST(p != NULL && ksize(p) == 100, "kmalloc and ksize");
	p = krealloc(p, 5000, GFP_KERNEL | __GFP_ZERO);
	SELFTEST(p != NULL && ((char *)p)[4999] == 0, "krealloc with zeroing");
	kfree(p);
	p = kmalloc(4096, GFP_KERNEL);
	SELFTEST(p != NULL && ((uintptr_t)p & 4095) == 0, "page sized kmalloc aligned");
	kfree(p);
	p = vzalloc(1 << 20);
	SELFTEST(p != NULL && is_vmalloc_addr(p), "vzalloc");
	kvfree(p);

	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, 2);
	SELFTEST(page != NULL && pfn_to_page(page_to_pfn(page) + 3) == page + 3,
		"alloc_pages order 2 and pfn_to_page");
	if (page != NULL) {
		memset(page_address(page), 0x5a, 4 * PAGE_SIZE);
		for (i = 0; i < 4; i++)
			pages[i] = page + 3 - i;
		mapped = vmap(pages, 4, VM_MAP, PAGE_KERNEL);
		SELFTEST(mapped != NULL && ((u8 *)mapped)[3 * PAGE_SIZE + 17] == 0x5a,
			"vmap");
		if (mapped != NULL)
			vunmap(mapped);
		__free_pages(page, 2);
	}

	xa_init_flags(&xa, XA_FLAGS_ALLOC1);
	result = xa_alloc(&xa, &id, &xa, xa_limit_32b, GFP_KERNEL);
	SELFTEST(result == 0 && id == 1 && xa_load(&xa, 1) == &xa, "xa_alloc");
	xa_lock(&xa);
	SELFTEST(xa_load(&xa, 1) == &xa, "xa_load with xa_lock held");
	xa_unlock(&xa);
	xa_erase(&xa, 1);
	SELFTEST(xa_empty(&xa), "xa_erase");
	xa_destroy(&xa);
	idr_init_base(&idr, 1);
	result = idr_alloc(&idr, &idr, 1, 0, GFP_KERNEL);
	SELFTEST(result == 1 && idr_find(&idr, 1) == &idr, "idr_alloc");
	idr_destroy(&idr);

	ww_mutex_init(&a, &selftest_ww_class);
	ww_mutex_init(&b, &selftest_ww_class);
	ww_acquire_init(&ctx, &selftest_ww_class);
	SELFTEST(ww_mutex_lock(&a, &ctx) == 0 && ww_mutex_lock(&b, &ctx) == 0
		&& ww_mutex_lock(&a, &ctx) == -EALREADY, "ww_mutex");
	ww_mutex_unlock(&b);
	ww_mutex_unlock(&a);
	ww_acquire_fini(&ctx);

	atomic_set(&selftest_counter, 0);
	init_completion(&selftest_done);
	thread = kthread_run(selftest_thread, NULL, "selftest");
	SELFTEST(!IS_ERR(thread) && wait_for_completion_timeout(&selftest_done,
		msecs_to_jiffies(2000)) != 0, "kthread start and completion");
	if (!IS_ERR(thread))
		SELFTEST(kthread_stop(thread) == 42, "kthread_stop");

	INIT_WORK(&work, selftest_work);
	queue_work(system_wq, &work);
	flush_work(&work);
	SELFTEST(atomic_read(&selftest_counter) == 2, "work and flush_work");

	INIT_DELAYED_WORK(&dwork, (work_func_t)selftest_work);
	schedule_delayed_work(&dwork, msecs_to_jiffies(20));
	msleep(100);
	SELFTEST(atomic_read(&selftest_counter) == 3, "delayed work");
	cancel_delayed_work_sync(&dwork);

	timer_setup(&timer, selftest_timer, 0);
	mod_timer(&timer, jiffies + msecs_to_jiffies(10));
	msleep(60);
	SELFTEST(atomic_read(&selftest_counter) == 4, "timer_list");
	timer_delete_sync(&timer);

	hrtimer_setup(&hrtimer, selftest_hrtimer, CLOCK_MONOTONIC,
		HRTIMER_MODE_REL);
	hrtimer_start(&hrtimer, ms_to_ktime(5), HRTIMER_MODE_REL);
	msleep(50);
	SELFTEST(atomic_read(&selftest_counter) == 5, "hrtimer");
	hrtimer_cancel(&hrtimer);

	call_rcu(&rcu, selftest_rcu);
	rcu_barrier();
	SELFTEST(atomic_read(&selftest_counter) == 6, "call_rcu and rcu_barrier");

	pr_info("amdgpu selftest: %d failure(s)\n", failures);
	return failures;
}


/* #pragma mark - fake device */

/*	A DRM device without hardware ("haiku_selftest 2"), so that opening
	/dev/dri nodes, ioctls and closing can be tried in a virtual machine. */

static const struct file_operations fake_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
	.mmap = drm_gem_mmap,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

static const struct drm_driver fake_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER,
	.fops = &fake_fops,
	.name = "haiku_fake",
	.desc = "Haiku compat test device",
	.major = 1,
	.minor = 0,
};

static struct device fake_parent;


int
lx_glue_fake_device_start(void)
{
	int error;
	device_initialize(&fake_parent);
	dev_set_name(&fake_parent, "haiku-fake");
	fake_drm = drm_dev_alloc(&fake_driver, &fake_parent);
	if (IS_ERR(fake_drm)) {
		error = PTR_ERR(fake_drm);
		fake_drm = NULL;
		return error;
	}
	error = drm_dev_register(fake_drm, 0);
	if (error != 0) {
		drm_dev_put(fake_drm);
		fake_drm = NULL;
		return error;
	}
	pr_info("amdgpu selftest: fake DRM device registered\n");
	return 0;
}


void
lx_glue_fake_device_stop(void)
{
	if (fake_drm == NULL)
		return;
	drm_dev_unregister(fake_drm);
	drm_dev_put(fake_drm);
	fake_drm = NULL;
}
