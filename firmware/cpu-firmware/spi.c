/*
 *   CNC-remote-control
 *   SPI primitives
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

#include "spi.h"

#include <avr/wdt.h>
#include <avr/interrupt.h>


#if SPI_HAVE_ASYNC

static struct spi_async_state {
	uint8_t flags;
	uint8_t wait_ms;
	uint8_t wait_ms_left;
	uint8_t bytes_left;
	const uint8_t *txbuf;
	uint8_t *rxbuf;
} async_state;


static void spi_transfer_async(void)
{
	uint8_t txbyte;

	if (async_state.flags & SPI_ASYNC_TXPROGMEM) {
		const prog_uint8_t *p = (const prog_uint8_t *)async_state.txbuf;
		txbyte = pgm_read_byte(p);
	} else
		txbyte = *async_state.txbuf;
	async_state.txbuf++;
	async_state.bytes_left--;

	mb();
	SPDR = txbyte;
}

ISR(SPI_STC_vect)
{
	uint8_t rxbyte;

	rxbyte = SPDR;

	*async_state.rxbuf = rxbyte;
	async_state.rxbuf++;
	if (async_state.bytes_left) {
		if (async_state.wait_ms)
			async_state.wait_ms_left = async_state.wait_ms + 1;
		else
			spi_transfer_async();
	} else {
		SPCR &= ~(1 << SPIE);
		spi_slave_select(0);
		mb();
		async_state.flags &= ~SPI_ASYNC_RUNNING;
		spi_async_done();
	}
}

void spi_async_start(void *rxbuf, const void *txbuf,
		     uint8_t nr_bytes, uint8_t flags, uint8_t wait_ms)
{
	BUG_ON(ATOMIC_LOAD(async_state.flags) & SPI_ASYNC_RUNNING);
	BUG_ON(!nr_bytes);

	async_state.flags = flags | SPI_ASYNC_RUNNING;
	async_state.bytes_left = nr_bytes;
	async_state.wait_ms = wait_ms;
	async_state.wait_ms_left = 0;
	async_state.txbuf = txbuf;
	async_state.rxbuf = rxbuf;
	mb();

	(void)SPSR; /* clear state */
	(void)SPDR; /* clear state */
	SPCR |= (1 << SPIE);
	spi_slave_select(1);
	spi_transfer_async();
}

bool spi_async_running(void)
{
	return !!(ATOMIC_LOAD(async_state.flags) & SPI_ASYNC_RUNNING);
}

void spi_async_ms_tick(void)
{
	bool send_next = 0;

	irq_disable();

	if (!(async_state.flags & SPI_ASYNC_RUNNING)) {
		irq_enable();
		return;
	}
	if (async_state.wait_ms_left == 0) {
		irq_enable();
		return;
	}
	async_state.wait_ms_left--;
	if (async_state.wait_ms_left == 0)
		send_next = 1;
	irq_enable();

	if (send_next)
		spi_transfer_async();
}

#endif /* SPI_HAVE_ASYNC */

uint8_t spi_transfer_sync(uint8_t tx)
{
	SPDR = tx;
	while (!(SPSR & (1 << SPIF)));
	return SPDR;
}

uint8_t spi_transfer_slowsync(uint8_t tx)
{
	mdelay(10);
	return spi_transfer_sync(tx);
}

void spi_lowlevel_exit(void)
{
	SPCR = 0;
	SPSR = 0;
	SPDR = 0;
	(void)SPSR; /* clear state */
	(void)SPDR; /* clear state */
	DDRB = 0;
}

void spi_lowlevel_init(void)
{
	spi_slave_select(0);
	DDRB |= (1 << 5/*MOSI*/) | (1 << 7/*SCK*/) | (1 << 4/*SS*/);
	DDRB &= ~(1 << 6/*MISO*/);
	SPI_MASTER_TRANSIRQ_DDR &= ~(1 << SPI_MASTER_TRANSIRQ_BIT);
	SPI_MASTER_TRANSIRQ_PORT &= ~(1 << SPI_MASTER_TRANSIRQ_BIT);
	GICR &= ~(1 << SPI_MASTER_TRANSIRQ_INT);
	SPCR = (1 << SPE) | (1 << MSTR) |
	       (0 << CPOL) | (0 << CPHA) |
	       (0 << SPR0) | (1 << SPR1);
	SPSR = 0;
	mdelay(150);
	(void)SPSR; /* clear state */
	(void)SPDR; /* clear state */
}
