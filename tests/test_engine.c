/* Engine + config tests. A recording output stands in for uinput, so no root needed. */
#include <string.h>

#include "grime/config.h"
#include "grime/engine.h"
#include "grime/keynames.h"
#include "grime/log.h"
#include "tinytest.h"

/* ---- recording output: "+a" press, "-a" release, space separated ---- */
static char rec[4096];

static void rec_emit(grime_output *out, uint16_t code, int value)
{
	strcat(rec, rec[0] ? " " : "");
	strcat(rec, value ? "+" : "-");
	strcat(rec, grime_key_name(code));
}

static grime_output rec_out = {.emit = rec_emit};
static grime_loop *loop;
static grime_runtime rt;
static int quits;
static void rt_quit(grime_runtime *r) { quits++; }
static void rt_reload(grime_runtime *r) {}

static grime_engine *load(const char *json)
{
	grime_config cfg;
	char err[512];
	if (grime_config_parse(json, &cfg, err, sizeof err) < 0) {
		fprintf(stderr, "  config error: %s\n", err);
		return NULL;
	}
	memset(&rt, 0, sizeof rt);
	rt = (grime_runtime){.loop = loop, .out = &rec_out, .quit = rt_quit, .reload = rt_reload};
	grime_engine *e = grime_engine_new(&rt, cfg.keymap);
	cfg.keymap = NULL;
	grime_config_free(&cfg);
	rec[0] = 0;
	return e;
}

/* "+caps +h -h -caps" -> feed those physical events */
static void feed(grime_engine *e, const char *events)
{
	char buf[512];
	strcpy(buf, events);
	for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
		grime_key_event ev = {
			.code = grime_key_from_name(tok + 1),
			.edge = tok[0] == '+' ? GRIME_PRESS : GRIME_RELEASE,
		};
		grime_engine_feed(e, &ev);
	}
}

static bool config_fails(const char *json, const char *needle)
{
	grime_config cfg;
	char err[512] = "";
	if (grime_config_parse(json, &cfg, err, sizeof err) == 0) {
		grime_config_free(&cfg);
		return false;
	}
	return strstr(err, needle) != NULL;
}

TEST(passthrough_pairs_press_and_release)
{
	grime_engine *e = load("{\"keymap\": {\"a\": \"a\", \"leftshift\": \"leftshift\"}}");
	feed(e, "+leftshift +a -a -leftshift");
	CHECK(!strcmp(rec, "+leftshift +a -a -leftshift"));
	grime_engine_free(e);
}

TEST(unmapped_key_does_nothing)
{
	grime_engine *e = load("{\"keymap\": {\"a\": \"a\"}}");
	feed(e, "+b -b +a -a");
	CHECK(!strcmp(rec, "+a -a"));
	grime_engine_free(e);
}

TEST(any_key_can_type_anything)
{
	grime_engine *e = load("{\"keymap\": {\"rightshift\": \"a\"}}");
	feed(e, "+rightshift -rightshift");
	CHECK(!strcmp(rec, "+a -a"));
	grime_engine_free(e);
}

static const char *caps_layer =
	"{\"keymap\": {"
	"  \"h\": \"h\","
	"  \"capslock\": {\"tap\": \"esc\", \"hold\": {\"h\": \"left\"}}"
	"}}";

TEST(hold_layer)
{
	grime_engine *e = load(caps_layer);
	feed(e, "+capslock +h -h +h -h -capslock");
	CHECK(!strcmp(rec, "+left -left +left -left"));
	rec[0] = 0;
	feed(e, "+h -h"); /* layer left on release */
	CHECK(!strcmp(rec, "+h -h"));
	grime_engine_free(e);
}

TEST(tap_alone)
{
	grime_engine *e = load(caps_layer);
	feed(e, "+capslock -capslock");
	CHECK(!strcmp(rec, "+esc -esc"));
	grime_engine_free(e);
}

TEST(release_stays_paired_across_context_change)
{
	grime_engine *e = load(caps_layer);
	/* h pressed at root, released inside the layer: still releases h. capslock
	 * counts as alone (no key was *pressed* while it was held) so it taps esc. */
	feed(e, "+h +capslock -h -capslock");
	CHECK(!strcmp(rec, "+h -h +esc -esc"));
	grime_engine_free(e);
}

static const char *emacs =
	"{\"keymap\": {"
	"  \"a\": \"a\","
	"  \"rightctrl x f\": {\"do\": \"type\", \"text\": \"F!\"}"
	"}}";

TEST(emacs_prefix_sequence)
{
	grime_engine *e = load(emacs);
	feed(e, "+rightctrl -rightctrl +x -x +f -f +a -a");
	CHECK(!strcmp(rec, "+leftshift +f -f -leftshift +leftshift +1 -1 -leftshift +a -a"));
	grime_engine_free(e);
}

TEST(emacs_prefix_cancelled_by_unmapped_key)
{
	grime_engine *e = load(emacs);
	feed(e, "+rightctrl -rightctrl +x -x +q -q +f -f +a -a");
	CHECK(!strcmp(rec, "+a -a"));
	grime_engine_free(e);
}

/* f13 toggles an "override" layer: ctrl+f is taken over by grime, every other
 * ctrl chord (ctrl+s) still reaches the app as ctrl+s. */
