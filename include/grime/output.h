/* output.h — where synthesized key events go (uinput on Linux, a recorder in tests). */
#ifndef GRIME_OUTPUT_H
#define GRIME_OUTPUT_H

#include <stdint.h>

typedef struct grime_output grime_output;
struct grime_output {
	/* value: 0 release, 1 press, 2 repeat. Implementations sync (EV_SYN) per call. */
	void (*emit)(grime_output *out, uint16_t code, int value);
	void (*destroy)(grime_output *out);
	void *impl;
	/* Optional (may be NULL). One raw evdev event, with NO implicit sync, for
	 * replaying a grabbed device's own stream verbatim -- feed it every event
	 * including that device's SYN_REPORT and the grouping is preserved
	 * exactly. Types are evdev EV_* numbers on every platform, the same
	 * convention as grime_key_event.code. */
	void (*emit_ev)(grime_output *out, uint16_t type, uint16_t code, int32_t value);
};

/* emit_ev if the output has one, otherwise a no-op. */
void grime_output_ev(grime_output *out, uint16_t type, uint16_t code, int32_t value);

/* Linux backend: a uinput virtual keyboard. NULL on failure. */
grime_output *grime_output_uinput_new(void);
/* Logs instead of emitting (--dry-run). */
grime_output *grime_output_log_new(void);

#define GRIME_UINPUT_NAME "grime virtual keyboard"
/* A second device, created only once grime actually has a button or some
 * motion to emit: a keyboard that also advertised BTN_LEFT and REL_X would be
 * tagged ID_INPUT_MOUSE and picked up by every pointer heuristic on the desk. */
#define GRIME_UINPUT_POINTER_NAME "grime virtual pointer"

#endif
