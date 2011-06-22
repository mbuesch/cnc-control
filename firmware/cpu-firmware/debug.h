#ifndef DEBUG_INTERFACE_H_
#define DEBUG_INTERFACE_H_

#include "util.h"
#include "machine_interface.h"

#include <avr/pgmspace.h>


void debug_init(void);

void do_debug_printf(const prog_char *fmt, ...);

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


#endif /* DEBUG_INTERFACE_H_ */
