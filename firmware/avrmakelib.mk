######################################################
# AVR make library                                   #
# Copyright (c) 2015-2016 Michael Buesch <m@bues.ch> #
# Version 1.4                                        #
######################################################

ifeq ($(NAME),)
$(error NAME not defined)
endif
ifeq ($(SRCS),)
$(error SRCS not defined)
endif
ifeq ($(F_CPU),)
$(error F_CPU not defined)
endif
ifeq ($(GCC_ARCH),)
$(error GCC_ARCH not defined)
endif
ifeq ($(AVRDUDE_ARCH),)
$(error AVRDUDE_ARCH not defined)
endif

# The toolchain definitions
CC			:= avr-gcc
OBJCOPY			:= avr-objcopy
OBJDUMP			:= avr-objdump
SIZE			:= avr-size
MKDIR			:= mkdir
MV			:= mv
RM			:= rm
CP			:= cp
ECHO			:= echo
GREP			:= grep
TRUE			:= true
TEST			:= test
AVRDUDE			:= avrdude
MYSMARTUSB		:= mysmartusb.py
DOXYGEN			:= doxygen
PYTHON2			:= python2
PYTHON3			:= python3

V			:= @		# Verbose build:  make V=1
DEBUG			:= 0		# Debug build:  make DEBUG=1
O			:= s		# Optimize flag
Q			:= $(V:1=)
QUIET_CC		= $(Q:@=@$(ECHO) '     CC       '$@;)$(CC)
QUIET_DEPEND		= $(Q:@=@$(ECHO) '     DEPEND   '$@;)$(CC)
QUIET_OBJCOPY		= $(Q:@=@$(ECHO) '     OBJCOPY  '$@;)$(OBJCOPY)
QUIET_SIZE		= $(Q:@=@$(ECHO) '     SIZE     '$@;)$(SIZE)
QUIET_PYTHON2		= $(Q:@=@$(ECHO) '     PYTHON2  '$@;)$(PYTHON2)
QUIET_PYTHON3		= $(Q:@=@$(ECHO) '     PYTHON3  '$@;)$(PYTHON3)

FUNC_STACK_LIMIT	?= 128

WARN_CFLAGS		:= -Wall -Wextra -Wno-unused-parameter -Wswitch-enum \
			   -Wsuggest-attribute=noreturn \
			   -Wundef -Wpointer-arith -Winline \
			   $(if $(FUNC_STACK_LIMIT),-Wstack-usage=$(FUNC_STACK_LIMIT)) \
			   -Wcast-qual -Wlogical-op -Wshadow \
			   -Wconversion

DEFINE_CFLAGS		:= "-Dinline=inline __attribute__((__always_inline__))" \
			   -DF_CPU=$(F_CPU) \
			   $(if $(BOOT_OFFSET),-DBOOT_OFFSET=$(BOOT_OFFSET)) \
			   $(if $(DEBUG),-DDEBUG=$(DEBUG))

MAIN_CFLAGS		:= -mmcu=$(GCC_ARCH) -std=gnu11 -g0 -O$(O) \
			   $(WARN_CFLAGS) \
			   $(DEFINE_CFLAGS) \
			   -mcall-prologues -mrelax -mstrict-X \
			   -fshort-enums -flto

MAIN_LDFLAGS		:=

CFLAGS			+= $(MAIN_CFLAGS)
BOOT_CFLAGS		+= $(MAIN_CFLAGS) -DBOOTLOADER

LDFLAGS			+= $(MAIN_LDFLAGS)
BOOT_LDFLAGS		+= $(MAIN_LDFLAGS) -Wl,--section-start=.text=$(BOOT_OFFSET)

BIN			:= $(NAME).bin
HEX			:= $(NAME).hex
EEP			:= $(NAME).eep.hex
BOOT_BIN		:= $(NAME).bootloader.bin
BOOT_HEX		:= $(NAME).bootloader.hex

OBJ_DIR			:= obj
DEP_DIR			:= dep
BOOT_OBJ_DIR		:= obj-boot
BOOT_DEP_DIR		:= dep-boot

.SUFFIXES:
.DEFAULT_GOAL := all

# Programmer parameters
AVRDUDE_SPEED		?= 1
AVRDUDE_SLOW_SPEED	?= 200

