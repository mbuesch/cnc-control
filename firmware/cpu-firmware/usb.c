/*
 *   Tiny USB stack
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

#include "usb.h"
#include "usb_config.h"
#include "usb_application.h"
#include "main.h"
#include "util.h"

#include <string.h>
#include <avr/pgmspace.h>

#if USB_MINI
# include "descriptor_table_mini.h"
#else
# include "descriptor_table.h"
#endif


enum usb_frame_status {
	USB_FRAME_UNHANDLED,
	USB_FRAME_HANDLED,
	USB_FRAME_ERROR,
};

static uint8_t usb_control_buf[USBCFG_EP0_MAXSIZE];
static uint8_t usb_control_ptr;
static uint8_t usb_control_len;
static bool usb_control_nullframe_pending;
#if USB_WITH_EP1
static uint8_t usb_ep1_buf[USBCFG_EP1_MAXSIZE];
static uint8_t usb_ep1_ptr;
static uint8_t usb_ep1_len;
#endif /* WITH_EP1 */
#if USB_WITH_EP2
static uint8_t usb_ep2_buf[USBCFG_EP2_MAXSIZE];
static uint8_t usb_ep2_ptr;
static uint8_t usb_ep2_len;
#endif /* WITH_EP2 */

/* Current USB_DEVICE_... bits. */
static uint16_t usb_device_status;
/* The active bConfigurationValue (or zero if none). */
static uint8_t usb_active_configuration;


void usb_reset(void)
{
	usb_control_len = 0;
	usb_control_nullframe_pending = 0;
#if USB_WITH_EP1
	usb_ep1_len = 0;
#endif
#if USB_WITH_EP2
	usb_ep2_len = 0;
#endif
	usb_device_status = ((!!USBCFG_SELFPOWERED) << USB_DEVICE_SELF_POWERED);
	usb_active_configuration = 0;

	usb_app_reset();
}

static uint8_t create_device_descriptor(void *buf)
{
	DBG(usb_printstr("USB: Requested device descriptor"));

	usb_copy_from_pgm(buf, &device_descriptor, sizeof(device_descriptor));

	return sizeof(device_descriptor);
}

static uint8_t create_config_descriptor(void *buf, uint8_t index)
{
	const uint8_t * USB_PROGMEM ptr;
	uint8_t size;

	DBG(usb_print1num("USB: Requested config descriptor", index));

	if (index >= ARRAY_SIZE(config_descriptor_ptrs)) {
		usb_printstr("USB: Get config descriptor index out of range");
		return 0xFF;
	}

	ptr = (void *)(uintptr_t)usb_pgm_read(&config_descriptor_ptrs[index].ptr);
	size = (uint8_t)usb_pgm_read(&config_descriptor_ptrs[index].size);

	usb_copy_from_pgm(buf, ptr, size);

	return size;
}

static uint8_t create_string_descriptor(void *buf, uint8_t index)
{
	struct usb_string_descriptor *s = buf;
	const char * USB_PROGMEM string;
	uint8_t size;

	DBG(usb_print1num("USB: Requested string descriptor", index));

	if (index >= ARRAY_SIZE(string_descriptor_ptrs)) {
		usb_printstr("USB: Get string descriptor index out of range");
		return 0xFF;
	}

	string = (void *)(uintptr_t)usb_pgm_read(&string_descriptor_ptrs[index].ptr);
	size = (uint8_t)usb_pgm_read(&string_descriptor_ptrs[index].size);

	s->bLength = (uint8_t)(2u + size);
	s->bDescriptorType = USB_DT_STRING;
	usb_copy_from_pgm(s->string, string, size);

	return s->bLength;
}

static uint8_t usb_set_configuration(uint8_t bConfigurationValue)
{
	DBG(usb_print1num("USB: Set configuration", bConfigurationValue));

	if (bConfigurationValue) {
		/* Select a configuration */
		if (bConfigurationValue - 1u >= ARRAY_SIZE(config_descriptor_ptrs)) {
			usb_printstr("USB: Invalid bConfigurationValue");
			return 1;
		}
		usb_enable_endpoints(1);
		usb_app_highpower(1);
	} else {
		/* Deconfigure the device */
		usb_app_highpower(0);
		usb_enable_endpoints(0);
	}
	usb_active_configuration = bConfigurationValue;

	return 0;
}

