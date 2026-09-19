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
};

/* Linux backend: a uinput virtual keyboard. NULL on failure. */
grime_output *grime_output_uinput_new(void);
/* Logs instead of emitting (--dry-run). */
grime_output *grime_output_log_new(void);

#define GRIME_UINPUT_NAME "grime virtual keyboard"

#endif
