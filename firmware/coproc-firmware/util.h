#ifndef MY_UTIL_H_
#define MY_UTIL_H_

#include <stdint.h>
#include <avr/interrupt.h>


#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)

#undef offsetof
#define offsetof(type, member)	((size_t)&((type *)0)->member)

#define min(a, b)		((a) < (b) ? (a) : (b))
#define max(a, b)		((a) > (b) ? (a) : (b))

#define abs(x)			((x) >= 0 ? (x) : -(x))

#define lo8(x)			((uint8_t)(x))
#define hi8(x)			((uint8_t)((x) >> 8))

#define ARRAY_SIZE(x)		(sizeof(x) / sizeof((x)[0]))

#define BUILD_BUG_ON(x)		((void)sizeof(char[1 - 2 * !!(x)]))

/* Progmem pointer annotation. */
#define PROGPTR			/* progmem pointer */

/* Memory barrier.
 * The CPU doesn't have runtime reordering, so we just
 * need a compiler memory clobber. */
#define mb()			__asm__ __volatile__("" : : : "memory")

/* Convert something indirectly to a string */
#define __stringify(x)		#x
#define stringify(x)		__stringify(x)

/* Delay one CPU cycle */
#undef nop
#define nop()		do { __asm__ __volatile__("nop\n"); } while (0)

/* Code flow attributes */
#define noreturn	__attribute__((__noreturn__))
#define _mainfunc	__attribute__((__OS_main__))
#if __GNUC_MAJOR__ >= 4 && __GNUC_MINOR__ >= 5
# define unreachable()	__builtin_unreachable()
#else
# define unreachable()	while (1)
#endif

/* Forced no-inline */
#define noinline	__attribute__((__noinline__))


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


#endif /* MY_UTIL_H_ */
