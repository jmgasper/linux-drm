/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	ACPI firmware tables of lx/haiku.h. */

#include <ACPI.h>
#include <KernelExport.h>

#include "../include/lx/haiku.h"


static const int kLinuxENODEV = 19;


extern "C" int
lx_haiku_acpi_get_table(const char* signature, unsigned int instance,
	void** table)
{
	// Only looked up while a GPU starts: no reference kept in between. The
	// ACPI module keeps its tables mapped.
	acpi_module_info* acpi;
	if (get_module(B_ACPI_MODULE_NAME, (module_info**)&acpi) != B_OK)
		return -kLinuxENODEV;

	status_t status = acpi->get_table(signature, instance, table);
	put_module(B_ACPI_MODULE_NAME);
	return status == B_OK ? 0 : -kLinuxENODEV;
}
