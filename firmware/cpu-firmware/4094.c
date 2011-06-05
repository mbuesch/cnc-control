/*
 *   74HCT4094 shift register driver
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

#include "4094.h"
#include "util.h"

#include <avr/io.h>


#define sr4094_clear(pin)	SR4094_##pin##_PORT &= ~(1 << SR4094_##pin##_BIT)
#define sr4094_set(pin)		SR4094_##pin##_PORT |= (1 << SR4094_##pin##_BIT)

static inline void sr4094_transfer_start(void)
{
	sr4094_clear(STROBE);
}

static inline void sr4094_transfer_end(void)
{
	sr4094_set(STROBE);
}

static void sr4094_put_byte(uint8_t data)
{
	uint8_t mask = 0x80;

	do {
		if (data & mask)
			sr4094_set(DATA);
		else
			sr4094_clear(DATA);
		sr4094_set(CLOCK);
		nop();
		nop();
		sr4094_clear(CLOCK);
		mask >>= 1;
	} while (mask);
}

void sr4094_put_data(void *_data, uint8_t nr_chips)
{
	uint8_t *data = _data;
	uint8_t i;

	sr4094_transfer_start();
	for (i = 0; i < nr_chips; i++)
		sr4094_put_byte(data ? data[i] : 0);
	sr4094_transfer_end();
}

void sr4094_init(void *initial_data, uint8_t nr_chips)
{
	sr4094_clear(OUTEN);
	SR4094_OUTEN_DDR |= (1 << SR4094_OUTEN_BIT);

	sr4094_clear(DATA);
	SR4094_DATA_DDR |= (1 << SR4094_DATA_BIT);

	sr4094_clear(CLOCK);
	SR4094_CLOCK_DDR |= (1 << SR4094_CLOCK_BIT);

	sr4094_set(STROBE);
	SR4094_STROBE_DDR |= (1 << SR4094_STROBE_BIT);

	sr4094_put_data(initial_data, nr_chips);
	sr4094_set(OUTEN);
}
