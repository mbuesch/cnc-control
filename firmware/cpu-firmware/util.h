#ifndef MY_UTIL_H_
#define MY_UTIL_H_

#include <stdint.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>


#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)

#undef offsetof
#define offsetof(type, member)	((size_t)&((type *)0)->member)

#undef typeof
#define typeof			__typeof__

#undef container_of
#define container_of(p, type, member)	({				\
		typeof(((type *)0)->member) *__m = (p);			\
		(type *)((char *)__m - offsetof(type, member));	})

#define min(a, b)		((a) < (b) ? (a) : (b))
#define max(a, b)		((a) > (b) ? (a) : (b))

#define abs(x)			((x) >= 0 ? (x) : -(x))

#define lo8(x)			((uint8_t)(x))
#define hi8(x)			((uint8_t)((x) >> 8))

#define ARRAY_SIZE(x)		(sizeof(x) / sizeof((x)[0]))

#define BIT(nr)			(1ull << (nr))

/* Progmem pointer annotation. */
#define PROGPTR			/* progmem pointer */

/* Memory barrier.
 * The CPU doesn't have runtime reordering, so we just
 * need a compiler memory clobber. */
#define mb()			__asm__ __volatile__("" : : : "memory")

#define ACCESS_ONCE(x)		(*(volatile typeof(x) *)&(x))

/* Atomic 8bit load and store.
 * On AVR 8bit loads and stores are always atomic. However, use
 * ACCESS_ONCE() to suppress compiler optimization. */
#define ATOMIC_LOAD8(u8var)		ACCESS_ONCE(u8var)
#define ATOMIC_STORE8(u8var, newval)	do { ACCESS_ONCE(u8var) = newval; } while (0)

/* Generic atomic load and store helpers. */
#define ATOMIC_LOAD(var)		({		\
		typeof(var) _retval = 0;		\
		if (sizeof(_retval) == 1)		\
			_retval = ATOMIC_LOAD8(var);	\
		else					\
			_ATOMIC_LOAD_invalid_size();	\
		_retval;				\
	})

#define ATOMIC_STORE(var, newval)	do {		\
		typeof(var) _dummyvar;			\
		if (sizeof(_dummyvar) == 1)		\
			ATOMIC_STORE8(var, newval);	\
		else					\
			_ATOMIC_STORE_invalid_size();	\
	} while (0)

extern void _ATOMIC_LOAD_invalid_size(void); /* Linker error helper */
extern void _ATOMIC_STORE_invalid_size(void); /* Linker error helper */


/* Convert something indirectly to a string */
#define __stringify(x)		#x
#define stringify(x)		__stringify(x)

/* Assertions */
void do_panic(const char PROGPTR *msg) __attribute__((noreturn));
#define panic(string_literal)	do_panic(PSTR(string_literal))
#define BUILD_BUG_ON(x)		((void)sizeof(char[1 - 2 * !!(x)]))
#define BUG_ON(x)					\
	do {						\
		if (unlikely(x))			\
			panic(__FILE__			\
			      stringify(__LINE__));	\
	} while (0)

/** reboot - Reboot the device */
void reboot(void) __attribute__((noreturn));

void long_delay_ms(uint16_t ms);

/* Delay one CPU cycle */
#undef nop
#define nop()			__asm__ __volatile__("nop\n" : : : "memory")

/* Code flow attributes */
#define noreturn	__attribute__((__noreturn__))
#define _mainfunc	__attribute__((__OS_main__))
#if defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ >= 5
# define unreachable()	__builtin_unreachable()
#else
# define unreachable()	while (1)
#endif

/* Forced no-inline */
#define noinline	__attribute__((__noinline__))

/* Forced no-instrumentation */
#define noinstrument	__attribute__((__no_instrument_function__))


typedef _Bool		bool;


static inline void irq_disable(void)
{
	cli();
	mb();
}

static inline void irq_enable(void)
{
	mb();
	sei();
}

static inline uint8_t irq_disable_save(void)
{
	uint8_t sreg = SREG;
	cli();
	mb();
	return sreg;
}

static inline void irq_restore(uint8_t sreg_flags)
{
	mb();
	SREG = sreg_flags;
}

#define __irqs_disabled(sreg)	(!((sreg) & (1 << SREG_I)))
#define irqs_disabled()		__irqs_disabled(SREG)


/* Convert a number (0-F) to a hexadecimal ASCII digit */
uint8_t hexdigit_to_ascii(uint8_t digit) noinstrument;


/* Smart program memory read */
#define pgm_read(ptr)	({						\
		typeof(*(ptr)) *_pgm_ptr = (ptr);			\
		uint32_t _pgm_ret;					\
		if (sizeof(*_pgm_ptr) == 1)				\
			_pgm_ret = (uint8_t)pgm_read_byte(_pgm_ptr);	\
		else if (sizeof(*_pgm_ptr) == 2)			\
			_pgm_ret = (uint16_t)pgm_read_word(_pgm_ptr);	\
		else if (sizeof(*_pgm_ptr) == 4)			\
			_pgm_ret = (uint32_t)pgm_read_dword(_pgm_ptr);	\
		else							\
			_pgm_read_invalid_type_size();			\
		_pgm_ret;						\
	})
extern void _pgm_read_invalid_type_size(void);


/* Find first set bit.
 * Returns 0, if no bit is set. */
uint8_t ffs16(uint16_t value);

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1ul) / (d))


#endif /* MY_UTIL_H_ */
