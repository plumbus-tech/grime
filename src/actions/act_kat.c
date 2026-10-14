/* kat: point and click by codes laid over the screen (libkat).
 *   {"do": "kat"}                                   open the grid, left click
 *   {"do": "kat", "mode": "right_click"}            or grab_release, a drag
 *   {"do": "kat", "settings": "/path/kat.json"}     colours, glyph size, mode keys
 * The key that runs it is the grid's trigger: pressed again it closes the grid.
 * While the grid is up, or a drag waits for its drop, kat has the keyboard.
 *
 * libkat sleeps while it glides and clicks, so it runs on a thread of its own.
 * Key events go to it over a pipe and what it does not take comes back the
 * same way, in order; the loop only ever reads and writes. */
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <kat.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grime/action.h"
#include "grime/log.h"
#include "spec.h"

#define KEY_BITS (GRIME_KEY_COUNT / 8)

enum { TO_SHOW, TO_KEY, TO_NOTE, TO_QUIT };
enum { FROM_KEY, FROM_END };

struct to_worker {
	uint8_t type;
	uint8_t value;
	uint16_t code;
	uint8_t down[KEY_BITS]; /* TO_SHOW: what is held as the grid opens */
};

struct from_worker {
	uint8_t type;
	uint8_t value;
	uint16_t code;
};

struct kat_state {
	kat_config cfg;
	kat_mode mode;
	bool has_trigger;
	grime_runtime *rt;
	int to[2], from[2];
	pthread_t thread;
	bool running, capturing;
	/* the worker's own */
	kat *k;
	bool active;
	uint8_t told[GRIME_KEY_COUNT]; /* what kat was last told is down */
};

static bool io_full(int fd, void *buf, size_t n, bool writing)
{
	char *p = buf;
	while (n) {
		ssize_t r = writing ? write(fd, p, n) : read(fd, p, n);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		p += r;
		n -= r;
	}
	return true;
}

static void reply(struct kat_state *s, uint8_t type, uint16_t code, uint8_t value)
{
	struct from_worker m = {.type = type, .value = value, .code = code};
	io_full(s->from[1], &m, sizeof m, true);
}

/* kat keeps its own count of what is held, for a mode key held at the
 * trigger; it only hears keys while it has the keyboard, so it is brought up
 * to date as the grid opens. The trigger is left out: told it went down, kat
 * would open the grid itself. */
static void sync_down(struct kat_state *s, const uint8_t *down)
{
	for (int code = 1; code < GRIME_KEY_COUNT; code++) {
		bool want = (down[code / 8] >> (code % 8)) & 1 && code != s->cfg.trigger;
		if (want != s->told[code]) {
			kat_key(s->k, code, want);
			s->told[code] = want;
		}
	}
}

static void worker_show(struct kat_state *s, const struct to_worker *m)
{
	if (!s->k && !(s->k = kat_open(&s->cfg))) {
		LOG_ERR("kat: %s", kat_why());
		reply(s, FROM_END, 0, 0);
		return;
	}
	sync_down(s, m->down);
	if (!kat_show(s->k, s->mode)) {
		LOG_ERR("kat: %s", kat_why());
		reply(s, FROM_END, 0, 0);
		return;
	}
	s->active = true;
}

static void worker_key(struct kat_state *s, const struct to_worker *m)
{
	if (!s->active) { /* sent before grime heard the pick end */
		reply(s, FROM_KEY, m->code, m->value);
		return;
	}
	if (m->value != 2)
		s->told[m->code] = m->value;
	kat_result r = kat_key(s->k, m->code, m->value);
	if (r == KAT_IDLE)
		reply(s, FROM_KEY, m->code, m->value);
	else if (r == KAT_ERROR)
		LOG_ERR("kat: %s", kat_why());
	if (!kat_shown(s->k) && (r == KAT_DONE || r == KAT_CANCELLED || r == KAT_ERROR)) {
		s->active = false;
		reply(s, FROM_END, 0, 0);
	}
}

static void *worker(void *ud)
{
	struct kat_state *s = ud;
	struct to_worker m;
	while (io_full(s->to[0], &m, sizeof m, false)) {
		switch (m.type) {
		case TO_SHOW:
			worker_show(s, &m);
			break;
		case TO_KEY:
			worker_key(s, &m);
			break;
		case TO_NOTE: /* a release the keymap has; kat only counts it */
			if (s->active) {
				kat_key(s->k, m.code, 0);
				s->told[m.code] = 0;
			}
			break;
		case TO_QUIT:
			goto out;
		}
	}
out:
	if (s->k) {
		kat_cancel(s->k);
		kat_close(s->k);
	}
	return NULL;
}

static void send_worker(struct kat_state *s, struct to_worker *m)
{
	if (!io_full(s->to[1], m, sizeof *m, true))
		LOG_ERR("kat: worker gone: %s", strerror(errno));
}

