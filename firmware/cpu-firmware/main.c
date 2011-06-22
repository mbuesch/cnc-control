/*
 *   CNC-remote-control
 *   CPU
 *
 *   Copyright (C) 2009-2011 Michael Buesch <m@bues.ch>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2 as published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include "main.h"
#include "debug.h"
#include "util.h"
#include "lcd.h"
#include "override.h"
#include "4094.h"
#include "pdiusb.h"
#include "machine_interface.h"
#include "spi.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>

#include <string.h>


enum jog_state {
	JOG_STOPPED,			/* Not jogging */
	JOG_RUNNING_POS,		/* Jogging in positive direction */
	JOG_RUNNING_NEG,		/* Jogging in negative direction */
};

/* Left */
enum softkey0_states {
	SK0_AXISPOS,		/* Axis position is displayed. */
	SK0_VELOCITY,		/* Jog velocity is displayed. */

	NR_SK0_STATES,
};

/* Right */
enum softkey1_states {
	SK1_INCREMENT,
	SK1_DEVSTATE,

	NR_SK1_STATES,
};

/* The current state */
struct device_state {
	bool rapid;			/* Rapid-jog on */
	bool incremental;		/* Incremental-jog on */
	fixpt_t selected_increment;	/* Selected increment for jog */
	uint8_t axis;			/* Selected axis (enum axis_id) (Modified in IRQ context) */
	uint8_t axis_enable_mask;	/* Enabled axes (Modified in IRQ context) */

	uint8_t jog;			/* enum jog_state */
	fixpt_t jog_velocity;		/* Jogging velocity */
	jiffies_t next_jog_keepalife;	/* Deadline of next jog keepalife */
	uint8_t fo_feedback_percent;	/* Feed override feedback percentage */

	/* The current axis positions.
	 * Updated in IRQ context! */
	fixpt_t positions[NR_AXIS];

	bool spindle_on;		/* Spindle state. Changed in IRQ context. */
	bool spindle_delayed_on;	/* Delayed spindle-on request */
	jiffies_t spindle_change_time;	/* Deadline for spindle-on */

	/* Deferred UI update requests. May be accessed in IRQ context. */
	bool lcd_need_update;
	bool leds_need_update;

	/* Button states. Use get_buttons() to access these fields. */
	bool button_update_required;
	uint16_t buttons;
	int8_t jogwheel;

	/* Softkey states */
	uint8_t softkey[2];

	/* Emergency stop state (read only). */
	bool estop;
};
static struct device_state state;

/* The system timer. Use get_jiffies() to access. */
jiffies_t jiffies;
/* The "external output-port interface" status. */
typedef uint16_t extports_t;
static extports_t extports;

/* The list of possible increment step sizes */
static fixpt_t PROGMEM increments[] = {
	FLOAT_TO_FIXPT(0.001),
	FLOAT_TO_FIXPT(0.01),
	FLOAT_TO_FIXPT(0.05),
	FLOAT_TO_FIXPT(0.1),
};


static void select_next_axis(void)
{
	uint8_t sreg, axis;

	sreg = irq_disable_save();

	BUG_ON(state.axis_enable_mask == 0);
	axis = state.axis;
	while (1) {
		if (++axis >= NR_AXIS)
			axis = 0;
		if (BIT(axis) & state.axis_enable_mask)
			break;
	}
	state.axis = axis;

	irq_restore(sreg);
}

static void select_previous_axis(void)
{
	uint8_t sreg, axis;

	sreg = irq_disable_save();

	BUG_ON(state.axis_enable_mask == 0);
	axis = state.axis;
	while (1) {
		if (axis == 0)
			axis = NR_AXIS - 1;
		else
			axis--;
		if (BIT(axis) & state.axis_enable_mask)
			break;
	}
	state.axis = axis;

	irq_restore(sreg);
}

/* Returns the next bigger increment size */
static fixpt_t increment_size_up(fixpt_t inc)
{
	uint8_t i, next;
	fixpt_t cur;

	for (i = 0; i < ARRAY_SIZE(increments); i++) {
		cur = pgm_read(&increments[i]);
		if (cur == inc)
			break;
	}
	next = i + 1;
	if (next >= ARRAY_SIZE(increments))
		next = 0;

	return pgm_read(&increments[next]);
}

