/*
 *   CNC-remote-control
 *   Button processor
 *
 *   Copyright (C) 2009-2016 Michael Buesch <m@bues.ch>
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

#include "util.h"
#include "spi_interface.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#include <stdint.h>


typedef uint16_t jiffies_t;


#define BUTTON_DEBOUNCE		msec2jiffies(40)
#define ENC_DEBOUNCE		usec2jiffies(3500)


/* Hardware state of a button */
struct button_hwstate {
	bool state;			/* 1 = pressed, 0 = released */
	bool synchronized;		/* Is synchronized with software state? */
	jiffies_t sync_deadline;	/* Deadline for sync */
};

/* Hardware state of a torque encoder */
struct encoder_hwstate {
	uint8_t gray;			/* The graycode state */
	uint8_t prev_gray;
	bool synchronized;		/* Is synchronized with software state? */
	jiffies_t sync_deadline;	/* Deadline for sync */
};

/* Software state of a torque encoder */
struct encoder_swstate {
	int8_t state;
};

static struct button_hwstate hwstates[14];
static uint16_t swstates;
static struct encoder_hwstate enc_hwstates[1];
static struct encoder_swstate enc_swstates[1];


/* Convert 2bit graycode to binary */
static inline uint8_t gray2bin_2bit(uint8_t graycode)
{
	if (graycode & 2)
		graycode ^= 1;
	return graycode;
}

static void jiffies_init(void)
{
#define JPS			31250 /* jiffies per second */

	/* Initialize the timer to 8M/256=31250 */
	TCNT1 = 0;
	OCR1A = 0;
	TIMSK = 0;
	TCCR1A = 0;
	TCCR1B = (0 << CS10) | (0 << CS11) | (1 << CS12);
}

#define msec2jiffies(ms)	((jiffies_t)((uint32_t)(ms) * JPS / (uint32_t)1000))
#define usec2jiffies(us)	((jiffies_t)((uint32_t)(us) * JPS / (uint32_t)1000000))

#define time_after(a, b)	((int16_t)(b) - (int16_t)(a) < 0)
#define time_before(a, b)	time_after(b, a)

static inline jiffies_t jiffies_get(void)
{
	return TCNT1;
}

static inline void do_button_read(struct button_hwstate *hw,
				  bool state,
				  jiffies_t timestamp)
{
	if (state != hw->state) {
		hw->state = state;
		hw->synchronized = 0;
		hw->sync_deadline = timestamp + BUTTON_DEBOUNCE;
	}
}

static inline void do_encoder_read(struct encoder_hwstate *hw,
				   bool a, bool b,
				   jiffies_t timestamp)
{
	uint8_t gray;

	gray = (uint8_t)((uint8_t)a | ((uint8_t)b << 1u));

	if (gray != hw->gray) {
		hw->gray = gray;
		hw->synchronized = 0;
		hw->sync_deadline = timestamp + ENC_DEBOUNCE;
	}
}

/* Read the hardware states of the buttons */
static void buttons_read(void)
{
	uint8_t b, c, d;
	jiffies_t now;

	b = PINB;
	c = PINC;
	d = PIND;
	now = jiffies_get();

	/* Interpret the buttons */
	do_button_read(&hwstates[0], !(b & (1 << 0)), now);
	do_button_read(&hwstates[1], !(b & (1 << 1)), now);
	do_button_read(&hwstates[2], !(c & (1 << 0)), now);
	do_button_read(&hwstates[3], !(c & (1 << 1)), now);
	do_button_read(&hwstates[4], !(c & (1 << 2)), now);
	do_button_read(&hwstates[5], !(c & (1 << 3)), now);
	do_button_read(&hwstates[6], !(c & (1 << 4)), now);
	do_button_read(&hwstates[7], !(c & (1 << 5)), now);
	do_button_read(&hwstates[8], !(d & (1 << 0)), now);
	do_button_read(&hwstates[9], !(d & (1 << 1)), now);
	do_button_read(&hwstates[10], !(d & (1 << 2)), now);
	do_button_read(&hwstates[11], !(d & (1 << 3)), now);
	do_button_read(&hwstates[12], !(d & (1 << 4)), now);
	do_button_read(&hwstates[13], !(d & (1 << 5)), now);
	BUILD_BUG_ON(ARRAY_SIZE(hwstates) != 14);
	BUILD_BUG_ON(ARRAY_SIZE(hwstates) > sizeof(swstates) * 8);

	/* Interpret the torque encoders */
	do_encoder_read(&enc_hwstates[0], !(d & (1 << 6)), !(d & (1 << 7)), now);
	BUILD_BUG_ON(ARRAY_SIZE(enc_hwstates) != 1);
	BUILD_BUG_ON(ARRAY_SIZE(enc_hwstates) != ARRAY_SIZE(enc_swstates));
}

static void buttons_init(void)
{
	uint8_t i;

	/* Configure inputs and pullups */
	DDRB = (uint8_t)(DDRB & ~0x03u);
	PORTB = (uint8_t)(PORTB | 0x03u);

	DDRC = (uint8_t)(DDRC & ~0x3Fu);
	PORTC = (uint8_t)(PORTC | 0x3Fu);

	DDRD = (uint8_t)(DDRD & ~0xFFu);
	PORTD = (uint8_t)(PORTD | 0xFFu);

	buttons_read();
	for (i = 0; i < ARRAY_SIZE(enc_hwstates); i++)
		enc_hwstates[i].prev_gray = enc_hwstates[i].gray;
}

