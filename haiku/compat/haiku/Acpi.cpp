/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	ACPI firmware tables of lx/haiku.h. */

#include <ACPI.h>
#include <KernelExport.h>

#include "../include/lx/haiku.h"


static const int kLinuxENODEV = 19;

static acpi_module_info* sACPI = NULL;


static acpi_module_info*
acpi_module()
{
	if (sACPI == NULL) {
		if (get_module(B_ACPI_MODULE_NAME, (module_info**)&sACPI) != B_OK)
			sACPI = NULL;
	}
	return sACPI;
}


extern "C" int
lx_haiku_acpi_get_table(const char* signature, unsigned int instance,
	void** table)
{
	acpi_module_info* acpi = acpi_module();
	if (acpi == NULL || acpi->get_table(signature, instance, table) != B_OK)
		return -kLinuxENODEV;
	return 0;
}
