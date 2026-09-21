/* device.h — which physical input devices grime takes, and on what terms.
 *
 * A config's "devices" list is a list of *matchers*. A device is used if any
 * matcher matches it; within one matcher every field that is set must match.
 * The first matching entry supplies that device's options (grab, and what to
 * do with events the keymap didn't handle).
 *
 * Matching itself is pure -- no ioctls, no Linux headers -- so the backend
 * probes a device into a grime_device_info and the policy stays testable.
 * See docs/CONFIG.md. */
#ifndef GRIME_DEVICE_H
#define GRIME_DEVICE_H

#include <stdbool.h>
#include <stddef.h>

/* What a device looks like from its capability bits. The backend decides which
 * of these a device is; the names are what a config writes. */
typedef enum {
	GRIME_KIND_ANY = 0,   /* no capability filter */
	GRIME_KIND_KEYBOARD,  /* a full alphabet: the thing you type on */
	GRIME_KIND_KEYS,      /* keys but no alphabet: laptop fn rows, hotkey blocks */
	GRIME_KIND_POINTER,   /* buttons and relative motion: mice, trackpoints */
} grime_device_kind;

/* What happens to a press the keymap had no binding for. */
typedef enum {
	GRIME_UNMATCHED_DROP = 0, /* swallow it -- the device is grabbed, so it is gone */
	GRIME_UNMATCHED_PASS,     /* emit it unchanged, so the key still does its job */
} grime_unmatched;

typedef struct {
	char *path;  /* glob; matches the event node and its by-id/by-path links */
	char *name;  /* glob */
	char *phys;  /* glob */
	char *uniq;  /* glob */
	int vendor;  /* -1 = don't care */
	int product; /* -1 = don't care */
	int bus;     /* -1 = don't care */
	grime_device_kind kind;
	bool grab;               /* exclusive; false = observe only */
	grime_unmatched unmatched;
} grime_device_match;

/* One device, as the backend probed it. Strings are borrowed, not owned. */
typedef struct {
	const char *path;
	const char *name;
	const char *phys;
	const char *uniq;
	const char *const *links; /* by-id / by-path symlinks pointing at path */
	size_t nlinks;
	int vendor, product, bus;
	grime_device_kind kind;
} grime_device_info;

/* Index of the first matcher that takes this device, or -1 for none. */
int grime_device_match_find(const grime_device_match *m, size_t n,
			    const grime_device_info *info);
/* A deep copy, so a caller can outlive the config the matchers came from. */
grime_device_match *grime_device_match_dup(const grime_device_match *m, size_t n);
void grime_device_match_free(grime_device_match *m, size_t n);

const char *grime_device_kind_name(grime_device_kind k);
int grime_device_kind_from_name(const char *s); /* -1 if unknown */

#endif
