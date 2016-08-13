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
#include "uart.h"

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

char hexdigit_to_ascii(uint8_t digit)
{
	/* Convert a hexadecimal digit (0-F) to an ASCII character */
	if (digit >= 0xAu)
		digit = (uint8_t)(digit + 0x41u - 0xAu);
	else
		digit = (uint8_t)(digit + 0x30u);
	return (char)digit;
}

#ifndef BOOTLOADER
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

#endif /* BOOTLOADER */

#ifdef STACKCHECK

/* Stack usage threshold, in percent of SRAM */
#define STACKTHRES	25

#define RAMSIZE		(RAMEND - RAMSTART)
#define STACKLIM	(RAMEND - ((uint32_t)RAMSIZE * STACKTHRES / 100))

static noinstrument void stack_check(void *this_fn,
				     void *call_site)
{
	static bool had_overflow = 0;
	uint16_t sp = SP;

	if (sp >= STACKLIM)
		return;
	if (had_overflow)
		return;

	had_overflow = 1;
	mb();

	uart_putstr("WARNING: Stack size limit reached in 0x");
	uart_puthex((uint16_t)this_fn >> 8);
	uart_puthex((uint16_t)this_fn);
	uart_putstr(" (called from 0x");
	uart_puthex((uint16_t)call_site >> 8);
	uart_puthex((uint16_t)call_site);
	uart_putstr(")\n");
}

noinstrument noinline void __cyg_profile_func_enter(void *this_fn,
						    void *call_site)
{
	stack_check(this_fn, call_site);
}

noinstrument noinline void __cyg_profile_func_exit(void *this_fn,
						   void *call_site)
{
}
#endif /* STACKCHECK */