static uint8_t usb_control_endpoint_rx(struct usb_ctrl *ctl)
{
	switch (ctl->bRequest) {
	case USB_REQ_GET_STATUS: {
		uint16_t index = le16_to_cpu(ctl->wIndex);

		DBG(usb_print1num("USB: EP get status on", index));

		usb_control_buf[0] = 0;
		usb_control_buf[1] = 0;
		usb_control_len = 2;
		if (index <= 0xFFu &&
		    usb_endpoint_is_stalled((uint8_t)index))
			usb_control_buf[0] |= (1 << USB_ENDPOINT_HALT);
		break;
	}
	case USB_REQ_CLEAR_FEATURE: {
		uint16_t index = le16_to_cpu(ctl->wIndex);
		uint16_t feature = le16_to_cpu(ctl->wValue);

		DBG(usb_print2num("USB: EP clear feature", feature,
				  "on", index));

		if (index <= 0xFFu &&
		    (feature & (1 << USB_ENDPOINT_HALT)))
			usb_unstall_endpoint((uint8_t)index);
		break;
	}
	case USB_REQ_SET_FEATURE: {
		uint16_t index = le16_to_cpu(ctl->wIndex);
		uint16_t feature = le16_to_cpu(ctl->wValue);

		DBG(usb_print2num("USB: EP set feature", feature,
				  "on", index));

		if (index <= 0xFFu &&
		    (feature & (1 << USB_ENDPOINT_HALT)))
			usb_stall_endpoint((uint8_t)index);
		break;
	}
	default:
		return USB_FRAME_UNHANDLED;
	}

	return USB_FRAME_HANDLED;
}

static uint8_t usb_control_interface_rx(struct usb_ctrl *ctl)
{
	switch (ctl->bRequest) {
	case USB_REQ_GET_INTERFACE: {
		uint8_t bAlternateSetting;

		DBG(usb_printstr("USB: IF get interface"));

		/* We only support one altsetting. */
		bAlternateSetting = 0;

		usb_control_buf[0] = bAlternateSetting;
		usb_control_len = 1;

		return USB_FRAME_HANDLED;
	}
	case USB_REQ_SET_INTERFACE: {
		uint16_t bInterfaceNumber = le16_to_cpu(ctl->wIndex);
		uint16_t bAlternateSetting = le16_to_cpu(ctl->wValue);

		DBG(usb_print2num("USB: IF set interface", bInterfaceNumber,
				  "altsetting", bAlternateSetting));

		/* We only support one interface and altsetting */
		if (bInterfaceNumber != 0 ||
		    bAlternateSetting != 0)
			return USB_FRAME_ERROR;

		return USB_FRAME_HANDLED;
	}
	case USB_REQ_GET_STATUS: {
		DBG(usb_printstr("USB: IF get status"));

		usb_control_buf[0] = 0;
		usb_control_buf[1] = 0;
		usb_control_len = 2;

		return USB_FRAME_HANDLED;
	}
	case USB_REQ_SET_FEATURE: {
		DBG(usb_printstr("USB: IF set feature"));
		return USB_FRAME_HANDLED;
	}
	case USB_REQ_CLEAR_FEATURE: {
		DBG(usb_printstr("USB: IF clear feature"));
		return USB_FRAME_HANDLED;
	} }

	return USB_FRAME_UNHANDLED;
}

