/*
 * egala_video.c - DisplayLink (M3120) command generation for the Eee PC 900
 * panel. Windows 8.1 port of the pixel path used by the XP "EGALA" driver.
 *
 * The wire protocol implemented here is the one documented by the Linux udlfb
 * driver (GPLv2). We reproduce its semantics, not its code: every function
 * builds a byte buffer that is later handed to EgUsbSubmitUrb().
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "egala.h"

/* Saved start pointer of the command buffer currently being built, used to
 * compute how many bytes were emitted. */
static PUCHAR BufBase;

/* ------------------------------------------------------------------ */
/* Low-level register writes                                          */
/* ------------------------------------------------------------------ */

/* All DisplayLink bulk operations start with 0xAF followed by an opcode. */
static PUCHAR DlSetRegister(PUCHAR Buf, UCHAR Reg, UCHAR Val)
{
	*Buf++ = 0xAF;   /* render op: set register          */
	*Buf++ = 0x20;   /* sub-opcode                       */
	*Buf++ = Reg;
	*Buf++ = Val;
	return Buf;
}

static PUCHAR DlSetRegister16(PUCHAR Buf, UCHAR Reg, USHORT Value)
{
	Buf = DlSetRegister(Buf, Reg, (UCHAR)(Value >> 8));
	Buf = DlSetRegister(Buf, Reg + 1, (UCHAR)Value);
	return Buf;
}

/* Some controller registers take values in the opposite byte order. */
static PUCHAR DlSetRegister16BE(PUCHAR Buf, UCHAR Reg, USHORT Value)
{
	Buf = DlSetRegister(Buf, Reg, (UCHAR)Value);
	Buf = DlSetRegister(Buf, Reg + 1, (UCHAR)(Value >> 8));
	return Buf;
}

/*
 * The display controller minimizes clock depth of its counters by running
 * them through a 16-bit LFSR. To program an "actual count" we must write the
 * scrambled value obtained by walking the LFSR backwards from 0xFFFF.
 */
static USHORT DlLfsr16(USHORT ActualCount)
{
	ULONG Lv = 0xFFFF;

	while (ActualCount--) {
		Lv = ((Lv << 1) |
		      (((Lv >> 15) ^ (Lv >> 4) ^ (Lv >> 2) ^ (Lv >> 1)) & 1))
		     & 0xFFFF;
	}
	return (USHORT)Lv;
}

static PUCHAR DlSetRegisterLfsr16(PUCHAR Buf, UCHAR Reg, USHORT Value)
{
	return DlSetRegister16(Buf, Reg, DlLfsr16(Value));
}

/* Video register space lock/unlock (must bracket any mode change). */
static PUCHAR DlVidRegLock(PUCHAR Buf)
{
	return DlSetRegister(Buf, 0xFF, 0x00);
}

static PUCHAR DlVidRegUnlock(PUCHAR Buf)
{
	return DlSetRegister(Buf, 0xFF, 0xFF);
}

/*
 * H/V sync on/off:
 *   0x00 -> syncs on (display driven)
 *   0x07 -> DPMS powerdown (needs a modeset to come back)
 */
static PUCHAR DlEnableHvSync(PUCHAR Buf, BOOLEAN Enable)
{
	return DlSetRegister(Buf, 0x1F, Enable ? 0x00 : 0x07);
}

static PUCHAR DlSetColorDepth(PUCHAR Buf, UCHAR Selection)
{
	return DlSetRegister(Buf, 0x00, Selection);
}

/* Base pointer of the 16bpp framebuffer segment (24-bit address). */
static PUCHAR DlSetBase16bpp(PUCHAR Buf, ULONG Base)
{
	Buf = DlSetRegister(Buf, 0x20, (UCHAR)(Base >> 16));
	Buf = DlSetRegister(Buf, 0x21, (UCHAR)(Base >> 8));
	Buf = DlSetRegister(Buf, 0x22, (UCHAR)Base);
	return Buf;
}