static const char *override =
	"{\"keymap\": {"
	"  \"s\": \"s\", \"f\": \"f\", \"leftctrl\": \"leftctrl\","
	"  \"f13\":     {\"pass\": true, \"toggle\": {\"ctrl+f\": {\"do\": \"tap\", \"key\": \"f1\"}}},"
	"  \"capslock\": {\"hold\":   {\"o\": {\"pass\": true, \"toggle\": {\"s\": \"f\"}}}},"
	"  \"rightalt\": {\"prefix\": {\"o\": {\"pass\": true, \"toggle\": {\"f\": \"s\"}}}}"
	"}}";

TEST(toggle_overrides_chords_until_toggled_again)
{
	grime_engine *e = load(override);
	feed(e, "+leftctrl +f -f -leftctrl");         /* off: plain ctrl+f */
	feed(e, "+f13 -f13");                         /* on */
	feed(e, "+leftctrl +f -f +s -s -leftctrl");   /* ctrl+f overridden, ctrl+s passes */
	feed(e, "+f -f");                             /* plain f falls through */
	feed(e, "+f13 -f13 +leftctrl +f -f -leftctrl"); /* off again */
	CHECK(!strcmp(rec, "+leftctrl +f -f -leftctrl "
			   "+leftctrl +f1 -f1 +s -s -leftctrl "
			   "+f -f "
			   "+leftctrl +f -f -leftctrl"));
	grime_engine_free(e);
}

TEST(toggle_outlives_the_layer_it_was_turned_on_from)
{
	grime_engine *e = load(override);
	feed(e, "+capslock +o -o -capslock"); /* on, from inside the capslock layer */
	feed(e, "+s -s");                     /* still on after capslock is released */
	feed(e, "+capslock +o -o -capslock"); /* same path again: off */
	feed(e, "+s -s");
	CHECK(!strcmp(rec, "+f -f +s -s"));
	grime_engine_free(e);
}

TEST(toggle_from_a_prefix_ends_the_prefix)
{
	grime_engine *e = load(override);
	feed(e, "+rightalt -rightalt +o -o"); /* rightalt o: on */
	feed(e, "+f -f +s -s");
	feed(e, "+rightalt -rightalt +o -o"); /* rightalt o: off */
	feed(e, "+f -f");
	CHECK(!strcmp(rec, "+s -s +s -s +f -f"));
	grime_engine_free(e);
}

TEST(toggles_stack)
{
	grime_engine *e = load(override);
	feed(e, "+capslock +o -o -capslock +rightalt -rightalt +o -o"); /* both on; top wins */
	feed(e, "+s -s +f -f");
	feed(e, "+capslock +o -o -capslock"); /* turn the lower one off, upper stays */
	feed(e, "+s -s +f -f");
	CHECK(!strcmp(rec, "+f -f +s -s +s -s +s -s"));
	grime_engine_free(e);
}

TEST(fallthrough_to_parent)
{
	grime_engine *e =
		load("{\"keymap\": {\"a\": \"a\", "
		     "\"space\": {\"pass\": true, \"hold\": {\"j\": \"down\"}}}}");
	feed(e, "+space +j -j +a -a -space");
	CHECK(!strcmp(rec, "+down -down +a -a"));
	grime_engine_free(e);
}

TEST(quit_action)
{
	grime_engine *e = load("{\"keymap\": {\"f12\": {\"do\": \"quit\"}}}");
	quits = 0;
	feed(e, "+f12 -f12");
	CHECK(quits == 1);
	grime_engine_free(e);
}

TEST(config_errors_are_helpful)
{
	CHECK(config_fails("{\"keymap\": {\"nope\": \"a\"}}", "unknown key \"nope\""));
	CHECK(config_fails("{\"keymap\": {\"a\": \"nope\"}}", "unknown key \"nope\""));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"do\": \"fly\"}}}", "unknown action"));
	CHECK(config_fails("{\"keymap\": ", "invalid JSON"));
	CHECK(config_fails("{\"keymap\": {\"a\": {}}}", "nothing to do"));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"tap\": \"b\", \"release\": \"c\"}}}",
			   "can't both be on one key"));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"hold\": {}, \"toggle\": {}}}}",
			   "can't both be on one key"));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"pass\": true, \"tap\": \"b\"}}}",
			   "\"pass\" needs a layer"));
	/* the retired spellings say what to use instead */
	CHECK(config_fails("{\"keymap\": {\"a\": {\"press\": {\"then\": {}}}}}", "\"then\" is gone"));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"exit\": \"toggle\"}}}", "\"exit\" is gone"));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"alone\": true}}}", "\"alone\" is gone"));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"fallthrough\": true}}}", "\"fallthrough\" is gone"));
}

int main(void)
{
	grime_log_level = GRIME_LOG_ERROR;
	loop = grime_loop_new();
	RUN(passthrough_pairs_press_and_release);
	RUN(unmapped_key_does_nothing);
	RUN(any_key_can_type_anything);
	RUN(hold_layer);
	RUN(tap_alone);
	RUN(release_stays_paired_across_context_change);
	RUN(emacs_prefix_sequence);
	RUN(emacs_prefix_cancelled_by_unmapped_key);
	RUN(toggle_overrides_chords_until_toggled_again);
	RUN(toggle_outlives_the_layer_it_was_turned_on_from);
	RUN(toggle_from_a_prefix_ends_the_prefix);
	RUN(toggles_stack);
	RUN(fallthrough_to_parent);
	RUN(quit_action);
	RUN(config_errors_are_helpful);
	grime_loop_free(loop);
	return DONE();
}
