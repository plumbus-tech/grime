/* Output through a uinput virtual keyboard. */
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grime/keynames.h"
#include "grime/log.h"
#include "grime/output.h"

struct uinput {
	grime_output base;
	int fd;
};

static void write_event(int fd, int type, int code, int value)
{
	struct input_event ev = {.type = type, .code = code, .value = value};
	if (write(fd, &ev, sizeof ev) != sizeof ev)
		LOG_WARN("uinput write: %s", strerror(errno));
}

static void emit(grime_output *out, uint16_t code, int value)
{
	struct uinput *u = (struct uinput *)out;
	write_event(u->fd, EV_KEY, code, value);
	write_event(u->fd, EV_SYN, SYN_REPORT, 0);
}

static void destroy(grime_output *out)
{
	struct uinput *u = (struct uinput *)out;
	ioctl(u->fd, UI_DEV_DESTROY);
	close(u->fd);
	free(u);
}

static bool is_button(int code)
{
	/* advertising mouse/joystick buttons makes desktops treat us as a pointer */
	return (code >= BTN_MISC && code < KEY_OK) || (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT) ||
	       (code >= BTN_TRIGGER_HAPPY && code < KEY_MAX);
}

grime_output *grime_output_uinput_new(void)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		LOG_ERR("open /dev/uinput: %s", strerror(errno));
		return NULL;
	}
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	for (int code = 1; code < KEY_MAX; code++)
		if (!is_button(code))
			ioctl(fd, UI_SET_KEYBIT, code);

	struct uinput_setup setup = {0};
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x6772; /* "gr" */
	setup.id.product = 0x696d;
	strncpy(setup.name, GRIME_UINPUT_NAME, UINPUT_MAX_NAME_SIZE - 1);
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		LOG_ERR("create uinput device: %s", strerror(errno));
		close(fd);
		return NULL;
	}
	struct uinput *u = calloc(1, sizeof *u);
	u->base = (grime_output){emit, destroy, u};
	u->fd = fd;
	return &u->base;
}

/* --dry-run: just say what would have been typed */
static void log_emit(grime_output *out, uint16_t code, int value)
{
	(void)out;
	static const char *what[] = {"release", "press", "repeat"};
	LOG_INFO("emit %s %s", grime_key_name(code), what[value < 0 || value > 2 ? 0 : value]);
}

static void log_destroy(grime_output *out)
{
	free(out);
}

grime_output *grime_output_log_new(void)
{
	grime_output *out = calloc(1, sizeof *out);
	*out = (grime_output){log_emit, log_destroy, NULL};
	return out;
}