void leds_enable(bool enable)
{
	sr4094_outen(enable);
}

static inline void extports_commit(void)
{
	sr4094_put_data(&extports, sizeof(extports));
}

#define _extports_is_set(state, port_id)	(!!((state) & (port_id)))
#define _extports_set(state, port_id)		do { state |= (port_id); } while (0)
#define _extports_clear(state, port_id)		do { state &= ~(port_id); } while (0)

static void extports_set(uint16_t extport_id)
{
	if (!_extports_is_set(extports, extport_id)) {
		_extports_set(extports, extport_id);
		extports_commit();
	}
}

static void extports_clear(uint16_t extport_id)
{
	if (_extports_is_set(extports, extport_id)) {
		_extports_clear(extports, extport_id);
		extports_commit();
	}
}

static void extports_init(void)
{
	sr4094_init(&extports, sizeof(extports));
}

static void coprocessor_init(void)
{
	uint8_t result;

	spi_lowlevel_init();

	/* Boot coprocessor application */
	spi_slave_select(1);
	spi_transfer_slowsync(SPI_CONTROL_ENTERAPP);
	spi_slave_select(0);
	mdelay(300);

	spi_slave_select(1);
	spi_transfer_slowsync(SPI_CONTROL_TESTAPP);
	result = spi_transfer_slowsync(SPI_CONTROL_NOP);
	spi_slave_select(0);
	if (result == SPI_RESULT_OK) {
		debug_printf("Coprocessor initialized\n");
	} else {
		debug_printf("Coprocessor init failed (%u)\n", result);
		return;
	}

	GIFR |= (1 << SPI_MASTER_TRANSIRQ_INTF);
	GICR |= (1 << SPI_MASTER_TRANSIRQ_INT);
}

ISR(SPI_MASTER_TRANSIRQ_VECT)
{
	state.button_update_required = 1;
}

struct spi_rx_data {
	uint8_t _undefined;
	uint8_t low;
	uint8_t high;
	uint8_t enc;
	uint8_t sum;
} __packed spi_rx_data;

/* Commands sent to the coprocessor (in that order).
 * Must match struct spi_rx_data. */
static const uint8_t PROGMEM spi_tx_data[] = {
	SPI_CONTROL_GETLOW,
	SPI_CONTROL_GETHIGH,
	SPI_CONTROL_GETENC,
	SPI_CONTROL_GETSUM,
	SPI_CONTROL_NOP,
};

static void trigger_button_state_fetching(void)
{
	uint8_t sreg;

	sreg = irq_disable_save();
	state.button_update_required = 0;
	irq_restore(sreg);

	BUILD_BUG_ON(sizeof(spi_tx_data) != sizeof(spi_rx_data));

	if (!spi_async_running()) {
		spi_async_start(&spi_rx_data, (const void *)spi_tx_data,
				ARRAY_SIZE(spi_tx_data),
				SPI_ASYNC_TXPROGMEM, 1);
	}
}

/* Runs with IRQs disabled */
void spi_async_done(void)
{
	uint8_t expected_sum;

	/* We got all spi_rx_data */

	expected_sum = spi_rx_data.low ^ spi_rx_data.high ^
		       spi_rx_data.enc ^ 0xFF;
	if (unlikely(spi_rx_data.sum != expected_sum)) {
		if (debug_verbose()) {
			debug_printf("SPI: button checksum mismatch: "
				     "was %02X, expected %02X\n",
				     spi_rx_data.sum, expected_sum);
		}
		/* Try again */
		trigger_button_state_fetching();
		return;
	}

	/* Update state. */
	BUG_ON(!irqs_disabled());
	state.buttons = spi_rx_data.low | ((uint16_t)spi_rx_data.high << 8);
	state.jogwheel += (int8_t)spi_rx_data.enc;
}

