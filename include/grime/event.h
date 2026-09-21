/* event.h — the one event type that flows from input backends into the engine.
 * Key codes use the Linux evdev numbering (KEY_A == 30, ...) on every platform;
 * other backends translate into it. */
#ifndef GRIME_EVENT_H
#define GRIME_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#define GRIME_KEY_COUNT 0x300 /* == KEY_CNT */

/* evdev event types, for the raw events that flow past the keymap rather than
 * through it. Same deal as key codes: these numbers are the contract on every
 * platform, and a backend that isn't evdev translates into them. */
enum {
	GRIME_EV_SYN = 0,
	GRIME_EV_KEY = 1,
	GRIME_EV_REL = 2,
	GRIME_EV_ABS = 3,
	GRIME_EV_LED = 0x11,
};

/* The lights on a keyboard, in evdev numbering. */
enum {
	GRIME_LED_NUM = 0,
	GRIME_LED_CAPS = 1,
	GRIME_LED_SCROLL = 2,
	GRIME_LED_COUNT = 3,
};

/* Buttons share the key code space: mouse, joystick, gamepad and digitizer
 * buttons are codes like any other, which is why a click can be a keymap entry.
 * These are the evdev BTN_* ranges, spelled as numbers so the engine and the
 * config can ask without including a kernel header. */
static inline bool grime_is_button(uint16_t code)
{
	return (code >= 0x100 && code < 0x160) || (code >= 0x220 && code <= 0x227) ||
	       (code >= 0x2c0 && code < 0x2ff);
}

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
