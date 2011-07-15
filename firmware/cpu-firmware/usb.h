#ifndef USBSTACK_H_
#define USBSTACK_H_
/*
 *   Tiny USB stack
 *
 *   Copyright (C) 2009 Michael Buesch <m@bues.ch>
 *
 *   USB data structure and constant definitions
 *   taken from libusb-0.1 and the Linux kernel.
 *   Copyright (c) 2000-2003 Johannes Erdfelt and others
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

#include <stdint.h>
#include <avr/pgmspace.h>


#if USBCFG_ARCH_BE
# define le16_to_cpu(value)	usb_swap16(value)
# define le32_to_cpu(value)	usb_swap32(value)
# define cpu_to_le16(value)	usb_swap16(value)
# define cpu_to_le32(value)	usb_swap32(value)
#else
# define le16_to_cpu(value)	(value)
# define le32_to_cpu(value)	(value)
# define cpu_to_le16(value)	(value)
# define cpu_to_le32(value)	(value)
#endif

#define usb_swap16(value)	(((value & 0xFF00) >> 8) | \
				 ((value & 0x00FF) << 8))
#define usb_swap32(value)	(((value & 0xFF000000) >> 24) | \
				 ((value & 0x00FF0000) >> 8) | \
				 ((value & 0x0000FF00) << 8) | \
				 ((value & 0x000000FF) << 24))

#undef DBG
#if DEBUG
# define DBG(x) x
#else
# define DBG(x) /* nothing */
#endif


struct usb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
} __attribute__ ((packed));

struct usb_config_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
} __attribute__ ((packed));

/* from config descriptor bmAttributes */
#define USB_CONFIG_ATT_ONE		(1 << 7)	/* must be set */
#define USB_CONFIG_ATT_SELFPOWER	(1 << 6)	/* self powered */
#define USB_CONFIG_ATT_WAKEUP		(1 << 5)	/* can wakeup */
#define USB_CONFIG_ATT_BATTERY		(1 << 4)	/* battery powered */

struct usb_string_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t string[0];
} __attribute__ ((packed));

struct usb_interface_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
} __attribute__ ((packed));

struct usb_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
} __attribute__ ((packed));

struct usb_audio_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
	uint8_t bRefresh;
	uint8_t bSynchAddress;
} __attribute__ ((packed));


#define USB_ENDPOINT_XFERTYPE_MASK	0x03	/* in bmAttributes */
#define USB_ENDPOINT_XFER_CONTROL	0
#define USB_ENDPOINT_XFER_ISOC		1
#define USB_ENDPOINT_XFER_BULK		2
#define USB_ENDPOINT_XFER_INT		3
#define USB_ENDPOINT_MAX_ADJUSTABLE	0x80

struct usb_ctrl {
	uint8_t bRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} __attribute__ ((packed));


/* Device and/or Interface Class codes */
#define USB_CLASS_PER_INTERFACE		0	/* for DeviceClass */
#define USB_CLASS_AUDIO			1
#define USB_CLASS_COMM			2
#define USB_CLASS_HID			3
#define USB_CLASS_PRINTER		7
#define USB_CLASS_PTP			6
#define USB_CLASS_MASS_STORAGE		8
#define USB_CLASS_HUB			9
#define USB_CLASS_DATA			10
#define USB_CLASS_VENDOR_SPEC		0xff

/* Descriptor types */
#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05

#define USB_DT_HID			0x21
#define USB_DT_REPORT			0x22
#define USB_DT_PHYSICAL			0x23
#define USB_DT_HUB			0x29

/* Descriptor sizes per descriptor type */
#define USB_DT_DEVICE_SIZE		18
#define USB_DT_CONFIG_SIZE		9
#define USB_DT_INTERFACE_SIZE		9
#define USB_DT_ENDPOINT_SIZE		7
#define USB_DT_ENDPOINT_AUDIO_SIZE	9	/* Audio extension */
#define USB_DT_HUB_NONVAR_SIZE		7

/* Request types */
#define USB_REQ_GET_STATUS		0x00
#define USB_REQ_CLEAR_FEATURE		0x01
/* 0x02 is reserved */
#define USB_REQ_SET_FEATURE		0x03
/* 0x04 is reserved */
#define USB_REQ_SET_ADDRESS		0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_GET_INTERFACE		0x0A
#define USB_REQ_SET_INTERFACE		0x0B
#define USB_REQ_SYNCH_FRAME		0x0C

#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)
#define USB_TYPE_MASK			(0x03 << 5)

