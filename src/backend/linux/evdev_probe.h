/* Backend-internal: read one device's identity and capabilities off an fd, in
 * the neutral shape grime/device.h matches against. Shared by the daemon and
 * by --list-devices, so the preview cannot disagree with what actually runs. */
#ifndef GRIME_EVDEV_PROBE_H
#define GRIME_EVDEV_PROBE_H

#include <stdbool.h>
#include <stddef.h>

#include "grime/device.h"

#define PROBE_MAX_LINKS 8
#define PROBE_STR 256
#define PROBE_MAX_NODES 256

typedef struct {
	char path[PROBE_STR];
	char name[PROBE_STR];
	char phys[PROBE_STR];
	char uniq[PROBE_STR];
	char links[PROBE_MAX_LINKS][PROBE_STR];
	const char *linkv[PROBE_MAX_LINKS];
	bool has_rel, has_abs, has_sw;
	bool is_ours;   /* one of grime's own virtual devices */
	int open_errno; /* why fd is -1, when it is */
	grime_device_info info;
} grime_probe;

/* Fills `p` from an open device fd. -1 if the device won't answer at all. */
int grime_probe_fd(int fd, const char *path, grime_probe *p);

/* Opens every /dev/input/event* in numeric order and calls fn. Return true
 * from fn to keep the fd (it becomes yours); false and it is closed. A node
 * that would not open is still reported, with fd -1 and the reason in name[],
 * because "grime cannot see this one" is exactly what --list-devices is for. */
void grime_probe_each(bool (*fn)(const grime_probe *p, int fd, void *ud), void *ud);

#endif
