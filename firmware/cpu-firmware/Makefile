STACKCHECK		:=	# Set to 1 to enable stack instrumentation

# Project name
NAME			:= cnc-control.cpu

# Project source files
SRCS			:= main.c 4094.c debug.c uart.c util.c lcd.c \
			   override.c machine_interface.c \
			   pdiusb.c usb.c spi.c
GEN_SRCS		:= descriptor_table.h

# Bootloader source files
BOOT_SRCS		:= bootloader.c usb.c pdiusb.c util.c uart.c spi.c
BOOT_GEN_SRCS		:= descriptor_table_mini.h
BOOT_OFFSET		:= 0x7000

# CPU speed, in Hz
F_CPU			:= 16000000UL

# Project fuse bits
LFUSE			:= 0xA0
HFUSE			:= 0xD8
EFUSE			:=

# Architecture configuration
GCC_ARCH		:= atmega32
AVRDUDE_ARCH		:= m32
FUNC_STACK_LIMIT	:= 32

# Programmer selection.
# Values can be:  avrisp2, mysmartusb
PROGRAMMER		:= avrisp2

# Instrumentation
INSTRUMENT_FUNC		:= $(if $(STACKCHECK),1)
BOOT_INSTRUMENT_FUNC	:=

# Additional compiler flags
CFLAGS			:= -I.. $(if $(STACKCHECK),-DSTACKCHECK)
LDFLAGS			:=
SPARSEFLAGS		:= -Wno-address-space
BOOT_CFLAGS		:= -I..
BOOT_LDFLAGS		:=
BOOT_SPARSEFLAGS	:= -Wno-address-space

# Additional "clean" and "distclean" target files
CLEAN_FILES		:=
DISTCLEAN_FILES		:=


include ../avrmakelib.mk


# USB ID configuration (pdiusb)
USB_VENDOR		= 0x6666
USB_PRODUCT		= 0xC8CC

ADMIN_TOOL		:= ../../driver/admin.py

boot-app: all
	$(ADMIN_TOOL) --enterboot
	$(ADMIN_TOOL) --flash-cpu $(HEX)
	$(ADMIN_TOOL) --exitboot

$(GEN_SRCS) $(BOOT_GEN_SRCS): %.h: %.py descriptor_generator.py
	$(QUIET_PYTHON2) $< $(USB_VENDOR) $(USB_PRODUCT) > $@

.PHONY: boot-app
