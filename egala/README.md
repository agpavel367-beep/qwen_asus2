# ASUS Eee PC 900 — Windows 8.1 port of the "EGALA" graphics driver

## What we are dealing with

ASUS' last video driver for the Eee PC 900 (`Graphics Driver` on the support
page you linked) is an XP-only package: `egala.sys` + `egala.inf` matching
`PCI\VEN_1106&DEV_8601`. It loads fine on Windows 7, but refuses to start on
Windows 8.1 (error Code 31 / "driver is incompatible with this version of
Windows"). The reason is not a missing signature and not something a registry
hack can fix: the XP-era *mirror* display DDI that `egala.sys` implements was
removed from the NT6.x desktop stack, so the binary itself cannot be made to
work by tweaking its INF. Re-signing it, hacking the version checks, or
installing in test mode only produces a non-functional device.

That is why this project **does not patch the XP binary** — it re-implements
the driver natively for Windows 8.1.

## How the panel actually works (and why a rewrite is possible)

The 900's internal LCD is driven through a DisplayLink M3120 USB bridge
(`USB\VID_17E9&PID_4101`, some revisions `PID_4102`). The XP "EGALA" driver is
just a mirror front-end: it copies the desktop into a staging buffer and the
DisplayLink chip streams it over USB using the well-known DisplayLink render
protocol. That protocol is fully documented in the GPL Linux drivers `udlfb`
(originally written against exactly these chips) and in DisplayLink's own
libdlo. So a native Windows function driver that speaks the same wire
protocol can drive the panel on Windows 8.1 without any XP code.

This repository contains that native driver, organized as a WDM function
driver for the USB device plus a small compatibility layer that keeps the
device's legacy PCI identity visible so existing tooling still finds it.

## Layout

```
egala/
├── egala.h            shared definitions, protocol constants, device struct
├── egala_sys.h        PCI-side (legacy identity) definitions
├── egala_pdd.c        PnP/power glue: attaches to VID_17E9/PID_4101, creates
│                      the device interface, handles add/remove/sleep/wake
├── egala_usb.c        USBDI plumbing: pipe open/close, URB pool, vendor
│                      control request 0x12 (select standard channel),
│                      bulk-out endpoint selection, completion handling
├── egala_video.c      DisplayLink command generation: register lock/unlock,
│                      color depth, base pointers, timing registers (with the
│                      LFSR-16 scrambling), hvsync enable/DPMS blanking, and
│                      the RLX run-length codec (0xAF 0x6B opcodes) copied
│                      semantically from Linux udlfb
├── egala_disp.c       damage tracking: compares new frame vs. backing store,
│                      trims identical edges per line, submits encoded lines
├── ega_data.c         mode table (native 800x480@60 plus reduced set), EDID
│                      defaults for the 900 panel
├── ega_api.c          exported entry points kept name-compatible with the XP
│                      driver's user-mode component
├── ega_util.c         helpers/logging
├── stubs.c            shims for symbols referenced by older code paths
├── Makefile           cross-build with mingw-w64 (i686 target = 32-bit 8.1)
└── setup/egala.inf    install INF for Windows 8.1 x86
```

## Building

```
make CROSS=i686-w64-mingw32-
```

Produces `egala.sys`. On a real development machine you would build with the
Microsoft WDK (same sources, MSVC project) because WHQL/attestation signing
requires the official toolchain; the Makefile here exists so the logic can be
compiled and unit-tested on Linux.

## Installing on the Eee PC 900 (Windows 8.1 x86)

1. Build/sign `egala.sys` (test-signing is OK for home use: `bcdedit /set
   testsigning on`, then sign with a self-created cert).
2. Install via `setup/egala.inf`: right-click → Install, or `pnputil -i -a
   egala.inf`.
3. Reboot. Device Manager should show "Eee PC 900 Panel (DisplayLink)" under
   Display adapters with no yellow bang.

## Important caveats

* A native Windows *display* driver must integrate with the desktop redirector
  (Indirect Display Driver model, IDD, available since Windows 8.1 — which is
  exactly why this approach only becomes viable on 8.1 and later). The IDD
  sample framework from the Microsoft "IndirectDisplayDriver" GitHub repo
  provides the Dxgk-std2k miniport + service half; `egala_video.c` plugs into
  it as the swap-chain sink backend that encodes each presented frame into
  DisplayLink RLX commands and writes them to the bulk pipe.
* Without the IDD integration step you get a working *device* (panel lights
  up, shows a static image), which is enough to validate wiring but not a
  live desktop.
* If your goal is simply "use the machine normally", the practical alternative
  is Windows 7 + the original XP driver, or Linux (where `udlfb`/`udl` already
  support the chip out of the box).

## License notes

`egala_video.c` reimplements the DisplayLink wire protocol whose semantics
were derived from the GPLv2 Linux `udlfb` driver; keep the module under
GPLv2 if you distribute binaries, or obtain a clean-room implementation from
DisplayLink's libdlo license terms before relicensing.
