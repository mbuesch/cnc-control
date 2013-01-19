# The architecture configuration
ARCH			= atmega8
AVRDUDE_ARCH		= m8
AVRDUDE			= avrdude
AVRDUDE_SPEED		= 1
AVRDUDE_SLOW_SPEED	= 100
PROGRAMMER		= avrisp2
PROGPORT		= usb

# The toolchain definitions
CC		= avr-gcc
OBJCOPY		= avr-objcopy
SIZE		= avr-size
SPARSE		= sparse
ADMIN_TOOL	= ../../driver/admin.py

V		= @		# Verbose build:  make V=1
C		= 0		# Sparsechecker build:  make C=1
Q		= $(V:1=)
QUIET_CC	= $(Q:@=@echo '     CC       '$@;)$(CC)
QUIET_DEPEND	= $(Q:@=@echo '     DEPEND   '$@;)$(CC)
QUIET_OBJCOPY	= $(Q:@=@echo '     OBJCOPY  '$@;)$(OBJCOPY)
QUIET_SIZE	= $(Q:@=@echo '     SIZE     '$@;)$(SIZE)
ifeq ($(C),1)
QUIET_SPARSE	= $(Q:@=@echo '     SPARSE   '$@;)$(SPARSE)
else
QUIET_SPARSE	= @true
endif

CFLAGS		= -mmcu=$(ARCH) -std=c99 -g -Os -Wall \
		  "-Dinline=inline __attribute__((__always_inline__))" \
		  -Wstack-usage=32 -Wcast-qual
SPARSEFLAGS	= $(CFLAGS) -I "/usr/lib/avr/include" -D__AVR_ARCH__=4 \
		  -D__AVR_ATmega8__=1 -D__ATTR_PROGMEM__="" -Dsignal=dllexport \
		  -Dexternally_visible=dllexport
LDFLAGS		=


# The fuse bits
LFUSE		= 0xA4
HFUSE		= 0xD8

# Application code
APP_SRCS	= main.c
APP_NAME	= application
APP_BIN		= $(APP_NAME).bin
APP_HEX		= $(APP_NAME).hex
APP_EEP		= $(APP_NAME).eep.hex
APP_LDFLAGS	=

# Bootloader code
BOOT_SRCS	= bootloader.c
BOOT_NAME	= bootloader
BOOT_BIN	= $(BOOT_NAME).bin
BOOT_HEX	= $(BOOT_NAME).hex
BOOT_OFFSET	= 0x1800
BOOT_LDFLAGS	= -Wl,--section-start=.text=$(BOOT_OFFSET)
CFLAGS		+= -DBOOT_OFFSET=$(BOOT_OFFSET)

ALL_SRCS	= $(APP_SRCS) $(BOOT_SRCS)
ALL_BIN		= $(APP_BIN) $(BOOT_BIN)
ALL_HEX		= $(APP_HEX) $(BOOT_HEX)
ALL_EEP		= $(APP_EEP)


.SUFFIXES:
.PHONY: all avrdude install_flash install_eeprom install boot-app reset writefuse clean distclean
.DEFAULT_GOAL := all

DEPS = $(sort $(patsubst %.c,dep/%.d,$(1)))
OBJS = $(sort $(patsubst %.c,obj/%.o,$(1)))

# Generate dependencies
$(call DEPS,$(ALL_SRCS)): dep/%.d: %.c 
	@mkdir -p $(dir $@)
	$(QUIET_DEPEND) -o $@.tmp -MM -MG -MT "$@ $(patsubst dep/%.d,obj/%.o,$@)" $(CFLAGS) $< && mv -f $@.tmp $@

-include $(call DEPS,$(ALL_SRCS))

# Generate object files
$(call OBJS,$(ALL_SRCS)): obj/%.o:
	@mkdir -p $(dir $@)
	$(QUIET_SPARSE) $(SPARSEFLAGS) $<
	$(QUIET_CC) -o $@ -c $(CFLAGS) $<

all: $(ALL_HEX)

%.s: %.c
	$(QUIET_CC) $(CFLAGS) -S $*.c

$(APP_BIN): $(call OBJS,$(APP_SRCS))
	$(QUIET_CC) $(CFLAGS) -o $(APP_BIN) $(call OBJS,$(APP_SRCS)) $(LDFLAGS) $(APP_LDFLAGS)

$(APP_HEX): $(APP_BIN)
	$(QUIET_OBJCOPY) -R.eeprom -O ihex $(APP_BIN) $(APP_HEX)
#	$(QUIET_OBJCOPY) -j.eeprom --set-section-flags=.eeprom="alloc,load" \
#			 --change-section-lma .eeprom=0 -O ihex $(APP_BIN) $(APP_EEP)
	$(QUIET_SIZE) $(APP_BIN)

$(BOOT_BIN): $(call OBJS,$(BOOT_SRCS))
	$(QUIET_CC) $(CFLAGS) -o $(BOOT_BIN) $(call OBJS,$(BOOT_SRCS)) $(LDFLAGS) $(BOOT_LDFLAGS)

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

install_eeprom: all
	$(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
	 -c $(PROGRAMMER) -P $(PROGPORT) -U eeprom:w:$(APP_EEP)

install: install_flash

boot-app: all
	$(ADMIN_TOOL) --enterboot
	$(ADMIN_TOOL) --flash-coproc $(APP_HEX)
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
	-rm -Rf obj dep *~ $(ALL_BIN)

distclean: clean
	-rm -f *.s $(ALL_HEX) $(ALL_EEP)
