#ifndef PDIUSB_H_
#define PDIUSB_H_

#include <stdint.h>


enum pdiusb_ep_index {
	PDIUSB_EP_CTLOUT,
	PDIUSB_EP_CTLIN,
	PDIUSB_EP_EP1OUT,
	PDIUSB_EP_EP1IN,
	PDIUSB_EP_EP2OUT,
	PDIUSB_EP_EP2IN,
};
#define PDIUSB_EPIDX_OUT(ep_index)	((ep_index) & 0xFE)
#define PDIUSB_EPIDX_IN(ep_index)	((ep_index) | 0x01)


/* PDIUSBD12 commands */
#define PDIUSB_CMD_ADDREN		0xD0 /* 8bit command data */
#define  PDIUSB_AEN			0x80
#define  PDIUSB_ADDR			0x7F
#define PDIUSB_CMD_ENDPEN		0xD8 /* 8bit command data */
#define  PDIUSB_GENISOEN		0x01
#define PDIUSB_CMD_SETMODE		0xF3 /* 16bit command data */
#define  PDIUSB_MODE_NOLAZYCLK		0x0002
#define  PDIUSB_MODE_CLKARUN		0x0004
#define  PDIUSB_MODE_IRQM		0x0008
#define  PDIUSB_MODE_SOFTCONN		0x0010
#define  PDIUSB_MODE_EPCFG		0x00C0
#define   PDIUSB_MODE_EPNONISO		0x0000
#define   PDIUSB_MODE_EPISOOUT		0x0040
#define   PDIUSB_MODE_EPISOIN		0x0080
#define   PDIUSB_MODE_EPISOBI		0x00C0
#define  PDIUSB_MODE_CLKDIV		0x0F00
#define  PDIUSB_MODE_CLKDIV_SHIFT	8
#define  PDIUSB_MODE_STO		0x4000
#define  PDIUSB_MODE_SOFIRQ		0x8000
#define PDIUSB_CMD_DMA			0xFB /* 8bit command data */
#define  PDIUSB_DMA_BURST		0x03
#define   PDIUSB_DMAB_1CYC		0x00
#define   PDIUSB_DMAB_4CYC		0x01
#define   PDIUSB_DMAB_8CYC		0x02
#define   PDIUSB_DMAB_16CYC		0x03
#define  PDIUSB_DMAEN			0x04
#define  PDIUSB_DMADIRWR		0x08
#define  PDIUSB_DMAAUTOREL		0x10
#define  PDIUSB_DMASOFIRQ		0x20
#define  PDIUSB_EP4IRQEN		0x40
#define  PDIUSB_EP5IRQEN		0x80
#define PDIUSB_CMD_IRQSTAT		0xF4 /* 16bit command data */
#define  PDIUSB_IST_MASK		0x01FF
#define  PDIUSB_IST_EP(ep)		(1 << (0 + (ep)))
#define  PDIUSB_IST_BUSRST		(1 << 6)
#define  PDIUSB_IST_SUSPCHG		(1 << 7)
#define  PDIUSB_IST_DMAEOT		(1 << 8)
#define PDIUSB_CMD_SELEP(ep)		(0x00 + (ep)) /* 8bit command data */
#define  PDIUSB_SELEPR_FULL		0x01
#define  PDIUSB_SELEPR_STALL		0x02
#define PDIUSB_CMD_GEPSTAT(ep)		(0x80 + (ep)) /* 8bit command data */
#define  PDIUSB_GEPSTAT_SETUP		0x04
#define  PDIUSB_GEPSTAT_B0FULL		0x20
#define  PDIUSB_GEPSTAT_B1FULL		0x40
#define  PDIUSB_GEPSTAT_STALL		0x80
#define PDIUSB_CMD_SEPSTAT(ep)		(0x80 + (ep)) /* 8bit command data */
#define  PDIUSB_SEPSTAT_STALL		0x01
#define PDIUSB_CMD_TRSTAT(ep)		(0x40 + (ep)) /* 8bit command data */
#define  PDIUSB_TRSTAT_TRANSOK		0x01 /* TX/RX success */
#define  PDIUSB_TRSTAT_ERR		0x1E /* Error code */
#define   PDIUSB_TRERR_NOERR		0x00 /* No error */
#define   PDIUSB_TRERR_PIDENC		0x02 /* PID encoding error */
#define   PDIUSB_TRERR_PIDUNK		0x04 /* PID unknown; encoding is valid */
#define   PDIUSB_TRERR_UNEXP		0x06 /* Packet is not of the type expected */
#define   PDIUSB_TRERR_TCRC		0x08 /* Token CRC error */
#define   PDIUSB_TRERR_DCRC		0x0A /* Data CRC error */
#define   PDIUSB_TRERR_TOUT		0x0C /* Time Out error */
#define   PDIUSB_TRERR_31337		0x0E /* Never happens */
#define   PDIUSB_TRERR_UEOP		0x10 /* Unexpected End-Of-Packet */
#define   PDIUSB_TRERR_NAK		0x12 /* Sent or received NAK */
#define   PDIUSB_TRERR_SSTALL		0x14 /* Sent Stall, a token was RXed, but the EP was stalled */
#define   PDIUSB_TRERR_OFLOW		0x16 /* RX buffer overflow error */
#define   PDIUSB_TRERR_BITST		0x1A /* Bitstuff error */
#define   PDIUSB_TRERR_WDPID		0x1E /* Wrong DATA PID */
#define  PDIUSB_TRSTAT_SETUP		0x20 /* Last packet had SETUP token */
#define  PDIUSB_TRSTAT_D1PID		0x40 /* Last packet had DATA1 PID */
#define  PDIUSB_TRSTAT_PSTATNRD		0x80 /* Previous status not read */
#define PDIUSB_CMD_RWBUF		0xF0 /* 8bit command data */
#define PDIUSB_CMD_CLRBUF		0xF2 /* 0bit command data */
#define PDIUSB_CMD_VALBUF		0xFA /* 0bit command data */
#define PDIUSB_CMD_ACKSETUP		0xF1 /* 0bit command data */
#define PDIUSB_CMD_RESUME		0xF6 /* 0bit command data */
#define PDIUSB_CMD_CURFRNUM		0xF5 /* 16bit command data */
#define PDIUSB_CMD_GETCHIPID		0xFD /* 16bit command data */
#define  PDIUSB_CHIPID			0x1012 /* Magic ChipID value */


