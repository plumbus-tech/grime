/* Output through uinput: a virtual keyboard, and -- only once there is
 * actually a button or some motion to emit -- a virtual pointer beside it.
 *
 * Two devices, not one. systemd's input_id tags ID_INPUT_MOUSE on a device
 * carrying EV_REL + REL_X/REL_Y + BTN_LEFT and ID_INPUT_KEYBOARD on a
 * keyboard-shaped EV_KEY set; a device with both is legal but means grime's
 * *typing* device joins a libinput device group, picks up pointer
 * acceleration settings and takes part in disable-while-typing heuristics.
 * Splitting them costs one fd and keeps emit()'s signature untouched. */
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grime/event.h"
#include "grime/keynames.h"
#include "grime/log.h"
#include "grime/output.h"

/* What the virtual pointer carries. Deliberately narrow: BTN_0..BTN_9 would
 * make it look like a tablet pad and the joystick/gamepad ranges can win the
 * classification outright, either of which loses us the mouse tag. */
#define PTR_BTN_FIRST BTN_LEFT
#define PTR_BTN_LAST BTN_TASK

struct uinput {
	grime_output base;
	int fd;   /* keyboard */
	int pfd;  /* pointer, -1 if /dev/uinput wouldn't open */
	bool plive; /* UI_DEV_CREATE done */
};

static void write_event(int fd, int type, int code, int value)
{
	struct input_event ev = {.type = type, .code = code, .value = value};
	if (write(fd, &ev, sizeof ev) != sizeof ev)
		LOG_WARN("uinput write: %s", strerror(errno));
}

/* grime_is_button() spells these ranges as plain numbers; assert here, where
 * the kernel's names are in scope, that the two agree. */
_Static_assert(BTN_MISC == 0x100 && KEY_OK == 0x160 && BTN_DPAD_UP == 0x220 &&
		       BTN_GRIPR2 == 0x227 && BTN_TRIGGER_HAPPY == 0x2c0 && KEY_MAX == 0x2ff,
	       "grime_is_button() in grime/event.h is out of step with the kernel");

/* The pointer only exists once something needs it, so a keyboard-only config
 * never puts a phantom mouse in the desktop's settings panel. The fd and its
 * UI_SET_* ioctls happen at startup while we are still root; only the create
 * is deferred, so dropping privileges first cannot break this. */
static int pointer_fd(struct uinput *u)
{
	if (u->pfd < 0 || u->plive)
		return u->pfd;
	struct uinput_setup setup = {0};
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x6772; /* "gr" */
	setup.id.product = 0x7074; /* "pt" */
	strncpy(setup.name, GRIME_UINPUT_POINTER_NAME, UINPUT_MAX_NAME_SIZE - 1);
	if (ioctl(u->pfd, UI_DEV_SETUP, &setup) < 0 || ioctl(u->pfd, UI_DEV_CREATE) < 0) {
		LOG_ERR("create %s: %s -- buttons and motion will be dropped",
			GRIME_UINPUT_POINTER_NAME, strerror(errno));
		close(u->pfd);
		u->pfd = -1;
		return -1;
	}
	u->plive = true;
	LOG_INFO("created %s", GRIME_UINPUT_POINTER_NAME);
	return u->pfd;
}

/* Buttons outside what the pointer advertises are swallowed by the kernel, so
 * say it once rather than looking like the binding silently did nothing. */
static void warn_unemittable(uint16_t code)
{
	static uint8_t warned[GRIME_KEY_COUNT / 8];
	if (code >= GRIME_KEY_COUNT || warned[code / 8] & (1 << (code % 8)))
		return;
	warned[code / 8] |= 1 << (code % 8);
	LOG_WARN("%s can't be emitted: grime's virtual pointer carries mouse buttons "
		 "(btn_left..btn_task) only",
		 grime_key_name(code));
}

static void emit(grime_output *out, uint16_t code, int value)
{
	struct uinput *u = (struct uinput *)out;
	int fd = u->fd;
	if (grime_is_button(code)) {
		if (code < PTR_BTN_FIRST || code > PTR_BTN_LAST) {
			warn_unemittable(code);
			return;
		}
		if ((fd = pointer_fd(u)) < 0)
			return;
	}
	write_event(fd, EV_KEY, code, value);
	write_event(fd, EV_SYN, SYN_REPORT, 0);
}

