/*
 *   CNC-remote-control
 *   Debug interface
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

#include "debug.h"
#include "main.h"
#include "util.h"
#include "uart.h"
#include "machine_interface.h"

#include <stdio.h>


static int debug_putchar(char c, FILE *stream)
{
	uart_putchar(c);
	return 0;
}

void _debug_printf(const prog_char *_fmt, ...)
{
	if (!(get_active_devflags() & DEVICE_FLG_NODEBUG)) {
		char fmt[64];
		va_list args;

		strlcpy_P(fmt, _fmt, sizeof(fmt));
		va_start(args, _fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);
	}
}

void debug_dumpmem(const void *_mem, uint8_t size)
{
	if (!(get_active_devflags() & DEVICE_FLG_NODEBUG)) {
		const uint8_t *mem = _mem;
		uint8_t i;

		for (i = 0; i < size; i++) {
			if (i % 16 == 0) {
				if (i != 0)
					debug_printf("\n");
				debug_printf("0x");
				uart_puthex(i);
				debug_printf(": ");
			}
			if (i % 2 == 0)
				uart_putchar(' ');
			uart_puthex(mem[i]);
		}
		debug_printf("\n");
	}
}

static FILE debug_stdout = FDEV_SETUP_STREAM(debug_putchar, NULL, _FDEV_SETUP_WRITE);

void debug_init(void)
{
	uart_init();
	stdout = &debug_stdout;
	stderr = &debug_stdout;
	debug_printf("CNC control initializing\n");
}
