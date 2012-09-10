/*
 *   CNC-remote-control
 *   Button processor - Bootloader
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

#include "util.h"
#include "spi_interface.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include <util/crc16.h>

#include <string.h>
#include <stdint.h>


static uint8_t page_buffer[SPM_PAGESIZE];


static void disable_all_irq_sources(void)
{
	GICR = 0;
	TIMSK = 0;
	SPCR = 0;
	UCSRB = 0;
	ADCSRA = 0;
	EECR = 0;
	ACSR = 0;
	TWCR = 0;
	SPMCR = 0;
}

static void route_irqs_to_bootloader(void)
{
	uint8_t tmp;

	__asm__ __volatile__(
"	ldi %[_tmp], %[_IVCE]		\n"
"	out %[_GICR], %[_tmp]		\n"
"	ldi %[_tmp], %[_IVSEL]		\n"
"	out %[_GICR], %[_tmp]		\n"
	: [_tmp]	"=a" (tmp)
	: [_GICR]	"I" (_SFR_IO_ADDR(GICR))
	, [_IVSEL]	"M" (1 << IVSEL)
	, [_IVCE]	"M" (1 << IVCE)
	);
}

static void route_irqs_to_application(void)
{
	uint8_t tmp;

	__asm__ __volatile__(
"	ldi %[_tmp], %[_IVCE]		\n"
"	out %[_GICR], %[_tmp]		\n"
"	clr %[_tmp]			\n"
"	out %[_GICR], %[_tmp]		\n"
	: [_tmp]	"=a" (tmp)
	: [_GICR]	"I" (_SFR_IO_ADDR(GICR))
	, [_IVCE]	"M" (1 << IVCE)
	);
}

static inline void spi_busy(bool busy)
{
	if (busy) {
		/* Busy */
		SPI_SLAVE_TRANSIRQ_PORT |= (1 << SPI_SLAVE_TRANSIRQ_BIT);
	} else {
		/* Ready */
		SPI_SLAVE_TRANSIRQ_PORT &= ~(1 << SPI_SLAVE_TRANSIRQ_BIT);
	}
}

static void spi_init(void)
{
	DDRB |= (1 << 4/*MISO*/);
	DDRB &= ~((1 << 5/*SCK*/) | (1 << 3/*MOSI*/) | (1 << 2/*SS*/));
	spi_busy(1);
	SPI_SLAVE_TRANSIRQ_DDR |= (1 << SPI_SLAVE_TRANSIRQ_BIT);

	SPCR = (1 << SPE) | (0 << SPIE) | (0 << CPOL) | (0 << CPHA);
	SPSR = 0;
	(void)SPSR; /* clear state */
	(void)SPDR; /* clear state */
}

static void spi_disable(void)
{
	SPCR = 0;
	SPSR = 0;
	SPDR = 0;
	(void)SPSR; /* clear state */
	(void)SPDR; /* clear state */
	DDRB = 0;
}

static inline void spi_transwait(void)
{
	while (!(SPSR & (1 << SPIF)));
}

static noinline uint8_t spi_xfer_sync(uint8_t tx)
{
	uint8_t data;

	SPDR = tx;
	spi_busy(0);
	spi_transwait();
	spi_busy(1);
	data = SPDR;

	return data;
}

static noreturn noinline void exit_bootloader(void)
{
	irq_disable();
	spi_disable();
	disable_all_irq_sources();

	route_irqs_to_application();
	/* Jump to application code */
	__asm__ __volatile__(
	"ijmp\n"
	: /* None */
	: [_Z]		"z" (0x0000)
	);
	unreachable();
}

static void spm_busywait(void)
{
	while (SPMCR & (1 << SPMEN));
}

static noinline void spm(uint8_t spmcrval, uint16_t addr, uint16_t data)
{
	void *z = (void *)addr;
	register uint8_t r0 __asm__("r0") = lo8(data);
	register uint8_t r1 __asm__("r1") = hi8(data);

	spm_busywait();
	eeprom_busy_wait();
	__asm__ __volatile__(
"	; SPM timed sequence		\n"
"	out %[_SPMCR], %[_spmcrval]	\n"
"	spm				\n"
	: /* None */
	: [_spmcrval]	"r" (spmcrval)
	, [_SPMCR]	"I" (_SFR_IO_ADDR(SPMCR))
	, [_Z]		"z" (z)
	, [_R0]		"l" (r0)
	, [_R1]		"l" (r1)
	);
}

