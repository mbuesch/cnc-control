/*
 *   4-bit HD44780 LCD support
 *
 *   Copyright (C) 2007-2012 Michael Buesch <m@bues.ch>
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

#include <avr/io.h>
#include <util/delay.h>

#include <string.h>
#include <stdio.h>


#define LCD_NR_CHARS		(LCD_NR_LINES * LCD_NR_COLUMNS)
#define LCD_BUFFER_SIZE		LCD_NR_CHARS

static uint8_t lcd_buffer[LCD_BUFFER_SIZE];
uint8_t lcd_cursor_pos;


/** lcd_enable_pulse - Send an E-pulse */
static void lcd_enable_pulse(void)
{
	LCD_PORT |= LCD_PIN_E;
	__asm__ __volatile__("nop\n\t"
			     "nop\n\t"
			     : : : "memory");
	LCD_PORT &= ~LCD_PIN_E;
}

/** lcd_write - Write one byte to the LCD. */
static void lcd_write(uint8_t data)
{
	LCD_PORT = (LCD_PORT & ~(0xF << LCD_DATA_SHIFT)) |
		   (((data & 0xF0) >> 4) << LCD_DATA_SHIFT);
	lcd_enable_pulse();
	LCD_PORT = (LCD_PORT & ~(0xF << LCD_DATA_SHIFT)) |
		   ((data & 0x0F) << LCD_DATA_SHIFT);
	lcd_enable_pulse();
	_delay_us(50);
}

/** lcd_data - Send data to the LCD. */
static void lcd_data(uint8_t data)
{
	LCD_PORT |= LCD_PIN_RS;
	lcd_write(data);
}

/** lcd_command - Send command to the LCD. */
static void lcd_command(uint8_t command)
{
	LCD_PORT &= ~LCD_PIN_RS;
	lcd_write(command);
}

/** lcd_cmd_clear - Clear LCD and return cursor to home position. */
static void lcd_cmd_clear(void)
{
	lcd_command(0x01);
	_delay_ms(2);
}

/** lcd_cmd_home - Move cursor to home position. */
static void lcd_cmd_home(void)
{
	lcd_command(0x02);
	_delay_ms(2);
}

/** lcd_cmd_entrymode - Set entry mode.
 * @cursor_inc: 1 = increment cursor, 0 = decrement cursor.
 * @display_shift: 1 = shift display, 0 = don't shift display.
 */
static void lcd_cmd_entrymode(uint8_t cursor_inc,
			      uint8_t display_shift)
{
	lcd_command(0x04 |
		    (cursor_inc ? 0x02 : 0x00) |
		    (display_shift ? 0x01 : 0x00));
}

/** lcd_cmd_dispctl - Display control.
 * @display_on: 1 = turn display on, 0 = turn display off.
 * @cursor_on: 1 = turn cursor on, 0 = turn cursor off.
 * @cursor_blink: 1 = blink cursor, 0 = don't blink cursor.
 */
void lcd_cmd_dispctl(uint8_t display_on,
		     uint8_t cursor_on,
		     uint8_t cursor_blink)
{
	lcd_command(0x08 |
		    (display_on ? 0x04 : 0x00) |
		    (cursor_on ? 0x02 : 0x00) |
		    (cursor_blink ? 0x01 : 0x00));
}

/** lcd_cmd_shiftctl - Display shift control.
 * @shift_display: 1 = move display, 0 = move cursor.
 * @shift_right: 1 = right direction, 0 = left direction.
 */
static void lcd_cmd_shiftctl(uint8_t shift_display,
			     uint8_t shift_right)
{
	lcd_command(0x10 |
		    (shift_display ? 0x08 : 0x00) |
		    (shift_right ? 0x04 : 0x00));
}

/** lcd_cmd_funcset - Set basic display function.
 * @eight_bit: 1 = 8bit mode, 0 = 4bit mode.
 * @two_lines: 1 = two rows, 0 = one row.
 * @font_5x10: 1 = 5x10 font, 0 = 5x8 font.
 */
static void lcd_cmd_funcset(uint8_t eight_bit,
			    uint8_t two_lines,
			    uint8_t font_5x10)
{
	lcd_command(0x20 |
		    (eight_bit ? 0x10 : 0x00) |
		    (two_lines ? 0x08 : 0x00) |
		    (font_5x10 ? 0x04 : 0x00));
}

/** lcd_cmd_cgram_addr_set - Move CGRAM address pointer.
 * @address: The address to move to.
 */
static void lcd_cmd_cgram_addr_set(uint8_t address)
{
	lcd_command(0x40 | (address & 0x3F));
}

/** lcd_cmd_cursor - Move cursor (DDRAM address).
 * @line: Line number. 0 - 1.
 * @column: Column number. 0 - 15.
 */
