#ifndef SPI_CONFIG_H_
#define SPI_CONFIG_H_

#include <avr/io.h>
#include <avr/pgmspace.h>

#include "util.h"
#include "coproc-firmware/spi_interface.h"


/* No async for the bootloader */
#ifdef BOOTLOADER
# define SPI_HAVE_ASYNC		0
#else
# define SPI_HAVE_ASYNC		1
#endif


enum spi_async_flags {
	SPI_ASYNC_RUNNING	= (1 << 0),
	SPI_ASYNC_TXPROGMEM	= (1 << 1), /* TX buffer is in progmem */
};

void spi_async_start(void *rxbuf, const void *txbuf,
		     uint8_t nr_bytes, uint8_t flags, uint8_t wait_ms);
bool spi_async_running(void);
void spi_async_ms_tick(void);
extern void spi_async_done(void);


static inline void spi_slave_select(bool select)
{
	if (select)
		PORTB = (uint8_t)(PORTB & ~(1u << 4/*SS*/));
	else
		PORTB = (uint8_t)(PORTB | (1u << 4/*SS*/));
}

uint8_t spi_transfer_sync(uint8_t tx);
uint8_t spi_transfer_slowsync(uint8_t tx);

void spi_lowlevel_exit(void);
void spi_lowlevel_init(void);

#endif /* SPI_CONFIG_H_ */
