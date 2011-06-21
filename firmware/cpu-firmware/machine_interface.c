/*
 *   CNC-remote-control
 *   Machine interface
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

#include "machine_interface.h"
#include "usb.h"
#include "usb_application.h"
#include "util.h"
#include "main.h"
#include "pdiusb.h"
#include "debug.h"
#include "lcd.h"
#include "tiny-list.h"

#include <avr/wdt.h>

#include <string.h>


struct tx_queue_entry {
	struct control_interrupt buffer;
	uint8_t size;
	struct tiny_list list;
};

static struct tx_queue_entry tx_queue_entry_buffer[16];
static struct tiny_list tx_queued;
static struct tiny_list tx_inflight;
static struct tiny_list tx_free;
static bool irq_queue_overflow;


static uint16_t active_devflags;


void usb_app_reset(void)
{
	uint8_t sreg, i;
	struct tx_queue_entry *e;

	sreg = irq_disable_save();

	memset(tx_queue_entry_buffer, 0, sizeof(tx_queue_entry_buffer));

	tlist_init(&tx_queued);
	tlist_init(&tx_inflight);
	tlist_init(&tx_free);
	for (i = 0; i < ARRAY_SIZE(tx_queue_entry_buffer); i++) {
		e = &tx_queue_entry_buffer[i];
		tlist_add_tail(&e->list, &tx_free);
	}

	irq_queue_overflow = 0;

	irq_restore(sreg);
}

void usb_app_highpower(bool granted)
{
	leds_enable(granted);
}

uint16_t get_active_devflags(void)
{
	uint16_t flags;
	uint8_t sreg;

	sreg = irq_disable_save();
	flags = active_devflags;
	irq_restore(sreg);

	return flags;
}

static uint16_t do_modify_devflags(uint16_t mask, uint16_t set)
{
	uint8_t sreg;
	uint16_t flags;

	sreg = irq_disable_save();
	flags = active_devflags;
	flags |= mask & set;
	flags &= ~mask | set;
	active_devflags = flags;
	irq_restore(sreg);

	return flags;
}

void reset_devflags(void)
{
	do_modify_devflags(0xFFFF, 0);
}

void modify_devflags(uint16_t mask, uint16_t set)
{
	struct control_interrupt irq = {
		.id		= IRQ_DEVFLAGS,
	};
	uint8_t sreg;

	sreg = irq_disable_save();
	irq.devflags.flags = do_modify_devflags(mask, set);
	send_interrupt_discard_old(&irq, CONTROL_IRQ_SIZE(devflags));
	irq_restore(sreg);
}

static noreturn void enter_bootloader(void)
{
	debug_printf("Entering bootloader...\n");

	irq_disable();
	wdt_reset();

	pdiusb_exit();

	/* Jump to bootloader code */
	__asm__ __volatile__(
	"ijmp\n"
	: /* None */
	: [_Z]		"z" (BOOT_OFFSET / 2)
	);
	unreachable();
}

static int8_t rx_raw_message(const void *msg, uint8_t ctl_size,
			     void *reply_buf, uint8_t reply_buf_size)
{
	const struct control_message *ctl = msg;
	struct control_reply *reply = reply_buf;

	if (reply_buf_size < CONTROL_REPLY_MAX_SIZE)
		return -1;

	if (ctl_size < CONTROL_MSG_HDR_SIZE)
		goto err_size;
	if (ctl->flags & CONTROL_FLG_BOOTLOADER)
		goto err_context;

	switch (ctl->id) {
	case CONTROL_PING:
		break;
	case CONTROL_RESET: {
		reset_device_state();
		break;
	}
	case CONTROL_DEVFLAGS: {
		uint16_t flags;

		if (ctl_size < CONTROL_MSG_SIZE(devflags))
			goto err_size;

		flags = do_modify_devflags(ctl->devflags.mask,
					   ctl->devflags.set);
		update_userinterface();

		init_control_reply(reply, REPLY_VAL16, 0);
		reply->val16.value = flags;
		return CONTROL_REPLY_SIZE(val16);
	}
	case CONTROL_AXISUPDATE: {
		if (ctl_size < CONTROL_MSG_SIZE(axisupdate))
			goto err_size;

		if (ctl->axisupdate.axis >= AXIS_INVALID)
			goto err_inval;
		axis_pos_update(ctl->axisupdate.axis, ctl->axisupdate.pos);
		break;
	}
	case CONTROL_SPINDLEUPDATE: {
		if (ctl_size < CONTROL_MSG_SIZE(spindleupdate))
			goto err_size;

		spindle_state_update(ctl->spindleupdate.state == SPINDLE_CW);
		break;
	}
	case CONTROL_FOUPDATE: {
		if (ctl_size < CONTROL_MSG_SIZE(feedoverride))
			goto err_size;

		feed_override_feedback_update(ctl->feedoverride.percent);
		break;
	}
	case CONTROL_AXISENABLE: {
		if (ctl_size < CONTROL_MSG_SIZE(axisenable))
			goto err_size;

		if (!ctl->axisenable.mask)
			goto err_inval;
		set_axis_enable_mask(ctl->axisenable.mask);
		break;
	}
	case CONTROL_ESTOPUPDATE: {
		if (ctl_size < CONTROL_MSG_SIZE(estopupdate))
			goto err_size;

		set_estop_state(!!ctl->estopupdate.asserted);
		break;
	}
	case CONTROL_ENTERBOOT: {
		if (ctl_size < CONTROL_MSG_SIZE(enterboot))
			goto err_size;

		if (!control_enterboot_magic_ok(ctl))
			goto err_inval;

		switch (ctl->enterboot.target) {
		case TARGET_CPU:
			lcd_clear_buffer();
			lcd_printf("BOOTLOADER");
			lcd_commit();
			enter_bootloader();
			break;
		case TARGET_COPROC:
		default:
			goto err_context;
		}
		break;
	}
	case CONTROL_EXITBOOT:
		break;
	default:
		goto err_command;
	}

	init_control_reply(reply, REPLY_OK, 0);
	return CONTROL_REPLY_SIZE(ok);

err_command:
	reply->error.code = CTLERR_COMMAND;
	goto error;
err_inval:
	reply->error.code = CTLERR_INVAL;
	goto error;
err_size:
	reply->error.code = CTLERR_SIZE;
	goto error;
err_context:
	reply->error.code = CTLERR_CONTEXT;
	goto error;

error:
	init_control_reply(reply, REPLY_ERROR, 0);
	return CONTROL_REPLY_SIZE(error);
}