/* Spindle state may change at any time before or right after this check */
static inline bool spindle_is_on(void)
{
	mb();
	return state.spindle_on;
}

static uint16_t get_buttons(int8_t *_jogwheel)
{
	uint16_t buttons;
	int8_t jogwheel;
	uint8_t sreg;

	sreg = irq_disable_save();

	/* Get the pushbuttons state */
	buttons = state.buttons;
	/* Get the jogwheel state.
	 * One "wheel-click" is equivalent to two state increments.
	 * So we convert the value to "wheel-clicks". */
	jogwheel = state.jogwheel / 2;
	state.jogwheel %= 2;

	irq_restore(sreg);

	if (buttons & BTN_TWOHAND) {
		extports_set(EXT_LED_TWOHAND);
	} else {
		extports_clear(EXT_LED_TWOHAND);
		if (devflag_is_set(DEVICE_FLG_TWOHANDEN)) {
			/* Security switch is not pressed. Report security
			 * related buttons as released. */
			if (!spindle_is_on())
				buttons &= ~BTN_SPINDLE;
			buttons &= ~(BTN_JOG_POSITIVE |
				     BTN_JOG_NEGATIVE |
				     BTN_JOG_INC |
				     BTN_ENCPUSH);
			jogwheel = 0;
		}
	}

	*_jogwheel = jogwheel;
	return buttons;
}

static void do_update_lcd(void)
{
	uint8_t sreg;
	uint16_t devflags = get_active_devflags();

	if (state.estop) {
		lcd_cursor(0, 2);
		lcd_put_pstr("ESTOP ACTIVE");
		return;
	}

	switch (state.softkey[0]) {
	case SK0_AXISPOS: {
		uint8_t axis;
		fixpt_t pos;

		sreg = irq_disable_save();
		axis = state.axis;
		pos = state.positions[axis];
		irq_restore(sreg);

		switch (axis) {
		case AXIS_X:
			lcd_put_char('X');
			break;
		case AXIS_Y:
			lcd_put_char('Y');
			break;
		case AXIS_Z:
			lcd_put_char('Z');
			break;
		case AXIS_A:
			lcd_put_char('A');
			break;
		default:
			BUG_ON(1);
		}

		lcd_printf(FIXPT_FMT3, FIXPT_ARG3(pos));
		break;
	}
	case SK0_VELOCITY: {
		lcd_printf("Vf" FIXPT_FMT0,
			   FIXPT_ARG0(state.jog_velocity));
		break;
	}
	default:
		BUG_ON(1);
	}

	switch (state.softkey[1]) {
	case SK1_INCREMENT:
		lcd_cursor(0, 10);
		lcd_printf("i" FIXPT_FMT3, FIXPT_ARG3(state.selected_increment));
		break;
	case SK1_DEVSTATE:
		lcd_cursor(0, 9);
		lcd_printf("%3d%%", state.fo_feedback_percent);
		lcd_put_char(state.rapid ? 'R' : '-');
		lcd_put_char(state.jog != JOG_STOPPED ? 'J' : '-');
		lcd_put_char(spindle_is_on() ? 'S' : '-');
		break;
	default:
		BUG_ON(1);
	}

	/* Left softkey label */
	switch (state.softkey[0]) {
	case SK0_AXISPOS:
		lcd_cursor(1, 0);
		lcd_put_pstr("Vf");
		break;
	case SK0_VELOCITY:
		lcd_cursor(1, 0);
		lcd_put_pstr("pos");
		break;
	default:
		BUG_ON(1);
	}

	if (devflags & DEVICE_FLG_ON) {
		lcd_cursor(1, 6);
		lcd_put_pstr("[ON]");
	} else {
		lcd_cursor(1, 5);
		lcd_put_pstr("[OFF]");
	}

	/* Right softkey label */
	switch (state.softkey[1]) {
	case SK1_INCREMENT:
		lcd_cursor(1, 11);
		lcd_put_pstr("state");
		break;
	case SK1_DEVSTATE:
		lcd_cursor(1, 12);
		lcd_put_pstr("incr");
		break;
	default:
		BUG_ON(1);
	}
}