static uint8_t usb_control_device_rx(struct usb_ctrl *ctl)
{
	uint8_t res;

	switch (ctl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR: {
		switch (le16_to_cpu(ctl->wValue) >> 8) {
		case USB_DT_DEVICE:
			res = create_device_descriptor(usb_control_buf);
			break;
		case USB_DT_CONFIG:
			res = create_config_descriptor(
				usb_control_buf, le16_to_cpu(ctl->wValue) & 0xFF);
			break;
		case USB_DT_STRING:
			res = create_string_descriptor(
				usb_control_buf, le16_to_cpu(ctl->wValue) & 0xFF);
			break;
		default:
			return USB_FRAME_UNHANDLED;
		}
		if (res == 0xFF)
			return USB_FRAME_ERROR;
		usb_control_len = res;
		break;
	}
	case USB_REQ_SET_ADDRESS: {
		uint16_t address = le16_to_cpu(ctl->wValue);

		if (address <= 0x7Fu) {
			DBG(usb_print1num("USB: DEV set address to", address));

			usb_set_address((uint8_t)address);
		}
		break;
	}
	case USB_REQ_GET_CONFIGURATION: {
		DBG(usb_printstr("USB: DEV get configuration"));

		usb_control_buf[0] = usb_active_configuration;
		usb_control_len = 1;

		break;
	}
	case USB_REQ_SET_CONFIGURATION: {
		uint16_t cfg = le16_to_cpu(ctl->wValue);

		if (cfg > 0xFFu ||
		    usb_set_configuration((uint8_t)cfg))
			return USB_FRAME_ERROR;
		break;
	}
	case USB_REQ_GET_STATUS: {
		DBG(usb_printstr("USB: DEV get status"));

		usb_control_buf[0] = (uint8_t)usb_device_status;
		usb_control_buf[1] = (uint8_t)(usb_device_status >> 8);
		usb_control_len = 2;
		break;
	}
	case USB_REQ_SET_FEATURE: {
		uint16_t feature = le16_to_cpu(ctl->wValue);

		DBG(usb_printstr("USB: DEV set feature"));
		if (feature >= 16 || feature == USB_DEVICE_SELF_POWERED) {
			usb_print1num("USB: Illegal set feature request", feature);
			return USB_FRAME_ERROR;
		}
		usb_device_status |= ((uint16_t)1 << feature);
		break;
	}
	case USB_REQ_CLEAR_FEATURE: {
		uint16_t feature = le16_to_cpu(ctl->wValue);

		DBG(usb_printstr("USB: DEV clear feature"));
		if (feature >= 16 || feature == USB_DEVICE_SELF_POWERED) {
			usb_print1num("USB: Illegal clear feature request", feature);
			return USB_FRAME_ERROR;
		}
		usb_device_status &= ~((uint16_t)1 << feature);
		break;
	}
	default:
		return USB_FRAME_UNHANDLED;
	}

	return USB_FRAME_HANDLED;
}

uint8_t usb_control_setup_rx(struct usb_ctrl *ctl)
{
	uint8_t status = USB_FRAME_UNHANDLED, res;

	usb_control_len = 0;
	usb_control_ptr = 0;
	usb_control_nullframe_pending = 0;

	switch (ctl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		status = usb_control_device_rx(ctl);
		break;
	case USB_RECIP_INTERFACE:
		status = usb_control_interface_rx(ctl);
		break;
	case USB_RECIP_ENDPOINT:
		status = usb_control_endpoint_rx(ctl);
		break;
	}
	if (status == USB_FRAME_ERROR)
		return USB_RX_ERROR;
	if (status == USB_FRAME_UNHANDLED) {
		/* Nobody handled the frame.
		 * Try if the application layer is able to service the frame. */
		res = usb_app_control_setup_rx(ctl, usb_control_buf);
		if (res == USB_APP_UNHANDLED) {
			usb_printstr("USB: Unhandled control frame:");
			usb_dumpmem(ctl, sizeof(*ctl));
			return USB_RX_DONE;
		}
		usb_control_len = res;
	}

	if (usb_ctrl_is_out(ctl)) { /* OUT transfer */
		usb_control_nullframe_pending = 1;
		if (unlikely(usb_control_len)) {
			usb_control_len = 0;
			usb_printstr("USB: Want to reply, but host did not request it");
			return USB_RX_ERROR;
		}
	} else { /* IN transfer */
		uint16_t len = le16_to_cpu(ctl->wLength);

		if ((uint16_t)usb_control_len > len)
			usb_control_len = (uint8_t)len;
	}

	return USB_RX_DONE;
}

