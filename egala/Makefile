# Makefile for the ASUS Eee PC 900 "EGALA" WDM -> NT6.x (Windows 8.1) driver
# port, built with the open-source MinGW-w64 cross toolchain and the
# reactos/wdk-compatible headers shipped in ./include.
#
# Targets:
#   egala.sys  - kernel-mode display miniport replacement for Windows 8.1 x86
#   setup/     - .inf + cat-ready installation directory
#
# Usage:
#   make CROSS=i686-w64-mingw32-

CROSS ?= i686-w64-mingw32-
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres

CFLAGS  := -std=gnu99 -O2 -Wall -Wextra -D_X86_ -D_WIN32_WINNT=0x0603 \
           -DWINVER=0x0603 -DEGALA_PORT -fno-stack-protector \
           -Iinclude -I.
LDFLAGS := -nostdlib -Wl,-subsystem,native -Wl,--entry,GsDriverEntry \
           -Wl,--kill-at

OBJS := egala_pdd.o egala_disp.o egala_video.o egala_usb.o \
        ega_data.o ega_api.o ega_util.o stubs.o entry.o

all: egala.sys

egala.sys: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lntoskrmlib

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) egala.sys

.PHONY: all clean
