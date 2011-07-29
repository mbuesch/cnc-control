#ifndef MACHINE_INTERFACE_INTERNAL_H_
#define MACHINE_INTERFACE_INTERNAL_H_

#include "util.h"
#include "machine_interface.h"


#define INTERRUPT_QUEUE_MAX_LEN		16

/** interrupt_queue_freecount - Returns count of free slots in TX queue.
 * Count can change at any time, if IRQs are enabled.
 */
uint8_t interrupt_queue_freecount(void);

void send_interrupt_count(const struct control_interrupt *irq,
			  uint8_t size, uint8_t count);

/** send_interrupt - Send an interrupt to the host.
 * This is the API for sending an interrupt.
 */
static inline
void send_interrupt(const struct control_interrupt *irq,
		    uint8_t size)
{
	send_interrupt_count(irq, size, 1);
}

/** send_interrupt_discard_old - Send an interrupt to the host.
 * Also discard already queued IRQs of the same type.
 * It's not guaranteed that all old IRQs are discarded, though.
 */
void send_interrupt_discard_old(const struct control_interrupt *irq,
				uint8_t size);

/** get_active_devflags - Get device flags atomically.
 */
uint16_t get_active_devflags(void);

static inline uint8_t get_active_devflags_low(void)
{
	extern uint16_t active_devflags;
	mb();
	return lo8(active_devflags); /* atomic on AVR */
}
static inline uint8_t get_active_devflags_high(void)
{
	extern uint16_t active_devflags;
	mb();
	return hi8(active_devflags); /* atomic on AVR */
}
/** devflag_is_set - Flag test optimized for constant mask.
 * Inefficient for non-const mask! */
static inline bool devflag_is_set(const uint16_t mask)
{
	bool res;

	if (mask == 0)
		res = 0;
	else if (hi8(mask) == 0)
		res = !!(get_active_devflags_low() & lo8(mask));
	else if (lo8(mask) == 0)
		res = !!(get_active_devflags_high() & hi8(mask));
	else
		res = !!(get_active_devflags() & mask);

	return res;
}

/** modify_devflags - Modify device flags atomically
 * and send notification interrupt to the host.
 */
void modify_devflags(uint16_t mask, uint16_t set);

/** reset_devflags - Reset device flags to defaults */
void reset_devflags(void);


#endif /* MACHINE_INTERFACE_INTERNAL_H_ */
