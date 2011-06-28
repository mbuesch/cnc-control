#ifndef MACHINE_INTERFACE_H_
#define MACHINE_INTERFACE_H_

/*** The CNC-machine interface ***/

#include <stdint.h>

#undef __packed
#define __packed	__attribute__((__packed__))


/* Fixed point arithmetics */
typedef int32_t fixpt_t;
#define FIXPT_FRAC_BITS		16
#define FIXPT_FRAC_MASK		(((uint32_t)1 << FIXPT_FRAC_BITS) - 1)
#define FIXPT_INT_PART(val)	(((int32_t)(val) >> FIXPT_FRAC_BITS) + (fixpt_is_neg(val) ? 1 : 0))
#define FIXPT_FRAC_PART(val)	((uint32_t)fixpt_abs(val) & FIXPT_FRAC_MASK)
#define TO_FIXPT(val)		((fixpt_t)(int32_t)(val))

/* Fixed point arithmetics conversion helpers */
#define FLOAT_TO_FIXPT(f)	TO_FIXPT(((float)(f)) * (FIXPT_FRAC_MASK + 1) + 1)
#define INT32_TO_FIXPT(i)	TO_FIXPT(((uint32_t)(int32_t)(i)) << FIXPT_FRAC_BITS)

#define fixpt_add(val0, val1)	((fixpt_t)((int32_t)(val0) + (int32_t)(val1)))
#define fixpt_sub(val0, val1)	((fixpt_t)((int32_t)(val0) - (int32_t)(val1)))
#define fixpt_is_neg(val)	((int32_t)(val) < 0)
#define fixpt_neg(val)		((fixpt_t)(-((int32_t)(val))))

static inline fixpt_t fixpt_abs(fixpt_t val)
{
	if (fixpt_is_neg(val))
		return fixpt_neg(val);
	return val;
}

static inline fixpt_t fixpt_mult(fixpt_t val0, fixpt_t val1)
{
	int64_t tmp;

	tmp = (int64_t)val0 * (int64_t)val1;
	tmp += (1ll << (FIXPT_FRAC_BITS - 1));
	tmp >>= FIXPT_FRAC_BITS;

	return (fixpt_t)(int32_t)tmp;
}

static inline fixpt_t fixpt_div(fixpt_t val0, fixpt_t val1)
{
	int64_t tmp;

	tmp = (int64_t)val0 << FIXPT_FRAC_BITS;
	tmp = tmp + (int64_t)val1 / 2ll;
	tmp /= (int64_t)val1;

	return (fixpt_t)(int32_t)tmp;
}

#define FIXPT_BIAS(val, bias)							\
	(fixpt_is_neg(val) ?							\
	 fixpt_sub(val, FLOAT_TO_FIXPT(bias)) :					\
	 fixpt_add(val, FLOAT_TO_FIXPT(bias)))

#define FIXPT_ARG_PREFIX(val, bias)						\
	(fixpt_is_neg(FIXPT_BIAS(val, bias)) ? "-" : "")

#define FIXPT_ARG_INTPART(val, bias)						\
	((int)fixpt_abs(FIXPT_INT_PART(FIXPT_BIAS(val, bias))))

#define FIXPT_ARG_FRACPART(val, mult, bias)					\
	((unsigned int)((FIXPT_FRAC_PART(FIXPT_BIAS(val, bias))			\
			 * (mult)) / (FIXPT_FRAC_MASK + 1)))

#define FIXPT_ARG_FULL(val, mult, bias)						\
	FIXPT_ARG_PREFIX(val, bias),						\
	FIXPT_ARG_INTPART(val, bias),						\
	FIXPT_ARG_FRACPART(val, mult, bias)

#define FIXPT_ARG_INTONLY(val, bias)						\
	FIXPT_ARG_PREFIX(val, bias),						\
	FIXPT_ARG_INTPART(val, bias)

/* printf() format and arguments for fixpt_t.
 * Note that val is evaluated multiple times. */
#define FIXPT_FMT0		"%s%d"
#define FIXPT_ARG0(val)		FIXPT_ARG_INTONLY(val, 0.5)

#define FIXPT_FMT1		"%s%d.%01u"
#define FIXPT_ARG1(val)		FIXPT_ARG_FULL(val, 10, 0.05)

#define FIXPT_FMT2		"%s%d.%02u"
#define FIXPT_ARG2(val)		FIXPT_ARG_FULL(val, 100, 0.005)

