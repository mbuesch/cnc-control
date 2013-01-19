# The architecture configuration
ARCH			= atmega32
AVRDUDE_ARCH		= m32
AVRDUDE			= avrdude
AVRDUDE_SPEED		= 1
AVRDUDE_SLOW_SPEED	= 10
PROGRAMMER		= avrisp2
PROGPORT		= usb

# USB ID configuration (pdiusb)
USB_VENDOR		= 0x6666
USB_PRODUCT		= 0xC8CC

# The toolchain definitions
CC		= avr-gcc
OBJCOPY		= avr-objcopy
SIZE		= avr-size
PYTHON		= python
SPARSE		= sparse
ADMIN_TOOL	= ../../driver/admin.py

DEBUG		= 0		# Debug build:  make DEBUG=1
COMBINE		= 0		# Set to 1 to enable whole program optimization
STACKCHECK	= 0		# Set to 1 to enable stack instrumentation

V		= @		# Verbose build:  make V=1
C		= 0		# Sparsechecker build:  make C=1
Q		= $(V:1=)
QUIET_CC	= $(Q:@=@echo '     CC       '$@;)$(CC)
QUIET_DEPEND	= $(Q:@=@echo '     DEPEND   '$@;)$(CC)
QUIET_OBJCOPY	= $(Q:@=@echo '     OBJCOPY  '$@;)$(OBJCOPY)
QUIET_SIZE	= $(Q:@=@echo '     SIZE     '$@;)$(SIZE)
QUIET_PYTHON	= $(Q:@=@echo '     PYTHON   '$<;)$(PYTHON)
ifeq ($(C),1)
QUIET_SPARSE	= $(Q:@=@echo '     SPARSE   '$@;)$(SPARSE)
else
QUIET_SPARSE	= @true
endif

CFLAGS		= -mmcu=$(ARCH) -std=gnu99 -g -Os -Wall -DDEBUG=$(DEBUG) \
		  "-Dinline=inline __attribute__((__always_inline__)) \
		   __attribute__((__no_instrument_function__))" \
		  -Wstack-usage=32 -Wcast-qual \
		  -D__EMBEDDED__ -I..
COMBINE_CFLAGS	= -fwhole-program --combine
SPARSEFLAGS	= $(CFLAGS) -I "/usr/lib/avr/include" -D__AVR_ARCH__=5 \
		  -D__AVR_ATmega32__=1 -D__ATTR_PROGMEM__="" -Dsignal=dllexport \
		  -Dexternally_visible=dllexport
LDFLAGS		=

ifeq ($(STACKCHECK),1)
CFLAGS		+= -DSTACKCHECK=1 \
		   -finstrument-functions \
		   -finstrument-functions-exclude-file-list=.h
endif

# The fuse bits
LFUSE	= 0xA0
HFUSE	= 0xD8

# CPU freq
F_CPU	:= 16000000
CFLAGS	+= -DF_CPU=$(F_CPU)

# Application code
APP_SRCS	= main.c 4094.c debug.c uart.c util.c lcd.c \
		  override.c machine_interface.c \
		  pdiusb.c usb.c spi.c
APP_NAME	= application
APP_BIN		= $(APP_NAME).bin
APP_HEX		= $(APP_NAME).hex
APP_EEP		= $(APP_NAME).eep.hex
APP_CFLAGS	= -DIN_APP
APP_LDFLAGS	=

# Bootloader code
BOOT_SRCS	= bootloader.c usb.c pdiusb.c util.c uart.c spi.c
BOOT_NAME	= bootloader
BOOT_BIN	= $(BOOT_NAME).bin
BOOT_HEX	= $(BOOT_NAME).hex
BOOT_OFFSET	= 0x7000
BOOT_LDFLAGS	= -Wl,--section-start=.text=$(BOOT_OFFSET)
BOOT_CFLAGS	= -DIN_BOOT
CFLAGS		+= -DBOOT_OFFSET=$(BOOT_OFFSET)

ALL_SRCS	= $(APP_SRCS) $(BOOT_SRCS)
ALL_BIN		= $(APP_BIN) $(BOOT_BIN)
ALL_HEX		= $(APP_HEX) $(BOOT_HEX)
ALL_EEP		= $(APP_EEP)


.SUFFIXES:
.PHONY: all avrdude install_flash install_boot install_eeprom install boot-app reset writefuse clean distclean
.DEFAULT_GOAL := all

DEPS = $(sort $(patsubst %.c,dep-$(2)/%.d,$(1)))
OBJS = $(sort $(patsubst %.c,obj-$(2)/%.o,$(1)))

# Generate dependencies
$(call DEPS,$(APP_SRCS),app): dep-app/%.d: %.c 
	@mkdir -p $(dir $@)
	$(QUIET_DEPEND) -o $@.tmp -MM -MG -MT "$@ $(patsubst dep-app/%.d,obj-app/%.o,$@)" \
		$(CFLAGS) $(APP_CFLAGS) $< && mv -f $@.tmp $@

