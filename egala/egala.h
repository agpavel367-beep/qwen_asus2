/*
 * egala.h - ASUS Eee PC 900 "EGALA" display driver, Windows 8.1 port
 *
 * This is the private header for the NT6.x (Windows Vista/7/8/8.1) rewrite of
 * the last XP-only video driver that ASUS shipped for the Eee PC 900.
 *
 * Background / why this works at all:
 *   The Eee PC 900 has an Intel 945GSE chipset whose graphics are driven on
 *   XP by a mirror/system driver ("EGALA", PCI VEN_1106&DEV_8601).  That XP
 *   binary cannot load on Windows 8.1 because WDM display drivers written
 *   against the XP DDI were removed from the NT6.x desktop stack.
 *   However, the actual pixels on the 900's internal panel are pushed to the
 *   panel through a DisplayLink-style path that speaks the well-known
 *   DisplayLink USB render protocol (device ULi/DisplayLink M3120,
 *   USB VID_17E9&PID_4101 on the 900; some revisions report PID_4102).
 *   Because that protocol is fully documented in the GPL-licensed Linux
 *   udlfb driver, we can re-implement it natively as a Windows 8.1 kernel
 *   function driver and drive the panel without any XP code at all.
 *
 * Layout of the port:
 *   egala_pdd.c   - PnP / power / WDF glue, device creation, INF matching
 *   egala_disp.c  - frame-rate pacing, damage queue, blit engine
 *   egala_video.c - DisplayLink command generation (registers + RLX codec)
 *   egala_usb.c   - USBDI pipe management, URB pool, vendor requests
 *   ega_data.c    - mode table (800x480 native etc.), color formats
 *   ega_api.c     - exported miniport-like entry points (DrvInit, DrvEnable,
 *                   DrvSetMode ...) mirroring the XP driver's exports so the
 *                   user-mode component keeps working unchanged
 *   ega_util.c    - shared helpers (checksums, logging, memory tags)
 *   stubs.c       - compatibility shims for symbols the XP build referenced
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0-or-later WITH Windows-driver-note
 */

#ifndef _EGALA_H_
#define _EGALA_H_

#include <ntddk.h>
#include <usb.h>        /* usb100.h / usbdi.h style definitions */
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>

/* ------------------------------------------------------------------ */
/* Identity                                                            */
/* ------------------------------------------------------------------ */

#define EGALA_VENDOR_ID        0x1106   /* ASUSTeK      (PCI, XP-era)   */
#define EGALA_DEVICE_ID        0x8601   /* EGALA system device          */
#define DLINK_VENDOR_ID        0x17E9   /* DisplayLink  (USB, real HW)  */
#define DLINK_PID_M3120_A      0x4101   /* as shipped on Eee PC 900     */
#define DLINK_PID_M3120_B      0x4102   /* later revision               */

#define EGALA_TAG              'LAGE'   /* pool tag 'EAGL' little-endian*/

/* ------------------------------------------------------------------ */
/* DisplayLink USB protocol constants (from Linux udlfb, GPLv2)        */
/* ------------------------------------------------------------------ */

#define NR_USB_REQUEST_CHANNEL         0x12
#define NR_USB_REQUEST_I2C_SUB_IO      0x02

#define BPP                            2
#define MAX_CMD_PIXELS                 255
#define WRITES_IN_FLIGHT               4
#define MAX_TRANSFER                   (PAGE_SIZE * 16 - 512)
#define GET_URB_TIMEOUT_MS             1000
#define FREE_URB_TIMEOUT_MS            2000

/* ------------------------------------------------------------------ */
/* Modes                                                               */
/* ------------------------------------------------------------------ */

typedef struct _EGALA_MODE {
	ULONG XRes;
	ULONG YRes;
	ULONG RefreshRate;      /* Hz                      */
	ULONG PixClockKHz;      /* pixel clock, kHz       */
	ULONG LeftMargin;
	ULONG RightMargin;
	ULONG UpperMargin;
	ULONG LowerMargin;
	ULONG HSyncLen;
	ULONG VSyncLen;
	ULONG SyncFlags;
	ULONG VModeFlags;
} EGALA_MODE, *PEGALA_MODE;

#define EGALA_MAX_MODES 16

typedef struct _EGALA_DEV {
	PDEVICE_OBJECT        Pdo;
	PDEVICE_OBJECT        Fdo;
	USBD_HANDLE           UsbdHandle;
	PURB                WriteUrb[WRITES_IN_FLIGHT];
	PVOID               WriteBuffer[WRITES_IN_FLIGHT];
	DMA_ADDRESS         WriteDma[WRITES_IN_FLIGHT];
	LIST_ENTRY          FreeUrbs;
	KSPIN_LOCK          UrbLock;
	KEVENT              UrbEvent;
	LONG                AvailableUrbs;

	/* current mode / framebuffer geometry */
	EGALA_MODE          Mode;
	ULONG             BytesPerPixel;
	ULONG             PitchBytes;
	ULONG             FrameSizeBytes;
	PVOID               BackingStore;   /* shadow copy of what HW shows */
	PVOID               FrontBuffer;    /* client-visible surface       */

	/* state */
	BOOLEAN             DevicePresent;
	BOOLEAN             HwEnabled;
	ULONG               StdChannel;
	KEVENT              StopEvent;
	KTIMER              PaceTimer;
	WORK_QUEUE_ITEM     PaceWorkItem;
	WORK_QUEUE_ITEM     CompletionWorkItem;
} EGALA_DEV, *PEGALA_DEV;

/* ------------------------------------------------------------------ */
/* Cross-file prototypes                                               */
/* ------------------------------------------------------------------ */

/* egala_usb.c */
NTSTATUS EgUsbClaimPipes(PDEVICE_OBJECT Fido, PEGALA_DEV Dev);
VOID     EgUsbReleasePipes(PEGALA_DEV Dev);
NTSTATUS EgUsbSelectStdChannel(PEGALA_DEV Dev);
NTSTATUS EgUsbVendorRequest(PEGALA_DEV Dev, UCHAR Request, PVOID Buf, ULONG Len);
struct urb_pool_entry *EgUsbGetUrb(PEGALA_DEV Dev);
VOID     EgUsbSubmitUrb(PEGALA_DEV Dev, PURB Urb, ULONG Length);
VOID     EgUsbReleaseUrb(PEGALA_DEV Dev, PURB Urb);

/* egala_video.c */
NTSTATUS EgVideoSetMode(PEGALA_DEV Dev, PEGALA_MODE Mode);
VOID     EgVideoBlank(PEGALA_DEV Dev, BOOLEAN On);
ULONG    EgRenderDamageLines(PEGALA_DEV Dev, PUCHAR Src, ULONG OffsetBytes,
                             ULONG WidthBytes, PUCHAR CmdBuf, ULONG CmdCap);

/* egala_disp.c */
NTSTATUS EgDispStart(PEGALA_DEV Dev);
VOID     EgDispStop(PEGALA_DEV Dev);
VOID     EgDispFlushDirtyRegion(PEGALA_DEV Dev, ULONG X, ULONG Y, ULONG W, ULONG H);

/* ega_data.c */
extern const EGALA_MODE EgalaModes[];
extern const ULONG      EgalaModeCount;

/* ega_api.c */
BOOLEAN DrvInit(PVOID Context);
BOOLEAN DrvEnable(PVOID Context, ULONG Flags);
BOOLEAN DrvSetMode(PVOID Context, ULONG ModeIdx);
VOID    DrvDisable(PVOID Context);

#endif /* _EGALA_H_ */
