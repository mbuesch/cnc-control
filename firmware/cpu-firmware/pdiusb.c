/*
 *   Philips PDIUSBD12 USB 2.0 device driver
 *
 *   Copyright (C) 2007-2011 Michael Buesch <m@bues.ch>
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

#include "pdiusb.h"
#include "util.h"
#include "main.h"
#include "usb.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <string.h>


/*************************************************************************
 * Microcontroller dependent pin definitions                             *
 * Adjust this to your pin configuration                                 *
 *************************************************************************/

#define PDIUSB_CTL_PIN		PIND
#define PDIUSB_CTL_PORT		PORTD
#define PDIUSB_CTL_DDR		DDRD
#define  PDIUSB_CTL_IRQ		(1 << 3) /* INT_N pin. Mandatory */
#define  PDIUSB_CTL_A0		(1 << 6) /* A0 pin. Mandatory */
#define  PDIUSB_CTL_WR		(1 << 4) /* WD_N pin. Mandatory */
#define  PDIUSB_CTL_RD		(1 << 5) /* RD_N pin. Mandatory */
/* Optional pins. Don't define these, if the pin is not connected. */
//#define  PDIUSB_CTL_RST		(1 << 4) /* RESET_N pin. Optional */
//#define  PDIUSB_CTL_SUSP	(1 << 3) /* SUSPEND pin. Optional */

/* The interrupt vector */
#define PDIUSB_IRQ_VECTOR	INT1_vect

/* Define to 1, if the MCU (Microcontroller) uses the CLKOUT
 * pin for its input clock. Define to 0, if it doesn't. */
#define MCU_USES_CLKOUT		0

/* Endpoint operation mode. (see datasheet)
 * PDIUSB_MODE_EPNONISO	=> Non-ISO mode
 * PDIUSB_MODE_EPISOOUT	=> ISO-OUT mode
 * PDIUSB_MODE_EPISOIN	=> ISO-IN mode
 * PDIUSB_MODE_EPISOBI	=> ISO-I/O mode
 */
#define PDIUSB_OPMODE		PDIUSB_MODE_EPNONISO


/* Prepare for data-read */
static inline void raw_data_in_prepare(void)
{
	/* Disable pullups */
	PORTC = 0;
	/* Pins = input */
	DDRC = 0;
}

/* Read the pins */
static inline uint8_t raw_data_in(void)
{
	return PINC;
}

/* Prepare for data-write */
static inline void raw_data_out_prepare(void)
{
	/* Pins = output */
	DDRC = 0xFF;
}

/* Write the pins */
static inline void raw_data_out(uint8_t data)
{
	PORTC = data;
}

/* Delay for the data-in/out routine. */
static inline void raw_data_delay(void)
{
	nop();
	nop();
}

/* Enable the PDIUSB interrupt line */
static inline void pdiusb_interrupt_enable(void)
{
	GICR |= (1 << INT1);
}

/* Disable the PDIUSB interrupt line */
static inline void pdiusb_interrupt_disable(void)
{
	GICR &= ~(1 << INT1);
}

/* Clear the PDIUSB interrupt line flag */
static inline void pdiusb_interrupt_flag_clear(void)
{
	GIFR |= (1 << INTF1);
}

/*************************************************************************
 * END: Microcontroller dependent pin definitions                        *
 *************************************************************************/


typedef uint16_t trans_stat_t;
#define TRANS_STAT(status, size)	((uint16_t)(status) | ((uint16_t)(size) << 8))
#define GET_TRANS_STAT(trans)		((uint16_t)(trans) & 0xFF)
#define GET_TRANS_SIZE(trans)		(((uint16_t)(trans) >> 8) & 0xFF)

/* Define the optional pins to zero, if they are undefined. */
#ifndef PDIUSB_CTL_RST
# define PDIUSB_CTL_RST		0
#endif
#ifndef PDIUSB_CTL_SUSP
# define PDIUSB_CTL_SUSP	0
#endif

#define PDIUSB_EP0_MAXSIZE	16
#define PDIUSB_EP1_MAXSIZE	16