AVRDUDE_PROGRAMMER	:=
ifeq ($(PROGRAMMER),mysmartusb)
AVRDUDE_PROGRAMMER	:= avr910
PROGPORT		:= /dev/ttyUSB0
endif
ifeq ($(PROGRAMMER),avrisp2)
AVRDUDE_PROGRAMMER	:= avrisp2
PROGPORT		:= usb
endif

ifeq ($(AVRDUDE_PROGRAMMER),)
$(error Invalid PROGRAMMER specified)
endif

define _programmer_cmd_pwrcycle
	$(if $(filter mysmartusb,$(PROGRAMMER)), \
		$(MYSMARTUSB) -p0 $(PROGPORT) && \
		sleep 1 && \
		$(MYSMARTUSB) -p1 $(PROGPORT) \
	)
endef

define _programmer_cmd_prog_enter
	$(if $(filter mysmartusb,$(PROGRAMMER)), \
		$(MYSMARTUSB) -mp $(PROGPORT) \
	)
endef

define _programmer_cmd_prog_leave
	$(if $(filter mysmartusb,$(PROGRAMMER)), \
		$(MYSMARTUSB) -md $(PROGPORT) \
	)
endef

DEPS = $(sort $(patsubst %.c,$(2)/%.d,$(1)))
OBJS = $(sort $(patsubst %.c,$(2)/%.o,$(1)))

# Generate dependencies
$(call DEPS,$(SRCS),$(DEP_DIR)): $(DEP_DIR)/%.d: %.c
	@$(MKDIR) -p $(dir $@)
	@$(MKDIR) -p $(OBJ_DIR)
	$(QUIET_DEPEND) -o $@.tmp -MM -MG -MT "$@ $(patsubst $(DEP_DIR)/%.d,$(OBJ_DIR)/%.o,$@)" $(CFLAGS) $<
	@$(MV) -f $@.tmp $@

ifneq ($(BOOT_SRCS),)
$(call DEPS,$(BOOT_SRCS),$(BOOT_DEP_DIR)): $(BOOT_DEP_DIR)/%.d: %.c
	@$(MKDIR) -p $(dir $@)
	@$(MKDIR) -p $(BOOT_OBJ_DIR)
	$(QUIET_DEPEND) -o $@.tmp -MM -MG -MT "$@ $(patsubst $(BOOT_DEP_DIR)/%.d,$(BOOT_OBJ_DIR)/%.o,$@)" $(BOOT_CFLAGS) $<
	@$(MV) -f $@.tmp $@
endif

-include $(call DEPS,$(SRCS),$(DEP_DIR))
ifneq ($(BOOT_SRCS),)
-include $(call DEPS,$(BOOT_SRCS),$(BOOT_DEP_DIR))
endif

# Generate object files
$(call OBJS,$(SRCS),$(OBJ_DIR)): $(OBJ_DIR)/%.o: %.c
	@$(MKDIR) -p $(dir $@)
	$(QUIET_CC) -o $@ -c $(CFLAGS) $<

ifneq ($(BOOT_SRCS),)
$(call OBJS,$(BOOT_SRCS),$(BOOT_OBJ_DIR)): $(BOOT_OBJ_DIR)/%.o: %.c
	@$(MKDIR) -p $(dir $@)
	$(QUIET_CC) -o $@ -c $(BOOT_CFLAGS) $<
endif

all: $(HEX) $(if $(BOOT_SRCS),$(BOOT_HEX))

%.s: %.c
	$(QUIET_CC) $(CFLAGS) -S $*.c

$(BIN): $(call OBJS,$(SRCS),$(OBJ_DIR))
	$(QUIET_CC) $(CFLAGS) -o $(BIN) -fwhole-program $(call OBJS,$(SRCS),$(OBJ_DIR)) $(LDFLAGS)

$(BOOT_BIN): $(call OBJS,$(BOOT_SRCS),$(BOOT_OBJ_DIR))
	$(QUIET_CC) $(BOOT_CFLAGS) -o $(BOOT_BIN) -fwhole-program $(call OBJS,$(BOOT_SRCS),$(BOOT_OBJ_DIR)) $(BOOT_LDFLAGS)

$(HEX): $(BIN)
	$(QUIET_OBJCOPY) -R.eeprom -O ihex $(BIN) $(HEX)
	@$(OBJDUMP) -h $(BIN) | $(GREP) -qe .eeprom && \
	 $(OBJCOPY) -j.eeprom --set-section-flags=.eeprom="alloc,load" \
	 --change-section-lma .eeprom=0 -O ihex $(BIN) $(EEP) \
	 || $(TRUE)
	$(QUIET_SIZE) --format=SysV $(BIN)

