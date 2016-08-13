/*
 *   CNC-remote-control
 *   Feed override switch
 *
 *   Copyright (C) 2011 Michael Buesch <m@bues.ch>
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

#include "override.h"
#include "util.h"
#include "lcd.h"
#include "main.h"

#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>


#define ADC_HYST		16
#define ADC_MINMAX_DEADBAND	4
#define ADC_REAL_MIN		0
#define ADC_REAL_MAX		0x3FF
#define ADC_MIN			(ADC_REAL_MIN + ADC_MINMAX_DEADBAND)
#define ADC_MAX			(ADC_REAL_MAX - ADC_MINMAX_DEADBAND)


static uint16_t last_override_adc;
static uint8_t last_override_pos;


static void adc_trigger(bool freerunning)
{
	/* Start ADC0 with AVCC reference and a prescaler of 128. */
	ADMUX = (1 << REFS0);
	ADCSRA = (1 << ADEN) | (0 << ADIE) | (1 << ADSC) |
		 (freerunning ? (1 << ADATE) : 0) |
		 (1 << ADPS0) | (1 << ADPS1) | (1 << ADPS2);
}

static inline void adc_busywait(void)
{
	do { } while (ADCSRA & (1 << ADSC));
	ADCSRA |= (1 << ADIF); /* Clear IRQ flag */
}

void override_init(void)
{
	last_override_adc = ADC_REAL_MIN;
	last_override_pos = 0;
	/* Discard the first measurement */
	adc_trigger(0);
	adc_busywait();
	/* Start the ADC in freerunning mode. */
	adc_trigger(1);
}

uint8_t override_get_pos(void)
{
	uint16_t adc;
	uint8_t pos;

	if (!(ADCSRA & (1 << ADIF))) {
		/* There was no new conversion */
		return last_override_pos;
	}
	adc = ADC;
	ADCSRA |= (1 << ADIF); /* Clear IRQ flag */

	if (adc <= ADC_MIN)
		adc = ADC_REAL_MIN;
	else if (adc >= ADC_MAX)
		adc = ADC_REAL_MAX;
	else if (abs((int16_t)adc - (int16_t)last_override_adc) <= ADC_HYST)
		adc = last_override_adc;

	if (adc == last_override_adc)
		return last_override_pos;

	pos = (uint8_t)(((uint32_t)adc - (uint32_t)ADC_REAL_MIN) * (uint32_t)0xFF /
			((uint32_t)ADC_REAL_MAX - (uint32_t)ADC_REAL_MIN));

	last_override_adc = adc;
	last_override_pos = pos;

	return pos;
}