static void update_lcd(void)
{
	if (debug_verbose())
		debug_printf("Update LCD\n");

	lcd_clear_buffer();
	do_update_lcd();
	lcd_commit();
}

static void update_leds(void)
{
	uint16_t devflags = get_active_devflags();
	extports_t ext = extports;

	if (spindle_is_on())
		_extports_set(ext, EXT_LED_SPINDLE);
	else
		_extports_clear(ext, EXT_LED_SPINDLE);

	if (state.rapid)
		_extports_set(ext, EXT_LED_JOGRAPID);
	else
		_extports_clear(ext, EXT_LED_JOGRAPID);

	if (state.incremental)
		_extports_set(ext, EXT_LED_JOGINC);
	else
		_extports_clear(ext, EXT_LED_JOGINC);

	switch (state.jog) {
	case JOG_STOPPED:
		_extports_clear(ext, EXT_LED_JOGPOS);
		_extports_clear(ext, EXT_LED_JOGNEG);
		break;
	case JOG_RUNNING_POS:
		_extports_set(ext, EXT_LED_JOGPOS);
		_extports_clear(ext, EXT_LED_JOGNEG);
		break;
	case JOG_RUNNING_NEG:
		_extports_clear(ext, EXT_LED_JOGPOS);
		_extports_set(ext, EXT_LED_JOGNEG);
		break;
	default:
		BUG_ON(1);
	}

	if (devflags & DEVICE_FLG_ON)
		_extports_set(ext, EXT_LED_ONOFF);
	else
		_extports_clear(ext, EXT_LED_ONOFF);

	if (extports != ext) {
		extports = ext;
		extports_commit();
	}
}

static void interpret_one_softkey(bool sk, uint8_t index, uint8_t count)
{
	uint8_t sk_state;

	if (!sk)
		return;
	/* Key was pressed */
	sk_state = state.softkey[index];
	sk_state++;
	if (sk_state >= count)
		sk_state = 0;
	state.softkey[index] = sk_state;
	state.lcd_need_update = 1;
}

static void interpret_softkeys(bool sk0, bool sk1)
{
	interpret_one_softkey(sk0, 0, NR_SK0_STATES);
	interpret_one_softkey(sk1, 1, NR_SK1_STATES);
}

static void set_jog_keepalife_deadline(void)
{
	state.next_jog_keepalife = get_jiffies() + msec2jiffies(200);
}

static void jog_incremental(int8_t inc_count)
{
	struct control_interrupt irq = {
		.id		= IRQ_JOG,
		.flags		= IRQ_FLG_DROPPABLE,
	};

	if (!inc_count)
		return;

	irq.jog.increment = state.selected_increment;
	if (inc_count < 0)
		irq.jog.increment = fixpt_neg(irq.jog.increment);
	if (abs(inc_count) > 1) {
		irq.jog.increment = fixpt_mult(irq.jog.increment,
					       INT32_TO_FIXPT(abs(inc_count)));
	}
	irq.jog.axis = state.axis;
	irq.jog.flags = state.rapid ? IRQ_JOG_RAPID : 0;
	irq.jog.velocity = state.jog_velocity;

	send_interrupt(&irq, CONTROL_IRQ_SIZE(jog));
}

static void jog_stop(void)
{
	struct control_interrupt irq = {
		.id		= IRQ_JOG,
		.flags		= IRQ_FLG_PRIO,
	};

	if (state.jog == JOG_STOPPED)
		return;

	irq.jog.axis = state.axis;
	irq.jog.flags = IRQ_JOG_CONTINUOUS;
	irq.jog.increment = INT32_TO_FIXPT(0);

	send_interrupt(&irq, CONTROL_IRQ_SIZE(jog));

	state.jog = JOG_STOPPED;
}

