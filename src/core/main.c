/* grime: wire config + loop + input + engine + output together, and keep the
 * user from being locked out of their own keyboard. */
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grime/action.h"
#include "grime/config.h"
#include "grime/engine.h"
#include "grime/input.h"
#include "grime/keynames.h"
#include "grime/log.h"
#include "grime/loop.h"
#include "grime/output.h"

#define EMERGENCY_HOLD_MS 1000

static struct {
	const char *config_path;
	grime_loop *loop;
	grime_engine *engine;
	grime_runtime rt;
	grime_timer *reload_timer;
	grime_timer *emergency_timer;
	uint8_t down[GRIME_KEY_COUNT]; /* physically held, before the keymap sees it */
	uint16_t emergency[GRIME_EMERGENCY_MAX];
	size_t nemergency;
	/* a release is passed through only if its press was, so pairs stay paired
	 * even when the keymap changed in between */
	uint8_t passed[GRIME_KEY_COUNT];
} app;

static void usage(FILE *f)
{
	fprintf(f,
		"usage: grime [options]\n"
		"  -c, --config PATH   config file (default: $XDG_CONFIG_HOME/grime/config.json)\n"
		"  -t, --timeout SEC   exit automatically after SEC seconds (safety net while testing)\n"
		"  -n, --dry-run       don't grab or emit; log what would happen\n"
		"      --check         validate the config and exit\n"
		"      --expand        print the config as the tree grime walks\n"
		"      --list-devices  print every input device and what your config would do\n"
		"      --watch         print events from every device, without grabbing\n"
		"      --list-keys     print every key name\n"
		"      --list-actions  print every action name\n"
		"  -v, --verbose       log every step of the keymap walk\n"
		"  -h, --help\n"
		"Emergency exit: hold Esc + Backspace, or all three mouse buttons, for 1 second.\n");
}

static void do_reload(grime_loop *loop, void *ud)
{
	(void)loop;
	(void)ud;
	grime_config cfg;
	char err[512];
	if (grime_config_load(app.config_path, &cfg, err, sizeof err) < 0) {
		LOG_ERR("reload failed, keeping old config: %s", err);
		return;
	}
	grime_release_all(&app.rt);
	/* release_all already sent a release for everything held, passed-through
	 * keys included, so the next physical release must not send another */
	memset(app.passed, 0, sizeof app.passed);
	grime_engine_set_keymap(app.engine, cfg.keymap);
	cfg.keymap = NULL;
	memcpy(app.emergency, cfg.emergency, sizeof app.emergency);
	app.nemergency = cfg.nemergency;
	grime_config_free(&cfg);
	LOG_INFO("config reloaded");
}

static void rt_reload(grime_runtime *rt)
{
	(void)rt;
	grime_timer_arm(app.reload_timer, 0); /* not mid-walk: the old tree is still in use */
}

static void rt_quit(grime_runtime *rt)
{
	(void)rt;
	LOG_INFO("quit");
	grime_loop_stop(app.loop);
}

static void on_sighup(grime_loop *loop, int sig, void *ud)
{
	(void)loop;
	(void)sig;
	(void)ud;
	rt_reload(&app.rt);
}

static void on_emergency(grime_loop *loop, void *ud)
{
	(void)ud;
	LOG_WARN("emergency exit (Esc + Backspace, or all three mouse buttons, held)");
	grime_loop_stop(loop);
}

static void on_timeout(grime_loop *loop, void *ud)
{
	(void)ud;
	LOG_INFO("--timeout reached, exiting");
	grime_loop_stop(loop);
}

/* Two ways out, because a setup where grime owns the pointer and not much else
 * still has to be escapable with a pointer. Both are checked before the keymap,
 * so no binding can shadow them. */
static bool all_down(const uint16_t *codes, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (!app.down[codes[i]])
			return false;
	return n > 0;
}