#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03
#define USB_RECIP_MASK			0x03

#define USB_ENDPOINT_IN			0x80
#define USB_ENDPOINT_OUT		0x00

static inline uint8_t usb_ep_is_in(uint8_t ep)
{
	return ep & USB_ENDPOINT_IN;
}
static inline uint8_t usb_ep_is_out(uint8_t ep)
{
	return !(ep & USB_ENDPOINT_IN);
}

static inline uint8_t usb_ctrl_is_in(const struct usb_ctrl *ctrl)
{
	return (ctrl->bRequestType & USB_ENDPOINT_IN);
}
static inline uint8_t usb_ctrl_is_out(const struct usb_ctrl *ctrl)
{
	return !(ctrl->bRequestType & USB_ENDPOINT_IN);
}

/* USB feature flags are written using USB_REQ_{CLEAR,SET}_FEATURE, and
 * are read as a bit array returned by USB_REQ_GET_STATUS.  (So there
 * are at most sixteen features of each type.)  Hubs may also support a
 * new USB_REQ_TEST_AND_SET_FEATURE to put ports into L1 suspend. */
#define USB_DEVICE_SELF_POWERED		0	/* (read only) */
#define USB_DEVICE_REMOTE_WAKEUP	1	/* dev may initiate wakeup */
#define USB_DEVICE_TEST_MODE		2	/* (wired high speed only) */
#define USB_DEVICE_BATTERY		2	/* (wireless) */
#define USB_DEVICE_B_HNP_ENABLE		3	/* (otg) dev may initiate HNP */
#define USB_DEVICE_WUSB_DEVICE		3	/* (wireless)*/
#define USB_DEVICE_A_HNP_SUPPORT	4	/* (otg) RH port supports HNP */
#define USB_DEVICE_A_ALT_HNP_SUPPORT	5	/* (otg) other RH port does */
#define USB_DEVICE_DEBUG_MODE		6	/* (special devices only) */

#define USB_ENDPOINT_HALT		0	/* IN/OUT will STALL */


/*** Device driver calls ***/

/** usb_reset - Reset the USB statemachine.
 * Called by the lowlevel device driver. */
void usb_reset(void);

/** usb_control_rx - Received setup token on control-EP.
 * Returns enum usb_rx_returncode.
 * Called by the lowlevel device driver. */
uint8_t usb_control_setup_rx(struct usb_ctrl *ctl);

/** usb_control_rx - Received data on control-EP.
 * Returns enum usb_rx_returncode.
 * Called by the lowlevel device driver. */
uint8_t usb_control_rx(void *data, uint8_t size);

#define USB_TX_POLL_NONE	0xFF

/** usb_control_tx_poll - Poll TX data on control-EP.
 * Returns the number of octets or USB_TX_POLL_NONE on error.
 * Called by the lowlevel device driver. */
uint8_t usb_control_tx_poll(void **data, uint8_t chunksize);

/** usb_ep1_rx - Received data on EP1.
 * Returns enum usb_rx_returncode.
 * Called by the lowlevel device driver. */
uint8_t usb_ep1_rx(void *data, uint8_t size);

/** usb_ep1_tx_poll - Poll TX data on EP1.
 * Returns the number of octets or USB_TX_POLL_NONE on error.
 * Called by the lowlevel device driver. */
uint8_t usb_ep1_tx_poll(void **data, uint8_t chunksize);

/** usb_ep2_rx - Received data on EP2.
 * Returns enum usb_rx_returncode.
 * Called by the lowlevel device driver. */
uint8_t usb_ep2_rx(void *data, uint8_t size);

/** usb_ep2_tx_poll - Poll TX data on EP2.
 * Returns the number of octets or USB_TX_POLL_NONE on error.
 * Called by the lowlevel device driver. */
uint8_t usb_ep2_tx_poll(void **data, uint8_t chunksize);

/** enum usb_rx_returncode - Returncode to usb_*_rx() */
enum usb_rx_returncode {
	USB_RX_DONE,		/* Everything is done */
	USB_RX_ERROR,		/* An error occured while processing the frame */
};




/*** Callbacks to device driver - Defined in the lowlevel driver ***/

extern void usb_set_address(uint8_t address);
extern void usb_enable_endpoints(uint8_t enable);
extern void usb_stall_endpoint(uint8_t ep);
extern void usb_unstall_endpoint(uint8_t ep);
extern uint8_t usb_endpoint_is_stalled(uint8_t ep);

#endif /* USBSTACK_H_ */