static void jog(int8_t direction)
{
	struct control_interrupt irq = {
		.id		= IRQ_JOG,
	};

	if (direction) {
		if (state.incremental) {
			jog_stop();
			jog_incremental(direction > 0 ? 1 : -1);
		} else {
			/* Positive or negative jog */
			irq.flags |= IRQ_FLG_DROPPABLE;
			irq.jog.axis = state.axis;
			irq.jog.flags = IRQ_JOG_CONTINUOUS;
			if (state.rapid)
				irq.jog.flags |= IRQ_JOG_RAPID;
			irq.jog.velocity = state.jog_velocity;
			irq.jog.increment = INT32_TO_FIXPT(direction > 0 ? 1 : -1);
			send_interrupt(&irq, CONTROL_IRQ_SIZE(jog));

			state.jog = direction > 0 ? JOG_RUNNING_POS : JOG_RUNNING_NEG;
			set_jog_keepalife_deadline();
		}
	} else
		jog_stop();
	update_userinterface();
}

static void jog_update(void)
{
	switch (state.jog) {
	case JOG_STOPPED:
		jog(0);
		break;
	case JOG_RUNNING_POS:
		jog(1);
		break;
	case JOG_RUNNING_NEG:
		jog(-1);
		break;
	default:
		BUG_ON(1);
	}
}

static void handle_jog_keepalife(void)
{
	struct control_interrupt irq = {
		.id		= IRQ_JOG_KEEPALIFE,
		.flags		= IRQ_FLG_DROPPABLE,
	};

	if (state.jog == JOG_STOPPED)
		return;
	if (time_before(get_jiffies(), state.next_jog_keepalife))
		return;

	send_interrupt_discard_old(&irq, CONTROL_IRQ_SIZE(jog_keepalife));

	set_jog_keepalife_deadline();
}

static void halt_motion(void)
{
	struct control_interrupt irq = {
		.id		= IRQ_HALT,
		.flags		= IRQ_FLG_PRIO,
	};

	send_interrupt(&irq, CONTROL_IRQ_SIZE(halt));
}

static void interpret_jogwheel(int8_t jogwheel, bool wheel_pressed)
{
	fixpt_t mult, increment, velocity;

	if (wheel_pressed) {
		/* Select bigger INC step. */
		state.selected_increment = increment_size_up(
			state.selected_increment);
		state.softkey[1] = SK1_INCREMENT;
		update_userinterface();
		return;
	}

	if (jogwheel) {
		switch (state.softkey[0]) {
		case SK0_AXISPOS:
			jog_incremental(jogwheel);
			break;
		case SK0_VELOCITY:
			if (state.rapid)
				mult = FLOAT_TO_FIXPT(15.0);
			else
				mult = FLOAT_TO_FIXPT(1.0);
			increment = INT32_TO_FIXPT((int32_t)jogwheel);
			increment = fixpt_mult(increment, mult);
			velocity = state.jog_velocity;
			velocity = fixpt_add(velocity, increment);
			if (fixpt_is_neg(velocity))
				velocity = INT32_TO_FIXPT(0);
			if (velocity >= INT32_TO_FIXPT(30000))
				velocity = INT32_TO_FIXPT(30000);
			state.jog_velocity = velocity;
			break;
		default:
			BUG_ON(1);
		}
		update_userinterface();
	}
}

static void turn_spindle_on(void)
{
	struct control_interrupt irq = {
		.id		= IRQ_SPINDLE,
		.flags		= IRQ_FLG_DROPPABLE,
	};

	irq.spindle.state = SPINDLE_CW;
	send_interrupt(&irq, CONTROL_IRQ_SIZE(spindle));
}

static void turn_spindle_off(void)
{
	struct control_interrupt irq = {
		.id		= IRQ_SPINDLE,
		.flags		= IRQ_FLG_PRIO,
	};

	irq.spindle.state = SPINDLE_OFF;
	send_interrupt(&irq, CONTROL_IRQ_SIZE(spindle));
}

static void update_button_led(bool btn_pressed, extports_t ledport)
{
	if (btn_pressed)
		extports_set(ledport);
	else
		extports_clear(ledport);
}

