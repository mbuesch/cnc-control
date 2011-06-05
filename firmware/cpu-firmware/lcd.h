#ifndef HD44780_LCD_H_
#define HD44780_LCD_H_

#include <stdint.h>
#include <avr/pgmspace.h>


#define LCD_NR_LINES		2
#define LCD_NR_COLUMNS		16
#define LCD_NR_CHARS		(LCD_NR_LINES * LCD_NR_COLUMNS)


extern uint8_t lcd_cursor_pos;


void lcd_init(void);

void lcd_put_char(char c);

void _lcd_printf(const prog_char *fmt, ...);
#define lcd_printf(fmt, ...)	_lcd_printf(PSTR(fmt) ,##__VA_ARGS__)

void _lcd_put_pstr(const prog_char *str);
#define lcd_put_pstr(str)	_lcd_put_pstr(PSTR(str))

void lcd_clear_buffer(void);
void lcd_commit(void);

void lcd_hwcursor(uint8_t line, uint8_t column);

static inline void lcd_cursor(uint8_t line, uint8_t column)
{
	lcd_cursor_pos = (line * LCD_NR_COLUMNS) + column;
}

static inline uint8_t lcd_getline(void)
{
	return lcd_cursor_pos / LCD_NR_COLUMNS;
}

static inline uint8_t lcd_getcolumn(void)
{
	return lcd_cursor_pos % LCD_NR_COLUMNS;
}

#endif /* HD44780_LCD_H_ */