static void on_capture(grime_runtime *rt, const grime_key_event *ev, bool keymap_has_it, void *ud)
{
	struct to_worker m = {.type = keymap_has_it ? TO_NOTE : TO_KEY,
			      .value = ev->edge,
			      .code = ev->code};
	send_worker(ud, &m);
}

static void on_reply(grime_loop *loop, int fd, int io, void *ud)
{
	struct kat_state *s = ud;
	struct from_worker m;
	while (read(fd, &m, sizeof m) == sizeof m) {
		if (m.type == FROM_END) {
			if (s->capturing)
				s->rt->uncapture(s->rt);
			s->capturing = false;
		} else {
			grime_key_event ev = {.code = m.code, .edge = m.value, .time_ms = grime_now_ms()};
			s->rt->feed(s->rt, &ev);
		}
	}
}

static bool start(struct kat_state *s, grime_runtime *rt)
{
	if (pipe2(s->to, O_CLOEXEC) < 0)
		return false;
	if (pipe2(s->from, O_CLOEXEC | O_NONBLOCK) < 0) {
		close(s->to[0]);
		close(s->to[1]);
		return false;
	}
	/* only the read end is the loop's; the worker may block writing */
	fcntl(s->from[1], F_SETFL, 0);
	s->rt = rt;
	if (pthread_create(&s->thread, NULL, worker, s)) {
		for (int i = 0; i < 2; i++) {
			close(s->to[i]);
			close(s->from[i]);
		}
		return false;
	}
	grime_loop_add_fd(rt->loop, s->from[0], GRIME_IO_READ, on_reply, s);
	s->running = true;
	return true;
}

static void run(grime_runtime *rt, void *state)
{
	struct kat_state *s = state;
	if (rt->dry_run) {
		LOG_INFO("kat (dry run): the grid would open");
		return;
	}
	if (!rt->capture || s->capturing)
		return;
	if (!s->has_trigger) { /* settled once: the worker reads it from here on */
		s->cfg.trigger = rt->key;
		s->has_trigger = true;
	}
	const char *bad = kat_config_check(&s->cfg);
	if (bad) {
		LOG_ERR("kat: %s", bad);
		return;
	}
	if (!s->running && !start(s, rt)) {
		LOG_ERR("kat: cannot start: %s", strerror(errno));
		return;
	}
	struct to_worker m = {.type = TO_SHOW};
	for (int code = 0; rt->down && code < GRIME_KEY_COUNT; code++)
		if (rt->down[code])
			m.down[code / 8] |= 1 << (code % 8);
	/* kat swallows the releases of whatever is held now */
	grime_release_all(rt);
	send_worker(s, &m);
	rt->capture(rt, on_capture, s);
	s->capturing = true;
}

static bool mode_named(const char *name, kat_mode *out)
{
	static const char *const names[KAT_MODES] = {"left_click", "grab_release", "right_click"};
	for (int m = 0; m < KAT_MODES; m++)
		if (!strcmp(name, names[m])) {
			*out = (kat_mode)m;
			return true;
		}
	return false;
}

static int compile(json_object *spec, void **out, char *err, size_t errlen)
{
	struct kat_state *s = calloc(1, sizeof *s);
	if (!s) {
		snprintf(err, errlen, "out of memory");
		return -1;
	}
	kat_config_defaults(&s->cfg);
	const char *settings = grime_spec_str(spec, "settings");
	if (settings && !kat_config_load(&s->cfg, settings, true)) {
		snprintf(err, errlen, "cannot read kat settings \"%s\"", settings);
		free(s);
		return -1;
	}
	const char *trigger = grime_spec_str(spec, "trigger");
	if (trigger) {
		if (!(s->cfg.trigger = kat_key_code(trigger))) {
			snprintf(err, errlen, "unknown trigger \"%s\"", trigger);
			free(s);
			return -1;
		}
		s->has_trigger = true;
	}
	const char *mode = grime_spec_str(spec, "mode");
	if (mode && !mode_named(mode, &s->mode)) {
		snprintf(err, errlen, "\"mode\" is left_click, grab_release or right_click");
		free(s);
		return -1;
	}
	*out = s;
	return 0;
}

static void kat_free(void *state)
{
	struct kat_state *s = state;
	if (s->capturing)
		s->rt->uncapture(s->rt);
	if (s->running) {
		struct to_worker m = {.type = TO_QUIT};
		send_worker(s, &m);
		pthread_join(s->thread, NULL);
		grime_loop_del_fd(s->rt->loop, s->from[0]);
		for (int i = 0; i < 2; i++) {
			close(s->to[i]);
			close(s->from[i]);
		}
	}
	free(s);
}

static const grime_action_type kat_action = {"kat", compile, run, kat_free};
GRIME_REGISTER_ACTION(kat_action)