#define FIXPT_FMT3		"%s%d.%03u"
#define FIXPT_ARG3(val)		FIXPT_ARG_FULL(val, 1000, 0.0005)

#define FIXPT_FMT4		"%s%d.%04u"
#define FIXPT_ARG4(val)		FIXPT_ARG_FULL(val, 10000, 0.00005)


enum axis_id {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
	AXIS_U,
	AXIS_V,
	AXIS_W,
	AXIS_A,
	AXIS_B,
	AXIS_C,

	NR_AXIS,
};

static inline uint8_t axis_is_angular(enum axis_id id)
{
	switch (id) {
	case AXIS_A:
	case AXIS_B:
	case AXIS_C:
		return 1;
	default:
		break;
	}
	return 0;
}

#define control_crc8(crc, data)		_crc_ibutton_update(crc, data)

enum control_id {
	CONTROL_PING,			/* Pong request */
	CONTROL_RESET,			/* Reset device state */
	CONTROL_DEVFLAGS,		/* Update and read device flags */
	CONTROL_AXISUPDATE,		/* Axis position update */
	CONTROL_SPINDLEUPDATE,		/* Spindle state update */
	CONTROL_FOUPDATE,		/* Feed override update */
	CONTROL_AXISENABLE,		/* Set the axis-enable mask */
	CONTROL_ESTOPUPDATE,		/* E-stop status update */
	CONTROL_SETINCREMENT,		/* Upload an increment definition */

	/* Bootloader messages */
	CONTROL_ENTERBOOT = 0xA0,	/* Enter the CPU/coprocessor bootloader */
	CONTROL_EXITBOOT,		/* Exit CPU/coprocessor bootloader */
	CONTROL_BOOT_WRITEBUF,		/* Write to the page buffer */
	CONTROL_BOOT_FLASHPG,		/* Flash page buffer */
	CONTROL_BOOT_EEPWRITE,		/* Write page buffer to eeprom */
};

enum control_message_flags {
	CONTROL_FLG_BOOTLOADER = 0x80,	/* Intended message recipient is the bootloader */
};

enum device_flags {
	DEVICE_FLG_NODEBUG	= (1ul << 0), /* Debugging disabled */
	DEVICE_FLG_VERBOSEDBG	= (1ul << 1), /* Verbose debugging */
	DEVICE_FLG_ON		= (1ul << 2), /* The CNC Control device is turned on */
	DEVICE_FLG_TWOHANDEN	= (1ul << 3), /* Twohand switch enabled */
	DEVICE_FLG_USBLOGMSG	= (1ul << 4), /* Send debug messages through USB */
};

enum enterboot_magic {
	ENTERBOOT_MAGIC0 = 0xB0,
	ENTERBOOT_MAGIC1 = 0x07,
};

enum mcu_target {
	TARGET_CPU,			/* Target is the CPU */
	TARGET_COPROC,			/* Target is the coprocessor */
};

enum spindle_state {
	SPINDLE_OFF,
	SPINDLE_CW,
	SPINDLE_CCW,
};

/* Control message from the CNC machine. */
struct control_message {
	uint8_t id;
	uint8_t flags;
	uint8_t _reserved;
	uint8_t seqno;

	int _header_end[0];

	union {
		struct {
		} __packed ping;
		struct {
			uint16_t mask;
			uint16_t set;
		} __packed devflags;
		struct {
			fixpt_t pos;
			uint8_t axis;
		} __packed axisupdate;
		struct {
			uint8_t state;
		} __packed spindleupdate;
		struct {
			uint8_t percent;
		} __packed feedoverride;
		struct {
			uint16_t mask;
		} __packed axisenable;
		struct {
			uint8_t asserted;
		} __packed estopupdate;
		struct {
			fixpt_t increment;
			uint8_t index;
		} __packed setincrement;

		/* Bootloader messages */
		struct {
			uint8_t magic[2];
			uint8_t target; /* enum mcu_target */
		} __packed enterboot;
		struct {
			uint8_t target; /* enum mcu_target */
		} __packed exitboot;
		struct {
			uint16_t offset;
			uint8_t size;
			uint8_t crc;
			uint8_t data[32];
		} __packed boot_writebuf;
		struct {
			uint16_t address;
			uint8_t target; /* enum mcu_target */
		} __packed boot_flashpg;
		struct {
			uint16_t address;
			uint16_t size;
			uint8_t target; /* enum mcu_target */
		} __packed boot_eepwrite;
	} __packed;
} __packed;

