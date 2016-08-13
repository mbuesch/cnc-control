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

	PDIUSB_EP_COUNT,
};
#define PDIUSB_EPIDX_OUT(ep_index)	((ep_index) & 0xFEu)
#define PDIUSB_EPIDX_IN(ep_index)	((ep_index) | 0x01u)


/* PDIUSBD12 commands */
#define PDIUSB_CMD_ADDREN		0xD0u /* 8bit command data */
#define  PDIUSB_AEN			0x80u
#define  PDIUSB_ADDR			0x7Fu
#define PDIUSB_CMD_ENDPEN		0xD8u /* 8bit command data */
#define  PDIUSB_GENISOEN		0x01u
#define PDIUSB_CMD_SETMODE		0xF3u /* 16bit command data */
#define  PDIUSB_MODE_NOLAZYCLK		0x0002u
#define  PDIUSB_MODE_CLKARUN		0x0004u
#define  PDIUSB_MODE_IRQM		0x0008u
#define  PDIUSB_MODE_SOFTCONN		0x0010u
#define  PDIUSB_MODE_EPCFG		0x00C0u
#define   PDIUSB_MODE_EPNONISO		0x0000u
#define   PDIUSB_MODE_EPISOOUT		0x0040u
#define   PDIUSB_MODE_EPISOIN		0x0080u
#define   PDIUSB_MODE_EPISOBI		0x00C0u
#define  PDIUSB_MODE_CLKDIV		0x0F00u
#define  PDIUSB_MODE_CLKDIV_SHIFT	8u
#define  PDIUSB_MODE_STO		0x4000u
#define  PDIUSB_MODE_SOFIRQ		0x8000u
#define PDIUSB_CMD_DMA			0xFBu /* 8bit command data */
#define  PDIUSB_DMA_BURST		0x03u
#define   PDIUSB_DMAB_1CYC		0x00u
#define   PDIUSB_DMAB_4CYC		0x01u
#define   PDIUSB_DMAB_8CYC		0x02u
#define   PDIUSB_DMAB_16CYC		0x03u
#define  PDIUSB_DMAEN			0x04u
#define  PDIUSB_DMADIRWR		0x08u
#define  PDIUSB_DMAAUTOREL		0x10u
#define  PDIUSB_DMASOFIRQ		0x20u
#define  PDIUSB_EP4IRQEN		0x40u
#define  PDIUSB_EP5IRQEN		0x80u
#define PDIUSB_CMD_IRQSTAT		0xF4u /* 16bit command data */
#define  PDIUSB_IST_MASK		0x01FFu
#define  PDIUSB_IST_EP(ep)		(1u << (0u + (ep)))
#define  PDIUSB_IST_BUSRST		(1u << 6u)
#define  PDIUSB_IST_SUSPCHG		(1u << 7u)
#define  PDIUSB_IST_DMAEOT		(1u << 8u)
#define PDIUSB_CMD_SELEP(ep)		((uint8_t)(0x00u + (ep))) /* 8bit command data */
#define  PDIUSB_SELEPR_FULL		0x01u
#define  PDIUSB_SELEPR_STALL		0x02u
#define PDIUSB_CMD_GEPSTAT(ep)		((uint8_t)(0x80u + (ep))) /* 8bit command data */
#define  PDIUSB_GEPSTAT_SETUP		0x04u
#define  PDIUSB_GEPSTAT_B0FULL		0x20u
#define  PDIUSB_GEPSTAT_B1FULL		0x40u
#define  PDIUSB_GEPSTAT_STALL		0x80u
#define PDIUSB_CMD_SEPSTAT(ep)		((uint8_t)(0x80u + (ep))) /* 8bit command data */
#define  PDIUSB_SEPSTAT_STALL		0x01u
#define PDIUSB_CMD_TRSTAT(ep)		((uint8_t)(0x40u + (ep))) /* 8bit command data */
#define  PDIUSB_TRSTAT_TRANSOK		0x01u /* TX/RX success */
#define  PDIUSB_TRSTAT_ERR		0x1Eu /* Error code */
#define   PDIUSB_TRERR_NOERR		0x00u /* No error */
#define   PDIUSB_TRERR_PIDENC		0x02u /* PID encoding error */
#define   PDIUSB_TRERR_PIDUNK		0x04u /* PID unknown; encoding is valid */
#define   PDIUSB_TRERR_UNEXP		0x06u /* Packet is not of the type expected */
#define   PDIUSB_TRERR_TCRC		0x08u /* Token CRC error */
#define   PDIUSB_TRERR_DCRC		0x0Au /* Data CRC error */
#define   PDIUSB_TRERR_TOUT		0x0Cu /* Time Out error */
#define   PDIUSB_TRERR_31337		0x0Eu /* Never happens */
#define   PDIUSB_TRERR_UEOP		0x10u /* Unexpected End-Of-Packet */
#define   PDIUSB_TRERR_NAK		0x12u /* Sent or received NAK */
#define   PDIUSB_TRERR_SSTALL		0x14u /* Sent Stall, a token was RXed, but the EP was stalled */
#define   PDIUSB_TRERR_OFLOW		0x16u /* RX buffer overflow error */
#define   PDIUSB_TRERR_BITST		0x1Au /* Bitstuff error */
#define   PDIUSB_TRERR_WDPID		0x1Eu /* Wrong DATA PID */
#define  PDIUSB_TRSTAT_SETUP		0x20u /* Last packet had SETUP token */
#define  PDIUSB_TRSTAT_D1PID		0x40u /* Last packet had DATA1 PID */
#define  PDIUSB_TRSTAT_PSTATNRD		0x80u /* Previous status not read */
#define PDIUSB_CMD_RWBUF		0xF0u /* 8bit command data */
#define PDIUSB_CMD_CLRBUF		0xF2u /* 0bit command data */
#define PDIUSB_CMD_VALBUF		0xFAu /* 0bit command data */
#define PDIUSB_CMD_ACKSETUP		0xF1u /* 0bit command data */
#define PDIUSB_CMD_RESUME		0xF6u /* 0bit command data */
#define PDIUSB_CMD_CURFRNUM		0xF5u /* 16bit command data */
#define PDIUSB_CMD_GETCHIPID		0xFDu /* 16bit command data */
#define  PDIUSB_CHIPID			0x1012u /* Magic ChipID value */


