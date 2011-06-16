#ifndef USB_CONFIG_H_
#define USB_CONFIG_H_
/*
 *   Tiny USB stack
 *
 *   Static stack configuration
 */

#include "debug.h"

/* Compile minimal USB stack for bootloader code */
#ifdef IN_BOOT
# define USB_MINI		1
#else
# define USB_MINI		0
#endif

/* Enable(1)/disable(0) USB debug messages */
#define USB_MESSAGES		1


#if !USB_MINI && USB_MESSAGES

/* Print an integer (8bit or 16bit) together with a message string literal. */
# define usb_print1num(description_string, number)	debug_printf(description_string " %X\n", (number))
/* Print two integers (8bit or 16bit) together with message string literals. */
# define usb_print2num(d1, n1, d2, n2)			debug_printf(d1 " %X " d2 " %X \n", (n1), (n2))
/* Print a string */
# define usb_printstr(string_literal)			debug_printf(string_literal "\n")
/* Dump a memory region */
# define usb_dumpmem(memory, size)			debug_dumpmem((memory), (size))
/* Assertion */
# define USB_BUG_ON(cond)				BUG_ON(cond)

#else
# define usb_print1num(description_string, number)	do { } while (0)
# define usb_print2num(d1, n1, d2, n2)			do { } while (0)
# define usb_printstr(string_literal)			do { } while (0)
# define usb_dumpmem(memory, size)			do { } while (0)
# define USB_BUG_ON(cond)				do { if (cond) { } } while (0)
#endif

/* Maximum software packet buffer size for the endpoints. */
#define USBCFG_EP0_MAXSIZE	64
#define USBCFG_EP1_MAXSIZE	64
#define USBCFG_EP2_MAXSIZE	64

/* Power control
 * Set to 1, if the device is selfpowered.
 * Set to 0, if the device is buspowered. */
#define USBCFG_SELFPOWERED	0

/* Architecture endianness.
 * Set to 1 for BigEndian.
 * Set to 0 for LittleEndian. */
#define USBCFG_ARCH_BE		0


/* Endpoint configuration */
#if USB_MINI
# define USB_WITH_EP1		0
# define USB_WITH_EP2		1
#else
# define USB_WITH_EP1		1
# define USB_WITH_EP2		1
#endif


/* Application layer configuration */
#if USB_MINI
# define USB_APP_HAVE_RESET		0
# define USB_APP_HAVE_HIGHPOWER		0
# define USB_APP_HAVE_CTLSETUPRX	0
# define USB_APP_HAVE_EP1RX		0
# define USB_APP_HAVE_EP1TXPOLL		0
# define USB_APP_HAVE_EP2RX		1
# define USB_APP_HAVE_EP2TXPOLL		0
#else
# define USB_APP_HAVE_RESET		1
# define USB_APP_HAVE_HIGHPOWER		1
# define USB_APP_HAVE_CTLSETUPRX	1
# define USB_APP_HAVE_EP1RX		1
# define USB_APP_HAVE_EP1TXPOLL		1
# define USB_APP_HAVE_EP2RX		1
# define USB_APP_HAVE_EP2TXPOLL		1
#endif

#endif /* USB_CONFIG_H_ */
