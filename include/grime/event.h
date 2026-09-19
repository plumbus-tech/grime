/* event.h — the one event type that flows from input backends into the engine.
 * Key codes use the Linux evdev numbering (KEY_A == 30, ...) on every platform;
 * other backends translate into it. */
#ifndef GRIME_EVENT_H
#define GRIME_EVENT_H

#include <stdint.h>

#define GRIME_KEY_COUNT 0x300 /* == KEY_CNT */

typedef enum {
	GRIME_RELEASE = 0,
	GRIME_PRESS = 1,
	GRIME_REPEAT = 2,
} grime_edge;

typedef struct {
	uint16_t code;
	grime_edge edge;
	uint64_t time_ms; /* monotonic */
} grime_key_event;

#endif
