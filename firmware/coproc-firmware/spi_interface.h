#ifndef SPI_INTERFACE_H_
#define SPI_INTERFACE_H_

#include <util/crc16.h>

/* Button coprocessor wire protocol */

enum spi_commands {
	/* No operation */
	SPI_CONTROL_NOP,
	/* Application identification */
	SPI_CONTROL_TESTAPP,

	/* Data fetch commands */
	SPI_CONTROL_GETLOW,
	SPI_CONTROL_GETHIGH,
	SPI_CONTROL_GETENC,
	SPI_CONTROL_GETSUM,

	/* Bootloader related commands */
	SPI_CONTROL_ENTERBOOT = 0xA0,	/* Enter the bootloader */
	SPI_CONTROL_ENTERBOOT2,		/* Enter the bootloader (second stage) */
	SPI_CONTROL_ENTERAPP,		/* Enter the application */
	SPI_CONTROL_STARTFLASH,		/* Begin flashing sequence */
};

enum spi_result {
	SPI_RESULT_OK = 0xFA,
	SPI_RESULT_FAIL = 0x8A,
};

#define SPI_SLAVE_TRANSIRQ_DDR		DDRB
#define SPI_SLAVE_TRANSIRQ_PORT		PORTB
#define SPI_SLAVE_TRANSIRQ_PIN		PINB
#define SPI_SLAVE_TRANSIRQ_BIT		6

#define SPI_MASTER_TRANSIRQ_DDR		DDRD
#define SPI_MASTER_TRANSIRQ_PORT	PORTD
#define SPI_MASTER_TRANSIRQ_PIN		PIND
#define SPI_MASTER_TRANSIRQ_BIT		2
#define SPI_MASTER_TRANSIRQ_INT		INT0
#define SPI_MASTER_TRANSIRQ_INTF	INTF0
#define SPI_MASTER_TRANSIRQ_VECT	INT0_vect

static inline uint8_t spi_crc8(uint8_t crc, uint8_t data)
{
	return _crc_ibutton_update(crc, data);
}

#endif /* SPI_INTERFACE_H_ */