/* One event of a grabbed device's own stream, replayed verbatim -- including
 * its SYN_REPORT, which is why this never syncs on its own. */
static void emit_ev(grime_output *out, uint16_t type, uint16_t code, int32_t value)
{
	struct uinput *u = (struct uinput *)out;
	if (type != EV_REL && type != EV_SYN)
		return;
	int fd = pointer_fd(u);
	if (fd < 0)
		return;
	write_event(fd, type, code, value);
}

static void prepare(grime_output *out)
{
	pointer_fd((struct uinput *)out);
}

/* uinput hands LED changes back on the same fd we write events to. */
int grime_output_event_fd(grime_output *out)
{
	return out && out->emit == emit ? ((struct uinput *)out)->fd : -1;
}

void grime_output_pump(grime_output *out, grime_led_cb cb, void *ud)
{
	if (grime_output_event_fd(out) < 0)
		return;
	struct uinput *u = (struct uinput *)out;
	struct input_event evs[16];
	ssize_t n = read(u->fd, evs, sizeof evs);
	for (size_t i = 0; n > 0 && i < n / sizeof evs[0]; i++)
		if (evs[i].type == EV_LED)
			cb(evs[i].code, evs[i].value, ud);
}

static void destroy(grime_output *out)
{
	struct uinput *u = (struct uinput *)out;
	if (u->plive)
		ioctl(u->pfd, UI_DEV_DESTROY);
	if (u->pfd >= 0)
		close(u->pfd);
	ioctl(u->fd, UI_DEV_DESTROY);
	close(u->fd);
	free(u);
}

static int open_uinput(void)
{
	int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		LOG_ERR("open /dev/uinput: %s", strerror(errno));
	return fd;
}

/* Everything the pointer will ever need, declared up front: the create is
 * deferred but the capability set cannot be changed afterwards. */
static void setup_pointer_bits(int fd)
{
	static const int rel[] = {REL_X,          REL_Y,          REL_WHEEL,
				  REL_HWHEEL,     REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES};
	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_REL);
	for (int code = PTR_BTN_FIRST; code <= PTR_BTN_LAST; code++)
		ioctl(fd, UI_SET_KEYBIT, code);
	for (size_t i = 0; i < sizeof rel / sizeof rel[0]; i++)
		ioctl(fd, UI_SET_RELBIT, rel[i]);
	ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
}

grime_output *grime_output_uinput_new(void)
{
	int fd = open_uinput();
	if (fd < 0)
		return NULL;
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	/* so held keys autorepeat on the Linux console, where no compositor does it */
	ioctl(fd, UI_SET_EVBIT, EV_REP);
	/* so the OS can tell us when caps/num/scroll lock changed */
	ioctl(fd, UI_SET_EVBIT, EV_LED);
	ioctl(fd, UI_SET_LEDBIT, LED_NUML);
	ioctl(fd, UI_SET_LEDBIT, LED_CAPSL);
	ioctl(fd, UI_SET_LEDBIT, LED_SCROLLL);
	for (int code = 1; code < KEY_MAX; code++)
		if (!grime_is_button(code))
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
	u->base = (grime_output){.emit = emit, .destroy = destroy, .impl = u,
				 .emit_ev = emit_ev, .prepare = prepare};
	u->fd = fd;
	u->pfd = open_uinput();
	if (u->pfd >= 0)
		setup_pointer_bits(u->pfd);
	return &u->base;
}

/* --dry-run: just say what would have been typed */
static void log_emit(grime_output *out, uint16_t code, int value)
{
	(void)out;
	static const char *what[] = {"release", "press", "repeat"};
	LOG_INFO("emit %s %s", grime_key_name(code), what[value < 0 || value > 2 ? 0 : value]);
}

static void log_emit_ev(grime_output *out, uint16_t type, uint16_t code, int32_t value)
{
	(void)out;
	if (type == EV_REL)
		LOG_INFO("forward rel %s %+d", grime_rel_name(code), value);
}

static void log_destroy(grime_output *out)
{
	free(out);
}

grime_output *grime_output_log_new(void)
{
	grime_output *out = calloc(1, sizeof *out);
	*out = (grime_output){.emit = log_emit, .destroy = log_destroy, .emit_ev = log_emit_ev};
	return out;
}