/* Base pointer of the 8bpp segment (used only in 24bpp visual modes). */
static PUCHAR DlSetBase8bpp(PUCHAR Buf, ULONG Base)
{
	Buf = DlSetRegister(Buf, 0x26, (UCHAR)(Base >> 16));
	Buf = DlSetRegister(Buf, 0x27, (UCHAR)(Base >> 8));
	Buf = DlSetRegister(Buf, 0x28, (UCHAR)Base);
	return Buf;
}

/* ------------------------------------------------------------------ */
/* Mode programming                                                   */
/* ------------------------------------------------------------------ */

static PUCHAR DlSetVideoCmds(PUCHAR Buf, PEGALA_MODE M)
{
	USHORT Xds, Xde, Yds, Yde, Yec;
	ULONG  Pclk5k;

	Xds = (USHORT)(M->LeftMargin + M->HSyncLen);
	Buf = DlSetRegisterLfsr16(Buf, 0x01, Xds);           /* x display start   */
	Xde = (USHORT)(Xds + M->XRes);
	Buf = DlSetRegisterLfsr16(Buf, 0x03, Xde);           /* x display end     */

	Yds = (USHORT)(M->UpperMargin + M->VSyncLen);
	Buf = DlSetRegisterLfsr16(Buf, 0x05, Yds);           /* y display start   */
	Yde = (USHORT)(Yds + M->YRes);
	Buf = DlSetRegisterLfsr16(Buf, 0x07, Yde);           /* y display end     */

	Buf = DlSetRegisterLfsr16(Buf, 0x09,                 /* x end count       */
	                          (USHORT)(Xde + M->RightMargin - 1));
	Buf = DlSetRegisterLfsr16(Buf, 0x0B, 1);             /* hsync start (fixed*/
	Buf = DlSetRegisterLfsr16(Buf, 0x0D,                 /* hsync end         */
	                          (USHORT)(M->HSyncLen + 1));
	Buf = DlSetRegister16(Buf, 0x0F, (USHORT)M->XRes);   /* hpixels (active)  */

	Yec = (USHORT)(M->YRes + M->UpperMargin + M->LowerMargin + M->VSyncLen);
	Buf = DlSetRegisterLfsr16(Buf, 0x11, Yec);           /* y end count       */
	Buf = DlSetRegisterLfsr16(Buf, 0x13, 0);             /* vsync start (fixed*/
	Buf = DlSetRegisterLfsr16(Buf, 0x15,                 /* vsync end         */
	                          (USHORT)M->VSyncLen);
	Buf = DlSetRegister16(Buf, 0x17, (USHORT)M->YRes);   /* vpixels (active)  */

	/* pixclock kHz -> period in units of 5kHz: pclk5k = 2e8 / khz */
	Pclk5k = (ULONG)(200000000UL / M->PixClockKHz);
	Buf = DlSetRegister16BE(Buf, 0x1B, (USHORT)Pclk5k);

	return Buf;
}

NTSTATUS
EgVideoSetMode(PEGALA_DEV Dev, PEGALA_MODE Mode)
{
	PUCHAR Buf;
	ULONG  Used;
	NTSTATUS Status;

	if (!Dev->HwEnabled || !Mode)
		return STATUS_DEVICE_NOT_READY;

	Dev->Mode = *Mode;
	Dev->BytesPerPixel = BPP;
	Dev->PitchBytes    = Mode->XRes * BPP;
	Dev->FrameSizeBytes = Dev->PitchBytes * Mode->YRes;

	Buf = ExAllocatePoolWithTag(NonPagedPool, 256, EGALA_TAG);
	if (!Buf)
		return STATUS_NO_MEMORY;

	Buf = DlVidRegLock(Buf);
	Buf = DlSetColorDepth(Buf, 0x00);            /* 16bpp RGB565 */
	Buf = DlSetBase16bpp(Buf, 0);
	Buf = DlSetBase8bpp(Buf, Dev->FrameSizeBytes);
	Buf = DlSetVideoCmds(Buf, Mode);
	Buf = DlEnableHvSync(Buf, TRUE);
	Buf = DlVidRegUnlock(Buf);

	Used = (ULONG)(Buf - (PUCHAR)Buf_Base(Buf)); /* see note below */

	Status = EgUsbVendorWrite(Dev, Buf, Used);
	ExFreePoolWithTag(Buf, EGALA_TAG);
	return Status;
}

