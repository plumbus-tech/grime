/* Probing /dev/input: what a device calls itself, what it can do, and which
 * stable symlinks point at it. */
#include "evdev_probe.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "grime/log.h"
#include "grime/output.h"

#define NBITS(x) (((x) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
#define TEST_BIT(bit, arr) ((arr)[(bit) / (8 * sizeof(long))] >> ((bit) % (8 * sizeof(long))) & 1)

/* Must agree with uinput_output.c: these are the codes that are buttons rather
 * than keys, and so don't count towards "this thing has keys on it". */
static bool is_button(int code)
{
	return (code >= BTN_MISC && code < KEY_OK) || (code >= BTN_DPAD_UP && code <= BTN_GRIPR2) ||
	       (code >= BTN_TRIGGER_HAPPY && code < KEY_MAX);
}

/* The full alphabet is what separates "the thing you type on" from a laptop's
 * hotkey block, which has real keys but no letters. */
static bool has_alphabet(const unsigned long *keys)
{
	for (int k = KEY_Q; k <= KEY_P; k++)
		if (!TEST_BIT(k, keys))
			return false;
	return TEST_BIT(KEY_A, keys) && TEST_BIT(KEY_Z, keys) && TEST_BIT(KEY_SPACE, keys);
}

static bool has_plain_keys(const unsigned long *keys)
{
	for (int k = 1; k < KEY_MAX; k++)
		if (!is_button(k) && TEST_BIT(k, keys))
			return true;
	return false;
}

/* by-id and by-path names survive reboots; eventN does not. Collect whatever
 * points at this node so a config can match on the stable spelling. */
static void collect_links(grime_probe *p)
{
	static const char *const dirs[] = {"/dev/input/by-id", "/dev/input/by-path"};
	const char *base = strrchr(p->path, '/');
	base = base ? base + 1 : p->path;

	for (size_t d = 0; d < sizeof dirs / sizeof dirs[0]; d++) {
		DIR *dir = opendir(dirs[d]);
		struct dirent *de;
		while (dir && (de = readdir(dir))) {
			if (de->d_name[0] == '.' || p->info.nlinks == PROBE_MAX_LINKS)
				continue;
			char full[PROBE_STR], target[PROBE_STR];
			snprintf(full, sizeof full, "%s/%s", dirs[d], de->d_name);
			ssize_t n = readlink(full, target, sizeof target - 1);
			if (n < 0)
				continue;
			target[n] = 0;
			const char *tb = strrchr(target, '/');
			if (strcmp(tb ? tb + 1 : target, base))
				continue;
			size_t i = p->info.nlinks++;
			snprintf(p->links[i], PROBE_STR, "%s", full);
			p->linkv[i] = p->links[i];
		}
		if (dir)
			closedir(dir);
	}
	p->info.links = p->linkv;
}

int grime_probe_fd(int fd, const char *path, grime_probe *p)
{
	memset(p, 0, sizeof *p);
	snprintf(p->path, sizeof p->path, "%s", path);
	snprintf(p->name, sizeof p->name, "?");
	ioctl(fd, EVIOCGNAME(sizeof p->name), p->name);
	ioctl(fd, EVIOCGPHYS(sizeof p->phys), p->phys);
	ioctl(fd, EVIOCGUNIQ(sizeof p->uniq), p->uniq);

	struct input_id id = {0};
	if (ioctl(fd, EVIOCGID, &id) < 0)
		return -1;

	unsigned long evs[NBITS(EV_MAX)] = {0}, keys[NBITS(KEY_MAX)] = {0},
		      rels[NBITS(REL_MAX)] = {0};
	if (ioctl(fd, EVIOCGBIT(0, sizeof evs), evs) < 0)
		return -1;
	bool has_key = TEST_BIT(EV_KEY, evs);
	p->has_rel = TEST_BIT(EV_REL, evs);
	p->has_abs = TEST_BIT(EV_ABS, evs);
	p->has_sw = TEST_BIT(EV_SW, evs);
	if (has_key)
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
	if (p->has_rel)
		ioctl(fd, EVIOCGBIT(EV_REL, sizeof rels), rels);

	p->is_ours = !strcmp(p->name, GRIME_UINPUT_NAME) ||
		     !strcmp(p->name, GRIME_UINPUT_POINTER_NAME) ||
		     (id.bustype == BUS_VIRTUAL && id.vendor == 0x6772);

	grime_device_kind kind = GRIME_KIND_ANY;
	if (has_key && has_alphabet(keys))
		kind = GRIME_KIND_KEYBOARD;
	else if (has_key && TEST_BIT(BTN_LEFT, keys) && p->has_rel &&
		 TEST_BIT(REL_X, rels) && TEST_BIT(REL_Y, rels))
		kind = GRIME_KIND_POINTER;
	else if (has_key && has_plain_keys(keys))
		kind = GRIME_KIND_KEYS;

	p->info = (grime_device_info){
		.path = p->path,
		.name = p->name,
		.phys = p->phys[0] ? p->phys : NULL,
		.uniq = p->uniq[0] ? p->uniq : NULL,
		.vendor = id.vendor,
		.product = id.product,
		.bus = id.bustype,
		.kind = kind,
	};
	collect_links(p);
	return 0;
}

static int by_event_number(const void *a, const void *b)
{
	return atoi(*(const char *const *)a + 5) - atoi(*(const char *const *)b + 5);
}

void grime_probe_each(bool (*fn)(const grime_probe *p, int fd, void *ud), void *ud)
{
	DIR *dir = opendir("/dev/input");
	if (!dir) {
		LOG_ERR("open /dev/input: %s", strerror(errno));
		return;
	}
	char *names[PROBE_MAX_NODES];
	size_t n = 0;
	struct dirent *de;
	while ((de = readdir(dir)) && n < PROBE_MAX_NODES)
		if (!strncmp(de->d_name, "event", 5))
			names[n++] = strdup(de->d_name);
	closedir(dir);
	qsort(names, n, sizeof names[0], by_event_number);

	for (size_t i = 0; i < n; i++) {
		char path[PROBE_STR];
		snprintf(path, sizeof path, "/dev/input/%s", names[i]);
		free(names[i]);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) {
			grime_probe p;
			memset(&p, 0, sizeof p);
			snprintf(p.path, sizeof p.path, "%s", path);
			snprintf(p.name, sizeof p.name, "(%s)", strerror(errno));
			p.info = (grime_device_info){.path = p.path, .vendor = -1,
						     .product = -1, .bus = -1};
			fn(&p, -1, ud);
			continue;
		}
		grime_probe p;
		if (grime_probe_fd(fd, path, &p) < 0 || !fn(&p, fd, ud))
			close(fd);
	}
}
