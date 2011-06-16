#ifndef USBSTACK_APPLICATION_H_
#define USBSTACK_APPLICATION_H_
/*
 *   Tiny USB stack
 *   Application level code
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

#include "usb_config.h"
#include "util.h"

#include <stdint.h>


struct usb_ctrl;

#define USB_APP_UNHANDLED	0xFF


/** usb_app_reset - Reset all state */
#if USB_APP_HAVE_RESET
void usb_app_reset(void);
#else
static inline void usb_app_reset(void) { }
#endif

/** usb_app_highpower - Called when high-power permission changes.
 * @granted: If true, the device is granted more than 1 power unit (100mA),
 *	but only up to the bMaxPower specified in the configuration descriptor.
 */
#if USB_APP_HAVE_HIGHPOWER
void usb_app_highpower(bool granted);
#else
static inline void usb_app_highpower(bool granted) { }
#endif

/** usb_app_control_rx - Handle a received EP0 frame in the app layer.
 * @ctl: The received control frame
 * @reply_buf: Buffer for reply. Buffer size is USBCFG_EP0_MAXSIZE.
 * On success, returns the number of bytes in reply_buf.
 * On failure, returns USB_APP_UNHANDLED. */
#if USB_APP_HAVE_CTLSETUPRX
uint8_t usb_app_control_setup_rx(struct usb_ctrl *ctl, uint8_t *reply_buf);
#else
static inline
uint8_t usb_app_control_setup_rx(struct usb_ctrl *ctl, uint8_t *reply_buf)
{ return USB_APP_UNHANDLED; }
#endif

/** usb_app_ep1_rx - Handle a received EP1 frame in the app layer.
 * @data: The received frame
 * @size: The size of the received frame
 * @reply_buf: Buffer for reply. Buffer size is USBCFG_EP1_MAXSIZE.
 * On success, returns the number of bytes in reply_buf.
 * On failure, returns USB_APP_UNHANDLED. */
#if USB_APP_HAVE_EP1RX
uint8_t usb_app_ep1_rx(uint8_t *data, uint8_t size,
		       uint8_t *reply_buf);
#else
static inline
uint8_t usb_app_ep1_rx(uint8_t *data, uint8_t size,
		       uint8_t *reply_buf)
{ return USB_APP_UNHANDLED; }
#endif

/** usb_app_ep1_tx_poll - Poll TX data for EP1.
 * @buffer: The buffer to write the data to.
 * The buffer size is USBCFG_EP1_MAXSIZE
 */
#if USB_APP_HAVE_EP1TXPOLL
uint8_t usb_app_ep1_tx_poll(void *buffer);
#else
static inline
uint8_t usb_app_ep1_tx_poll(void *buffer)
{ return 0; }
#endif

/** usb_app_ep2_rx - Handle a received EP2 frame in the app layer.
 * @data: The received frame
 * @size: The size of the received frame
 * @reply_buf: Buffer for reply. Buffer size is USBCFG_EP2_MAXSIZE.
 * On success, returns the number of bytes in reply_buf.
 * On failure, returns USB_APP_UNHANDLED. */
#if USB_APP_HAVE_EP2RX
uint8_t usb_app_ep2_rx(uint8_t *data, uint8_t size,
		       uint8_t *reply_buf);
#else
static inline
uint8_t usb_app_ep2_rx(uint8_t *data, uint8_t size,
		       uint8_t *reply_buf)
{ return USB_APP_UNHANDLED; }
#endif

/** usb_app_ep2_tx_poll - Poll TX data for EP2.
 * @buffer: The buffer to write the data to.
 * The buffer size is USBCFG_EP2_MAXSIZE
 */
#if USB_APP_HAVE_EP2TXPOLL
uint8_t usb_app_ep2_tx_poll(void *buffer);
#else
static inline
uint8_t usb_app_ep2_tx_poll(void *buffer)
{ return 0; }
#endif


#endif /* USBSTACK_APPLICATION_H_ */
