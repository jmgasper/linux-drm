/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIROS_ACCELERANT_H
#define AIROS_ACCELERANT_H

/*	air/OS's additions to <Accelerant.h> that amdgpu_kms uses, for building it
	against a Haiku release that does not have them (forced in with -include
	by haiku/tools/build-release.sh). The release's app_server never asks for
	these hooks: it drives the monitors as one screen through the classic
	ones. Copied from air/OS's headers/os/add-ons/graphics/Accelerant.h. */

#include <Accelerant.h>

/* display outputs: the monitors one device drives, where each one sits in
   the frame buffer, and how it is scaled */
enum {
	B_GET_DISPLAY_OUTPUT_COUNT = 0x600,
	B_GET_DISPLAY_OUTPUTS,
	B_GET_DISPLAY_OUTPUT_MODES,
	B_SET_DISPLAY_LAYOUT,
	B_SET_DISPLAY_CHANGE_PORT
};

/* display outputs */
#define B_DISPLAY_OUTPUT_VERSION		1
#define B_DISPLAY_OUTPUT_NAME_LENGTH	32
#define B_DISPLAY_OUTPUT_EDID_LENGTH	512

enum {
	B_DISPLAY_OUTPUT_CONNECTED	= 1 << 0,	/* a monitor is attached */
	B_DISPLAY_OUTPUT_ENABLED	= 1 << 1,	/* the output is being driven */
	B_DISPLAY_OUTPUT_SCALABLE	= 1 << 2,	/* the hardware scales this */
											/* output's frame buffer region */
											/* to the monitor's resolution */
	B_DISPLAY_OUTPUT_INTERNAL	= 1 << 3,	/* a built-in panel */
	B_DISPLAY_OUTPUT_MIRROR		= 1 << 4,	/* the output shows what another */
											/* one shows: the enabled output */
											/* without this flag whose region */
											/* starts at the same place. Its */
											/* scale fits that region to the */
											/* monitor; what is left over is */
											/* black. */
};

typedef struct {
	uint32			version;			/* B_DISPLAY_OUTPUT_VERSION */
	uint32			id;					/* identifies the connector; stable */
										/* for the life of the accelerant */
	char			name[B_DISPLAY_OUTPUT_NAME_LENGTH];	/* e.g. "DP-2" */
	uint32			flags;				/* B_DISPLAY_OUTPUT_* */
	int32			x;					/* the region of the frame buffer */
	int32			y;					/* this output shows, in frame */
	uint16			width;				/* buffer pixels */
	uint16			height;
	uint16			scale;				/* percent; the size of the picture */
										/* on the monitor relative to the */
										/* region's logical size */
	uint16			render_scale;		/* frame buffer pixels per logical */
										/* pixel, in percent: the region is */
										/* the logical size times this. Equal */
										/* to scale, the region is the */
										/* monitor's own size and nothing is */
										/* scaled (HiDPI rendering); smaller, */
										/* the monitor enlarges it */
	display_timing	native_timing;		/* the monitor's preferred timing */
	display_timing	timing;				/* the timing it is driven with */
	uint32			edid_length;		/* 0 when the monitor has no EDID */
	uint8			edid[B_DISPLAY_OUTPUT_EDID_LENGTH];
} display_output;

typedef struct {
	uint32			id;
	uint32			flags;				/* B_DISPLAY_OUTPUT_ENABLED, */
										/* B_DISPLAY_OUTPUT_MIRROR */
	int32			x;					/* position, in logical pixels */
	int32			y;
	uint16			scale;				/* percent, see display_output */
	uint16			render_scale;		/* see display_output; 0 means 100 */
	display_timing	timing;				/* all zero: the native timing */
} display_output_config;

#endif	/* AIROS_ACCELERANT_H */