uint8_t usb_app_control_setup_rx(struct usb_ctrl *ctl, uint8_t *reply_buf)
{
	DBG(usb_printstr("USB-APP: Received control frame"));

	return USB_APP_UNHANDLED;
}

uint8_t usb_app_ep1_rx(uint8_t *data, uint8_t size,
		       uint8_t *reply_buf)
{
	DBG(usb_printstr("USB-APP: Received EP1 frame"));

	return USB_APP_UNHANDLED;
}

uint8_t usb_app_ep2_rx(uint8_t *data, uint8_t size,
		       uint8_t *reply_buf)
{
	int8_t res;

	DBG(usb_printstr("USB-APP: Received EP2 frame"));

	res = rx_raw_message(data, size, reply_buf, USBCFG_EP1_MAXSIZE);
	if (res < 0)
		return USB_APP_UNHANDLED;
	return res;
}

/* Interrupt endpoint */
uint8_t usb_app_ep1_tx_poll(void *buffer)
{
	struct tx_queue_entry *e, *tmp_e;
	uint8_t ret_size = 0, sreg;

	sreg = irq_disable_save();

	tlist_for_each_delsafe(e, tmp_e, &tx_inflight, list)
		tlist_move_tail(&e->list, &tx_free);

	if (!tlist_is_empty(&tx_queued)) {
		e = tlist_first_entry(&tx_queued, struct tx_queue_entry, list);
		tlist_move_tail(&e->list, &tx_inflight);

		BUILD_BUG_ON(sizeof(e->buffer) > USBCFG_EP1_MAXSIZE);
		memcpy(buffer, &e->buffer, e->size);
		ret_size = e->size;
	}

	irq_restore(sreg);

	return ret_size;
}

uint8_t usb_app_ep2_tx_poll(void *buffer)
{
	return 0;
}

static bool interface_queue_interrupt(const struct control_interrupt *irq,
				      uint8_t size)
{
	struct tx_queue_entry *e;
	uint8_t sreg;

	BUG_ON(size > sizeof(tx_queue_entry_buffer[0].buffer));

	sreg = irq_disable_save();

	if (tlist_is_empty(&tx_free)) {
		irq_restore(sreg);
		return 0;
	}
	e = tlist_last_entry(&tx_free, struct tx_queue_entry, list);
	tlist_move_tail(&e->list, &tx_queued);
	memcpy(&e->buffer, irq, size);
	e->size = size;

	irq_restore(sreg);

	return 1;
}

static void interface_discard_irqs_by_id(uint8_t irq_id)
{
	struct tx_queue_entry *e, *tmp_e;
	uint8_t sreg;

	sreg = irq_disable_save();
	tlist_for_each_delsafe(e, tmp_e, &tx_queued, list) {
		if (e->buffer.id == irq_id) {
			/* Dequeue and discard it. */
			tlist_move_tail(&e->list, &tx_free);
		}
	}
	irq_restore(sreg);
}

static bool interface_drop_one_droppable_irq(void)
{
	struct tx_queue_entry *e;
	uint8_t sreg;
	bool dropped = 0;

	sreg = irq_disable_save();
	tlist_for_each(e, &tx_queued, list) { /* not delsafe */
		if (e->buffer.flags & IRQ_FLG_DROPPABLE) {
			/* Dequeue and discard it. */
			tlist_move_tail(&e->list, &tx_free);
			dropped = 1;
			break;
		}
	}
	irq_restore(sreg);

	return dropped;
}

void send_interrupt(struct control_interrupt *irq, uint8_t size)
{
	bool ok, dropped;
	uint8_t i;

	while (1) {
		for (i = 0; i < 10; i++) {
			if (unlikely(irq_queue_overflow))
				irq->flags |= IRQ_FLG_TXQOVR;

			ok = interface_queue_interrupt(irq, size);
			if (likely(ok)) {
				irq_queue_overflow = 0;
				return;
			}
			irq_queue_overflow = 1;

			if (irqs_disabled())
				break; /* Out of luck. */
			mdelay(5);
		}
		debug_printf("Control IRQ queue overflow\n");

		/* Try to drop IRQs, if this is a higher priority IRQ. */
		if (!(irq->flags & IRQ_FLG_PRIO))
			break;
		dropped = interface_drop_one_droppable_irq();
		if (!dropped)
			break;
		debug_printf("Dropped one droppable IRQ\n");
	}
}

void send_interrupt_discard_old(struct control_interrupt *irq,
				uint8_t size)
{
	interface_discard_irqs_by_id(irq->id);
	send_interrupt(irq, size);
}
