/* --list-devices: every input device, what grime makes of it, and which
 * "devices" entry would take it. Calls the same grime_device_match_find() the
 * daemon does, so this can never be a second opinion. Never grabs anything. */
#include <stdio.h>
#include <string.h>

#include "evdev_probe.h"
#include "grime/input.h"

struct list_ctx {
	FILE *f;
	const grime_device_match *devices;
	size_t ndevices;
};

static bool print_one(const grime_probe *p, int fd, void *ud)
{
	struct list_ctx *c = ud;
	if (fd < 0) {
		fprintf(c->f, "  %-18s %-8s %-9s %-30s %s\n", p->path, "-", "-", p->name,
			"unreadable");
		return false;
	}

	char match[64] = "-";
	if (p->is_ours) {
		snprintf(match, sizeof match, "grime's own");
	} else {
		int i = grime_device_match_find(c->devices, c->ndevices, &p->info);
		if (i >= 0)
			snprintf(match, sizeof match, "devices[%d] %s", i,
				 c->devices[i].grab ? "grab" : "observe");
	}

	char id[16];
	snprintf(id, sizeof id, "%04x:%04x", p->info.vendor, p->info.product);
	fprintf(c->f, "  %-18s %-8s %-9s %-30s %s\n", p->path,
		grime_device_kind_name(p->info.kind), id, p->name, match);

	if (p->has_abs)
		fprintf(c->f, "      absolute axes: grime can't forward these, so it won't grab this one\n");
	if (p->has_sw)
		fprintf(c->f, "      switches: grabbing this also hides its lid/rfkill reports\n");
	for (size_t i = 0; i < p->info.nlinks; i++)
		fprintf(c->f, "      %s\n", p->info.links[i]);
	return false;
}

void grime_input_list(FILE *f, const grime_device_match *devices, size_t ndevices)
{
	fprintf(f, "  %-18s %-8s %-9s %-30s %s\n", "device", "kind", "id", "name", "match");
	struct list_ctx ctx = {.f = f, .devices = devices, .ndevices = ndevices};
	grime_probe_each(print_one, &ctx);
	fprintf(f, "\nkind: keyboard = has a full alphabet, keys = keys but no alphabet\n"
		   "      (laptop fn rows, hotkey blocks), pointer = buttons and motion.\n");
}