static bool verify_page(uint16_t page_address)
{
	uint8_t i, data0, data1;

	for (i = 0; i < ARRAY_SIZE(page_buffer); i++) {
		data0 = page_buffer[i];
		data1 = pgm_read_byte((void PROGPTR *)(void *)(page_address + i));
		if (data0 != data1)
			return 0;
	}

	return 1;
}

static void write_page(uint16_t page_address)
{
	uint8_t i;
	uint16_t data;

	/* Erase the page */
	spm((1 << PGERS) | (1 << SPMEN), page_address, 0);
	/* Re-enable RWW section */
	spm((1 << RWWSRE) | (1 << SPMEN), page_address, 0);
	/* Transfer data to hardware page buffer */
	for (i = 0; i < ARRAY_SIZE(page_buffer); i += 2) {
		data = (uint16_t)(page_buffer[i]);
		data |= ((uint16_t)(page_buffer[i + 1]) << 8);
		spm((1 << SPMEN), page_address + i, data);
	}
	/* Execute page write */
	spm((1 << PGWRT) | (1 << SPMEN), page_address, 0);
	/* Re-enable RWW section */
	spm((1 << RWWSRE) | (1 << SPMEN), page_address, 0);

	spm_busywait();
}

static void do_flash(void)
{
	uint8_t data, addr_lo, addr_hi;
	uint8_t crc = 0, expected_crc;
	uint16_t page_address, i;
	bool ok;

	addr_lo = spi_xfer_sync(0);
	crc = spi_crc8(crc, addr_lo);
	addr_hi = spi_xfer_sync(0);
	crc = spi_crc8(crc, addr_hi);
	page_address = addr_lo | (addr_hi << 8);

	for (i = 0; i < ARRAY_SIZE(page_buffer); i++) {
		data = spi_xfer_sync(0);
		page_buffer[i] = data;
		crc = spi_crc8(crc, data);
	}

	crc ^= 0xFF;
	expected_crc = spi_xfer_sync(0);
	if (expected_crc != crc) {
		spi_xfer_sync(SPI_RESULT_FAIL);
		return;
	}
	spi_xfer_sync(SPI_RESULT_OK);

	write_page(page_address);
	ok = verify_page(page_address);
	memset(page_buffer, 0xFF, sizeof(page_buffer));
	if (ok)
		spi_xfer_sync(SPI_RESULT_OK);
	else
		spi_xfer_sync(SPI_RESULT_FAIL);
}

static void handle_spi(void)
{
	uint8_t data, txdata = 0;

	while (1) {
		data = spi_xfer_sync(txdata);
		txdata = 0;
		switch (data) {
		case SPI_CONTROL_ENTERBOOT:
		case SPI_CONTROL_ENTERBOOT2:
			/* We're already here. */
			txdata = SPI_RESULT_OK;
			break;
		case SPI_CONTROL_TESTAPP:
			txdata = SPI_RESULT_FAIL;
			break;
		case SPI_CONTROL_ENTERAPP:
			exit_bootloader();
			break;
		case SPI_CONTROL_STARTFLASH:
			do_flash();
			break;
		default:
			/* Ignore unknown commands */
			break;
		}
	}
}

_mainfunc int main(void)
{
	uint8_t mcucsr;

	irq_disable();
	wdt_disable();

	mcucsr = MCUCSR;
	MCUCSR = 0;
	if (!(mcucsr & (1 << PORF))) {
		if ((mcucsr & (1 << WDRF)) ||
		    (mcucsr & (1 << BORF)))
			exit_bootloader();
	}

	disable_all_irq_sources();
	route_irqs_to_bootloader();

	spi_init();
	while (1) {
		handle_spi();
	}
}
