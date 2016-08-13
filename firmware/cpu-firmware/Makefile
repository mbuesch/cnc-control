# Project name
NAME			:= cnc-control.cpu

# Project source files
SRCS			:= main.c 4094.c debug.c uart.c util.c lcd.c \
		  override.c machine_interface.c \
		  pdiusb.c usb.c spi.c

# Bootloader code
BOOT_SRCS		:= bootloader.c usb.c pdiusb.c util.c uart.c spi.c
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

STACKCHECK		:= 0	# Set to 1 to enable stack instrumentation

# Additional compiler flags
CFLAGS			:= -I..
BOOT_CFLAGS		:= -I..
LDFLAGS			:=
BOOT_LDFLAGS		:=

ifeq ($(STACKCHECK),1)
CFLAGS			+= -DSTACKCHECK=1 \
			   -finstrument-functions \
			   -finstrument-functions-exclude-file-list=.h
endif

# Additional "clean" and "distclean" target files
CLEAN_FILES		:= descriptor_table*.h
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

descriptor_table.h: descriptor_table.py descriptor_generator.py
	$(QUIET_PYTHON2) descriptor_table.py $(USB_VENDOR) $(USB_PRODUCT) > descriptor_table.h

descriptor_table_mini.h: descriptor_table_mini.py descriptor_generator.py
	$(QUIET_PYTHON2) descriptor_table_mini.py $(USB_VENDOR) $(USB_PRODUCT) > descriptor_table_mini.h

.PHONY: boot-app