#if PDIUSB_OPMODE == PDIUSB_MODE_EPNONISO
# define PDIUSB_EP2_MAXSIZE	64
#elif PDIUSB_OPMODE == PDIUSB_MODE_EPISOOUT
# define PDIUSB_EP2_MAXSIZE	128
#elif PDIUSB_OPMODE == PDIUSB_MODE_EPISOIN
# define PDIUSB_EP2_MAXSIZE	128
#elif PDIUSB_OPMODE == PDIUSB_MODE_EPISOBI
# define PDIUSB_EP2_MAXSIZE	64
#else
# error "Invalid PDIUSB_OPMODE"
#endif

#define PDIUSB_MAXSIZE		(max(PDIUSB_EP0_MAXSIZE, \
				     max(PDIUSB_EP1_MAXSIZE, \
					 PDIUSB_EP2_MAXSIZE)))

/* Buffer for holding received data. */
static uint8_t pdiusb_buffer[PDIUSB_MAXSIZE];
/* Suspend status */
static uint8_t pdiusb_suspended;
/* IRQ status */
static uint16_t pdiusb_irq_status;



static inline void pdiusb_command_mode(void)
{
	PDIUSB_CTL_PORT |= PDIUSB_CTL_A0;
}

static inline void pdiusb_data_mode(void)
{
	PDIUSB_CTL_PORT &= ~PDIUSB_CTL_A0;
}

/* Write data to the controller. */
static void pdiusb_write(uint8_t data)
{
	raw_data_out_prepare();
	PDIUSB_CTL_PORT &= ~PDIUSB_CTL_WR;
	raw_data_out(data);
	PDIUSB_CTL_PORT |= PDIUSB_CTL_WR;
	raw_data_delay();
}

/* Read data from the controller. */
static uint8_t pdiusb_read(void)
{
	uint8_t data;

	raw_data_in_prepare();
	PDIUSB_CTL_PORT &= ~PDIUSB_CTL_RD;
	raw_data_delay();
	data = raw_data_in();
	PDIUSB_CTL_PORT |= PDIUSB_CTL_RD;

	return data;
}

/* Send a command to the controller. */
static void pdiusb_command(uint8_t command)
{
	pdiusb_command_mode();
	pdiusb_write(command);
	pdiusb_data_mode();
}

/* Send a command and write 8-bit of command data. */
static void pdiusb_command_w8(uint8_t command, uint8_t data)
{
	pdiusb_command(command);
	pdiusb_write(data);
}

/* Send a command and write 16-bit of command data. */
static void pdiusb_command_w16(uint8_t command, uint16_t data)
{
	pdiusb_command(command);
	pdiusb_write(data);
	pdiusb_write(data >> 8);
}

/* Send a command and read 8-bit of command data. */
static uint8_t pdiusb_command_r8(uint8_t command)
{
	uint8_t data;

	pdiusb_command(command);
	data = pdiusb_read();

	return data;
}

/* Send a command and read 16-bit of command data. */
static uint16_t pdiusb_command_r16(uint8_t command)
{
	uint16_t a, b;

	pdiusb_command(command);
	a = pdiusb_read();
	b = pdiusb_read();

	return (a | (b << 8));
}

static uint8_t pdiusb_read_buffer(uint8_t *buf, uint8_t max_size)
{
	uint8_t i, data_size;

	pdiusb_command(PDIUSB_CMD_RWBUF);
	pdiusb_read(); /* Read the reserved byte */
	data_size = pdiusb_read();
	if (data_size > max_size) {
		usb_print1num("PDIUSB: RX buffer overrun", data_size);
		return 0;
	}
	for (i = 0; i < data_size; i++)
		buf[i] = pdiusb_read();

	DBG(usb_print1num("PDIUSB: Received", data_size));
	DBG(usb_dumpmem(buf, data_size));

	return data_size;
}

static void pdiusb_write_buffer(const uint8_t *buf, uint8_t size)
{
	uint8_t i;

	DBG(usb_print1num("PDIUSB: Sending", size));
	DBG(usb_dumpmem(buf, size));

	pdiusb_command(PDIUSB_CMD_RWBUF);
	pdiusb_write(0); /* Write the reserved byte */
	pdiusb_write(size);
	for (i = 0; i < size; i++)
		pdiusb_write(buf[i]);
}

