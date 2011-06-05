#ifndef OVERRIDE_SWITCH_H_
#define OVERRIDE_SWITCH_H_

#include <stdint.h>

/** override_init - Initialize the feed-override switch */
void override_init(void);
/** override_get_pos - Get the feed-override position.
 * 0 = leftmost, 0xFF = rightmost. */
uint8_t override_get_pos(void);

#endif /* OVERRIDE_SWITCH_H_ */
