# Project name
NAME			:= cnc-control.coproc

# Project source files
SRCS			:= main.c
GEN_SRCS		:=

# Bootloader code
BOOT_SRCS		:= bootloader.c
BOOT_GEN_SRCS		:=
BOOT_OFFSET		:= 0x1800

# CPU speed, in Hz
F_CPU			:= 8000000UL

# Project fuse bits
LFUSE			:= 0xA4
HFUSE			:= 0xD8
EFUSE			:=

# Architecture configuration
GCC_ARCH		:= atmega8
AVRDUDE_ARCH		:= m8
FUNC_STACK_LIMIT	:= 32

# Programmer selection.
# Values can be:  avrisp2, mysmartusb
PROGRAMMER		:= avrisp2

# Instrumentation
INSTRUMENT_FUNC		:=
BOOT_INSTRUMENT_FUNC	:=

# Additional compiler flags
CFLAGS			:= -I..
LDFLAGS			:=
SPARSEFLAGS		:=
BOOT_CFLAGS		:= -I..
BOOT_LDFLAGS		:=
BOOT_SPARSEFLAGS	:=

# Additional "clean" and "distclean" target files
CLEAN_FILES		:=
DISTCLEAN_FILES		:=


include ../avrmakelib.mk


ADMIN_TOOL		:= ../../driver/admin.py

boot-app: all
	$(ADMIN_TOOL) --enterboot
	$(ADMIN_TOOL) --flash-coproc $(HEX)
	$(ADMIN_TOOL) --exitboot

.PHONY: boot-app