static bool emergency_held(void)
{
	static const uint16_t buttons[] = {0x110, 0x111, 0x112}; /* left, right, middle */
	if (!app.nemergency)
		return false; /* turned off in the config, buttons and all */
	return all_down(app.emergency, app.nemergency) ||
	       all_down(buttons, sizeof buttons / sizeof buttons[0]);
}

/* Physical events arrive here first. The emergency chords bypass the keymap. */
static void on_key(const grime_key_event *ev, grime_device *dev, void *ud)
{
	(void)ud;
	if (ev->edge != GRIME_REPEAT && ev->code < GRIME_KEY_COUNT) {
		app.down[ev->code] = ev->edge == GRIME_PRESS;
		if (emergency_held())
			grime_timer_arm(app.emergency_timer, EMERGENCY_HOLD_MS);
		else
			grime_timer_disarm(app.emergency_timer);
	}
	if (grime_engine_feed(app.engine, ev) || ev->edge == GRIME_REPEAT)
		return;

	/* Nothing in the keymap wanted it. On a grabbed pointer that would mean a
	 * button that no longer clicks, so its matcher can ask for the event to go
	 * out unchanged instead of being swallowed. */
	if (ev->code >= GRIME_KEY_COUNT || !dev ||
	    grime_device_unmatched(dev) != GRIME_UNMATCHED_PASS)
		return;
	if (ev->edge == GRIME_PRESS)
		app.passed[ev->code] = 1;
	else if (!app.passed[ev->code])
		return;
	else
		app.passed[ev->code] = 0;
	grime_emit(&app.rt, ev->code, ev->edge == GRIME_PRESS ? 1 : 0);
}

/* The OS toggles caps lock by lighting the virtual keyboard; the light the user
 * is actually looking at is on the physical one. */
static void on_led(uint16_t led, int on, void *ud)
{
	grime_input_set_led(ud, led, on);
}

static void on_output_readable(grime_loop *loop, int fd, int io, void *ud)
{
	(void)loop;
	(void)fd;
	(void)io;
	grime_output_pump(app.rt.out, on_led, ud);
}

/* A grabbed device's motion and wheel reach nobody unless grime replays them.
 * A watched one is still being read by the OS, so replaying would double it. */
static void on_raw(uint16_t type, uint16_t code, int32_t value, grime_device *dev, void *ud)
{
	(void)ud;
	if (grime_device_needs_replay(dev))
		grime_output_ev(app.rt.out, type, code, value);
}

/* Under sudo, give up root once the devices are open so "exec" actions run as you. */
static void drop_privileges(void)
{
	const char *uid_s = getenv("SUDO_UID"), *gid_s = getenv("SUDO_GID");
	if (getuid() != 0 || !uid_s || !gid_s)
		return;
	uid_t uid = atoi(uid_s);
	gid_t gid = atoi(gid_s);
	struct passwd *pw = getpwuid(uid);
	if (pw) {
		initgroups(pw->pw_name, gid);
		setenv("HOME", pw->pw_dir, 1);
		setenv("USER", pw->pw_name, 1);
		setenv("LOGNAME", pw->pw_name, 1);
	}
	if (setgid(gid) < 0 || setuid(uid) < 0) {
		LOG_ERR("failed to drop privileges, exiting");
		exit(1);
	}
	LOG_INFO("dropped root, running as %s", pw ? pw->pw_name : uid_s);
}

static char *default_config_path(void)
{
	static char buf[4096];
	const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
	if (xdg && *xdg)
		snprintf(buf, sizeof buf, "%s/grime/config.json", xdg);
	else
		snprintf(buf, sizeof buf, "%s/.config/grime/config.json", home ? home : ".");
	return buf;
}

static void print_action(const char *name, void *ud)
{
	(void)ud;
	printf("%s\n", name);
}

/* --watch: what is this key, and which device sends it? Opens everything
 * readable and grabs none of it, because the key you are hunting for is by
 * definition not in your config yet. */
