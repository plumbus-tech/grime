/* Read (and exclusively grab) keyboards from /dev/input/event*. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "grime/input.h"
#include "grime/log.h"
#include "grime/output.h"

#define MAX_DEVICES 64
#define NBITS(x) (((x) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
#define TEST_BIT(bit, arr) ((arr)[(bit) / (8 * sizeof(long))] >> ((bit) % (8 * sizeof(long))) & 1)

struct grime_device {
	int fd;
	char path[300];
	char name[256];
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

static bool looks_like_keyboard(int fd)
{
	unsigned long bits[NBITS(KEY_MAX)] = {0};
	char name[256] = "";
	ioctl(fd, EVIOCGNAME(sizeof name), name);
	if (!strcmp(name, GRIME_UINPUT_NAME))
		return false; /* never read our own output */
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0)
		return false;
	for (int k = KEY_Q; k <= KEY_P; k++)
		if (!TEST_BIT(k, bits))
			return false;
	return TEST_BIT(KEY_A, bits) && TEST_BIT(KEY_Z, bits) && TEST_BIT(KEY_SPACE, bits);
}

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
	if (in->grabbed)
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
		if (any_key_down(in->devs[i].fd)) {
			grime_timer_arm(in->grab_timer, 20);
			return;
		}
	}
	for (int i = 0; i < in->nfds; i++)
		if (ioctl(in->devs[i].fd, EVIOCGRAB, 1) < 0)
			LOG_WARN("grab %s: %s", in->devs[i].path, strerror(errno));
	in->grabbed = true;
	LOG_INFO("keyboard grabbed; grime is live");
}

static void add_device(grime_input *in, const char *path, bool must_be_keyboard)
{
	if (in->nfds == MAX_DEVICES) {
		LOG_WARN("more than %d devices, ignoring %s", MAX_DEVICES, path);
		return;
	}
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		if (!must_be_keyboard)
			LOG_ERR("open %s: %s", path, strerror(errno));
		else if (errno == EACCES)
			LOG_WARN("open %s: permission denied (run with sudo or see scripts/setup-permissions.sh)", path);
		return;
	}
	if (must_be_keyboard && !looks_like_keyboard(fd)) {
		close(fd);
		return;
	}
	grime_device *dev = &in->devs[in->nfds++];
	*dev = (grime_device){.fd = fd};
	snprintf(dev->path, sizeof dev->path, "%s", path);
	snprintf(dev->name, sizeof dev->name, "?");
	ioctl(fd, EVIOCGNAME(sizeof dev->name), dev->name);
	LOG_INFO("using %s (%s)", dev->path, dev->name);
	grime_loop_add_fd(in->loop, fd, GRIME_IO_READ, on_readable, in);
}

grime_input *grime_input_open(grime_loop *loop, const grime_device_match *devices,
			      size_t ndevices, bool grab, const grime_input_sink *sink)
{
	grime_input *in = calloc(1, sizeof *in);
	*in = (grime_input){.loop = loop, .sink = *sink, .grab = grab};

	for (size_t i = 0; i < ndevices; i++) {
		if (devices[i].path) {
			add_device(in, devices[i].path, false);
			continue;
		}
		DIR *dir = opendir("/dev/input");
		struct dirent *de;
		while (dir && (de = readdir(dir))) {
			if (strncmp(de->d_name, "event", 5))
				continue;
			char path[300];
			snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
			add_device(in, path, true);
		}
		if (dir)
			closedir(dir);
	}
	if (!in->nfds) {
		LOG_ERR("no keyboards found");
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
