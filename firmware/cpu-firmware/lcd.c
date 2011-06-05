/*
 *   4-bit HD44780 LCD support
 *
 *   Copyright (C) 2007-2011 Michael Buesch <m@bues.ch>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include "lcd.h"
#include "util.h"

#include <avr/io.h>
#include <string.h>
#include <stdio.h>


/* Hardware pin assignments */
#define LCD_PORT	PORTA
#define LCD_DDR		DDRA
#define LCD_PIN_E	(1 << 3)
#define LCD_PIN_RS	(1 << 2)
#define LCD_DATA_SHIFT	4 /* Data pins at 4-7 */


#define LCD_BUFFER_SIZE		LCD_NR_CHARS
static uint8_t lcd_buffer[LCD_BUFFER_SIZE];
uint8_t lcd_cursor_pos;


/* Send an ENABLE pulse to the device. */
static void lcd_enable_pulse(void)
{
	LCD_PORT |= LCD_PIN_E;
	udelay(1);
	LCD_PORT &= ~LCD_PIN_E;
}

static void lcd_write(uint8_t data)
{
	LCD_PORT = (LCD_PORT & ~(0xF << LCD_DATA_SHIFT)) |
		   (((data & 0xF0) >> 4) << LCD_DATA_SHIFT);
	lcd_enable_pulse();
	LCD_PORT = (LCD_PORT & ~(0xF << LCD_DATA_SHIFT)) |
		   ((data & 0x0F) << LCD_DATA_SHIFT);
	lcd_enable_pulse();
	udelay(50);
}

static void lcd_data(uint8_t data)
{
	LCD_PORT |= LCD_PIN_RS;
	lcd_write(data);
}

static void lcd_command(uint8_t command)
{
	LCD_PORT &= ~LCD_PIN_RS;
	lcd_write(command);
}

/* Clear LCD and return cursor to 0,0. */
static void lcd_hwclear(void)
{
	lcd_command(0x01);
	mdelay(2);
}

/* Move the hardware cursor.
 * Ranges:  line = 0-1,  column = 0-15 */
void lcd_hwcursor(uint8_t line, uint8_t column)
{
	lcd_command(0x80 | (line << 6) | column);
}

void lcd_clear_buffer(void)
{
	memset(lcd_buffer, ' ', LCD_BUFFER_SIZE);
	lcd_cursor_pos = 0;
}

void lcd_commit(void)
{
	uint8_t line, col;
	const uint8_t *buf = lcd_buffer;

	for (line = 0; line < LCD_NR_LINES; line++) {
		lcd_hwcursor(line, 0);
		for (col = 0; col < LCD_NR_COLUMNS; col++)
			lcd_data(*buf++);
	}
	/* Sync hw-cursor with sw-cursor. */
	lcd_hwcursor(lcd_getline(), lcd_getcolumn());
}

static inline void lcd_char_out(uint8_t c)
{
	uint8_t column;

//FIXME line/col handling needs a rewrite
	lcd_buffer[lcd_cursor_pos] = c;
	column = lcd_getcolumn() + 1;
	if (column < LCD_NR_COLUMNS)
		lcd_cursor(lcd_getline(), column);
}

void lcd_put_char(char c)
{
	if (c == '\r') {
		lcd_cursor(lcd_getline(), 0);
		return;
	}
	if (c == '\n') {
		uint8_t line = lcd_getline() + 1;
		lcd_cursor(line >= LCD_NR_LINES ? 0 : line, 0);
		return;
	}
	lcd_char_out(c);
}

static int lcd_stream_putchar(char c, FILE *unused)
{
	lcd_put_char(c);
	return 0;
}

static FILE lcd_fstream = FDEV_SETUP_STREAM(lcd_stream_putchar, NULL, _FDEV_SETUP_WRITE);

void _lcd_printf(const prog_char *_fmt, ...)
{
	char fmt[64];
	va_list args;

	strlcpy_P(fmt, _fmt, sizeof(fmt));
	va_start(args, _fmt);
	vfprintf(&lcd_fstream, fmt, args);
	va_end(args);
}

void _lcd_put_pstr(const prog_char *str)
{
	uint8_t c;

	for ( ; ; str++) {
		c = pgm_read_byte(str);
		if (c == '\0')
			break;
		lcd_put_char(c);
	}
}

void lcd_init(void)
{
	uint8_t i;

	LCD_DDR |= (0xF << LCD_DATA_SHIFT) |
		   LCD_PIN_E | LCD_PIN_RS;
	mdelay(250);
	LCD_PORT |= (0x03 << LCD_DATA_SHIFT);
	/* Poke it three times to wake it up. */
	for (i = 3; i; i--) {
		lcd_enable_pulse();
		mdelay(5);
	}
	/* Enable 4-bit mode. */
	LCD_PORT |= (0x02 << LCD_DATA_SHIFT);
	lcd_enable_pulse();
	mdelay(5);
	/* Function-Set parameters. */
	lcd_command(0x28);
	/* Switch display on; No cursor. */
	lcd_command(0x0C);
	/* No DD-autoinc, no display shifting. */
	lcd_command(0x04);
	lcd_hwclear();
	lcd_cursor_pos = 0;
	lcd_clear_buffer();

	lcd_printf("CNC-Control\nInitializing");
	lcd_commit();
}
