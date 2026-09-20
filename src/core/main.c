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
	bool esc_down, backspace_down;
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
		"      --list-keys     print every key name\n"
		"      --list-actions  print every action name\n"
		"  -v, --verbose       log every step of the keymap walk\n"
		"  -h, --help\n"
		"Emergency exit: hold Esc + Backspace for 1 second.\n");
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
	grime_engine_set_keymap(app.engine, cfg.keymap);
	cfg.keymap = NULL;
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
	LOG_WARN("emergency exit (Esc + Backspace held)");
	grime_loop_stop(loop);
}

static void on_timeout(grime_loop *loop, void *ud)
{
	(void)ud;
	LOG_INFO("--timeout reached, exiting");
	grime_loop_stop(loop);
}

/* Physical events arrive here first. The emergency chord bypasses the keymap. */
static void on_key(const grime_key_event *ev, void *ud)
{
	(void)ud;
	if (ev->edge != GRIME_REPEAT) {
		bool down = ev->edge == GRIME_PRESS;
		if (ev->code == 1 /* esc */)
			app.esc_down = down;
		if (ev->code == 14 /* backspace */)
			app.backspace_down = down;
		if (app.esc_down && app.backspace_down)
			grime_timer_arm(app.emergency_timer, EMERGENCY_HOLD_MS);
		else
			grime_timer_disarm(app.emergency_timer);
	}
	grime_engine_feed(app.engine, ev);
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

int main(int argc, char **argv)
{
	enum { OPT_CHECK = 256, OPT_LIST_KEYS, OPT_LIST_ACTIONS, OPT_EXPAND };
	static const struct option opts[] = {
		{"config", required_argument, 0, 'c'},     {"timeout", required_argument, 0, 't'},
		{"dry-run", no_argument, 0, 'n'},          {"verbose", no_argument, 0, 'v'},
		{"help", no_argument, 0, 'h'},             {"check", no_argument, 0, OPT_CHECK},
		{"list-keys", no_argument, 0, OPT_LIST_KEYS}, {"list-actions", no_argument, 0, OPT_LIST_ACTIONS},
		{"expand", no_argument, 0, OPT_EXPAND},
		{0},
	};
	bool dry_run = false, check = false, expand = false;
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
		case OPT_LIST_KEYS: grime_key_list(stdout); return 0;
		case OPT_LIST_ACTIONS: grime_action_list(print_action, NULL); return 0;
		case 'h': usage(stdout); return 0;
		default: usage(stderr); return 2;
		}
	}
	if (!app.config_path)
		app.config_path = default_config_path();

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
		LOG_ERR("config: %s", err);
		return 1;
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
	app.reload_timer = grime_timer_new(app.loop, do_reload, NULL);
	app.emergency_timer = grime_timer_new(app.loop, on_emergency, NULL);
	grime_loop_on_sighup(app.loop, on_sighup, NULL);

	grime_input *in = grime_input_open(app.loop, cfg.devices, cfg.ndevices, !dry_run, on_key, NULL);
	grime_config_free(&cfg);
	if (!in) {
		out->destroy(out);
		return 1;
	}
	drop_privileges();

	grime_timer *deadline = NULL;
	if (timeout_s > 0) {
		deadline = grime_timer_new(app.loop, on_timeout, NULL);
		grime_timer_arm(deadline, (uint64_t)timeout_s * 1000);
		LOG_INFO("will exit after %d s", timeout_s);
	}
	LOG_INFO("emergency exit: hold Esc + Backspace for 1 s");

	grime_loop_run(app.loop);

	grime_release_all(&app.rt);
	grime_input_close(in);
	grime_timer_free(deadline);
	grime_timer_free(app.reload_timer);
	grime_timer_free(app.emergency_timer);
	grime_engine_free(app.engine);
	out->destroy(out);
	grime_loop_free(app.loop);
	return 0;
}