#define CONTROL_MSG_SIZE(name)	(offsetof(struct control_message, name) +\
				 sizeof(((struct control_message *)0)->name))
#define CONTROL_MSG_HDR_SIZE	CONTROL_MSG_SIZE(_header_end)
#define CONTROL_MSG_MAX_SIZE	sizeof(struct control_message)

static inline int control_enterboot_magic_ok(const struct control_message *ctl)
{
	return (ctl->enterboot.magic[0] == ENTERBOOT_MAGIC0 &&
		ctl->enterboot.magic[1] == ENTERBOOT_MAGIC1);
}


enum reply_id {
	REPLY_OK,
	REPLY_ERROR,
	REPLY_VAL16,
};

enum reply_error {
	CTLERR_UNDEFINED,	/* Undefined error */
	CTLERR_COMMAND,		/* Unknown command */
	CTLERR_SIZE,		/* Command size mismatch */
	CTLERR_BUSY,		/* Busy / Action already committed */
	CTLERR_PERMISSION,	/* Permission denied */
	CTLERR_INVAL,		/* Invalid input data */
	CTLERR_CONTEXT,		/* Invalid context (boot vs app) */
	CTLERR_CHECKSUM,	/* Checksum/parity error */
	CTLERR_CMDFAIL,		/* Command failed */
};

/* Control reply to the CNC machine. */
struct control_reply {
	uint8_t id;
	uint8_t flags;
	uint8_t _reserved;
	uint8_t seqno;

	int _header_end[0];

	union {
		struct {
		} __packed ok;
		struct {
			uint8_t code;
		} __packed error;
		struct {
			uint16_t value;
		} __packed val16;
	} __packed;
} __packed;

#define CONTROL_REPLY_SIZE(name)	(offsetof(struct control_reply, name) +\
					 sizeof(((struct control_reply *)0)->name))
#define CONTROL_REPLY_HDR_SIZE		CONTROL_REPLY_SIZE(_header_end)
#define CONTROL_REPLY_MAX_SIZE		sizeof(struct control_reply)

static inline void init_control_reply(struct control_reply *reply,
				      uint8_t id, uint8_t flags, uint8_t seqno)
{
	reply->id = id;
	reply->flags = flags;
	reply->_reserved = 0;
	reply->seqno = seqno;
}

enum interrupt_id {
	IRQ_JOG,		/* Jog control */
	IRQ_JOG_KEEPALIFE,	/* Jog keepalife request */
	IRQ_SPINDLE,		/* Turn the master spindle on/off */
	IRQ_FEEDOVERRIDE,	/* Change the feed override */
	IRQ_DEVFLAGS,		/* Device flags changed. */
	IRQ_HALT,		/* Halt motion */
	IRQ_LOGMSG,		/* Log message */
};

enum control_irq_flags {
	IRQ_FLG_TXQOVR		= (1 << 0), /* TX queue overflow */
	IRQ_FLG_PRIO		= (1 << 1), /* Higher priority */
	IRQ_FLG_DROPPABLE	= (1 << 2), /* May be dropped in favor of higher prio irqs */
};

enum jogirq_flags {
	IRQ_JOG_CONTINUOUS	= (1 << 0), /* Continuous jog */
	IRQ_JOG_RAPID		= (1 << 1), /* Rapid jog */
};

/* Device interrupt. */
struct control_interrupt {
	uint8_t id;
	uint8_t flags;
	uint8_t _reserved;
	uint8_t seqno;

	int _header_end[0];

	union {
		struct {
			fixpt_t increment;
			fixpt_t velocity;
			uint8_t axis;
			uint8_t flags;
		} __packed jog;
		struct {
		} __packed jog_keepalife;
		struct {
			uint8_t state;
		} __packed spindle;
		struct {
			uint8_t state;
		} __packed feedoverride;
		struct {
			uint16_t flags;
		} __packed devflags;
		struct {
		} __packed halt;
		struct {
			uint8_t msg[10];
		} __packed logmsg;
	} __packed;
} __packed;

#define CONTROL_IRQ_SIZE(name)		(offsetof(struct control_interrupt, name) +\
					 sizeof(((struct control_interrupt *)0)->name))
#define CONTROL_IRQ_HDR_SIZE		CONTROL_IRQ_SIZE(_header_end)
#define CONTROL_IRQ_MAX_SIZE		sizeof(struct control_interrupt)

#endif /* MACHINE_INTERFACE_H_ */