static void stall_ep(uint8_t ep_index)
{
	DBG(usb_print1num("PDIUSB: Stalling EP index", ep_index));

	pdiusb_command_w8(PDIUSB_CMD_SEPSTAT(ep_index),
			  PDIUSB_SEPSTAT_STALL);
}

static void unstall_ep(uint8_t ep_index)
{
	DBG(usb_print1num("PDIUSB: Unstalling EP index", ep_index));

	pdiusb_command_w8(PDIUSB_CMD_SEPSTAT(ep_index), 0);
}

static uint8_t ep_is_stalled(uint8_t ep_index)
{
	return (pdiusb_command_r8(PDIUSB_CMD_GEPSTAT(ep_index))
		& PDIUSB_GEPSTAT_STALL);
}

static trans_stat_t handle_irq_ep_out(uint8_t ep_index)
{
	uint8_t status, size;

	DBG(usb_print1num("PDIUSB: OUT irq on EP", ep_index));

	status = pdiusb_command_r8(PDIUSB_CMD_TRSTAT(ep_index));
	if (!(status & PDIUSB_TRSTAT_TRANSOK)) {
		if (status != PDIUSB_TRERR_NOERR) {
			usb_print2num("PDIUSB: OUT trans on EP", ep_index, "failed with",
				      status & PDIUSB_TRSTAT_ERR);
			return TRANS_STAT(0, 0);
		}
	}
	pdiusb_command(PDIUSB_CMD_SELEP(ep_index));
	size = pdiusb_read_buffer(pdiusb_buffer, sizeof(pdiusb_buffer));
	if (status & PDIUSB_TRSTAT_SETUP) {
		pdiusb_command(PDIUSB_CMD_SELEP(PDIUSB_EPIDX_IN(ep_index)));
		pdiusb_command(PDIUSB_CMD_ACKSETUP);
		pdiusb_command(PDIUSB_CMD_SELEP(ep_index));
		pdiusb_command(PDIUSB_CMD_ACKSETUP);
	}
	pdiusb_command(PDIUSB_CMD_CLRBUF);

	return TRANS_STAT(status | PDIUSB_TRSTAT_TRANSOK, size);
}

static bool handle_irq_ep_in(uint8_t ep_index)
{
	uint8_t status;

//	DBG(usb_print1num("PDIUSB: IN irq on EP", ep_index));

	status = pdiusb_command_r8(PDIUSB_CMD_TRSTAT(ep_index));
	if (!(status & PDIUSB_TRSTAT_TRANSOK)) {
		status = status & PDIUSB_TRSTAT_ERR;
		if (status != PDIUSB_TRERR_NOERR && status != PDIUSB_TRERR_NAK) {
			usb_print2num("PDIUSB: trans on EP", ep_index, "failed with",
				      status);
			return 0;
		}
	}

	/* For non-doublebuffered EPs, check if the buffer is ready. */
	if (ep_index != PDIUSB_EP_EP2IN) {
		status = pdiusb_command_r8(PDIUSB_CMD_SELEP(ep_index));
		if (status & PDIUSB_SELEPR_FULL)
			return 0; /* Not yet */
	}

	return 1;
}

static void ep_queue_data(uint8_t ep_index, void *data, uint8_t size)
{
	pdiusb_command(PDIUSB_CMD_SELEP(ep_index));
	pdiusb_write_buffer(data, size);
	pdiusb_command(PDIUSB_CMD_VALBUF);
}

static void handle_ctlout_data(uint8_t trans_status, uint8_t size)
{
	uint8_t res;

	if (trans_status & PDIUSB_TRSTAT_SETUP) {
		if (size == sizeof(struct usb_ctrl)) {
			res = usb_control_setup_rx((struct usb_ctrl *)pdiusb_buffer);
		} else {
			usb_printstr("PDIUSB: CTLOUT received invalid SETUP");
			res = USB_RX_ERROR;
		}
	} else
		res = usb_control_rx(pdiusb_buffer, size);
	if (res == USB_RX_ERROR)
		stall_ep(PDIUSB_EP_CTLOUT);
}

