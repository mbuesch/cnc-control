#ifndef HD44780_LCD_H_
#define HD44780_LCD_H_

#include "util.h"

#include <stdint.h>


/*** Hardware pin assignments ***/
#define LCD_PORT	PORTA		/* LCD PORT register */
#define LCD_DDR		DDRA		/* LCD DDR register */
#define LCD_PIN_E	(1 << 3)	/* E pin */
#define LCD_PIN_RS	(1 << 2)	/* RS pin */
#define LCD_DATA_SHIFT	4		/* Data pins at D4-D7 */

/*** Hardware parameters ***/
#define LCD_NR_LINES		2   /* Linecount. Must be power of two. */
#define LCD_NR_COLUMNS		16  /* Columncount. Must be power of two. */
#define LCD_FONT_5x10		0   /* 5x10 or 5x8 font? */


/*** Hardware access ***/

void lcd_init(void);
void lcd_commit(void);
void lcd_cmd_cursor(uint8_t line, uint8_t column);
void lcd_cmd_dispctl(uint8_t display_on,
		     uint8_t cursor_on,
		     uint8_t cursor_blink);
void lcd_upload_char(uint8_t char_code,
		     const uint8_t PROGPTR *char_tab);


/*** Software buffer access ***/

void lcd_put_char(char c);

/** lcd_printf -- Formatted print to the LCD buffer. */
void _lcd_printf(const char PROGPTR *_fmt, ...);
#define lcd_printf(fmt, ...)	_lcd_printf(PSTR(fmt) ,##__VA_ARGS__)

/** lcd_put_str -- Write prog-str to LCD buffer. */
void lcd_put_pstr(const char PROGPTR *str);
#define lcd_put_str(str)	lcd_put_pstr(PSTR(str))

void lcd_clear_buffer(void);

/** lcd_cursor - Move the LCD software cursor. */
static inline void lcd_cursor(uint8_t line, uint8_t column)
{
	extern uint8_t lcd_cursor_pos;
	lcd_cursor_pos = (line * LCD_NR_COLUMNS) + column;
}

/** lcd_getline - Returns the current LCD software cursor line. */
static inline uint8_t lcd_getline(void)
{
	extern uint8_t lcd_cursor_pos;
	return lcd_cursor_pos / LCD_NR_COLUMNS;
}

/** lcd_getcolumn - Returns the current LCD software cursor column. */
static inline uint8_t lcd_getcolumn(void)
{
	extern uint8_t lcd_cursor_pos;
	return lcd_cursor_pos % LCD_NR_COLUMNS;
}

#endif /* HD44780_LCD_H_ */