static void trigger_trans_interrupt(void)
{
	SPI_SLAVE_TRANSIRQ_PORT = (uint8_t)(SPI_SLAVE_TRANSIRQ_PORT &
					    ~(1u << SPI_SLAVE_TRANSIRQ_BIT));
	nop();
	nop();
	SPI_SLAVE_TRANSIRQ_PORT = (uint8_t)(SPI_SLAVE_TRANSIRQ_PORT |
					    (1u << SPI_SLAVE_TRANSIRQ_BIT));
}

static inline uint8_t do_sync_button(struct button_hwstate *hw,
				     uint8_t swstate_bit,
				     jiffies_t now)
{
	bool state;

	if (!hw->synchronized) {
		if (time_after(now, hw->sync_deadline)) {
			state = hw->state;
			irq_disable();
			if (state)
				swstates |= (1u << swstate_bit);
			else
				swstates &= ~(1u << swstate_bit);
			irq_enable();
			hw->synchronized = 1;

			return 1;
		}
	}

	return 0;
}

static inline uint8_t do_sync_encoder(struct encoder_hwstate *hw,
				      struct encoder_swstate *sw,
				      jiffies_t now)
{
	uint8_t cur, prev;

	if (!hw->synchronized) {
		if (time_after(now, hw->sync_deadline)) {
			cur = gray2bin_2bit(hw->gray);
			prev = gray2bin_2bit(hw->prev_gray);
			hw->prev_gray = hw->gray;
			hw->synchronized = 1;
			if (cur == ((prev + 1) & 3)) {
				irq_disable();
				sw->state--;
				irq_enable();

				return 1;
			}
			if (cur == ((prev - 1) & 3)) {
				irq_disable();
				sw->state++;
				irq_enable();

				return 1;
			}
		}
	}

	return 0;
}

/* Synchronize the software state of the buttons */
static void buttons_synchronize(void)
{
	uint8_t i, one_state_changed = 0;
	jiffies_t now;

	now = jiffies_get();

	/* Sync buttons */
	for (i = 0; i < ARRAY_SIZE(hwstates); i++) {
		one_state_changed |= do_sync_button(&hwstates[i], i,
						    now);
	}

	/* Sync encoders */
	for (i = 0; i < ARRAY_SIZE(enc_hwstates); i++) {
		one_state_changed |= do_sync_encoder(&enc_hwstates[i],
						     &enc_swstates[i],
						     now);
	}

	if (one_state_changed)
		trigger_trans_interrupt();
}

static noreturn void enter_bootloader(void)
{
	irq_disable();
	wdt_reset();

	/* Jump to bootloader code */
	__asm__ __volatile__(
	"ijmp\n"
	: /* None */
	: [_Z]		"z" (BOOT_OFFSET / 2)
	);
	unreachable();
}

ISR(SPI_STC_vect)
{
	uint8_t data;
	static uint8_t checksum;
	static bool enterboot_first_stage_done;

	data = SPDR;

	switch (data) {
	case SPI_CONTROL_ENTERBOOT:
		data = SPI_RESULT_OK;
		checksum = 0;
		enterboot_first_stage_done = 1;
		goto out;
	case SPI_CONTROL_ENTERBOOT2:
		if (enterboot_first_stage_done)
			enter_bootloader();
		data = SPI_RESULT_FAIL;
		checksum = 0;
		goto out;
	default:
		enterboot_first_stage_done = 0;
	}

	switch (data) {
	case SPI_CONTROL_GETLOW:
		data = swstates & 0xFF;
		checksum ^= data;
		break;
	case SPI_CONTROL_GETHIGH:
		data = (uint8_t)((swstates >> 8) & 0xFFu);
		checksum ^= data;
		break;
	case SPI_CONTROL_GETENC:
		data = (uint8_t)(enc_swstates[0].state);
		enc_swstates[0].state = 0;
		checksum ^= data;
		break;
	case SPI_CONTROL_GETSUM:
		data = checksum ^ 0xFF;
		checksum = 0;
		break;
	case SPI_CONTROL_TESTAPP:
		data = SPI_RESULT_OK;
		checksum = 0;
		break;
	case SPI_CONTROL_ENTERAPP:
	case SPI_CONTROL_NOP:
	default:
		data = 0;
		checksum = 0;
	}

out:
	SPDR = data;
}

static void spi_init(void)
{
	/* SPI slave mode 0 with IRQ enabled. */
	DDRB = (uint8_t)(DDRB | (1u << 4/*MISO*/));
	DDRB = (uint8_t)(DDRB & ~((1u << 5/*SCK*/) | (1u << 3/*MOSI*/) |
				  (1u << 2/*SS*/)));
	SPI_SLAVE_TRANSIRQ_PORT = (uint8_t)(SPI_SLAVE_TRANSIRQ_PORT |
					    (1u << SPI_SLAVE_TRANSIRQ_BIT));
	SPI_SLAVE_TRANSIRQ_DDR = (uint8_t)(SPI_SLAVE_TRANSIRQ_DDR | 
					   (1u << SPI_SLAVE_TRANSIRQ_BIT));

	SPCR = (1u << SPE) | (1u << SPIE) | (0u << CPOL) | (0u << CPHA);
	(void)SPSR; /* clear state */
	(void)SPDR; /* clear state */
}

int main(void) _mainfunc;
int main(void)
{
	irq_disable();
	wdt_enable(WDTO_500MS);

	jiffies_init();
	buttons_init();
	spi_init();

	irq_enable();
	while (1) {
		buttons_read();
		buttons_synchronize();
		wdt_reset();
	}
}
