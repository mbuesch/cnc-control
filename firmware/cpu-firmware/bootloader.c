/*
 *   CNC-remote-control
 *   CPU - Bootloader
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
#include "uart.h"
#include "pdiusb.h"
#include "usb.h"
#include "usb_application.h"
#include "spi.h"
#include "machine_interface.h"
#include "4094.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include <util/crc16.h>

#include <string.h>
#include <stdint.h>


/* AtMega32 */
#define CPU_SPM_PAGESIZE	SPM_PAGESIZE
#define CPU_E2SIZE		(E2END + 1)

/* AtMega8 */
#define COPROC_SPM_PAGESIZE	64
#define COPROC_E2SIZE		(0x1FF + 1)

#define PGBUF_SIZE		(max(CPU_SPM_PAGESIZE,		\
				 max(CPU_E2SIZE,		\
				 max(COPROC_SPM_PAGESIZE,	\
				     COPROC_E2SIZE))))
static uint8_t page_buffer[PGBUF_SIZE];


static void disable_all_irq_sources(void)
{
	GICR = 0;
	TIMSK = 0;
	SPCR = 0;
	UCSRB &= ~(1 << RXCIE);
	UCSRB &= ~(1 << TXCIE);
	UCSRB &= ~(1 << UDRIE);
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

static void coprocessor_spi_busywait(void)
{
	while (SPI_MASTER_TRANSIRQ_PIN & (1 << SPI_MASTER_TRANSIRQ_BIT));
}

static uint8_t coprocessor_spi_transfer(uint8_t data)
{
	_delay_ms(1);
	coprocessor_spi_busywait();
	return spi_transfer_sync(data);
}

static uint8_t coprocessor_spi_transfer_nobusy(uint8_t data)
{
	return spi_transfer_slowsync(data);
}

static bool coprocessor_is_in_application(void)
{
	uint8_t result;

	spi_slave_select(1);
	coprocessor_spi_transfer_nobusy(SPI_CONTROL_TESTAPP);
	result = coprocessor_spi_transfer_nobusy(SPI_CONTROL_NOP);
	spi_slave_select(0);
	if (result == SPI_RESULT_OK)
		return 1;
	return 0;
}

static bool coprocessor_enter_bootloader(void)
{
	spi_slave_select(1);
	coprocessor_spi_transfer_nobusy(SPI_CONTROL_ENTERBOOT);
	coprocessor_spi_transfer_nobusy(SPI_CONTROL_ENTERBOOT2);
	spi_slave_select(0);
	_delay_ms(150);
	if (!coprocessor_is_in_application())
		return 1;
	return 0;
}

static bool coprocessor_exit_bootloader(void)
{
	spi_slave_select(1);
	coprocessor_spi_transfer_nobusy(SPI_CONTROL_ENTERAPP);
	spi_slave_select(0);
	_delay_ms(150);
	if (coprocessor_is_in_application())
		return 1;
	return 0;
}

static void boot_coprocessor_init(void)
{
	spi_lowlevel_init();
}

static noreturn noinline void exit_bootloader(void)
{
	uart_putstr("EXIT BOOT");

	irq_disable();

	/* Shutdown hardware */
	spi_lowlevel_exit();
	pdiusb_exit();
	uart_exit();
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
	, [_R0]		"r" (r0)
	, [_R1]		"r" (r1)
	);
}

