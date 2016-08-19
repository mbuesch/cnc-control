/*
 *   CNC-remote-control
 *   Button processor - Bootloader
 *
 *   Copyright (C) 2011-2016 Michael Buesch <m@bues.ch>
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
#include <avr/boot.h>

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
		SPI_SLAVE_TRANSIRQ_PORT = (uint8_t)(SPI_SLAVE_TRANSIRQ_PORT |
						    (1u << SPI_SLAVE_TRANSIRQ_BIT));
	} else {
		/* Ready */
		SPI_SLAVE_TRANSIRQ_PORT = (uint8_t)(SPI_SLAVE_TRANSIRQ_PORT &
						    ~(1u << SPI_SLAVE_TRANSIRQ_BIT));
	}
}

static void spi_init(void)
{
	DDRB = (uint8_t)(DDRB | (1u << 4/*MISO*/));
	DDRB = (uint8_t)(DDRB & ~((1u << 5/*SCK*/) | (1u << 3/*MOSI*/) |
				  (1u << 2/*SS*/)));
	spi_busy(1);
	SPI_SLAVE_TRANSIRQ_DDR = (uint8_t)(SPI_SLAVE_TRANSIRQ_DDR |
					   (1u << SPI_SLAVE_TRANSIRQ_BIT));

	SPCR = (1u << SPE) | (0u << SPIE) | (0u << CPOL) | (0u << CPHA);
	SPSR = 0u;
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
	wdt_enable(WDTO_2S);

	route_irqs_to_application();
	/* Jump to application code */
	__asm__ __volatile__(
	"ijmp\n"
	: /* None */
	: [_Z]		"z" (0x0000)
	);
	unreachable();
}

static bool verify_page(uint16_t page_address)
{
	uint8_t i, data0, data1;

	for (i = 0; i < ARRAY_SIZE(page_buffer); i++) {
		wdt_reset();
		data0 = page_buffer[i];
		data1 = pgm_read_byte((void PROGPTR *)(void *)(page_address + i));
		if (data0 != data1)
			return 0;
	}

	return 1;
}

static void write_page(uint16_t page_address)
{
	uint8_t i, sreg;
	uint16_t data;

	eeprom_busy_wait();
	boot_spm_busy_wait();

	sreg = irq_disable_save();

	boot_page_erase(page_address);
	boot_spm_busy_wait();
	for (i = 0; i < SPM_PAGESIZE; i = (uint8_t)(i + 2u)) {
		wdt_reset();
		data = (uint16_t)(page_buffer[i]);
		data |= ((uint16_t)(page_buffer[i + 1]) << 8);
		boot_page_fill(page_address + i, data);
	}
	boot_page_write(page_address);
	boot_spm_busy_wait();
	boot_rww_enable();

	irq_restore(sreg);
}

static noinline uint8_t calc_crc8(uint8_t crc, uint8_t data)
{
	return spi_crc8(crc, data);
}

static void do_flash(void)
{
	uint8_t data, addr_lo, addr_hi;
	uint8_t crc = 0, expected_crc;
	uint16_t page_address, i;
	bool ok;

	addr_lo = spi_xfer_sync(0);
	crc = calc_crc8(crc, addr_lo);
	addr_hi = spi_xfer_sync(0);
	crc = calc_crc8(crc, addr_hi);
	page_address = (uint16_t)addr_lo | ((uint16_t)addr_hi << 8);

	for (i = 0; i < ARRAY_SIZE(page_buffer); i++) {
		data = spi_xfer_sync(0);
		page_buffer[i] = data;
		crc = calc_crc8(crc, data);
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

static uint8_t saved_mcucsr __attribute__((section(".noinit")));

void early_init(void) __attribute__((naked, section(".init3"), used));
void early_init(void)
{
	irq_disable();
	saved_mcucsr = MCUCSR;
	MCUCSR = 0;
	wdt_enable(WDTO_2S);
}

int main(void) _mainfunc;
int main(void)
{
	uint8_t mcucsr = saved_mcucsr;

	irq_disable();
	wdt_enable(WDTO_2S);

	if (!(mcucsr & (1 << PORF))) {
		if ((mcucsr & (1 << WDRF)) ||
		    (mcucsr & (1 << BORF)))
			exit_bootloader();
	}

	disable_all_irq_sources();
	route_irqs_to_bootloader();

	spi_init();
	while (1) {
		wdt_reset();
		handle_spi();
	}
}
