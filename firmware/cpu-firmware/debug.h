#ifndef DEBUG_INTERFACE_H_
#define DEBUG_INTERFACE_H_

#include "machine_interface_internal.h"
#include "util.h"

#include <avr/pgmspace.h>


void debug_init(void);

void do_debug_printf(const char PROGPTR *fmt, ...);

#define _debug_printf(pfmt, ...)	do {				\
		if (debug_enabled())					\
			do_debug_printf(pfmt ,##__VA_ARGS__);		\
	} while (0)

#define debug_printf(fmt, ...)		do {				\
		if (debug_enabled())					\
			do_debug_printf(PSTR(fmt) ,##__VA_ARGS__);	\
	} while (0)

void debug_dumpmem(const void *_mem, uint8_t size);

static inline bool debug_enabled(void)
{
	return unlikely(!devflag_is_set(DEVICE_FLG_NODEBUG));
}

static inline bool debug_verbose(void)
{
	return unlikely(devflag_is_set(DEVICE_FLG_VERBOSEDBG));
}


/** debug_ringbuf_count -- Get an approximate count of bytes
 * currently in the ring buffer. */
uint8_t debug_ringbuf_count(void);

/** debug_ringbuf_get - Get bytes from the ringbuffer.
 * @buf: Target buffer
 * @size: Target buffer size
 * Returns the number of bytes copied to the target buffer. */
uint8_t debug_ringbuf_get(void *buf, uint8_t size);

#endif /* DEBUG_INTERFACE_H_ */
