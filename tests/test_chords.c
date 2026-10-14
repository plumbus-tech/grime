/* Chords through layers, consumed modifiers, modes and "leave": the pieces an
 * Emacs-style editing layer is made of. */
#include <string.h>

#include "grime/config.h"
#include "grime/engine.h"
#include "grime/keynames.h"
#include "grime/log.h"
#include "tinytest.h"

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
static void rt_quit(grime_runtime *r) {}
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

static bool emits(const char *events, const char *want);

static const char *emacs =
	"{\"keymap\": {"
	"  \"a\": \"a\", \"f\": \"f\", \"s\": \"s\", \"x\": \"x\", \"g\": \"g\", \"w\": \"w\","
	"  \"comma\": \"comma\", \"space\": \"space\","
	"  \"leftshift\": \"leftshift\", \"leftalt\": \"leftalt\","
	"  \"capslock\": {\"do\": \"tap\", \"key\": \"f13\"},"
	"  \"ctrl+capslock\": {\"toggle\": \"emacs\", \"pass\": true, \"on\": true}"
	"},"
	"\"layers\": {"
	"  \"emacs\": {"
	"    \"ctrl+a\": \"home\", \"ctrl+f\": \"right\", \"alt+shift+comma\": \"ctrl+home\","
	"    \"ctrl+space\": {\"mode\": \"mark\"}"
	"  },"
	"  \"mark\": {"
	"    \"ctrl+f\": \"shift+right\","
	"    \"ctrl+w\": {\"do\": \"tap\", \"key\": \"x\", \"mods\": [\"leftctrl\"], \"leave\": true},"
	"    \"ctrl+g\": {\"leave\": true}"
	"  }"
	"}}";

static grime_engine *eng;

static bool emits(const char *events, const char *want)
{
	grime_engine_free(eng);
	eng = load(emacs);
	feed(eng, events);
	if (strcmp(rec, want)) {
		fprintf(stderr, "  got [%s], wanted [%s]\n", rec, want);
		return false;
	}
	return true;
}

TEST(a_consumed_modifier_is_lifted_for_the_key_it_made)
{
	CHECK(emits("+leftctrl +a -a -leftctrl", "+leftctrl -leftctrl +home -home"));
	/* held across several: lifted once, and its release is not sent twice */
	CHECK(emits("+leftctrl +a -a +f -f -leftctrl",
		    "+leftctrl -leftctrl +home -home +right -right"));
}

TEST(a_lifted_modifier_comes_back_for_an_unbound_chord)
{
	CHECK(emits("+leftctrl +a -a +s -s -leftctrl",
		    "+leftctrl -leftctrl +home -home +leftctrl +s -s -leftctrl"));
}

TEST(a_toggle_can_start_on_and_be_reached_from_inside_itself)
{
	/* ctrl+capslock is not in the emacs layer; ctrl falls through to the
	 * root's own ctrl, which is where the toggle is */
	CHECK(emits("+leftctrl +capslock -capslock +a -a -leftctrl", "+leftctrl +a -a -leftctrl"));
	CHECK(emits("+leftctrl +capslock -capslock -leftctrl "
		    "+leftctrl +capslock -capslock +a -a -leftctrl",
		    "+leftctrl -leftctrl +leftctrl -leftctrl +home -home"));
	/* capslock alone is the root's */
	CHECK(emits("+capslock -capslock", "+f13 -f13"));
}

TEST(a_chord_follows_held_modifiers_in_any_order)
{
	CHECK(emits("+leftalt +leftshift +comma -comma -leftshift -leftalt",
		    "+leftalt +leftshift -leftalt -leftshift +leftctrl +home -home -leftctrl"));
	CHECK(emits("+leftshift +leftalt +comma -comma -leftalt -leftshift",
		    "+leftshift +leftalt -leftalt -leftshift +leftctrl +home -home -leftctrl"));
}

TEST(a_mode_entered_with_ctrl_held_sees_that_ctrl)
{
	CHECK(emits("+leftctrl +space -space +f -f +f -f -leftctrl",
		    "+leftctrl -leftctrl +leftshift +right -right -leftshift "
		    "+leftshift +right -right -leftshift"));
}

