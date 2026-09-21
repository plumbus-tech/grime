/* Read (and, for the ones a matcher says to, exclusively grab) input devices. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "evdev_probe.h"
#include "grime/input.h"
#include "grime/log.h"
#include "grime/output.h"

#define MAX_DEVICES 64
#define NBITS(x) (((x) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))

struct grime_device {
	int fd;
	char path[300];
	char name[256];
	bool grab;
	grime_unmatched unmatched;
};

struct grime_input {
	grime_loop *loop;
	grime_input_sink sink;
	bool grab;
	bool grabbed;
	struct grime_device devs[MAX_DEVICES];
	int nfds;
	grime_timer *grab_timer;
};

const char *grime_device_path(const grime_device *dev) { return dev->path; }
const char *grime_device_name(const grime_device *dev) { return dev->name; }

static bool any_key_down(int fd)
{
	unsigned long bits[NBITS(KEY_MAX)] = {0};
	if (ioctl(fd, EVIOCGKEY(sizeof bits), bits) < 0)
		return false;
	for (size_t i = 0; i < NBITS(KEY_MAX); i++)
		if (bits[i])
			return true;
	return false;
}

/* Forget a device: off the loop, closed, and compacted out of fds[]. */
static void drop_device(grime_input *in, int i)
{
	if (in->grabbed && in->devs[i].grab)
		ioctl(in->devs[i].fd, EVIOCGRAB, 0);
	grime_loop_del_fd(in->loop, in->devs[i].fd);
	close(in->devs[i].fd);
	for (; i < in->nfds - 1; i++)
		in->devs[i] = in->devs[i + 1];
	in->nfds--;
}

static int find_fd(const grime_input *in, int fd)
{
	for (int i = 0; i < in->nfds; i++)
		if (in->devs[i].fd == fd)
			return i;
	return -1;
}

static void on_readable(grime_loop *loop, int fd, int io, void *ud)
{
	(void)io;
	grime_input *in = ud;
	struct input_event evs[64];
	ssize_t n = read(fd, evs, sizeof evs);
	if (n < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return;
		LOG_WARN("input device fd %d: %s, dropping it", fd, strerror(errno));
		int i = find_fd(in, fd);
		if (i >= 0)
			drop_device(in, i);
		else
			grime_loop_del_fd(loop, fd);
		if (!in->nfds) {
			LOG_ERR("no input devices left, exiting");
			grime_loop_stop(loop);
		}
		return;
	}
	if (!in->grabbed && in->grab)
		return; /* still waiting for keys to be released */
	int di = find_fd(in, fd);
	grime_device *dev = di >= 0 ? &in->devs[di] : NULL;
	for (size_t i = 0; i < n / sizeof evs[0]; i++) {
		if (evs[i].type != EV_KEY || evs[i].value < 0 || evs[i].value > 2)
			continue;
		grime_key_event ev = {
			.code = evs[i].code,
			.edge = (grime_edge)evs[i].value,
			.time_ms = grime_now_ms(),
		};
		in->sink.on_key(&ev, dev, in->sink.ud);
	}
}

/* Grabbing while a key is down leaves the OS thinking it's stuck, so wait. */
static void try_grab(grime_loop *loop, void *ud)
{
	(void)loop;
	grime_input *in = ud;
	for (int i = 0; i < in->nfds; i++) {
		if (in->devs[i].grab && any_key_down(in->devs[i].fd)) {
			grime_timer_arm(in->grab_timer, 20);
			return;
		}
	}
	int n = 0;
	for (int i = 0; i < in->nfds; i++) {
		if (!in->devs[i].grab)
			continue;
		if (ioctl(in->devs[i].fd, EVIOCGRAB, 1) < 0)
			LOG_WARN("grab %s: %s", in->devs[i].path, strerror(errno));
		else
			n++;
	}
	in->grabbed = true;
	LOG_INFO("grabbed %d device(s); grime is live", n);
}

/* Take a device the matchers chose. The fd is ours from here. */
static void keep_device(grime_input *in, const grime_probe *p, int fd,
			const grime_device_match *m)
{
	if (in->nfds == MAX_DEVICES) {
		LOG_WARN("more than %d devices, ignoring %s", MAX_DEVICES, p->path);
		return;
	}
	grime_device *dev = &in->devs[in->nfds++];
	*dev = (grime_device){.fd = fd, .grab = m->grab && in->grab, .unmatched = m->unmatched};
	snprintf(dev->path, sizeof dev->path, "%s", p->path);
	snprintf(dev->name, sizeof dev->name, "%s", p->name);
	LOG_INFO("using %s (%s) [%s%s]", dev->path, dev->name,
		 grime_device_kind_name(p->info.kind), dev->grab ? ", grabbed" : ", observing");
	grime_loop_add_fd(in->loop, fd, GRIME_IO_READ, on_readable, in);
}

struct open_ctx {
	grime_input *in;
	const grime_device_match *devices;
	size_t ndevices;
	bool matched[MAX_DEVICES];
};

static bool consider(const grime_probe *p, int fd, void *ud)
{
	struct open_ctx *c = ud;
	if (fd < 0) {
		LOG_DEBUG("skipping %s: %s", p->path, p->name);
		return false;
	}
	if (p->is_ours)
		return false; /* never read our own output back in */
	int i = grime_device_match_find(c->devices, c->ndevices, &p->info);
	if (i < 0)
		return false;
	if (c->ndevices <= MAX_DEVICES)
		c->matched[i] = true;
	keep_device(c->in, p, fd, &c->devices[i]);
	return true;
}

grime_input *grime_input_open(grime_loop *loop, const grime_device_match *devices,
			      size_t ndevices, bool grab, const grime_input_sink *sink)
{
	grime_input *in = calloc(1, sizeof *in);
	*in = (grime_input){.loop = loop, .sink = *sink, .grab = grab};

	struct open_ctx ctx = {.in = in, .devices = devices, .ndevices = ndevices};
	grime_probe_each(consider, &ctx);

	/* A matcher that names one exact path deserves a straight answer when
	 * nothing came back -- usually a typo or a device that isn't plugged in. */
	for (size_t i = 0; i < ndevices && i < MAX_DEVICES; i++) {
		if (ctx.matched[i])
			continue;
		if (devices[i].path)
			LOG_WARN("devices[%zu]: nothing matched \"%s\"", i, devices[i].path);
		else
			LOG_WARN("devices[%zu]: nothing matched (see grime --list-devices)", i);
	}
	if (!in->nfds) {
		LOG_ERR("no input devices matched; try grime --list-devices");
		free(in);
		return NULL;
	}
	if (grab) {
		LOG_INFO("waiting for all keys to be released...");
		in->grab_timer = grime_timer_new(loop, try_grab, in);
		grime_timer_arm(in->grab_timer, 0);
	}
	return in;
}

void grime_input_close(grime_input *in)
{
	if (!in)
		return;
	while (in->nfds)
		drop_device(in, in->nfds - 1);
	grime_timer_free(in->grab_timer);
	free(in);
}
