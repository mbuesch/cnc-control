/*
 *   CNC-remote-control
 *   UART driver
 *
 *   Copyright (C) 2011 Michael Buesch <m@bues.ch>
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

#include "uart.h"
#include "main.h"
#include "util.h"

#include <avr/io.h>


void uart_putchar(char c)
{
	if (c == '\n')
		uart_putchar('\r');
	while (!(UCSRA & (1 << UDRE)));
	UDR = c;
}

void uart_puthex(uint8_t val)
{
	uart_putchar(hexdigit_to_ascii((val >> 4) & 0xF));
	uart_putchar(hexdigit_to_ascii(val & 0xF));
}

void _uart_putstr(const char PROGPTR *pstr)
{
	char c;

	while (1) {
		c = pgm_read_byte(pstr);
		if (c == '\0')
			break;
		uart_putchar(c);
		pstr++;
	}
	uart_putchar('\n');
}

#if UART_USE_2X
# define UBRR_FACTOR	2
#else
# define UBRR_FACTOR	1
#endif

void uart_init(void)
{
	/* Set baud rate */
	UBRRL = lo8((CPU_HZ / 16 / UART_BAUD) * UBRR_FACTOR);
	UBRRH = hi8((CPU_HZ / 16 / UART_BAUD) * UBRR_FACTOR) & ~(1 << URSEL);
#if UART_USE_2X
	UCSRA = (1 << U2X);
#endif
	/* 8 Data bits, 1 Stop bit, No parity */
	UCSRC = (1 << UCSZ0) | (1 << UCSZ1) | (1 << URSEL);
	/* Enable transmitter */
	UCSRB = (0 << RXEN) | (1 << TXEN) | (0 << RXCIE);
}

void uart_exit(void)
{
	while (!(UCSRA & (1 << UDRE)));
	mdelay(10);
	UCSRB = 0;
	UCSRC = 0;
	UCSRA = 0;
	UBRRL = 0;
	UBRRH = 0;
}
