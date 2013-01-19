/*
 *   Utility functions
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
#include "main.h"
#include "debug.h"
#include "lcd.h"

#include <avr/io.h>
#include <avr/wdt.h>


void long_delay_ms(uint16_t ms)
{
	while (ms) {
		_delay_ms(50);
		wdt_reset();
		ms = (ms >= 50) ? ms - 50 : 0;
	}
}

uint8_t hexdigit_to_ascii(uint8_t digit)
{
	/* Convert a hexadecimal digit (0-F) to an ASCII character */
	if (digit >= 0xA)
		digit += 0x41 - 0xA;
	else
		digit += 0x30;
	return digit;
}

#ifndef IN_BOOT
void do_panic(const char PROGPTR *msg)
{
	irq_disable();

	debug_printf("*** PANIC :( ***\n");
	_debug_printf(msg);
	debug_printf("\n");

	lcd_clear_buffer();
	lcd_printf("*** PANIC :( ***\n");
	lcd_commit();

	long_delay_ms(10000);
	reboot();
}

void reboot(void)
{
	irq_disable();
	debug_printf("*** REBOOTING ***\n");
	wdt_enable(WDTO_15MS);
	while (1);
}

uint8_t ffs16(uint16_t value)
{
	uint16_t mask;
	uint8_t count;

	for (mask = 1, count = 1; mask; mask <<= 1, count++) {
		if (value & mask)
			return count;
	}

	return 0;
}

#endif /* !IN_BOOT */

#ifdef STACKCHECK
void __cyg_profile_func_enter(void *, void *)
	noinstrument noinline;

void __cyg_profile_func_exit(void *, void *)
	noinstrument noinline;

void __cyg_profile_func_enter(void *this_fn,
			      void *call_site)
{
	//TODO
}

void __cyg_profile_func_exit(void *this_fn,
			     void *call_site)
{
	//TODO
}
#endif