/* CLKOUT speed divisors */
#define PDIUSB_CLKOUT_48MHZ		0	/* 48.00000000000 MHz */
#define PDIUSB_CLKOUT_24MHZ		1	/* 24.00000000000 MHz */
#define PDIUSB_CLKOUT_16MHZ		2	/* 16.00000000000 MHz */
#define PDIUSB_CLKOUT_12MHZ		3	/* 12.00000000000 MHz */
#define PDIUSB_CLKOUT_9p6MHZ		4	/*  9.60000000000 MHz */
#define PDIUSB_CLKOUT_8MHZ		5	/*  8.00000000000 MHz */
#define PDIUSB_CLKOUT_6p9MHZ		6	/*  6.85714285714 MHz */
#define PDIUSB_CLKOUT_6MHZ		7	/*  6.00000000000 MHz */
#define PDIUSB_CLKOUT_5p3MHZ		8	/*  5.33333333333 MHz */
#define PDIUSB_CLKOUT_4p8MHZ		9	/*  4.80000000000 MHz */
#define PDIUSB_CLKOUT_4p4MHZ		10	/*  4.36363636364 MHz */
#define PDIUSB_CLKOUT_4MHZ		11	/*  4.00000000000 MHz */
#define PDIUSB_CLKOUT_3p7MHZ		12	/*  3.69230769231 MHz */
#define PDIUSB_CLKOUT_3p4MHZ		13	/*  3.42857142857 MHz */
#define PDIUSB_CLKOUT_3p2MHZ		14	/*  3.20000000000 MHz */
#define PDIUSB_CLKOUT_3MHZ		15	/*  3.00000000000 MHz */


uint8_t pdiusb_configure_clkout(void);
uint8_t pdiusb_init(void);
void pdiusb_exit(void);

#endif /* PDIUSB_H_ */