void lcd_cmd_cursor(uint8_t line, uint8_t column)
{
	lcd_command(0x80 |
		    ((line & (LCD_NR_LINES - 1)) << 6) |
		    (column & (LCD_NR_COLUMNS - 1)));
}

/** lcd_clear_buffer - Clear the software buffer. */
void lcd_clear_buffer(void)
{
	memset(lcd_buffer, ' ', LCD_BUFFER_SIZE);
	lcd_cursor_pos = 0;
}

/** lcd_commit - Write the software buffer to the display. */
void lcd_commit(void)
{
	uint8_t line, col;
	const uint8_t *buf = lcd_buffer;

	for (line = 0; line < LCD_NR_LINES; line++) {
		lcd_cmd_cursor(line, 0);
		for (col = 0; col < LCD_NR_COLUMNS; col++)
			lcd_data(*buf++);
	}
	lcd_cmd_cursor(lcd_getline(), lcd_getcolumn());
}

/** lcd_put_char - Put one character into software buffer. */
void lcd_put_char(char c)
{
	uint8_t line, column;

	if (c == '\r') {
		lcd_cursor(lcd_getline(), 0);
	} else if (c == '\n') {
		line = lcd_getline() + 1;
		lcd_cursor(line & (LCD_NR_LINES - 1), 0);
	} else {
		lcd_buffer[lcd_cursor_pos] = c;
		column = (lcd_getcolumn() + 1) & (LCD_NR_COLUMNS - 1);
		lcd_cursor(lcd_getline(), column);
	}
}

static int lcd_stream_putchar(char c, FILE *unused)
{
	lcd_put_char(c);
	return 0;
}

static FILE lcd_fstream = FDEV_SETUP_STREAM(lcd_stream_putchar, NULL,
					    _FDEV_SETUP_WRITE);

void _lcd_printf(const char PROGPTR *_fmt, ...)
{
	char fmt[64];
	va_list args;

	strlcpy_P(fmt, _fmt, sizeof(fmt));
	va_start(args, _fmt);
	vfprintf(&lcd_fstream, fmt, args);
	va_end(args);
}

void lcd_put_pstr(const char PROGPTR *str)
{
	uint8_t c;

	for ( ; ; str++) {
		c = pgm_read_byte(str);
		if (c == '\0')
			break;
		lcd_put_char(c);
	}
}

/** lcd_upload_char - Upload a user defined character to CGRAM.
 * @char_code: The character code to use.
 * @char_tab: The character bitmap. The bitmap has got one byte
 *	per character pixel row. The upper 3 bits of each byte
 *	are unused. For 5x10 char-pixel displays, the char_tab
 *	is 10 bytes long. For 5x8 char-pixel displays, it's 8 bytes.
 */
void lcd_upload_char(uint8_t char_code,
		     const uint8_t PROGPTR *char_tab)
{
	uint8_t i, c, address;

	address = char_code << (LCD_FONT_5x10 ? 4 : 3);
	for (i = 0; i < (LCD_FONT_5x10 ? 10 : 8); i++) {
		lcd_cmd_cgram_addr_set(address);
		c = pgm_read_byte(char_tab);
		lcd_data(c);
		address++;
		char_tab++;
	}
	lcd_cmd_cursor(lcd_getline(), lcd_getcolumn());
}

/** lcd_init - Initialize the LCD. */
void lcd_init(void)
{
	uint8_t i;

	LCD_DDR |= (0xF << LCD_DATA_SHIFT) |
		   LCD_PIN_E | LCD_PIN_RS;

	/* Force it into 8-bit mode first */
	LCD_PORT &= ~(LCD_PIN_E | LCD_PIN_RS |
		      (0xF << LCD_DATA_SHIFT));
	LCD_PORT |= (0x03 << LCD_DATA_SHIFT);
	long_delay_ms(200);
	for (i = 3; i; i--) {
		lcd_enable_pulse();
		_delay_ms(5);
	}
	/* We're in a known state. Enable 4-bit mode. */
	LCD_PORT = (LCD_PORT & ~(0xF << LCD_DATA_SHIFT)) |
		   (0x02 << LCD_DATA_SHIFT);
	lcd_enable_pulse();
	_delay_ms(10);

	lcd_cmd_funcset(0,
			(LCD_NR_LINES > 1) ? 1 : 0,
			LCD_FONT_5x10 ? 1 : 0);
	lcd_cmd_dispctl(0, 0, 0);
	lcd_cmd_clear();
	lcd_cmd_entrymode(1, 0);
	lcd_cmd_shiftctl(0, 0);
	lcd_cmd_dispctl(1, 0, 0);
	lcd_cmd_home();

	lcd_cursor_pos = 0;
	lcd_clear_buffer();
	lcd_commit();
}