#if USB_WITH_EP1
static void handle_ep1out_data(uint8_t trans_status, uint8_t size)
{
	uint8_t res;

	res = usb_ep1_rx(pdiusb_buffer, size);
	if (res == USB_RX_ERROR)
		stall_ep(PDIUSB_EP_EP1OUT);
}
#endif

#if USB_WITH_EP2
static void handle_ep2out_data(uint8_t trans_status, uint8_t size)
{
	uint8_t res;

	res = usb_ep2_rx(pdiusb_buffer, size);
	if (res == USB_RX_ERROR)
		stall_ep(PDIUSB_EP_EP2OUT);
}
#endif

static void handle_irq_busrst(void)
{
	usb_printstr("PDIUSB: Bus reset detected");
	pdiusb_suspended = 0;
	usb_reset();
}

static void handle_irq_suspchg(void)
{
	if (PDIUSB_CTL_PIN & PDIUSB_CTL_SUSP) {
		if (!pdiusb_suspended)
			usb_printstr("PDIUSB: Suspended");
		pdiusb_suspended = 1;
	} else {
		if (pdiusb_suspended)
			usb_printstr("PDIUSB: Resumed");
		pdiusb_suspended = 0;
	}
}

static void handle_irq_dmaeot(void)
{
}

ISR(PDIUSB_IRQ_VECTOR)
{
	uint16_t status;

	status = pdiusb_command_r16(PDIUSB_CMD_IRQSTAT);
	if (status) {
		pdiusb_interrupt_disable();
		pdiusb_irq_status = status;
	}
}

void pdiusb_work(void)
{
	uint16_t status;
	trans_stat_t trans;
	bool ok;
	void *buf;
	uint8_t size;

	/* Check status==0 without disabling IRQs first.
	 * This might race with the interrupt handler, but it is safe.
	 */
	mb();
	if (!pdiusb_irq_status)
		return;

	irq_disable();

	status = pdiusb_irq_status;
	pdiusb_irq_status = 0;

	if (status & PDIUSB_IST_BUSRST)
		handle_irq_busrst();
	if (status & PDIUSB_IST_SUSPCHG)
		handle_irq_suspchg();
	if (status & PDIUSB_IST_DMAEOT)
		handle_irq_dmaeot();

	if (status & PDIUSB_IST_EP(PDIUSB_EP_CTLOUT)) {
		trans = handle_irq_ep_out(PDIUSB_EP_CTLOUT);
		if (GET_TRANS_STAT(trans) & PDIUSB_TRSTAT_TRANSOK) {
			handle_ctlout_data(GET_TRANS_STAT(trans),
					   GET_TRANS_SIZE(trans));
		}
	}
	if (status & PDIUSB_IST_EP(PDIUSB_EP_CTLIN)) {
		ok = handle_irq_ep_in(PDIUSB_EP_CTLIN);
		if (ok) {
			size = usb_control_tx_poll(&buf, PDIUSB_EP0_MAXSIZE);
			if (size != USB_TX_POLL_NONE)
				ep_queue_data(PDIUSB_EP_CTLIN, buf, size);
		}
	}

	if (status & PDIUSB_IST_EP(PDIUSB_EP_EP1OUT)) {
		trans = handle_irq_ep_out(PDIUSB_EP_EP1OUT);
#if USB_WITH_EP1
		if (GET_TRANS_STAT(trans) & PDIUSB_TRSTAT_TRANSOK) {
			handle_ep1out_data(GET_TRANS_STAT(trans),
					   GET_TRANS_SIZE(trans));
		}
#endif
	}
	if (status & PDIUSB_IST_EP(PDIUSB_EP_EP1IN)) {
		ok = handle_irq_ep_in(PDIUSB_EP_EP1IN);
#if USB_WITH_EP1
		if (ok) {
			size = usb_ep1_tx_poll(&buf, PDIUSB_EP1_MAXSIZE);
			if (size != USB_TX_POLL_NONE)
				ep_queue_data(PDIUSB_EP_EP1IN, buf, size);
		}
#endif
	}

	if (status & PDIUSB_IST_EP(PDIUSB_EP_EP2OUT)) {
		trans = handle_irq_ep_out(PDIUSB_EP_EP2OUT);
#if USB_WITH_EP2
		if (GET_TRANS_STAT(trans) & PDIUSB_TRSTAT_TRANSOK) {
			handle_ep2out_data(GET_TRANS_STAT(trans),
					   GET_TRANS_SIZE(trans));
		}
#endif
	}
	if (status & PDIUSB_IST_EP(PDIUSB_EP_EP2IN)) {
		ok = handle_irq_ep_in(PDIUSB_EP_EP2IN);
#if USB_WITH_EP2
		if (ok) {
			size = usb_ep2_tx_poll(&buf, PDIUSB_EP2_MAXSIZE);
			if (size != USB_TX_POLL_NONE)
				ep_queue_data(PDIUSB_EP_EP2IN, buf, size);
		}
#endif
	}

	pdiusb_interrupt_enable();
	irq_enable();
}