VOID
EgVideoBlank(PEGALA_DEV Dev, BOOLEAN On)
{
	PUCHAR Buf;
	ULONG  Used;

	if (!Dev->HwEnabled)
		return;

	Buf = ExAllocatePoolWithTag(NonPagedPool, 32, EGALA_TAG);
	if (!Buf)
		return;

	Buf = DlVidRegLock(Buf);
	Buf = DlEnableHvSync(Buf, !On);
	Buf = DlVidRegUnlock(Buf);

	Used = (ULONG)((PUCHAR)Buf - (PUCHAR)NULL); /* placeholder, see note */
	EgUsbVendorWrite(Dev, Buf, Used);
	ExFreePoolWithTag(Buf, EGALA_TAG);
}

/* ------------------------------------------------------------------ */
/* RLX codec (opcode 0xAF 0x6B): raw spans + repeat runs per line      */
/* ------------------------------------------------------------------ */

#define MIN_RLX_CMD_BYTES   11

ULONG
EgRenderDamageLines(PEGALA_DEV Dev, PUCHAR Src, ULONG OffsetBytes,
                    ULONG WidthBytes, PUCHAR CmdBuf, ULONG CmdCap)
{
	USHORT UNALIGNED *Pixel;
	USHORT UNALIGNED *PixelEnd;
	PUCHAR  Cmd = CmdBuf;
	PUCHAR  CmdEnd = CmdBuf + CmdCap;
	ULONG   DevAddr = OffsetBytes;
	ULONG  Out = 0;

	Pixel = (USHORT UNALIGNED *)(Src + OffsetBytes);
	PixelEnd = Pixel + WidthBytes / BPP;

	while (Pixel < PixelEnd && (CmdEnd - Cmd) > MIN_RLX_CMD_BYTES) {
		PUCHAR RawCountByte;
		USHORT UNALIGNED *RawStart;
		USHORT UNALIGNED *CmdStart, *Run;

		*Cmd++ = 0xAF;
		*Cmd++ = 0x6B;
		*Cmd++ = (UCHAR)((DevAddr >> 16) & 0xFF);
		*Cmd++ = (UCHAR)((DevAddr >> 8) & 0xFF);
		*Cmd++ = (UCHAR)(DevAddr & 0xFF);

		RawCountByte = Cmd++;   /* filled once we know the span */
		CmdStart = Pixel;
		RawStart = Pixel;

		/* emit at most one command's worth of pixels */
		{
			ULONG Room = (ULONG)(CmdEnd - Cmd) / BPP;
			ULONG N = min(MAX_CMD_PIXELS + 1,
			            min((ULONG)(PixelEnd - Pixel), Room));
			USHORT UNALIGNED *Stop = Pixel + N;

			while (Pixel < Stop) {
				USHORT Val = *Pixel++;
				*(USHORT UNALIGNED *)Cmd = RtlUshortByteSwap(Val);
				Cmd += 2;

				if (Pixel < Stop && *Pixel == Val) {
					/* close current raw span */
					*RawCountByte =
						(UCHAR)(((Pixel - RawStart - 1) + 1) & 0xFF);
					RawStart = Pixel;

					while (Pixel < Stop && *Pixel == Val)
						Pixel++;

					/* repeat count immediately after raw data */
					*Cmd++ = (UCHAR)(((Pixel - RawStart) - 1) & 0xFF);
					RawCountByte = Cmd++;  /* next raw span header */
					RawStart = Pixel;
				}
			}
		}

		if (Pixel > RawStart)
			*RawCountByte = (UCHAR)((Pixel - RawStart) & 0xFF);

		DevAddr += (ULONG)(Pixel - CmdStart) * BPP;
	}

	Out = (ULONG)(Cmd - CmdBuf);
	return Out;
}