static void watch_key(const grime_key_event *ev, grime_device *dev, void *ud)
{
	static const char *const edge[] = {"release", "press", "repeat"};
	printf("%-18s %-30s key  %-16s %s (%u)\n", grime_device_path(dev),
	       grime_device_name(dev), grime_key_name(ev->code), edge[ev->edge], ev->code);
	fflush(stdout);
}

static void watch_raw(uint16_t type, uint16_t code, int32_t value, grime_device *dev, void *ud)
{
	if (type == GRIME_EV_SYN)
		return;
	printf("%-18s %-30s %-4s %-16s %+d\n", grime_device_path(dev), grime_device_name(dev),
	       type == GRIME_EV_REL ? "rel" : type == GRIME_EV_ABS ? "abs" : "ev",
	       type == GRIME_EV_REL ? grime_rel_name(code) : "?", value);
	fflush(stdout);
}

static int run_watch(int timeout_s)
{
	app.loop = grime_loop_new();
	if (!app.loop)
		return 1;
	grime_device_match all = {.vendor = -1, .product = -1, .bus = -1,
				  .kind = GRIME_KIND_ANY, .grab = false};
	grime_input_sink sink = {.on_key = watch_key, .on_raw = watch_raw};
	printf("watching every readable device, grabbing none -- ctrl-c to stop\n");
	fflush(stdout);
	/* grab=false twice over: the matcher says so and so does the global. */
	grime_input *in = grime_input_open(app.loop, &all, 1, false, &sink);
	if (!in)
		return 1;
	drop_privileges();
	grime_timer *deadline = NULL;
	if (timeout_s > 0) {
		deadline = grime_timer_new(app.loop, on_timeout, NULL);
		grime_timer_arm(deadline, (uint64_t)timeout_s * 1000);
	}
	grime_loop_run(app.loop);
	grime_timer_free(deadline);
	grime_input_close(in);
	grime_loop_free(app.loop);
	return 0;
}