static bool verify_page(uint16_t page_address)
{
	uint8_t i, data0, data1;

	for (i = 0; i < CPU_SPM_PAGESIZE; i++) {
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
	for (i = 0; i < CPU_SPM_PAGESIZE; i += 2) {
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

uint8_t usb_app_ep2_rx(uint8_t *data, uint8_t ctl_size,
		       uint8_t *reply_buf)
{
	struct control_message *ctl = (struct control_message *)data;
	struct control_reply *reply = (struct control_reply *)reply_buf;

	BUILD_BUG_ON(USBCFG_EP2_MAXSIZE < CONTROL_REPLY_MAX_SIZE);

	if (ctl_size < CONTROL_MSG_HDR_SIZE)
		goto err_size;
	if (!(ctl->flags & CONTROL_FLG_BOOTLOADER))
		goto err_context;

	switch (ctl->id) {
	case CONTROL_PING: {
		break;
	}
	case CONTROL_ENTERBOOT: {
		if (ctl_size < CONTROL_MSG_SIZE(enterboot))
			goto err_size;

		if (!control_enterboot_magic_ok(ctl))
			goto err_inval;

		switch (ctl->enterboot.target) {
		case TARGET_CPU:
			/* We're already here. */
			break;
		case TARGET_COPROC:
			if (!coprocessor_enter_bootloader())
				goto err_cmdfail;
			break;
		default:
			goto err_context;
		}
		break;
	}
	case CONTROL_EXITBOOT: {
		if (ctl_size < CONTROL_MSG_SIZE(exitboot))
			goto err_size;

		switch (ctl->exitboot.target) {
		case TARGET_CPU:
			exit_bootloader();
			goto err_cmdfail;
		case TARGET_COPROC:
			if (!coprocessor_exit_bootloader())
				goto err_cmdfail;
			break;
		default:
			goto err_context;
		}
		break;
	}
	case CONTROL_BOOT_WRITEBUF: {
		uint8_t i, size, crc = 0, data;
		uint16_t offset;

		if (ctl_size < CONTROL_MSG_SIZE(boot_writebuf))
			goto err_size;
		size = ctl->boot_writebuf.size;
		offset = ctl->boot_writebuf.offset;

		if (size > sizeof(ctl->boot_writebuf.data) ||
		    offset >= sizeof(page_buffer) ||
		    offset + size > sizeof(page_buffer))
			goto err_inval;

		for (i = 0; i < size; i++) {
			data = ctl->boot_writebuf.data[i];
			crc = control_crc8(crc, data);
			page_buffer[offset + i] = data;
		}
		crc ^= 0xFF;
		if (crc != ctl->boot_writebuf.crc)
			goto err_checksum;
		break;
	}
	case CONTROL_BOOT_FLASHPG: {
		uint16_t i, address;
		uint8_t data, retval, crc = 0;

		if (ctl_size < CONTROL_MSG_SIZE(boot_flashpg))
			goto err_size;
		address = ctl->boot_flashpg.address;

		switch (ctl->boot_flashpg.target) {
		case TARGET_CPU:
			write_page(address);
			if (!verify_page(address))
				goto err_cmdfail;
			break;
		case TARGET_COPROC:
			spi_slave_select(1);
			coprocessor_spi_transfer(SPI_CONTROL_STARTFLASH);
			crc = spi_crc8(crc, lo8(address));
			coprocessor_spi_transfer(lo8(address));
			crc = spi_crc8(crc, hi8(address));
			coprocessor_spi_transfer(hi8(address));
			for (i = 0; i < COPROC_SPM_PAGESIZE; i++) {
				data = page_buffer[i];
				crc = spi_crc8(crc, data);
				coprocessor_spi_transfer(data);
			}
			crc ^= 0xFF;
			coprocessor_spi_transfer(crc);
			retval = coprocessor_spi_transfer(SPI_CONTROL_NOP);
			if (retval != SPI_RESULT_OK) {
				spi_slave_select(0);
				goto err_checksum; /* CRC error */
			}
			/* Flashing starts. Wait for result and read it. */
			retval = coprocessor_spi_transfer(SPI_CONTROL_NOP);
			if (retval != SPI_RESULT_OK) {
				spi_slave_select(0);
				goto err_cmdfail;
			}
			spi_slave_select(0);
			break;
		default:
			goto err_context;
		}
		break;
	}
	case CONTROL_BOOT_EEPWRITE: {
		uint16_t i, size, address;
		uint8_t data;

		if (ctl_size < CONTROL_MSG_SIZE(boot_eepwrite))
			goto err_size;
		address = ctl->boot_eepwrite.address;
		size = ctl->boot_eepwrite.size;

		switch (ctl->boot_eepwrite.target) {
		case TARGET_CPU: {
			if (size > CPU_E2SIZE ||
			    address >= CPU_E2SIZE ||
			    address + size > CPU_E2SIZE)
				goto err_inval;

			eeprom_busy_wait();
			eeprom_write_block(page_buffer, (void *)address, size);
			eeprom_busy_wait();

			for (i = 0; i < size; i++) {
				data = eeprom_read_byte((void *)(address + i));
				if (data != page_buffer[i])
					goto err_cmdfail;
			}
			break;
		}
		case TARGET_COPROC: {
			if (size > COPROC_E2SIZE ||
			    address >= COPROC_E2SIZE ||
			    address + size > COPROC_E2SIZE)
				goto err_inval;

			//TODO
			break;
		}
		default:
			goto err_context;
		}
		break;
	}
	default:
		goto err_command;
	}

	init_control_reply(reply, REPLY_OK, 0, ctl->seqno);
	return CONTROL_REPLY_SIZE(ok);

err_context:
	reply->error.code = CTLERR_CONTEXT;
	goto error;
err_size:
	reply->error.code = CTLERR_SIZE;
	goto error;
err_command:
	reply->error.code = CTLERR_COMMAND;
	goto error;
err_inval:
	reply->error.code = CTLERR_INVAL;
	goto error;
err_cmdfail:
	reply->error.code = CTLERR_CMDFAIL;
	goto error;
err_checksum:
	reply->error.code = CTLERR_CHECKSUM;
	goto error;

error:
	init_control_reply(reply, REPLY_ERROR, 0, ctl->seqno);
	return CONTROL_REPLY_SIZE(error);
}

static bool should_enter_bootloader(void)
{
	uint8_t value;

	uart_exit();
	/* If PD0 (which is UART RXD) is pulled low, stay in bootloader */
	PORTD = 0x01;
	DDRD = 0x00;
	_delay_ms(25);
	value = PIND;
	uart_init();
	if (value & 0x01)
		return 0;
	return 1;
}

_mainfunc int main(void)
{
	uint8_t mcucsr;

	irq_disable();
	wdt_disable();

	mcucsr = MCUCSR;
	MCUCSR = 0;

	uart_init();
	uart_putstr("BOOT");

	/* Only enter bootloader on special user request
	 * or by jump from application code. */
	if (!should_enter_bootloader()) {
		if ((mcucsr & (1 << EXTRF)) ||
		    (mcucsr & (1 << JTRF)) ||
		    (mcucsr & (1 << PORF)) ||
		    (mcucsr & (1 << WDRF)) ||
		    (mcucsr & (1 << BORF)))
			exit_bootloader();
	}

	/* Disable shift registers OE */
	SR4094_OUTEN_PORT &= ~(1 << SR4094_OUTEN_BIT);
	SR4094_OUTEN_DDR |= (1 << SR4094_OUTEN_BIT);

	disable_all_irq_sources();
	route_irqs_to_bootloader();

	boot_coprocessor_init();
	GICR = 0;
	MCUCR = (0 << ISC11) | (0 << ISC10) |
		(1 << ISC01) | (0 << ISC00);
	pdiusb_init();
	irq_enable();
	while (1)
		pdiusb_work();
}
