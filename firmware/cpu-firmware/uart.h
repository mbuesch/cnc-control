#ifndef UART_DRIVER_H_
#define UART_DRIVER_H_

#include "util.h"


/* UART port baud rate */
#define UART_BAUD	115200
#define UART_USE_2X	0


void uart_putchar(char c) noinstrument;
void _uart_putstr(const char PROGPTR *pstr) noinstrument;
#define uart_putstr(str)	_uart_putstr(PSTR(str))
void uart_puthex(uint8_t val) noinstrument;

void uart_init(void) noinstrument;
void uart_exit(void) noinstrument;

#endif /* UART_DRIVER_H_ */
