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

#include <stdio.h>


static uint8_t dbg_ringbuf[256];
static uint8_t dbg_ringbuf_in;
static uint8_t dbg_ringbuf_out;
static uint8_t dbg_ringbuf_used;


uint8_t debug_ringbuf_count(void)
{
	/* This is only approximate. Count may change at any time. */
	return ATOMIC_LOAD(dbg_ringbuf_used);
}

uint8_t debug_ringbuf_get(void *buf, uint8_t size)
{
	uint8_t *outbuf = buf;
	uint8_t sreg, count = 0;

	sreg = irq_disable_save();
	for ( ; dbg_ringbuf_used && size; dbg_ringbuf_used--, size--, count++) {
		*outbuf++ = dbg_ringbuf[dbg_ringbuf_out];
		if (dbg_ringbuf_out >= ARRAY_SIZE(dbg_ringbuf) - 1)
			dbg_ringbuf_out = -1;
		dbg_ringbuf_out++;
	}
	irq_restore(sreg);

	return count;
}

static void debug_putchar(char c)
{
	uint8_t sreg;

	uart_putchar(c);

	sreg = irq_disable_save();
	if (dbg_ringbuf_used < ARRAY_SIZE(dbg_ringbuf)) {
		dbg_ringbuf[dbg_ringbuf_in] = c;
		if (dbg_ringbuf_in >= ARRAY_SIZE(dbg_ringbuf) - 1)
			dbg_ringbuf_in = -1;
		dbg_ringbuf_in++;
		dbg_ringbuf_used++;
	}
	irq_restore(sreg);
}

static int debug_stream_putchar(char c, FILE *stream)
{
	debug_putchar(c);
	return 0;
}

void do_debug_printf(const prog_char *_fmt, ...)
{
	if (debug_enabled()) {
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
	const uint8_t *mem = _mem;
	uint8_t i;

	if (!debug_enabled())
		return;

	for (i = 0; i < size; i++) {
		if (i % 16 == 0) {
			if (i != 0)
				debug_putchar('\n');
			debug_printf("0x%02X: ", i);
		}
		if (i % 2 == 0)
			debug_putchar(' ');
		debug_printf("%02X", mem[i]);
	}
	debug_putchar('\n');
}

static FILE debug_stdout = FDEV_SETUP_STREAM(debug_stream_putchar, NULL,
					     _FDEV_SETUP_WRITE);

void debug_init(void)
{
	uart_init();
	stdout = &debug_stdout;
	stderr = &debug_stdout;
	debug_printf("CNC control initializing\n");
}
