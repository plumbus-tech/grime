/* input.h — physical keyboards feeding events into the loop. */
#ifndef GRIME_INPUT_H
#define GRIME_INPUT_H

#include <stdbool.h>
#include <stddef.h>

#include "grime/event.h"
#include "grime/loop.h"

typedef struct grime_input grime_input;
typedef void (*grime_input_cb)(const grime_key_event *ev, void *ud);

/* devices: paths, or the single entry "auto" (every device that looks like a
 * keyboard). With grab, waits until no keys are held, then grabs exclusively
 * so the OS only sees what grime emits. */
grime_input *grime_input_open(grime_loop *loop, char *const *devices, size_t ndevices, bool grab,
			      grime_input_cb cb, void *ud);
void grime_input_close(grime_input *in); /* ungrabs */

#endif
