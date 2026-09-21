/* Read (and, for the ones a matcher says to, exclusively grab) input devices. */
#include <errno.h>
#include <fcntl.h>
#include <sys/inotify.h>
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
	bool grabbed; /* per device: one plugged in later grabs on its own terms */
	bool forward; /* has motion of its own that must keep reaching the OS */
	grime_unmatched unmatched;
};

struct grime_input {
	grime_loop *loop;
	grime_input_sink sink;
	bool grab;
	grime_device_match *devices; /* our own copy: the config is freed after open */
	size_t ndevices;
	struct grime_device devs[MAX_DEVICES];
	int nfds;
	grime_timer *grab_timer;
	int inotify_fd;
	grime_timer *retry_timer; /* udev has not finished with a new node yet */
	char pending[MAX_DEVICES][PROBE_STR];
	int npending;
	int retries;
};

const char *grime_device_path(const grime_device *dev) { return dev->path; }
const char *grime_device_name(const grime_device *dev) { return dev->name; }
grime_unmatched grime_device_unmatched(const grime_device *dev) { return dev->unmatched; }
bool grime_device_needs_replay(const grime_device *dev) { return dev->grab && dev->forward; }

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
	if (in->devs[i].grabbed)
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
		/* With hotplug watching, an unplugged device is something to wait
		 * for, not a reason to quit; without it, there is nothing to wait
		 * for and staying alive would just be a process doing nothing. */
		if (!in->nfds) {
			if (in->inotify_fd < 0) {
				LOG_ERR("no input devices left, exiting");
				grime_loop_stop(loop);
			} else {
				LOG_WARN("no input devices left; waiting for one to appear");
			}
		}
		return;
	}
	int di = find_fd(in, fd);
	grime_device *dev = di >= 0 ? &in->devs[di] : NULL;
	if (dev && dev->grab && !dev->grabbed)
		return; /* still waiting for this device's keys to be released */
	for (size_t i = 0; i < n / sizeof evs[0]; i++) {
		if (evs[i].type == EV_KEY) {
			if (evs[i].value < 0 || evs[i].value > 2)
				continue;
			grime_key_event ev = {
				.code = evs[i].code,
				.edge = (grime_edge)evs[i].value,
				.time_ms = grime_now_ms(),
			};
			in->sink.on_key(&ev, dev, in->sink.ud);
			continue;
		}
		/* Motion, wheel, and the device's own SYN_REPORT. Whether these
		 * need replaying is the sink's call (grime_device_needs_replay),
		 * because --watch wants to see them without sending them anywhere. */
		if (dev && in->sink.on_raw)
			in->sink.on_raw(evs[i].type, evs[i].code, evs[i].value, dev,
					in->sink.ud);
	}
}

/* Grabbing while a key is down leaves the OS thinking it's stuck, so wait --
 * per device, because one plugged in later can't be made to wait for the rest. */
static void try_grab(grime_loop *loop, void *ud)
{
	(void)loop;
	grime_input *in = ud;
	bool pending = false;
	for (int i = 0; i < in->nfds; i++) {
		grime_device *d = &in->devs[i];
		if (!d->grab || d->grabbed)
			continue;
		if (any_key_down(d->fd)) {
			pending = true;
			continue;
		}
		if (ioctl(d->fd, EVIOCGRAB, 1) < 0) {
			LOG_WARN("grab %s: %s", d->path, strerror(errno));
			d->grab = false; /* don't retry forever on a device that won't */
			continue;
		}
		d->grabbed = true;
		LOG_INFO("grabbed %s (%s)", d->path, d->name);
	}
	if (pending)
		grime_timer_arm(in->grab_timer, 20);
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
	*dev = (grime_device){.fd = fd, .grab = m->grab && in->grab, .unmatched = m->unmatched,
			      .forward = p->has_rel};
	snprintf(dev->path, sizeof dev->path, "%s", p->path);
	snprintf(dev->name, sizeof dev->name, "%s", p->name);

	/* Absolute axes would need the source's ranges declared on a virtual
	 * device axis by axis, which grime doesn't do. Grabbing one and dropping
	 * its events means a touchpad or touchscreen that no longer works, so
	 * don't: watch it instead and say why. */
	if (dev->grab && p->has_abs) {
		LOG_ERR("%s (%s) reports absolute axes, which grime can't forward -- "
			"observing it instead of grabbing it",
			dev->path, dev->name);
		dev->grab = false;
	}
	if (dev->grab && p->has_sw)
		LOG_WARN("grabbing %s (%s) also hides its switch events (lid, rfkill) "
			 "from the system",
			 dev->path, dev->name);

	LOG_INFO("using %s (%s) [%s%s]", dev->path, dev->name,
		 grime_device_kind_name(p->info.kind), dev->grab ? ", grabbed" : ", observing");
	grime_loop_add_fd(in->loop, fd, GRIME_IO_READ, on_readable, in);
}

