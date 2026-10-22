/*
 * egala_sys.h - shared definitions for the Eee PC 900 "EGALA" system driver
 *
 * Copyright (c) 2026
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * The driver attaches to the PCI device "ASUSTeK EGALA virtual display"
 * (vendor 0x1106, device 0x8601, class 0x0300xx) that Windows XP reports on
 * the ASUS Eee PC 900.  On Linux this device does not exist in hardware: it
 * is a software-only artifact created by the XP driver so that the OS has
 * something to bind the graphics stack to.  The real framebuffer lives in
 * stolen system memory owned by the Intel 945GSE chipset, and panel modes /
 * backlight are controlled through the ACPI WMI interface exposed by the
 * Eee firmware (class "PNP0C14", GUID 97C6F4A4-...-D03A755574).
 *
 * This module therefore:
 *   - claims the EGALA PCI function (so nothing else touches it),
 *   - maps the LFB aperture reported by BAR0/BAR2,
 *   - exports a sysfs interface (/sys/class/egala/egalaN) that lets user
 *     space query the mode and drive the panel,
 *   - forwards brightness requests to the Eee WMI ACPI method.
 */

#ifndef _EGALA_SYS_H_
#define _EGALA_SYS_H_

#include <linux/types.h>

#define EGALA_DRIVER_NAME	"egala"
#define EGALA_CLASS_NAME	"egala"

/* PCI identity reported by the XP driver's INF file */
#define EGALA_PCI_VENDOR_ID	0x1106	/* ASUSTeK Computer Inc. */
#define EGALA_PCI_DEVICE_ID	0x8601	/* EGALA virtual display */

/*
 * Eee PC 900 panel, from the EDID / XP default mode: 800x480, 33 MHz pixel
 * clock, blanking values taken from the VESA GTF reduced-blanking formula
 * (the same numbers the XP inf hard-codes into the mirror driver).
 */
struct egala_mode {
	u32 xres;
	u32 yres;
	u32 refresh;		/* Hz, 0 = unspecified */
	u32 pixclock_khz;	/* pixel clock in kHz */
	u32 left_margin;
	u32 right_margin;
	u32 upper_margin;
	u32 lower_margin;
	u32 hsync_len;
	u32 vsync_len;
	u32 sync;		/* FB_SYNC_* style flags */
	u32 vmode;		/* FB_VMODE_* style flags */
};

#define EGALA_MODE_MAX		16

#endif /* _EGALA_SYS_H_ */