$(BOOT_HEX): $(BOOT_BIN)
	$(QUIET_OBJCOPY) -R.eeprom -O ihex $(BOOT_BIN) $(BOOT_HEX)
	$(QUIET_SIZE) --format=SysV $(BOOT_BIN)

define _avrdude_interactive
  $(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
    -c $(AVRDUDE_PROGRAMMER) -P $(PROGPORT) -t
endef

define _avrdude_reset
  $(AVRDUDE) -B $(AVRDUDE_SLOW_SPEED) -p $(AVRDUDE_ARCH) \
    -c $(AVRDUDE_PROGRAMMER) -P $(PROGPORT) \
    -U signature:r:/dev/null:i -q -q
endef

define _avrdude_write_flash
  $(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
    -c $(AVRDUDE_PROGRAMMER) -P $(PROGPORT) \
    $(if $(BOOT_SRCS),-U flash:w:$(BOOT_HEX)) \
    -U flash:w:$(HEX)
endef

define _avrdude_write_eeprom
  $(TEST) -r $(EEP) && ( \
    $(AVRDUDE) -B $(AVRDUDE_SPEED) -p $(AVRDUDE_ARCH) \
    -c $(AVRDUDE_PROGRAMMER) -P $(PROGPORT) \
    -U eeprom:w:$(EEP) \
  ) || $(TRUE)
endef

define _avrdude_write_fuse
  $(AVRDUDE) -B $(AVRDUDE_SLOW_SPEED) -p $(AVRDUDE_ARCH) \
    -c $(AVRDUDE_PROGRAMMER) -P $(PROGPORT) -q -q \
    -U lfuse:w:$(LFUSE):m -U hfuse:w:$(HFUSE):m $(if $(EFUSE),-U efuse:w:$(EFUSE):m)
endef

write_flash: all
	$(call _programmer_cmd_prog_enter)
	$(call _avrdude_write_flash)
	$(call _programmer_cmd_pwrcycle)
	$(call _programmer_cmd_prog_leave)

writeflash: write_flash

write_eeprom: all
	$(call _programmer_cmd_prog_enter)
	$(call _avrdude_write_eeprom)
	$(call _programmer_cmd_pwrcycle)
	$(call _programmer_cmd_prog_leave)

writeeeprom: write_eeprom

write_mem: all
	$(call _programmer_cmd_prog_enter)
	$(call _avrdude_write_flash)
	$(call _avrdude_write_eeprom)
	$(call _programmer_cmd_pwrcycle)
	$(call _programmer_cmd_prog_leave)

writemem: write_mem
install: write_mem

write_fuses:
	$(call _programmer_cmd_prog_enter)
	$(call _avrdude_write_fuse)
	$(call _programmer_cmd_pwrcycle)
	$(call _programmer_cmd_prog_leave)

write_fuse: write_fuses
writefuse: write_fuses
writefuses: write_fuses

print_fuses:
	@$(if $(LFUSE),echo "LFUSE (low fuse)      = $(LFUSE)",$(TRUE))
	@$(if $(HFUSE),echo "HFUSE (high fuse)     = $(HFUSE)",$(TRUE))
	@$(if $(EFUSE),echo "EFUSE (extended fuse) = $(EFUSE)",$(TRUE))

printfuses: print_fuses

reset:
	$(call _programmer_cmd_prog_enter)
	$(call _avrdude_reset)
	$(call _programmer_cmd_pwrcycle)

avrdude:
	$(call _programmer_cmd_prog_enter)
	$(call _avrdude_interactive)
	$(call _programmer_cmd_pwrcycle)
	$(call _programmer_cmd_prog_leave)

doxygen:
	$(DOXYGEN) Doxyfile

clean:
	-$(RM) -rf $(OBJ_DIR) $(DEP_DIR) \
		   $(BOOT_OBJ_DIR) $(BOOT_DEP_DIR) \
		   $(BIN) $(BOOT_BIN) \
		   *.pyc *.pyo __pycache__ \
		   $(CLEAN_FILES)

distclean: clean
	-$(RM) -f $(HEX) $(BOOT_HEX) $(EEP) *.s $(DISTCLEAN_FILES)