$(call DEPS,$(BOOT_SRCS),boot): dep-boot/%.d: %.c 
	@mkdir -p $(dir $@)
	$(QUIET_DEPEND) -o $@.tmp -MM -MG -MT "$@ $(patsubst dep-boot/%.d,obj-boot/%.o,$@)" \
		$(CFLAGS) $(BOOT_CFLAGS) $< && mv -f $@.tmp $@

-include $(call DEPS,$(APP_SRCS),app)
-include $(call DEPS,$(BOOT_SRCS),boot)

# Generate object files
$(call OBJS,$(APP_SRCS),app): obj-app/%.o:
	@mkdir -p $(dir $@)
	$(QUIET_SPARSE) $(SPARSEFLAGS) $(APP_CFLAGS) $<
	$(QUIET_CC) -o $@ -c $(CFLAGS) $(APP_CFLAGS) $<

$(call OBJS,$(BOOT_SRCS),boot): obj-boot/%.o:
	@mkdir -p $(dir $@)
	$(QUIET_SPARSE) $(SPARSEFLAGS) $(BOOT_CFLAGS) $<
	$(QUIET_CC) -o $@ -c $(CFLAGS) $(BOOT_CFLAGS) $<

all: $(ALL_HEX)

descriptor_table.h: descriptor_table.py descriptor_generator.py
	$(QUIET_PYTHON) descriptor_table.py $(USB_VENDOR) $(USB_PRODUCT) > descriptor_table.h

descriptor_table_mini.h: descriptor_table_mini.py descriptor_generator.py
	$(QUIET_PYTHON) descriptor_table_mini.py $(USB_VENDOR) $(USB_PRODUCT) > descriptor_table_mini.h

%.s: %.c
	$(QUIET_CC) $(CFLAGS) -S $*.c

ifeq ($(strip $(COMBINE)),0)
$(APP_BIN): $(call OBJS,$(APP_SRCS),app)
	$(QUIET_CC) $(CFLAGS) $(APP_CFLAGS) -o $(APP_BIN) \
		$(call OBJS,$(APP_SRCS),app) $(LDFLAGS) $(APP_LDFLAGS)
else
$(APP_BIN): $(APP_SRCS) $(call DEPS,$(APP_SRCS),app)
	$(QUIET_CC) $(CFLAGS) $(APP_CFLAGS) $(COMBINE_CFLAGS) -o $(APP_BIN) \
		$(APP_SRCS) $(LDFLAGS) $(APP_LDFLAGS)
endif

ifeq ($(strip $(COMBINE)),0)
$(BOOT_BIN): $(call OBJS,$(BOOT_SRCS),boot)
	$(QUIET_CC) $(CFLAGS) $(BOOT_CFLAGS) -o $(BOOT_BIN) \
		$(call OBJS,$(BOOT_SRCS),boot) $(LDFLAGS) $(BOOT_LDFLAGS)
else
$(BOOT_BIN): $(BOOT_SRCS) $(call DEPS,$(BOOT_SRCS),boot)
	$(QUIET_CC) $(CFLAGS) $(BOOT_CFLAGS) $(COMBINE_CFLAGS) -o $(BOOT_BIN) \
		$(BOOT_SRCS) $(LDFLAGS) $(BOOT_LDFLAGS)
endif

$(APP_HEX): $(APP_BIN)
	$(QUIET_OBJCOPY) -R.eeprom -O ihex $(APP_BIN) $(APP_HEX)
#	$(QUIET_OBJCOPY) -j.eeprom --set-section-flags=.eeprom="alloc,load" \
#			 --change-section-lma .eeprom=0 -O ihex $(APP_BIN) $(APP_EEP)
	$(QUIET_SIZE) $(APP_BIN)

$(BOOT_HEX): $(BOOT_BIN)
	$(QUIET_OBJCOPY) -R.eeprom -O ihex $(BOOT_BIN) $(BOOT_HEX)
	$(QUIET_SIZE) $(BOOT_BIN)

avrdude:
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) -t

install_flash: all
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) \
	 -U flash:w:$(BOOT_HEX) -U flash:w:$(APP_HEX)

install_boot: all
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) \
	 -U flash:w:$(BOOT_HEX)

install_eeprom: all
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) \
	 -U eeprom:w:$(APP_EEP)

install: install_flash

boot-app: all
	$(ADMIN_TOOL) --enterboot
	$(ADMIN_TOOL) --flash-cpu $(APP_HEX)
	$(ADMIN_TOOL) --exitboot

# Reset the microcontroller through avrdude
reset:
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) \
	 -U signature:r:/dev/null:i -q -q

writefuse:
	$(AVRDUDE) -B $(AVRDUDE_SLOW_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) -q -q \
	 -U lfuse:w:$(LFUSE):m \
	 -U hfuse:w:$(HFUSE):m

clean:
	-rm -Rf dep-* obj-* *~ *.pyc $(ALL_BIN) descriptor_table*.h

distclean: clean
	-rm -f *.s $(ALL_HEX) $(ALL_EEP)
