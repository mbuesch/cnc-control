#ifndef SHREG_4094_H_
#define SHREG_4094_H_

/*** HARDWARE PIN CONFIGURATION ***/

#define SR4094_DATA_PORT	PORTB
#define SR4094_DATA_DDR		DDRB
#define SR4094_DATA_BIT		2

#define SR4094_CLOCK_PORT	PORTB
#define SR4094_CLOCK_DDR	DDRB
#define SR4094_CLOCK_BIT	0

#define SR4094_STROBE_PORT	PORTB
#define SR4094_STROBE_DDR	DDRB
#define SR4094_STROBE_BIT	1

#define SR4094_OUTEN_PORT	PORTB
#define SR4094_OUTEN_DDR	DDRB
#define SR4094_OUTEN_BIT	3


#include <stdint.h>

/**
 * sr4094_init - Initialize the shiftregister chain
 * @nr_chips: Number of cascaded chips.
 */
void sr4094_init(void *initial_data, uint8_t nr_chips);

/**
 * sr4094_put_data - Put data on the output of the 4094 chip(s)
 *
 * @data: Byte array with output data. The size of the data array
 *	in bytes must match "nr_chips".
 * @nr_chips: Number of cascaded chips.
 *
 * bit0 of the first byte will be the QP0 output of the first chip.
 * bit1 of the first byte will be the QP1 output of the first chip.
 * ...
 * bit0 of the second byte will be the QP0 output of the second chip.
 * ...
 * The "first" chip is directly connected to the microcontroller.
 * The "second" chip is cascaded to the "first" chip...
 */
void sr4094_put_data(void *data, uint8_t nr_chips);

#endif /* SHREG_4094_H_ */
