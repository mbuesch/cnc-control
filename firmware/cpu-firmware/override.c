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


/* Hysteresis, in ADC LSBs */
#define OVERRIDE_HYST		7

#define ADC_MIN			0
#define ADC_MAX			0x3FF


/* The current position value, in ADC LSBs. */
static uint16_t override_position;


static void adc_trigger(bool with_irq)
{
	if (ADCSRA & (1 << ADSC)) {
		/* already running. */
		return;
	}
	/* Start ADC0 with AVCC reference and a prescaler of 128. */
	ADMUX = (1 << REFS0);
	ADCSRA = (1 << ADEN) | ((uint8_t)with_irq << ADIE) | (1 << ADSC) |
		 (1 << ADPS0) | (1 << ADPS1) | (1 << ADPS2);
}

ISR(ADC_vect)
{
	uint16_t adc;

	adc = ADC;

	if (adc < ADC_MIN + OVERRIDE_HYST)
		override_position = ADC_MIN;
	else if (adc > ADC_MAX - OVERRIDE_HYST)
		override_position = ADC_MAX;
	else if (abs((int16_t)adc - (int16_t)override_position) > OVERRIDE_HYST)
		override_position = adc - adc % OVERRIDE_HYST;

	adc_trigger(1);
}

static inline void adc_busywait(void)
{
	do { } while (ADCSRA & (1 << ADSC));
	ADCSRA |= (1 << ADIF); /* Clear IRQ flag */
}

static void adc_init(void)
{
	/* Discard the first measurement */
	adc_trigger(0);
	adc_busywait();
	/* Trigger a real measurement */
	adc_trigger(1);
}

void override_init(void)
{
	override_position = 0;
	adc_init();
}

uint8_t override_get_pos(void)
{
	uint8_t sreg, pos;
	uint16_t adc_pos;

	sreg = irq_disable_save();
	adc_pos = override_position;
	irq_restore(sreg);

	adc_pos -= ADC_MIN;
	pos = (uint32_t)adc_pos * (uint32_t)0xFF /
	      (uint32_t)(ADC_MAX - ADC_MIN);

	return pos;
}