#if MCU_USES_CLKOUT

#ifndef F_CPU
# error "F_CPU not defined. Unknown CPU frequency"
#endif

#if F_CPU == 48000000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_48MHZ
#elif F_CPU == 24000000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_24MHZ
#elif F_CPU == 16000000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_16MHZ
#elif F_CPU == 12000000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_12MHZ
#elif F_CPU == 9600000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_9p6MHZ
#elif F_CPU == 8000000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_8MHZ
#elif F_CPU == 6857142UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_6p9MHZ
#elif F_CPU == 6000000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_6MHZ
#elif F_CPU == 5333333UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_5p3MHZ
#elif F_CPU == 4800000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_4p8MHZ
#elif F_CPU == 4363636UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_4p4MHZ
#elif F_CPU == 4000000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_4MHZ
#elif F_CPU == 3692307UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_3p7MHZ
#elif F_CPU == 3428571UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_3p4MHZ
#elif F_CPU == 3200000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_3p2MHZ
#elif F_CPU == 3000000UL
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_3MHZ
#else
# error "Impossible CLKOUT frequency. Please adjust F_CPU"
#endif

#endif /* MCU_USES_CLKOUT */

/* Default value, if CLKOUT is unused. */
#ifndef PDIUSB_CLKOUT_DIVISOR
# define PDIUSB_CLKOUT_DIVISOR		PDIUSB_CLKOUT_3MHZ
#endif

static void pdiusb_set_mode(uint16_t mode)
{
	mode |= PDIUSB_MODE_STO;	/* Always set */
	mode |= PDIUSB_MODE_NOLAZYCLK;	/* Always keep CLKOUT at full speed */
	mode |= (PDIUSB_CLKOUT_DIVISOR << PDIUSB_MODE_CLKDIV_SHIFT)
		& PDIUSB_MODE_CLKDIV;	/* CLKOUT pin divisor */
	mode |= PDIUSB_MODE_CLKARUN;	/* Clock is always running */
	mode |= PDIUSB_MODE_IRQM;	/* Report errors */
	mode |= PDIUSB_OPMODE;		/* Endpoint configuration */
	pdiusb_command_w16(PDIUSB_CMD_SETMODE, mode);
}

/* Basic port setup */
static uint16_t pdiusb_configure_ports(void)
{
	PDIUSB_CTL_PORT |= (PDIUSB_CTL_WR | PDIUSB_CTL_RD | PDIUSB_CTL_RST);
	PDIUSB_CTL_PORT &= ~(PDIUSB_CTL_IRQ | PDIUSB_CTL_SUSP);
	PDIUSB_CTL_DDR |= (PDIUSB_CTL_A0 | PDIUSB_CTL_WR |
			   PDIUSB_CTL_RD | PDIUSB_CTL_RST);
	PDIUSB_CTL_DDR &= ~(PDIUSB_CTL_IRQ | PDIUSB_CTL_SUSP);
	pdiusb_data_mode();

	/* Return the chipID to verify the device is working. */
	return pdiusb_command_r16(PDIUSB_CMD_GETCHIPID);
}

