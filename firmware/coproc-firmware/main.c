/*
 *   CNC-remote-control
 *   Button processor
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

#include "util.h"
#include "spi_interface.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#include <stdint.h>


/* Hardware state of a button */
struct button_hwstate {
	bool state;			/* 1 = pressed, 0 = released */
	uint8_t changetimer;		/* Timer since last change */
};

/* Software state of a button */
struct button_swstate {
	bool state;			/* 1 = pressed, 0 = released */
	bool synchronized;		/* Is synchronized with hardware state? */
};

/* Hardware state of a torque encoder */
struct encoder_hwstate {
	uint8_t gray;			/* The graycode state */
	uint8_t prev_gray;
	uint8_t changetimer;		/* Timer since last change */
};

/* Software state of a torque encoder */
struct encoder_swstate {
	int8_t state;
	bool synchronized;		/* Is synchronized with hardware state? */
};

static struct button_hwstate hwstates[16];
static struct button_swstate swstates[16];
static struct encoder_hwstate enc_hwstate;
static struct encoder_swstate enc_swstate;


/* We can keep this in SRAM. It's not that big. */
static const uint8_t bit2mask_lt[] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
};
/* Convert a bit-number to a bit-mask.
 * Only valid for bitnr<=7.
 */
#define BITMASK(bitnr)	(__builtin_constant_p(bitnr) ? (1 << (bitnr)) : bit2mask_lt[(bitnr)])

/* Convert 2bit graycode to binary */
static inline uint8_t gray2bin_2bit(uint8_t graycode)
{
	if (graycode & 2)
		graycode ^= 1;
	return graycode;
}

ISR(TIMER1_COMPA_vect)
{
#define inc_changetimer(statestruct) do {		\
		if ((statestruct).changetimer < 0xFF)	\
			(statestruct).changetimer++;	\
	} while (0)

	inc_changetimer(hwstates[0]);
	inc_changetimer(hwstates[1]);
	inc_changetimer(hwstates[2]);
	inc_changetimer(hwstates[3]);
	inc_changetimer(hwstates[4]);
	inc_changetimer(hwstates[5]);
	inc_changetimer(hwstates[6]);
	inc_changetimer(hwstates[7]);
	inc_changetimer(hwstates[8]);
	inc_changetimer(hwstates[9]);
	inc_changetimer(hwstates[10]);
	inc_changetimer(hwstates[11]);
	inc_changetimer(hwstates[12]);
	inc_changetimer(hwstates[13]);
	inc_changetimer(hwstates[14]);
	inc_changetimer(hwstates[15]);
	inc_changetimer(enc_hwstate);
}

static void timers_init(void)
{
	/* Initialize a 1khz timer (CPU_HZ is 8Mhz). */
	TCCR1A = 0;
	TCCR1B = (1 << CS11) | (1 << WGM12);
	OCR1A = 1000;
	TIMSK |= (1 << OCIE1A);
}

static inline void do_button_read(bool state, uint8_t index)
{
	if (state != hwstates[index].state) {
		hwstates[index].state = state;
		hwstates[index].changetimer = 0; /* is atomic */
		swstates[index].synchronized = 0;
	}
}

/* Read the hardware states of the buttons */
static void buttons_read(void)
{
	uint8_t b, c, d, enc_a, enc_b, gray;

	b = PINB;
	c = PINC;
	d = PIND;

	/* Interpret the buttons */
	do_button_read(!(b & (1 << 0)), 0);
	do_button_read(!(b & (1 << 1)), 1);
	do_button_read(!(c & (1 << 0)), 2);
	do_button_read(!(c & (1 << 1)), 3);
	do_button_read(!(c & (1 << 2)), 4);
	do_button_read(!(c & (1 << 3)), 5);
	do_button_read(!(c & (1 << 4)), 6);
	do_button_read(!(c & (1 << 5)), 7);
	do_button_read(!(d & (1 << 0)), 8);
	do_button_read(!(d & (1 << 1)), 9);
	do_button_read(!(d & (1 << 2)), 10);
	do_button_read(!(d & (1 << 3)), 11);
	do_button_read(!(d & (1 << 4)), 12);
	do_button_read(!(d & (1 << 5)), 13);

	/* Interpret the torque encoder */
	enc_a = !(d & (1 << 6));
	enc_b = !(d & (1 << 7));
	gray = enc_a | (enc_b << 1);
	if (gray != enc_hwstate.gray) {
		enc_hwstate.gray = gray;
		enc_hwstate.changetimer = 0; /* is atomic */
		enc_swstate.synchronized = 0;
	}

	mb();
}