static void interpret_buttons(void)
{
	uint16_t buttons;
	int8_t jogwheel;

	static uint16_t prev_buttons;

#define rising_edge(btn)	((buttons & (btn)) && !(prev_buttons & (btn)))
#define falling_edge(btn)	(!(buttons & (btn)) && (prev_buttons & (btn)))
#define pressed(btn)		(buttons & (btn))
#define released(btn)		(!pressed(btn))

	buttons = get_buttons(&jogwheel);

	/* on/off button */
	if (rising_edge(BTN_ONOFF)) {
		if (devflag_is_set(DEVICE_FLG_ON))
			modify_devflags(DEVICE_FLG_ON, 0);
		else
			modify_devflags(DEVICE_FLG_ON, DEVICE_FLG_ON);
		update_userinterface();
	}

	/* Spindle button */
	if (rising_edge(BTN_SPINDLE)) {
		if (spindle_is_on()) {
			turn_spindle_off();
		} else {
			state.spindle_delayed_on = 1;
			state.spindle_change_time = get_jiffies() + msec2jiffies(800);
		}
	}
	if (falling_edge(BTN_SPINDLE))
		state.spindle_delayed_on = 0;

	update_button_led(pressed(BTN_HALT), EXT_LED_HALT);
	if (rising_edge(BTN_HALT))
		halt_motion();

	/* Next axis selection */
	update_button_led(pressed(BTN_AXIS_NEXT), EXT_LED_AXIS_NEXT);
	update_button_led(pressed(BTN_AXIS_PREV), EXT_LED_AXIS_PREV);
	if (rising_edge(BTN_AXIS_NEXT)) {
		jog(0);
		select_next_axis();
		state.softkey[0] = SK0_AXISPOS;
		update_userinterface();
	}

	/* Previous axis selection */
	if (rising_edge(BTN_AXIS_PREV)) {
		jog(0);
		select_previous_axis();
		state.softkey[0] = SK0_AXISPOS;
		update_userinterface();
	}

	/* Rapid-move button */
	if (rising_edge(BTN_JOG_RAPID)) {
		state.rapid = 1;
		jog_update();
		update_userinterface();
	}
	if (falling_edge(BTN_JOG_RAPID)) {
		state.rapid = 0;
		jog_update();
		update_userinterface();
	}

	/* Incremental-button */
	if (rising_edge(BTN_JOG_INC)) {
		jog(0);
		state.incremental = !state.incremental;
		update_userinterface();
	}

	/* Softkeys */
	update_button_led(pressed(BTN_SOFT0), EXT_LED_SK0);
	update_button_led(pressed(BTN_SOFT1), EXT_LED_SK1);
	interpret_softkeys(rising_edge(BTN_SOFT0),
			   rising_edge(BTN_SOFT1));

	/* Jog */
	if (rising_edge(BTN_JOG_POSITIVE))
		jog(1);
	if (rising_edge(BTN_JOG_NEGATIVE))
		jog(-1);
	if (falling_edge(BTN_JOG_NEGATIVE) ||
	    falling_edge(BTN_JOG_POSITIVE))
		jog(0);

	/* Jogwheel. Only if not jogging via buttons. */
	if (state.jog == JOG_STOPPED)
		interpret_jogwheel(jogwheel, rising_edge(BTN_ENCPUSH));

	prev_buttons = buttons;
#undef rising_edge
#undef falling_edge
#undef pressed
#undef released
}

static void handle_spindle_change_requests(void)
{
	if (state.spindle_delayed_on) {
		if (spindle_is_on()) {
			state.spindle_delayed_on = 0;
			return;
		}
		if (time_after(get_jiffies(), state.spindle_change_time)) {
			turn_spindle_on();
			state.spindle_delayed_on = 0;
		}
	}
}

static void interpret_feed_override(bool force)
{
	struct control_interrupt irq = {
		.id		= IRQ_FEEDOVERRIDE,
		.flags		= IRQ_FLG_DROPPABLE,
	};
	uint8_t fostate;
	static uint8_t prev_state;

	fostate = override_get_pos();
	if (fostate == prev_state && !force)
		return;
	prev_state = fostate;

	irq.feedoverride.state = fostate;
	send_interrupt_discard_old(&irq, CONTROL_IRQ_SIZE(feedoverride));
}