uint8_t pdiusb_configure_clkout(void)
{
	uint16_t chipid;

	if (!MCU_USES_CLKOUT)
		return 0;

	chipid = pdiusb_configure_ports();
	if (chipid != PDIUSB_CHIPID)
		return 1;
	pdiusb_set_mode(0);
	_delay_ms(10);

	return 0;
}

uint8_t pdiusb_init(void)
{
	uint16_t chipid;

	usb_reset();

	chipid = pdiusb_configure_ports();
	if (chipid != PDIUSB_CHIPID) {
		usb_print1num("PDIUSB unknown chip ID:", chipid);
		return 1;
	}

	pdiusb_set_mode(0);
	pdiusb_command_w8(PDIUSB_CMD_DMA, 0);
	_delay_ms(50);
	/* Enable software USB pullup */
	pdiusb_set_mode(PDIUSB_MODE_SOFTCONN);

	unstall_ep(PDIUSB_EP_CTLOUT);
	unstall_ep(PDIUSB_EP_CTLIN);

	pdiusb_interrupt_flag_clear();
	pdiusb_interrupt_enable();

	return 0;
}

void pdiusb_exit(void)
{
	uint8_t ep_index;

	pdiusb_interrupt_disable();

	for (ep_index = 0; ep_index < PDIUSB_EP_COUNT; ep_index++)
		stall_ep(ep_index);
	usb_set_address(0);
	pdiusb_set_mode(0); /* Disconnect SOFTCONN */

	long_delay_ms(500); /* Wait for host to handle disconnect */
}

/*************************************************************************
 * Callbacks from the USB stack                                          *
 *************************************************************************/

void usb_set_address(uint8_t address)
{
	address &= PDIUSB_ADDR;
	if (address)
		address |= PDIUSB_AEN;
	pdiusb_command_w8(PDIUSB_CMD_ADDREN, address);
}

void usb_enable_endpoints(uint8_t enable)
{
	pdiusb_command_w8(PDIUSB_CMD_ENDPEN, 0);
	if (enable)
		pdiusb_command_w8(PDIUSB_CMD_ENDPEN, PDIUSB_GENISOEN);
}

static uint8_t pdiusb_ep_addr_to_ep_index(uint8_t ep)
{
	uint8_t ep_index;

	switch (ep & ~0x80) {
	case 0:
		ep_index = PDIUSB_EP_CTLOUT;
		break;
#if USB_WITH_EP1
	case 1:
		ep_index = PDIUSB_EP_EP1OUT;
		break;
#endif
#if USB_WITH_EP2
	case 2:
		ep_index = PDIUSB_EP_EP2OUT;
		break;
#endif
	default:
		return 0xFF;
	}
	if (usb_ep_is_in(ep))
		ep_index = PDIUSB_EPIDX_IN(ep_index);

	return ep_index;
}

void usb_stall_endpoint(uint8_t ep)
{
	uint8_t ep_index;

	DBG(usb_print1num("PDIUSB: Stalling EP", ep));

	ep_index = pdiusb_ep_addr_to_ep_index(ep);
	if (ep_index == 0xFF) {
		usb_print1num("PDIUSB: stall-EP unknown EP", ep);
		return;
	}
	stall_ep(ep_index);
}

void usb_unstall_endpoint(uint8_t ep)
{
	uint8_t ep_index;

	DBG(usb_print1num("PDIUSB: Unstalling EP", ep));

	ep_index = pdiusb_ep_addr_to_ep_index(ep);
	if (ep_index == 0xFF) {
		usb_print1num("PDIUSB: unstall-EP unknown EP", ep);
		return;
	}
	unstall_ep(ep_index);
}

uint8_t usb_endpoint_is_stalled(uint8_t ep)
{
	uint8_t ep_index;

	ep_index = pdiusb_ep_addr_to_ep_index(ep);
	if (ep_index == 0xFF) {
		usb_print1num("PDIUSB: EP-is-stalled unknown EP", ep);
		return 1;
	}

	return ep_is_stalled(ep_index);
}