TEST(a_key_the_mode_lacks_ends_it_and_goes_through)
{
	CHECK(emits("+leftctrl +space -space -leftctrl +x -x +leftctrl +f -f -leftctrl",
		    "+leftctrl -leftctrl +x -x +leftctrl -leftctrl +right -right"));
}

TEST(a_modifier_alone_does_not_end_a_mode)
{
	CHECK(emits("+leftctrl +space -space -leftctrl +leftctrl +f -f -leftctrl",
		    "+leftctrl -leftctrl +leftctrl -leftctrl +leftshift +right -right -leftshift"));
}

TEST(leave_ends_the_layer_it_is_in)
{
	CHECK(emits("+leftctrl +space -space +w -w +f -f -leftctrl",
		    "+leftctrl -leftctrl +leftctrl +x -x -leftctrl +right -right"));
	CHECK(emits("+leftctrl +space -space +g -g -leftctrl +leftctrl +f -f -leftctrl",
		    "+leftctrl -leftctrl +leftctrl -leftctrl +right -right"));
}

/* The shipped Emacs layer, end to end. */
static bool example_emits(const char *events, const char *want)
{
	grime_config cfg;
	char err[512];
	if (grime_config_load("configs/examples/emacs.json", &cfg, err, sizeof err) < 0) {
		fprintf(stderr, "  config error: %s\n", err);
		return false;
	}
	memset(&rt, 0, sizeof rt);
	rt = (grime_runtime){.loop = loop, .out = &rec_out, .quit = rt_quit, .reload = rt_reload,
			     .dry_run = true};
	grime_engine *e = grime_engine_new(&rt, cfg.keymap);
	cfg.keymap = NULL;
	grime_config_free(&cfg);
	rec[0] = 0;
	feed(e, events);
	grime_engine_free(e);
	if (strcmp(rec, want)) {
		fprintf(stderr, "  got [%s], wanted [%s]\n", rec, want);
		return false;
	}
	return true;
}

TEST(the_emacs_example_edits_text)
{
	CHECK(example_emits("+h -h +i -i", "+h -h +i -i"));
	CHECK(example_emits("+leftctrl +e -e -leftctrl", "+leftctrl -leftctrl +end -end"));
	CHECK(example_emits("+leftalt +f -f -leftalt",
			    "+leftalt -leftalt +leftctrl +right -right -leftctrl"));
	CHECK(example_emits("+leftctrl +k -k -leftctrl",
			    "+leftctrl -leftctrl +leftshift +end -end -leftshift "
			    "+leftctrl +x -x -leftctrl"));
	/* mark, two words, cut */
	CHECK(example_emits("+leftctrl +space -space -leftctrl +leftalt +f -f +f -f -leftalt "
			    "+leftctrl +w -w -leftctrl",
			    "+leftctrl -leftctrl +leftalt -leftalt +leftctrl +leftshift +right "
			    "-right -leftshift -leftctrl +leftctrl +leftshift +right -right "
			    "-leftshift -leftctrl +leftctrl -leftctrl +leftctrl +x -x -leftctrl"));
	/* off: ctrl+a is the app's again; alt+j still pages */
	CHECK(example_emits("+leftctrl +capslock -capslock +a -a -leftctrl +leftalt +j -j -leftalt",
			    "+leftctrl +a -a -leftctrl +leftalt -leftalt +pagedown -pagedown"));
	/* M-< : alt and shift are both used up */
	CHECK(example_emits("+leftalt +leftshift +comma -comma -leftshift -leftalt",
			    "+leftalt +leftshift -leftalt -leftshift +leftctrl +home -home -leftctrl"));
}