/* Called in IRQ context! */
void set_axis_enable_mask(uint8_t mask)
{
	BUG_ON(mask == 0);

	state.axis_enable_mask = mask;
	if (!(BIT(state.axis) & mask))
		state.axis = ffs8(mask) - 1;
	mb();
	update_userinterface();
}

/* Called in IRQ context! */
void axis_pos_update(uint8_t axis, fixpt_t absolute_pos)
{
	BUG_ON(axis >= ARRAY_SIZE(state.positions));

	state.positions[axis] = absolute_pos;
	mb();
	state.lcd_need_update = 1;
}

/* Called in IRQ context! */
void spindle_state_update(bool on)
{
	state.spindle_on = on;
	mb();
	update_userinterface();
}

/* Called in IRQ context! */
void feed_override_feedback_update(uint8_t percent)
{
	state.fo_feedback_percent = min(percent, 200);
	mb();
	update_userinterface();
}

/* Called in IRQ context! */
void set_estop_state(bool asserted)
{
	state.estop = asserted;
	mb();
	update_userinterface();
}

void update_userinterface(void)
{
	mb();
	state.lcd_need_update = 1;
	state.leds_need_update = 1;
}

/* The system timer */
ISR(TIMER1_COMPA_vect)
{
	jiffies++;
}

static void systimer_init(void)
{
	/* Initialize a 0.001 second timer. */
	TCCR1A = 0;
	TCCR1B = (1 << CS11) | (1 << WGM12);
	OCR1A = 2000;
	TIMSK |= (1 << OCIE1A);
}

void reset_device_state(void)
{
	uint8_t i;

	memset(&state, 0, sizeof(state));
	state.selected_increment = FLOAT_TO_FIXPT(0.1);
	state.axis = AXIS_X;
	state.jog = JOG_STOPPED;
	state.jog_velocity = INT32_TO_FIXPT(100);
	for (i = 0; i < ARRAY_SIZE(state.positions); i++)
		state.positions[i] = INT32_TO_FIXPT(0);
	state.softkey[0] = SK0_AXISPOS;
	state.softkey[1] = SK1_INCREMENT;

	set_axis_enable_mask(BIT(AXIS_X) | BIT(AXIS_Y) | BIT(AXIS_Z) |
			     BIT(AXIS_A));
	reset_devflags();

	update_userinterface();
	interpret_feed_override(1); /* Force-send override state */
}

int main(void)
{
	uint8_t mcucsr;

	static jiffies8_t prev_jiffies_lo;
	jiffies8_t jiffies_lo;

	irq_disable();
	mcucsr = MCUCSR;
	MCUCSR = 0;
	wdt_enable(WDTO_500MS);
	debug_init();

	GICR = (1 << INT1);
	MCUCR = (0 << ISC11) | (0 << ISC10) |
		(1 << ISC01) | (0 << ISC00);

	if (mcucsr & (1 << WDRF))
		debug_printf("WARNING: watchdog reset triggered\n");
	lcd_init();
	extports_init();
	coprocessor_init();
	override_init();
	pdiusb_init();
	systimer_init();

	reset_device_state();

	irq_enable();
	while (1) {
		jiffies_lo = get_jiffies_low();
		if (jiffies_lo != prev_jiffies_lo) {
			prev_jiffies_lo = jiffies_lo;
			spi_async_ms_tick();
		}

		mb();
		if (!state.estop) {
			if (state.button_update_required)
				trigger_button_state_fetching();
			interpret_buttons();
			interpret_feed_override(0);
			handle_spindle_change_requests();
			handle_jog_keepalife();
		}

		mb();
		if (state.lcd_need_update || state.leds_need_update) {
			bool lcd, leds;

			irq_disable();
			lcd = state.lcd_need_update;
			leds = state.leds_need_update;
			state.lcd_need_update = 0;
			state.leds_need_update = 0;
			irq_enable();

			if (lcd)
				update_lcd();
			if (leds)
				update_leds();
		}

		wdt_reset();
	}
}