int main(int argc, char **argv)
{
	enum { OPT_CHECK = 256, OPT_LIST_KEYS, OPT_LIST_ACTIONS, OPT_EXPAND, OPT_LIST_DEVICES, OPT_WATCH };
	static const struct option opts[] = {
		{"config", required_argument, 0, 'c'},     {"timeout", required_argument, 0, 't'},
		{"dry-run", no_argument, 0, 'n'},          {"verbose", no_argument, 0, 'v'},
		{"help", no_argument, 0, 'h'},             {"check", no_argument, 0, OPT_CHECK},
		{"list-keys", no_argument, 0, OPT_LIST_KEYS}, {"list-actions", no_argument, 0, OPT_LIST_ACTIONS},
		{"expand", no_argument, 0, OPT_EXPAND},
		{"list-devices", no_argument, 0, OPT_LIST_DEVICES},
		{"watch", no_argument, 0, OPT_WATCH},
		{0},
	};
	bool dry_run = false, check = false, expand = false, list_devices = false, watch = false;
	int timeout_s = 0, c;
	app.config_path = NULL;
	while ((c = getopt_long(argc, argv, "c:t:nvh", opts, NULL)) != -1) {
		switch (c) {
		case 'c': app.config_path = optarg; break;
		case 't': timeout_s = atoi(optarg); break;
		case 'n': dry_run = true; break;
		case 'v': grime_log_level = GRIME_LOG_DEBUG; break;
		case OPT_CHECK: check = true; break;
		case OPT_EXPAND: expand = true; break;
		case OPT_LIST_DEVICES: list_devices = true; break;
		case OPT_WATCH: watch = true; break;
		case OPT_LIST_KEYS: grime_key_list(stdout); return 0;
		case OPT_LIST_ACTIONS: grime_action_list(print_action, NULL); return 0;
		case 'h': usage(stdout); return 0;
		default: usage(stderr); return 2;
		}
	}
	if (!app.config_path)
		app.config_path = default_config_path();

	if (watch)
		return run_watch(timeout_s);

	char err[512];
	if (expand) {
		char *text = grime_config_expand(app.config_path, err, sizeof err);
		if (!text) {
			LOG_ERR("config: %s", err);
			return 1;
		}
		puts(text);
		free(text);
		return 0;
	}

	grime_config cfg;
	if (grime_config_load(app.config_path, &cfg, err, sizeof err) < 0) {
		if (!list_devices) {
			LOG_ERR("config: %s", err);
			return 1;
		}
		/* Still worth listing: "which device is my mic-mute key on?" is
		 * exactly the question you ask before the config is right. */
		LOG_WARN("config: %s -- listing devices with no matchers", err);
		grime_input_list(stdout, NULL, 0);
		return 0;
	}
	if (list_devices) {
		grime_input_list(stdout, cfg.devices, cfg.ndevices);
		grime_config_free(&cfg);
		return 0;
	}
	if (check) {
		printf("%s: ok\n", app.config_path);
		grime_config_free(&cfg);
		return 0;
	}

	app.loop = grime_loop_new();
	if (!app.loop)
		return 1;
	grime_output *out = dry_run ? grime_output_log_new() : grime_output_uinput_new();
	if (!out)
		return 1;
	app.rt = (grime_runtime){.loop = app.loop, .out = out, .dry_run = dry_run,
				 .quit = rt_quit, .reload = rt_reload};
	app.engine = grime_engine_new(&app.rt, cfg.keymap);
	cfg.keymap = NULL;
	memcpy(app.emergency, cfg.emergency, sizeof app.emergency);
	app.nemergency = cfg.nemergency;
	app.reload_timer = grime_timer_new(app.loop, do_reload, NULL);
	app.emergency_timer = grime_timer_new(app.loop, on_emergency, NULL);
	grime_loop_on_sighup(app.loop, on_sighup, NULL);

	grime_input_sink sink = {.on_key = on_key, .on_raw = on_raw};
	grime_input *in = grime_input_open(app.loop, cfg.devices, cfg.ndevices, !dry_run, &sink);
	bool wants_pointer = cfg.wants_pointer;
	grime_config_free(&cfg);
	if (!in) {
		out->destroy(out);
		return 1;
	}
	/* Do it now, while nobody is clicking. A virtual pointer brought up in the
	 * middle of the first click emits that click before the desktop has opened
	 * the node, and the desktop never sees it. */
	if (wants_pointer || grime_input_needs_pointer(in))
		grime_output_prepare(out);
	int out_fd = grime_output_event_fd(out);
	if (out_fd >= 0)
		grime_loop_add_fd(app.loop, out_fd, GRIME_IO_READ, on_output_readable, in);
	drop_privileges();

	grime_timer *deadline = NULL;
	if (timeout_s > 0) {
		deadline = grime_timer_new(app.loop, on_timeout, NULL);
		grime_timer_arm(deadline, (uint64_t)timeout_s * 1000);
		LOG_INFO("will exit after %d s", timeout_s);
	}
	if (app.nemergency) {
		char chord[128] = "";
		for (size_t i = 0; i < app.nemergency; i++)
			snprintf(chord + strlen(chord), sizeof chord - strlen(chord), "%s%s",
				 i ? " + " : "", grime_key_name(app.emergency[i]));
		LOG_INFO("emergency exit: hold %s, or all three mouse buttons, for 1 s", chord);
	} else {
		LOG_WARN("emergency exit is turned off in your config -- --timeout and "
			 "killing grime from elsewhere are what you have left");
	}

	grime_loop_run(app.loop);

	grime_release_all(&app.rt);
	if (out_fd >= 0)
		grime_loop_del_fd(app.loop, out_fd);
	grime_input_close(in);
	grime_timer_free(deadline);
	grime_timer_free(app.reload_timer);
	grime_timer_free(app.emergency_timer);
	grime_engine_free(app.engine);
	out->destroy(out);
	grime_loop_free(app.loop);
	return 0;
}
