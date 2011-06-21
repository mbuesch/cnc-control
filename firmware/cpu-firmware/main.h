#ifndef MAIN_H_
#define MAIN_H_

/* CPU frequency */
#define CPU_HZ			16000000
/* 1ms timer calibration */
#define DELAY_1MS_TIMERFREQ	(1 << CS01) /* == CPU_HZ/8 */
#define DELAY_1MS_LOOP		80
#define DELAY_1MS_LOOP_TIMES	25
/* 1us delayloop calibration */
#define DELAY_1US_LOOP		4


#include "util.h"
#include "machine_interface.h"

#include <stdint.h>


/* Timekeeping */
typedef uint16_t	jiffies_t;
typedef int16_t		s_jiffies_t;
typedef uint8_t		jiffies8_t;
extern jiffies_t jiffies;

/* Jiffies timing helpers derived from the Linux Kernel sources.
 * These inlines deal with timer wrapping correctly.
 *
 * time_after(a, b) returns true if the time a is after time b.
 *
 * Do this with "<0" and ">=0" to only test the sign of the result. A
 * good compiler would generate better code (and a really good compiler
 * wouldn't care). Gcc is currently neither.
 */
#define time_after(a, b)        ((s_jiffies_t)(b) - (s_jiffies_t)(a) < 0)
#define time_before(a, b)       time_after(b, a)

/* Number of jiffies-per-second */
#define JPS			((uint32_t)1000)

/* Convert milliseconds to jiffies. */
#define msec2jiffies(msec)   ((jiffies_t)(JPS * (uint32_t)(msec) / (uint32_t)1000))

/* Get jiffies counter. IRQs must already be disabled. */
static inline jiffies_t __get_jiffies(void)
{
	mb();
	return jiffies;
}

/* Get jiffies counter. */
static inline jiffies_t get_jiffies(void)
{
	jiffies_t j;
	uint8_t sreg;

	sreg = irq_disable_save();
	j = __get_jiffies();
	irq_restore(sreg);

	return j;
}

/* Get the low 8 bits of jiffies */
static inline jiffies_t get_jiffies_low(void)
{
	uint8_t jiffies_lo8;

	/* This is atomic on AVR. */
	mb();
	jiffies_lo8 = lo8(jiffies);

	return (jiffies8_t)jiffies_lo8;
}


/* Pushbuttons */
#define BTN_HALT		(1ul << 0)	/* Motion halt */
#define BTN_SPINDLE		(1ul << 1)	/* Spindle on/off */
#define BTN_AXIS_NEXT		(1ul << 2)	/* Select next axis */
#define BTN_AXIS_PREV		(1ul << 3)	/* Select previous axis */
#define BTN_RESERVED		(1ul << 4)
#define BTN_TWOHAND		(1ul << 5)	/* Twohand security switch */
#define BTN_JOG_POSITIVE	(1ul << 6)	/* JOG + */
#define BTN_JOG_RAPID		(1ul << 7)	/* Rapid JOG */
#define BTN_JOG_NEGATIVE	(1ul << 8)	/* JOG - */
#define BTN_JOG_INC		(1ul << 9)	/* JOG incremental */
#define BTN_SOFT0		(1ul << 10)	/* Softkey 0 */
#define BTN_ONOFF		(1ul << 11)	/* Turn device on/off */
#define BTN_SOFT1		(1ul << 12)	/* Softkey 1 */
#define BTN_ENCPUSH		(1ul << 13)	/* Encoder pushbutton */


/* External output-port interface */
#define EXTPORT(chipnr, portnr)	((1ul << (portnr)) << ((chipnr) * 8))

#define EXT_LED_HALT		EXTPORT(0, 0)
#define EXT_LED_SPINDLE		EXTPORT(0, 1)
#define EXT_LED_AXIS_NEXT	EXTPORT(0, 2)
#define EXT_LED_AXIS_PREV	EXTPORT(0, 3)
#define EXT_LED_RESERVED	EXTPORT(0, 4)
#define EXT_LED_TWOHAND		EXTPORT(0, 5)
#define EXT_LED_JOGPOS		EXTPORT(0, 6)
#define EXT_LED_JOGRAPID	EXTPORT(0, 7)
#define EXT_LED_JOGNEG		EXTPORT(1, 0)
#define EXT_LED_JOGINC		EXTPORT(1, 1)
#define EXT_LED_SK0		EXTPORT(1, 2)
#define EXT_LED_ONOFF		EXTPORT(1, 3)
#define EXT_LED_SK1		EXTPORT(1, 4)


/* Enable/disable LEDs */
void leds_enable(bool enable);
/* Reset the device */
void reset_device_state(void);
/* Axis mask */
void set_axis_enable_mask(uint8_t mask);
/* Axis manipulation */
void axis_pos_update(uint8_t axis, fixpt_t absolute_pos);
/* Spindle state */
void spindle_state_update(bool on);
/* Feed override feedback */
void feed_override_feedback_update(uint8_t percent);
/* Set the estop feedback state */
void set_estop_state(bool asserted);

/* Request an update of the user interface */
void update_userinterface(void);

#endif /* MAIN_H_ */