struct open_ctx {
	grime_input *in;
	const grime_device_match *devices;
	size_t ndevices;
	bool matched[MAX_DEVICES];
	int seen, unreadable;
};

static bool consider(const grime_probe *p, int fd, void *ud)
{
	struct open_ctx *c = ud;
	c->seen++;
	if (fd < 0) {
		LOG_DEBUG("skipping %s: %s", p->path, p->name);
		if (p->open_errno == EACCES)
			c->unreadable++;
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

/* ---- hotplug ---- */

/* Try one node against the matchers and keep it if it fits. Returns false if
 * the node isn't there (or isn't ours to read) yet, which is the normal state
 * of affairs between IN_CREATE and udev finishing with it. */
static bool try_new_node(grime_input *in, const char *path)
{
	/* IN_ATTRIB fires on nodes we already hold; adding one twice would grab
	 * it twice and read it twice. */
	for (int i = 0; i < in->nfds; i++)
		if (!strcmp(in->devs[i].path, path))
			return true;
	int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return errno != EACCES && errno != ENOENT;
	grime_probe p;
	if (grime_probe_fd(fd, path, &p) < 0 || p.is_ours) {
		close(fd);
		return true;
	}
	int i = grime_device_match_find(in->devices, in->ndevices, &p.info);
	if (i < 0) {
		LOG_DEBUG("new device %s (%s) matches nothing", path, p.name);
		close(fd);
		return true;
	}
	keep_device(in, &p, fd, &in->devices[i]);
	if (in->grab)
		grime_timer_arm(in->grab_timer, 0);
	return true;
}

/* IN_CREATE arrives before udev has chowned the node, so the first open is
 * usually EACCES. IN_ATTRIB covers the chmod, and this covers the rest. */
static void retry_pending(grime_loop *loop, void *ud)
{
	(void)loop;
	grime_input *in = ud;
	bool done = true;
	for (int i = 0; i < in->npending; i++)
		if (!try_new_node(in, in->pending[i]))
			done = false;
	if (done || ++in->retries >= 10) {
		for (int i = 0; !done && i < in->npending; i++)
			LOG_WARN("can't read %s: %s. Hotplug needs access as the user grime "
				 "runs as -- see scripts/setup-permissions.sh. (Under sudo, "
				 "grime drops to your user once the first devices are open, "
				 "so later ones need the input group.)",
				 in->pending[i], strerror(EACCES));
		in->npending = 0;
		in->retries = 0;
		return;
	}
	grime_timer_arm(in->retry_timer, 100);
}

static void queue_node(grime_input *in, const char *name)
{
	char path[PROBE_STR];
	snprintf(path, sizeof path, "/dev/input/%s", name);
	if (try_new_node(in, path))
		return;
	for (int i = 0; i < in->npending; i++)
		if (!strcmp(in->pending[i], path))
			return;
	if (in->npending == MAX_DEVICES)
		return;
	snprintf(in->pending[in->npending++], PROBE_STR, "%s", path);
	in->retries = 0;
	grime_timer_arm(in->retry_timer, 100);
}

static void on_inotify(grime_loop *loop, int fd, int io, void *ud)
{
	(void)loop;
	(void)io;
	grime_input *in = ud;
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n = read(fd, buf, sizeof buf);
	for (char *p = buf; n > 0 && p < buf + n;) {
		const struct inotify_event *e = (const struct inotify_event *)p;
		p += sizeof *e + e->len;
		if (!e->len || strncmp(e->name, "event", 5))
			continue;
		if (e->mask & (IN_CREATE | IN_ATTRIB)) {
			queue_node(in, e->name);
		} else if (e->mask & IN_DELETE) {
			char path[PROBE_STR];
			snprintf(path, sizeof path, "/dev/input/%s", e->name);
			for (int i = 0; i < in->nfds; i++)
				if (!strcmp(in->devs[i].path, path)) {
					LOG_INFO("%s (%s) went away", in->devs[i].path,
						 in->devs[i].name);
					drop_device(in, i);
					if (!in->nfds)
						LOG_WARN("no input devices left; waiting "
							 "for one to appear");
					break;
				}
		}
	}
}

/* A keyboard plugged in after grime started should just start working. */
static void watch_for_new_devices(grime_input *in)
{
	in->retry_timer = grime_timer_new(in->loop, retry_pending, in);
	in->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (in->inotify_fd < 0 ||
	    inotify_add_watch(in->inotify_fd, "/dev/input",
			      IN_CREATE | IN_ATTRIB | IN_DELETE) < 0) {
		LOG_WARN("no hotplug: %s (devices plugged in later won't be picked up)",
			 strerror(errno));
		if (in->inotify_fd >= 0)
			close(in->inotify_fd);
		in->inotify_fd = -1;
		return;
	}
	grime_loop_add_fd(in->loop, in->inotify_fd, GRIME_IO_READ, on_inotify, in);
}

grime_input *grime_input_open(grime_loop *loop, const grime_device_match *devices,
			      size_t ndevices, bool grab, const grime_input_sink *sink)
{
	grime_input *in = calloc(1, sizeof *in);
	*in = (grime_input){.loop = loop, .sink = *sink, .grab = grab, .inotify_fd = -1};
	in->devices = grime_device_match_dup(devices, ndevices);
	in->ndevices = ndevices;
	in->grab_timer = grime_timer_new(loop, try_grab, in);

	struct open_ctx ctx = {.in = in, .devices = devices, .ndevices = ndevices};
	grime_probe_each(consider, &ctx);

	/* Every node unreadable is a permissions problem, not a config one, and
	 * saying "nothing matched" would send someone off editing the wrong file. */
	if (ctx.unreadable && ctx.unreadable == ctx.seen) {
		LOG_ERR("can't read any of the %d input devices: permission denied. "
			"Run with sudo, or scripts/setup-permissions.sh once to join the "
			"input group.",
			ctx.seen);
		grime_timer_free(in->grab_timer);
		grime_device_match_free(in->devices, in->ndevices);
		free(in);
		return NULL;
	}
	if (ctx.unreadable)
		LOG_WARN("%d of %d input devices could not be read (permission denied)",
			 ctx.unreadable, ctx.seen);

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
		grime_timer_free(in->grab_timer);
		grime_device_match_free(in->devices, in->ndevices);
		free(in);
		return NULL;
	}
	if (grab) {
		LOG_INFO("waiting for all keys to be released...");
		grime_timer_arm(in->grab_timer, 0);
	}
	watch_for_new_devices(in);
	return in;
}

/* Writing EV_LED to a grabbed device is what actually moves its light. */
void grime_input_set_led(grime_input *in, uint16_t led, int on)
{
	struct input_event ev = {.type = EV_LED, .code = led, .value = on};
	for (int i = 0; i < in->nfds; i++) {
		if (!in->devs[i].grabbed)
			continue;
		ssize_t n = write(in->devs[i].fd, &ev, sizeof ev);
		(void)n; /* read-only fd, or a device with no such light: fine */
	}
}

bool grime_input_needs_pointer(const grime_input *in)
{
	for (int i = 0; i < in->nfds; i++)
		if (grime_device_needs_replay(&in->devs[i]))
			return true;
	return false;
}

void grime_input_close(grime_input *in)
{
	if (!in)
		return;
	while (in->nfds)
		drop_device(in, in->nfds - 1);
	if (in->inotify_fd >= 0) {
		grime_loop_del_fd(in->loop, in->inotify_fd);
		close(in->inotify_fd);
	}
	grime_timer_free(in->retry_timer);
	grime_timer_free(in->grab_timer);
	grime_device_match_free(in->devices, in->ndevices);
	free(in);
}
