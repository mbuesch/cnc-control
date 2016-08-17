/*
 *   CNC-remote-control
 *   Debug interface
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
			dbg_ringbuf_out = 0u;
		else
			dbg_ringbuf_out++;
	}
	irq_restore(sreg);

	return count;
}

static void debug_ringbuf_putchar(char c)
{
	uint8_t sreg;
	uint16_t used;

	if (!devflag_is_set(DEVICE_FLG_USBLOGMSG))
		return;

	sreg = irq_disable_save();
	used = dbg_ringbuf_used;
	if (used < ARRAY_SIZE(dbg_ringbuf)) {
		dbg_ringbuf[dbg_ringbuf_in] = (uint8_t)c;
		if (dbg_ringbuf_in >= ARRAY_SIZE(dbg_ringbuf) - 1)
			dbg_ringbuf_in = 0u;
		else
			dbg_ringbuf_in++;
		dbg_ringbuf_used++;
	}
	irq_restore(sreg);
}

static void debug_putchar(char c)
{
	uart_putchar(c);
	debug_ringbuf_putchar(c);
}

static int debug_stream_putchar(char c, FILE *stream)
{
	debug_putchar(c);
	return 0;
}

static FILE debug_fstream = FDEV_SETUP_STREAM(debug_stream_putchar, NULL,
					      _FDEV_SETUP_WRITE);

void do_debug_printf(const char PROGPTR *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf_P(&debug_fstream, fmt, args);
	va_end(args);
}

void debug_dumpmem(const void *_mem, uint8_t size)
{
	const uint8_t *mem = _mem;
	uint8_t i;

	if (!debug_enabled())
		return;
	if (!mem || !size)
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

void debug_init(void)
{
	uart_init();
	stdout = &debug_fstream;
	stderr = &debug_fstream;
	debug_printf("CNC control initializing\n");
}
