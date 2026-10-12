/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AMDGPU_GLUE_H
#define AMDGPU_GLUE_H

/*	Between the Haiku driver (driver.cpp, Haiku headers) and the Linux side
	(glue.c, Linux headers): plain C types only. Results are 0 or negative
	Linux error numbers. */

#ifdef __cplusplus
extern "C" {
#endif

int lx_glue_set_param(const char *name, const char *value);
int lx_glue_init(void);
int lx_glue_probe(int bus, int device, int function);
void lx_glue_uninit(void);

int lx_glue_minors(int *minors, int max);
int lx_glue_minor_location(int minor, int *bus, int *device, int *function);

void *lx_glue_open(int minor, int flags, int *error);
void lx_glue_close(void *file);
long lx_glue_ioctl(void *file, unsigned int cmd, void *arg);
long lx_glue_read(void *file, void *buffer, unsigned long length);
void *lx_glue_select(void *file, void (*notify)(void *cookie, unsigned int events),
	void *cookie, unsigned int *ready);
void lx_glue_deselect(void *select);

void *lx_glue_map(void *file, unsigned long long offset, unsigned long size,
	int writable, unsigned long **pfns, int *caching);
void lx_glue_unmap(void *mapping);
int lx_glue_is_error(const void *pointer);
int lx_glue_selftest(void);
int lx_glue_fake_device_start(void);
int lx_glue_lab_bench(void *file);
int lx_glue_wait_hotplug(unsigned int *generation, unsigned int timeout_ms);
int lx_glue_lab_perf_level(void *file, unsigned int level);

#ifdef __cplusplus
}
#endif

#endif /* AMDGPU_GLUE_H */
