/* input.h — physical input devices feeding events into the loop. */
#ifndef GRIME_INPUT_H
#define GRIME_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grime/device.h"
#include "grime/event.h"
#include "grime/loop.h"

typedef struct grime_input grime_input;
/* One device grime has open. Opaque, and valid only while its grime_input is. */
typedef struct grime_device grime_device;

typedef void (*grime_input_key_cb)(const grime_key_event *ev, grime_device *dev, void *ud);
/* Everything from a grabbed device that is *not* a key event: motion, wheel,
 * and the device's own SYN_REPORT. A grabbed device's stream reaches nobody
 * but grime, so forward this or the pointer freezes. NULL to drop it. */
typedef void (*grime_input_raw_cb)(uint16_t type, uint16_t code, int32_t value,
				   grime_device *dev, void *ud);

typedef struct {
	grime_input_key_cb on_key;
	grime_input_raw_cb on_raw;
	void *ud;
} grime_input_sink;

/* Opens every device matched by `devices` (see grime/device.h). `grab` is the
 * global override: false (--dry-run) means grab nothing whatever a matcher
 * says. Grabbing waits until no keys are held, then takes the device
 * exclusively, so the OS sees only what grime emits. */
grime_input *grime_input_open(grime_loop *loop, const grime_device_match *devices,
			      size_t ndevices, bool grab, const grime_input_sink *sink);
void grime_input_close(grime_input *in); /* ungrabs */

const char *grime_device_path(const grime_device *dev);
const char *grime_device_name(const grime_device *dev);

#endif