uint8_t usb_control_rx(void *data, uint8_t size)
{
	if (size == 0) {
		DBG(usb_printstr("USB: Received data ACK (zero size data1)"));
		return USB_RX_DONE;
	}

	usb_print1num("USB: Unhandled control RX of size", size);

	return USB_RX_ERROR;
}

static noinline uint8_t usb_generic_tx_poll(void **data, uint8_t chunksize,
			uint8_t *buffer, uint8_t *buffer_size, uint8_t *buffer_ptr)
{
	if (chunksize > *buffer_size)
		chunksize = *buffer_size;
	*buffer_size = (uint8_t)(*buffer_size - chunksize);
	*data = buffer + *buffer_ptr;
	if (*buffer_size)
		*buffer_ptr = (uint8_t)(*buffer_ptr + chunksize);
	else
		*buffer_ptr = 0u;
	if (!chunksize)
		return USB_TX_POLL_NONE;

	return chunksize;
}

uint8_t usb_control_tx_poll(void **data, uint8_t chunksize)
{
	uint8_t res;

	res = usb_generic_tx_poll(data, chunksize, usb_control_buf,
				  &usb_control_len, &usb_control_ptr);
	if (res == USB_TX_POLL_NONE) {
		if (usb_control_nullframe_pending) {
			/* Send zero length frame */
			usb_control_nullframe_pending = 0;
			return 0;
		}
	}

	return res;
}

#if USB_WITH_EP1
uint8_t usb_ep1_rx(void *data, uint8_t size)
{
	uint8_t res;

	usb_ep1_len = 0;
	usb_ep1_ptr = 0;

	res = usb_app_ep1_rx(data, size, usb_ep1_buf);
	if (res != USB_APP_UNHANDLED) {
		usb_ep1_len = res;
		return USB_RX_DONE;
	}

	usb_printstr("USB: Unhandled EP1 frame:");
	usb_dumpmem(data, size);

	return USB_RX_DONE;
}

uint8_t usb_ep1_tx_poll(void **data, uint8_t chunksize)
{
	uint8_t res;

	if (usb_ep1_len == 0) {
		res = usb_app_ep1_tx_poll(usb_ep1_buf);
		if (res == USB_APP_UNHANDLED)
			return USB_TX_POLL_NONE;
		usb_ep1_len = res;
		usb_ep1_ptr = 0;
		if (!res)
			return 0;
	}

	res = usb_generic_tx_poll(data, chunksize, usb_ep1_buf,
				  &usb_ep1_len, &usb_ep1_ptr);

	return res;
}
#endif /* WITH_EP1 */

#if USB_WITH_EP2
uint8_t usb_ep2_rx(void *data, uint8_t size)
{
	uint8_t res;

	usb_ep2_len = 0;
	usb_ep2_ptr = 0;

	res = usb_app_ep2_rx(data, size, usb_ep2_buf);
	if (res != USB_APP_UNHANDLED) {
		usb_ep2_len = res;
		return USB_RX_DONE;
	}

	usb_printstr("USB: Unhandled EP2 frame:");
	usb_dumpmem(data, size);

	return USB_RX_DONE;
}

uint8_t usb_ep2_tx_poll(void **data, uint8_t chunksize)
{
	uint8_t res;

	if (usb_ep2_len == 0) {
		res = usb_app_ep2_tx_poll(usb_ep2_buf);
		if (res == USB_APP_UNHANDLED)
			return USB_TX_POLL_NONE;
		usb_ep2_len = res;
		usb_ep2_ptr = 0;
		if (!res)
			return 0;
	}

	res = usb_generic_tx_poll(data, chunksize, usb_ep2_buf,
				  &usb_ep2_len, &usb_ep2_ptr);

	return res;
}
#endif /* WITH_EP2 */
