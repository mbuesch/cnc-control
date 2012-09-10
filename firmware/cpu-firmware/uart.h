#ifndef UART_DRIVER_H_
#define UART_DRIVER_H_

#include "util.h"


/* UART port baud rate */
#define UART_BAUD	115200
#define UART_USE_2X	0


void uart_putchar(char c);
void _uart_putstr(const char PROGPTR *pstr);
#define uart_putstr(str)	_uart_putstr(PSTR(str))
void uart_puthex(uint8_t val);

void uart_init(void);
void uart_exit(void);

#endif /* UART_DRIVER_H_ */
