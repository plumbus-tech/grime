/* input.h — physical input devices feeding events into the loop. */
#ifndef GRIME_INPUT_H
#define GRIME_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "grime/device.h"
#include "grime/event.h"
#include "grime/loop.h"

typedef struct grime_input grime_input;
/* One device grime has open. Opaque, and valid only while its grime_input is. */
typedef struct grime_device grime_device;

typedef void (*grime_input_key_cb)(const grime_key_event *ev, grime_device *dev, void *ud);
/* Everything from a grabbed device that is *not* a key event: motion, wheel,
 * and the device's own SYN_REPORT. A grabbed device's stream reaches nobody
 * but grime, so replay it or the pointer freezes -- see
 * grime_device_needs_replay(). NULL to drop it. */
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
/* Mirror a lock light onto every device grime grabbed. Quietly does nothing
 * for a device that was opened read-only. */
void grime_input_set_led(grime_input *in, uint16_t led, int on);

/* True if any device it opened has motion grime is now responsible for
 * replaying, i.e. a virtual pointer is needed. */
bool grime_input_needs_pointer(const grime_input *in);

const char *grime_device_path(const grime_device *dev);
const char *grime_device_name(const grime_device *dev);
/* What its matcher said to do with a key the keymap had no binding for. */
grime_unmatched grime_device_unmatched(const grime_device *dev);
/* True if this device is grabbed AND has motion of its own, i.e. its raw
 * events reach nobody unless you replay them. False for a device that is only
 * being watched -- the OS is still reading that one directly, and replaying it
 * would double every movement. */
bool grime_device_needs_replay(const grime_device *dev);

/* Print every input device, what grime makes of it, and which matcher (if any)
 * would take it. Never opens /dev/uinput and never grabs. */
void grime_input_list(FILE *f, const grime_device_match *devices, size_t ndevices);

#endif