/* CLKOUT speed divisors */
#define PDIUSB_CLKOUT_48MHZ		0u	/* 48.00000000000 MHz */
#define PDIUSB_CLKOUT_24MHZ		1u	/* 24.00000000000 MHz */
#define PDIUSB_CLKOUT_16MHZ		2u	/* 16.00000000000 MHz */
#define PDIUSB_CLKOUT_12MHZ		3u	/* 12.00000000000 MHz */
#define PDIUSB_CLKOUT_9p6MHZ		4u	/*  9.60000000000 MHz */
#define PDIUSB_CLKOUT_8MHZ		5u	/*  8.00000000000 MHz */
#define PDIUSB_CLKOUT_6p9MHZ		6u	/*  6.85714285714 MHz */
#define PDIUSB_CLKOUT_6MHZ		7u	/*  6.00000000000 MHz */
#define PDIUSB_CLKOUT_5p3MHZ		8u	/*  5.33333333333 MHz */
#define PDIUSB_CLKOUT_4p8MHZ		9u	/*  4.80000000000 MHz */
#define PDIUSB_CLKOUT_4p4MHZ		10u	/*  4.36363636364 MHz */
#define PDIUSB_CLKOUT_4MHZ		11u	/*  4.00000000000 MHz */
#define PDIUSB_CLKOUT_3p7MHZ		12u	/*  3.69230769231 MHz */
#define PDIUSB_CLKOUT_3p4MHZ		13u	/*  3.42857142857 MHz */
#define PDIUSB_CLKOUT_3p2MHZ		14u	/*  3.20000000000 MHz */
#define PDIUSB_CLKOUT_3MHZ		15u	/*  3.00000000000 MHz */


uint8_t pdiusb_configure_clkout(void);
uint8_t pdiusb_init(void);
void pdiusb_exit(void);
void pdiusb_work(void);

#endif /* PDIUSB_H_ */