static void buttons_init(void)
{
	/* Configure inputs and pullups */
	DDRB &= ~0x03;
	PORTB |= 0x03;

	DDRC &= ~0x3F;
	PORTC |= 0x3F;

	DDRD &= ~0xFF;
	PORTD |= 0xFF;

	buttons_read();
	enc_hwstate.prev_gray = enc_hwstate.gray;
}

static void trigger_trans_interrupt(void)
{
	SPI_SLAVE_TRANSIRQ_PORT &= ~(1 << SPI_SLAVE_TRANSIRQ_BIT);
	nop();
	nop();
	SPI_SLAVE_TRANSIRQ_PORT |= (1 << SPI_SLAVE_TRANSIRQ_BIT);
}

/* Synchronize the software state of the buttons */
static void buttons_synchronize(void)
{
	uint8_t i, now, prev;
	bool one_state_changed = 0;

#define BUTTON_DEBOUNCE		40	/* Button debounce, in milliseconds */
#define ENC_DEBOUNCE		2	/* Encoder debounce, in milliseconds */

	/* Sync buttons */
	for (i = 0; i < ARRAY_SIZE(swstates); i++) {
		if (swstates[i].synchronized)
			continue;
		mb();
		if (hwstates[i].changetimer >= BUTTON_DEBOUNCE) { /* atomic read */
			irq_disable();
			swstates[i].state = hwstates[i].state;
			swstates[i].synchronized = 1;
			irq_enable();

			one_state_changed = 1;
		}
	}

	/* Sync encoder */
	if (!enc_swstate.synchronized) {
		mb();
		if (enc_hwstate.changetimer >= ENC_DEBOUNCE) { /* atomic read */
			now = gray2bin_2bit(enc_hwstate.gray);
			prev = gray2bin_2bit(enc_hwstate.prev_gray);
			irq_disable();
			if (now == ((prev + 1) & 3)) {
				enc_swstate.state--;
				one_state_changed = 1;
			}
			if (now == ((prev - 1) & 3)) {
				enc_swstate.state++;
				one_state_changed = 1;
			}
			enc_swstate.synchronized = 1;
			irq_enable();
			enc_hwstate.prev_gray = enc_hwstate.gray;
		}
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

#define swstate_bit(nr)	do {				\
		if (swstates[nr].state)			\
			data |= BITMASK((nr) % 8);	\
	} while (0)

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
		data = 0;
		swstate_bit(0);
		swstate_bit(1);
		swstate_bit(2);
		swstate_bit(3);
		swstate_bit(4);
		swstate_bit(5);
		swstate_bit(6);
		swstate_bit(7);
		checksum ^= data;
		break;
	case SPI_CONTROL_GETHIGH:
		data = 0;
		swstate_bit(8);
		swstate_bit(9);
		swstate_bit(10);
		swstate_bit(11);
		swstate_bit(12);
		swstate_bit(13);
		swstate_bit(14);
		swstate_bit(15);
		checksum ^= data;
		break;
	case SPI_CONTROL_GETENC:
		data = (uint8_t)enc_swstate.state;
		enc_swstate.state = 0;
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
	DDRB |= (1 << 4/*MISO*/);
	DDRB &= ~((1 << 5/*SCK*/) | (1 << 3/*MOSI*/) | (1 << 2/*SS*/));
	SPI_SLAVE_TRANSIRQ_PORT |= (1 << SPI_SLAVE_TRANSIRQ_BIT);
	SPI_SLAVE_TRANSIRQ_DDR |= (1 << SPI_SLAVE_TRANSIRQ_BIT);

	SPCR = (1 << SPE) | (1 << SPIE) | (0 << CPOL) | (0 << CPHA);
	(void)SPSR; /* clear state */
	(void)SPDR; /* clear state */
}

_mainfunc int main(void)
{
	irq_disable();
	wdt_enable(WDTO_500MS);

	timers_init();
	buttons_init();
	spi_init();

	irq_enable();
	while (1) {
		mb();
		buttons_read();
		buttons_synchronize();
		wdt_reset();
	}
}