TEST(the_emacs_example_marks_and_searches)
{
	/* C-g drops the highlight at the end the first motion went to */
	CHECK(example_emits("+leftctrl +space -space +f -f +g -g -leftctrl",
			    "+leftctrl -leftctrl +leftshift +right -right -leftshift +right -right"));
	CHECK(example_emits("+leftctrl +space -space +b -b +f -f +g -g -leftctrl",
			    "+leftctrl -leftctrl +leftshift +left -left -leftshift "
			    "+leftshift +right -right -leftshift +left -left"));
	/* C-SPC again drops it and starts a new one where the cursor is */
	CHECK(example_emits("+leftctrl +space -space +f -f +space -space +b -b +g -g -leftctrl",
			    "+leftctrl -leftctrl +leftshift +right -right -leftshift +right -right "
			    "+leftshift +left -left -leftshift +left -left"));
	/* an arrow extends it too */
	CHECK(example_emits("+leftctrl +space -space -leftctrl +right -right +leftctrl +g -g -leftctrl",
			    "+leftctrl -leftctrl +leftshift +right -right -leftshift +leftctrl -leftctrl "
			    "+right -right"));
	/* nothing selected yet: C-g just ends it */
	CHECK(example_emits("+leftctrl +space -space +g -g -leftctrl", "+leftctrl -leftctrl"));
	/* C-x h selects all; C-g leaves the cursor at the top */
	CHECK(example_emits("+leftctrl +x -x -leftctrl +h -h +leftctrl +g -g -leftctrl",
			    "+leftctrl -leftctrl +leftctrl +a -a -leftctrl +leftctrl -leftctrl +left -left"));
	CHECK(example_emits("+leftctrl +x -x +s -s -leftctrl",
			    "+leftctrl -leftctrl +leftctrl +s -s -leftctrl"));
	/* C-s C-s RET: find, find next, done */
	CHECK(example_emits("+leftctrl +s -s +s -s -leftctrl +enter -enter +leftctrl +s -s -leftctrl",
			    "+leftctrl -leftctrl +leftctrl +f -f -leftctrl +f3 -f3 +esc -esc "
			    "+leftctrl -leftctrl +leftctrl +f -f -leftctrl"));
	CHECK(example_emits("+leftctrl +t -t -leftctrl",
			    "+leftctrl -leftctrl +leftshift +right -right -leftshift +leftctrl +x -x "
			    "-leftctrl +left -left +leftctrl +v -v -leftctrl +right -right"));
}

TEST(a_mode_is_switched_off_by_its_own_key)
{
	grime_engine *e = load("{\"keymap\": {\"x\": \"x\", \"f13\": {\"mode\": {\"x\": \"y\"}}}}");
	feed(e, "+f13 -f13 +x -x +f13 -f13 +x -x");
	CHECK(!strcmp(rec, "+y -y +x -x"));
	grime_engine_free(e);
}

static bool config_fails(const char *json, const char *needle)
{
	grime_config cfg;
	char err[512] = "";
	if (grime_config_parse(json, &cfg, err, sizeof err) == 0) {
		grime_config_free(&cfg);
		return false;
	}
	if (!strstr(err, needle))
		fprintf(stderr, "  error was: %s\n", err);
	return strstr(err, needle) != NULL;
}

TEST(new_slots_say_what_is_wrong)
{
	CHECK(config_fails("{\"keymap\": {\"a\": {\"hold\": {}, \"on\": true}}}",
			   "\"on\" only works with \"toggle\""));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"hold\": {}, \"leave\": true}}}",
			   "can only start a \"mode\""));
	CHECK(config_fails("{\"keymap\": {\"a\": {\"mode\": {}, \"toggle\": {}}}}",
			   "can't both be on one key"));
}

int main(void)
{
	grime_log_level = GRIME_LOG_ERROR;
	loop = grime_loop_new();
	RUN(a_consumed_modifier_is_lifted_for_the_key_it_made);
	RUN(a_lifted_modifier_comes_back_for_an_unbound_chord);
	RUN(a_toggle_can_start_on_and_be_reached_from_inside_itself);
	RUN(a_chord_follows_held_modifiers_in_any_order);
	RUN(a_mode_entered_with_ctrl_held_sees_that_ctrl);
	RUN(a_key_the_mode_lacks_ends_it_and_goes_through);
	RUN(a_modifier_alone_does_not_end_a_mode);
	RUN(leave_ends_the_layer_it_is_in);
	RUN(a_mode_is_switched_off_by_its_own_key);
	RUN(new_slots_say_what_is_wrong);
	RUN(the_emacs_example_edits_text);
	RUN(the_emacs_example_marks_and_searches);
	grime_engine_free(eng);
	grime_loop_free(loop);
	return DONE();
}
