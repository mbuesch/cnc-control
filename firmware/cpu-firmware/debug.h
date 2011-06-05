#ifndef DEBUG_INTERFACE_H_
#define DEBUG_INTERFACE_H_

#include "util.h"
#include "machine_interface.h"

#include <avr/pgmspace.h>


void debug_init(void);
void _debug_printf(const prog_char *fmt, ...);
#define debug_printf(fmt, ...)	_debug_printf(PSTR(fmt) ,##__VA_ARGS__)
void debug_dumpmem(const void *_mem, uint8_t size);

static inline bool debug_verbose(void)
{
	return !!(get_active_devflags() & DEVICE_FLG_VERBOSEDBG);
}


#endif /* DEBUG_INTERFACE_H_ */
