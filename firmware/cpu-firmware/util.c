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


void mdelay(uint16_t msecs)
{
	uint8_t timer, i;

	TCCR0 = DELAY_1MS_TIMERFREQ;
	do {
		/* Delay one millisecond */
		for (i = DELAY_1MS_LOOP_TIMES; i; i--) {
			TCNT0 = 0;
			do {
				timer = TCNT0;
			} while (timer < DELAY_1MS_LOOP);
		}
		wdt_reset();
	} while (--msecs);
	TCCR0 = 0;
}

void udelay(uint16_t usecs)
{
	uint8_t tmp;

	__asm__ __volatile__(
	"1:				\n"
	"	ldi %1, %2		\n"
	"2:				\n"
	"	dec %1			\n"
	"	brne 2b			\n"
	"	dec %A3			\n"
	"	brne 1b			\n"
	"	cp %B3, __zero_reg__	\n"
	"	breq 3f			\n"
	"	dec %B3			\n"
	"	ldi %A3, 0xFF		\n"
	"	rjmp 1b			\n"
	"3:				\n"
	: "=d" (usecs),
	  "=d" (tmp)
	: "M" (DELAY_1US_LOOP),
	  "0" (usecs)
	);
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
void do_panic(const prog_char *msg)
{
	irq_disable();

	debug_printf("*** PANIC :( ***\n");
	_debug_printf(msg);
	debug_printf("\n");

	lcd_clear_buffer();
	lcd_printf("*** PANIC :( ***\n");
	lcd_commit();

	mdelay(10000);
	reboot();
}

void reboot(void)
{
	irq_disable();
	debug_printf("*** REBOOTING ***\n");
	wdt_enable(WDTO_15MS);
	while (1);
}

#endif /* !IN_BOOT */
